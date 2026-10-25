/** @file @brief 验证两个后端的压缩/数组真实采样、层视图/回读/拷贝、维度检查与精灵合批。 */
#include <iostream>
#include <renderer/renderer.hpp>
#include <renderer/rhi.hpp>
#include <renderer/sprite.hpp>
#include <renderer/texture.hpp>
using namespace renderer;
namespace {
void require(bool b, const char *s) {
    if (!b)
        throw std::runtime_error(s);
}
template <class F> void throws(F f) {
    bool b = false;
    try {
        f();
    } catch (const std::exception &) {
        b = true;
    }
    require(b, "Expected validation failure");
}
TextureDesc color(std::array<uint8_t, 4> c) {
    TextureDesc d{8, 8, std::vector<uint8_t>(256), false};
    for (size_t i = 0; i < d.pixels.size(); i += 4)
        std::copy(c.begin(), c.end(), d.pixels.begin() + ptrdiff_t(i));
    return d;
}
rhi::Device device() {
#ifdef RENDERER_TEST_VULKAN
    return rhi::Device::createVulkanOffscreen();
#else
    return rhi::Device::createOpenGLOffscreen();
#endif
}
Renderer createRenderer(bool thread, bool force = false) {
    RendererConfig c;
    c.renderThread = thread;
    c.forceTextureDecompression = force;
    c.textureCompressionFallback = !force; // 显式强制解码优先于自动降级开关。
#ifdef RENDERER_TEST_VULKAN
    return Renderer::createVulkanOffscreen({256, 128}, c);
#else
    return Renderer::createOpenGLOffscreen({256, 128}, c);
#endif
}
unsigned sample(const Image &i, unsigned x, unsigned y, unsigned c) {
    return i.pixels[(size_t(y) * i.width + x) * 4 + c];
}
} // namespace
int main() {
    try {
        std::array<TextureDesc, 3> images{color({255, 0, 0, 255}), color({0, 255, 0, 255}),
                                          color({0, 0, 255, 255})};
        for (unsigned y = 6; y < 8; ++y)
            for (unsigned x = 0; x < 8; ++x) {
                auto i = (y * 8 + x) * 4;
                images[1].pixels[i + 1] = 0;
                images[1].pixels[i + 2] = 255;
            }
        auto array = texture::makeArray(images);
        auto bc = texture::compress(array, TextureCompression::BC3);
        {
            auto gpu = device();
            require(gpu.capabilities().maxTextureArrayLayers >= 3, "No array capability");
            auto a = gpu.createTexture({8, 8, 4, rhi::Format::Rgba8, "Array", 3, true});
            for (uint32_t l = 0; l < 3; ++l)
                gpu.upload(a, images[l].pixels, 0, l);
            gpu.generateMipmaps(a);
            auto layer = gpu.createTextureView(a, 1, 1, 1, 1);
            gpu.destroy(a);
            auto data = gpu.readTexture(layer);
            require(data.width == 4 && data.pixels[(data.height - 1) * data.width * 4 + 1] > 250 &&
                        data.pixels[2] > 250,
                    "Layer/mip view lost parent storage");
            gpu.destroy(layer);
            for (auto f : {rhi::Format::Bc1, rhi::Format::Bc2, rhi::Format::Bc3, rhi::Format::Bc4,
                           rhi::Format::Bc5, rhi::Format::Bc7}) {
                if (!gpu.formatFeatures(f).sampled)
                    continue;
                auto fmt = f == rhi::Format::Bc1   ? TextureCompression::BC1
                           : f == rhi::Format::Bc2 ? TextureCompression::BC2
                           : f == rhi::Format::Bc3 ? TextureCompression::BC3
                           : f == rhi::Format::Bc4 ? TextureCompression::BC4
                           : f == rhi::Format::Bc5 ? TextureCompression::BC5
                                                   : TextureCompression::BC7;
                TextureDesc src;
                if (fmt == TextureCompression::BC2) {
                    auto bc1 = texture::compress(images[0], TextureCompression::BC1, false);
                    src.width = src.height = 8;
                    src.srgb = false;
                    src.compression = fmt;
                    src.data.resize(64);
                    for (unsigned b = 0; b < 4; ++b) {
                        std::fill_n(src.data.begin() + b * 16, 8, uint8_t(255));
                        std::copy_n(bc1.data.begin() + b * 8, 8, src.data.begin() + b * 16 + 8);
                    }
                } else if (fmt == TextureCompression::BC7) {
                    src.width = src.height = 4;
                    src.srgb = false;
                    src.compression = fmt;
                    src.data.resize(16);
                    src.data[0] = 0x40;
                } else
                    src = texture::compress(images[0], fmt, false);
                auto t = gpu.createTexture({src.width, src.height, 1, f, "BC upload", 2, true});
                auto bytes = texture::subresource(src);
                gpu.upload(t, bytes, 0, 0);
                gpu.upload(t, bytes, 0, 1);
                auto read = gpu.readTexture(t, 1);
                require(read.width == src.width, "Compressed array readback extent wrong");
                if (fmt != TextureCompression::BC7)
                    require(read.pixels[0] > 245, "Compressed array readback color wrong");
                gpu.destroy(t);
            }
            auto t = gpu.createTexture({8, 8, 1, rhi::Format::Rgba8, "Layer copy", 3, true});
            for (uint32_t l = 0; l < 3; ++l)
                gpu.upload(t, images[l].pixels, 0, l);
            rhi::CommandEncoder copy;
            copy.copyTexture(t, t,
                             {.width = 8, .height = 8, .sourceLayer = 0, .destinationLayer = 2});
            gpu.wait(gpu.submit(copy.finish()));
            require(gpu.readTexture(t, 2).pixels[0] > 250,
                    "Layer copy addressed the wrong subresource");
            gpu.destroy(t);
            ShaderCompiler compiler;
            auto shader = compiler.compile(ShaderStage::Compute, R"(#version 450
layout(local_size_x=1,local_size_y=1,local_size_z=1) in;
layout(set=0,binding=0,rgba8) uniform writeonly image2DArray outputImage;
void main(){imageStore(outputImage,ivec3(gl_GlobalInvocationID),vec4(0.2,0.4,0.8,1));}
)");
            require(shader.bindings.size() == 1 && shader.bindings[0].textureArray,
                    "Array image reflection lost dimension");
            auto output =
                gpu.createTexture({2, 2, 1, rhi::Format::Rgba8, "Array compute", 3, true});
            auto pipeline = gpu.createComputePipeline(shader);
            auto group = gpu.createBindingGroup({0,
                                                 {{.binding = 0,
                                                   .kind = BindingKind::StorageImage,
                                                   .texture = output,
                                                   .writable = true}}});
            rhi::CommandEncoder compute;
            compute.bindPipeline(pipeline);
            compute.bindGroup(group);
            compute.dispatch(2, 2, 3);
            gpu.wait(gpu.submit(compute.finish()));
            require(std::abs(int(gpu.readTexture(output, 2).pixels[2]) - 204) < 2,
                    "Array storage image addressed wrong layer");
            gpu.destroy(group);
            gpu.destroy(pipeline);
            gpu.destroy(output);
            require(!gpu.debugErrorCount(), "RHI texture validation error");
        }
        for (bool threaded : {false, true})
            for (bool force : {false, true}) {
                auto gpu = createRenderer(threaded, force);
                AssetStore assets;
                Scene scene;
                RenderExtractor extractor;
                auto camera = scene.createNode(scene.root());
                scene.setComponent(camera, Camera{.projection = Camera::Projection::Orthographic,
                                                  .orthographicHeight = 2});
                scene.setLocalTransform(camera, Mat4::translation({0, 0, 5}));
                auto tex = assets.createTexture(bc);
                SpriteDesc desc;
                desc.texture = tex;
                desc.rect = {0, 0, 8, 8};
                desc.pivot = {.5f, .5f};
                desc.pixelsPerUnit = 8;
                desc.alphaMode = AlphaMode::Mask;
                auto sprite = assets.createSprite(desc);
                for (uint32_t layer = 0; layer < 3; ++layer) {
                    auto n = scene.createNode(scene.root());
                    scene.setLocalTransform(n, Mat4::translation({float(layer) - 1, 0, 0}));
                    scene.setComponent(n, SpriteRenderer{.sprite = sprite,
                                                         .billboard = BillboardMode::Fixed,
                                                         .textureLayer = layer});
                }
                auto snapshot = extractor.extract(scene, assets);
                auto view =
                    RenderView{.cameraNode = camera, .clearColor = {0, 0, 0, 1}, .shadows = false};
                auto stats = gpu.render(snapshot, view);
                auto image = gpu.readback();
                require(stats.sceneDrawCalls == 1 && stats.visibleInstances == 3,
                        "Array layers split the shared material batch");
                require(sample(image, 64, 64, 0) > 245 && sample(image, 128, 64, 1) > 245 &&
                            sample(image, 192, 64, 2) > 245,
                        "Per-instance array layer sampling failed");
                require(sample(image, 128, 40, 2) > 245 && sample(image, 128, 80, 1) > 245,
                        "Compressed texture origin differs from RGBA fallback");
                auto mem = gpu.memoryStatistics();
                require(mem.compressedTextureBytes == (force ? 0 : bc.data.size()) &&
                            mem.textureArrayLayers == 3,
                        "Compressed/array GPU accounting wrong");
                require(!gpu.debugErrorCount(), "Scene texture graphics validation error");
            }
        std::cout << "BC and texture arrays on GPU passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
