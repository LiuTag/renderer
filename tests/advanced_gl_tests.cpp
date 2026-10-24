#include "graphics_fixture.hpp"
#include "renderer/render_graph.hpp"
#include "renderer/renderer.hpp"
#include "renderer/window.hpp"
#include <cmath>
#include <cstring>
#include <future>
#include <iostream>
#include <stdexcept>

using namespace renderer;
namespace {
void require(bool v, const char *text) {
    if (!v)
        throw std::runtime_error(text);
}
Vec3 pixel(const Image &image, uint32_t x, uint32_t y) {
    auto i = (size_t{y} * image.width + x) * 4;
    return {float(image.pixels[i]), float(image.pixels[i + 1]), float(image.pixels[i + 2])};
}
Vec3 centerPixel(const Image &image) {
    return pixel(image, image.width / 2, image.height / 2);
}
NodeHandle camera(Scene &scene, Vec3 eye = {0, 0, 3}, Vec3 target = {0, 0, 0}) {
    auto n = scene.createNode(scene.root());
    scene.setLocalTransform(n, *Mat4::lookAt(eye, target).inverse());
    scene.setComponent(n, Camera{});
    return n;
}
NodeHandle object(Scene &scene, MeshHandle mesh, MaterialInstanceHandle material,
                  Mat4 transform = Mat4::identity()) {
    auto n = scene.createNode(scene.root());
    scene.setLocalTransform(n, transform);
    scene.setComponent(n, MeshRenderer{mesh, {material}});
    return n;
}
MeshDesc quad() {
    return {{{{-1, -1, 0}, {0, 0, 1}, {0, 0}},
             {{1, -1, 0}, {0, 0, 1}, {1, 0}},
             {{1, 1, 0}, {0, 0, 1}, {1, 1}},
             {{-1, 1, 0}, {0, 0, 1}, {0, 1}}},
            {0, 1, 2, 0, 2, 3},
            {}};
}
template <class T> std::span<const std::byte> bytes(const std::vector<T> &v) {
    return std::as_bytes(std::span{v});
}
} // namespace
int main() {
    try {
        {
            auto window = graphicsTestWindow({.width = 96,
                                              .height = 96,
                                              .title = "RHI tests",
                                              .visible = false,
                                              .vsync = false});
            auto device = graphicsTestDevice(window);
            ShaderCompiler compiler;
            require(device.capabilities().compute &&
                        device.formatFeatures(rhi::Format::Rgba16Float).colorAttachment,
                    "Device capability query failed");
            auto shader = compiler.compile(ShaderStage::Compute, R"(#version 450
layout(local_size_x=1) in;
layout(std430,set=2,binding=7) buffer Data{uint values[];}data;
void main(){uint i=gl_GlobalInvocationID.x;data.values[i]=i*3+7;}
)");
            auto pipeline = device.createComputePipeline(shader);
            auto storage = device.createBuffer({64}),
                 readback = device.createBuffer({128, rhi::Memory::Readback});
            auto group = device.createBindingGroup(
                {2, {{.binding = 7, .kind = BindingKind::StorageBuffer, .buffer = storage}}});
            auto times = device.createTimestampQueries(2);
            auto record = std::async(std::launch::async, [=] {
                rhi::CommandEncoder encoder;
                encoder.timestamp(times, 0);
                encoder.bindPipeline(pipeline);
                encoder.bindGroup(group);
                encoder.dispatch(16);
                const rhi::ResourceBarrier barrier{rhi::Access::StorageWrite,
                                                   rhi::Access::TransferRead, false, storage};
                encoder.barrier(std::span{&barrier, 1});
                encoder.copyBuffer(storage, readback, 64);
                encoder.timestamp(times, 1);
                return encoder.finish();
            });
            auto completion = device.submit(record.get());
            device.wait(completion);
            auto values = device.readBuffer(readback, 64);
            for (uint32_t i = 0; i < 16; ++i) {
                uint32_t value = 0;
                std::memcpy(&value, values.data() + i * 4, 4);
                require(value == i * 3 + 7, "Compute/barrier/copy result mismatch");
            }
            require(device.timestampResult(times, 1).value() >=
                        device.timestampResult(times, 0).value(),
                    "GPU timestamps not monotonic");
            std::cout << "PASS RHI compute, remapped descriptor groups, worker recording, "
                         "barriers, copies and timestamps\n";
            {
                RenderGraph builder;
                auto a = builder.createBuffer({64}), b = builder.createBuffer({64});
                auto out =
                    builder.importBuffer(readback, {128, rhi::Memory::Readback}, rhi::Access::None);
                rhi::BufferHandle first, second;
                std::vector<rhi::BindingGroupHandle> bindings;
                auto dispatch = [&](GraphResource resource, rhi::BufferHandle &observed) {
                    return [&, resource](auto &encoder, const auto &resources) {
                        observed = resources.buffer(resource);
                        auto binding =
                            device.createBindingGroup({2,
                                                       {{.binding = 7,
                                                         .kind = BindingKind::StorageBuffer,
                                                         .buffer = observed}}});
                        bindings.push_back(binding);
                        encoder.bindPipeline(pipeline);
                        encoder.bindGroup(binding);
                        encoder.dispatch(16);
                    };
                };
                builder.addPass("A compute", {{a, rhi::Access::StorageWrite, rhi::Stage::Compute}},
                                dispatch(a, first));
                builder.addPass(
                    "A copy",
                    {{a, rhi::Access::TransferRead, rhi::Stage::Transfer},
                     {out, rhi::Access::TransferWrite, rhi::Stage::Transfer, {0, 0, 0, 64}}},
                    [=](auto &encoder, const auto &resources) {
                        encoder.copyBuffer(resources.buffer(a), resources.buffer(out), 64);
                    });
                builder.addPass("B compute", {{b, rhi::Access::StorageWrite, rhi::Stage::Compute}},
                                dispatch(b, second));
                builder.addPass(
                    "B copy",
                    {{b, rhi::Access::TransferRead, rhi::Stage::Transfer},
                     {out, rhi::Access::TransferWrite, rhi::Stage::Transfer, {0, 0, 64, 64}}},
                    [=](auto &encoder, const auto &resources) {
                        encoder.copyBuffer(resources.buffer(b), resources.buffer(out), 64, 0, 64);
                    });
                builder.exportResource(out);
                auto graph = builder.compile();
                require(graph.statistics().physicalResources == 1,
                        "Graph allocation plan did not pool buffers");
                auto token = graph.execute(device);
                device.wait(token);
                require(first == second,
                        "Graph did not resolve pooled resources to the same buffer");
                auto data = device.readBuffer(readback);
                for (uint32_t i = 0; i < 32; ++i) {
                    uint32_t v = 0;
                    std::memcpy(&v, data.data() + i * 4, 4);
                    require(v == (i % 16) * 3 + 7, "Graph pooled execution corrupted output");
                }
                for (auto binding : bindings)
                    device.destroy(binding);
            }
            {
                RenderGraph builder;
                auto target = builder.createTexture({4, 4, 1, rhi::Format::Rgba8});
                builder.addPass(
                    "Clear", {{target, rhi::Access::ColorWrite}},
                    [=](auto &encoder, const auto &resources) {
                        rhi::RenderingDesc pass;
                        pass.colors.push_back({resources.texture(target)});
                        encoder.beginRendering(pass);
                        encoder.endRendering();
                    },
                    true);
                auto compiled = builder.compile();
                device.wait(compiled.execute(device));
                auto moved = std::move(device);
                moved.wait(compiled.execute(moved));
                device = std::move(moved);
            }
            {
                auto size = device.capabilities().storageAlignment;
                auto buffer = device.createBuffer({size});
                bool rejected = false;
                try {
                    device.createBindingGroup({0,
                                               {{.binding = 0,
                                                 .kind = BindingKind::StorageBuffer,
                                                 .buffer = buffer,
                                                 .offset = size}}});
                } catch (const std::exception &) {
                    rejected = true;
                }
                require(rejected, "Empty aligned buffer range was accepted");
                device.destroy(buffer);
            }
            std::cout << "PASS executable RenderGraph and physical transient reuse\n";
            {
                auto args = device.createBuffer({20});
                auto compute = compiler.compile(ShaderStage::Compute, R"(#version 450
layout(local_size_x=1) in;layout(std430,set=0,binding=0) buffer Args{uint fields[5];}args;
void main(){args.fields[0]=3;args.fields[1]=1;args.fields[2]=0;args.fields[3]=0;args.fields[4]=0;}
)");
                auto cs = device.createComputePipeline(compute);
                auto argsGroup = device.createBindingGroup(
                    {0, {{.binding = 0, .kind = BindingKind::StorageBuffer, .buffer = args}}});
                rhi::GraphicsPipelineDesc desc;
                desc.vertex = compiler.compile(ShaderStage::Vertex, R"(#version 450
layout(location=0) in vec2 position;void main(){gl_Position=vec4(position,0.5,1);}
)");
                desc.fragment = compiler.compile(ShaderStage::Fragment, R"(#version 450
layout(location=0) out vec4 color;void main(){color=vec4(1,0,0,1);}
)");
                desc.attributes = {{0, 0, 2, 0}};
                desc.vertexBindings = {{0, 8, false}};
                desc.colorFormats = {rhi::Format::Rgba8Srgb};
                desc.depthTest = false;
                desc.depthWrite = false;
                desc.cull = rhi::CullMode::None;
                desc.colorBlends.resize(2);
                bool invalidBlendRejected = false;
                try {
                    device.createGraphicsPipeline(desc);
                } catch (const std::invalid_argument &) {
                    invalidBlendRejected = true;
                }
                require(invalidBlendRejected, "Mismatched per-attachment blend states accepted");
                desc.colorBlends.clear();
                auto graphics = device.createGraphicsPipeline(desc);
                std::vector<float> vertices = {-1, -1, 1, -1, 0, 1};
                std::vector<uint32_t> indices = {0, 1, 2};
                auto vb = device.createBuffer({vertices.size() * 4}, bytes(vertices));
                auto ib = device.createBuffer({indices.size() * 4}, bytes(indices));
                auto image = device.createTexture({96, 96, 1, rhi::Format::Rgba8Srgb});
                rhi::CommandEncoder encoder;
                encoder.bindPipeline(cs);
                encoder.bindGroup(argsGroup);
                encoder.dispatch(1);
                const rhi::ResourceBarrier barrier{rhi::Access::StorageWrite,
                                                   rhi::Access::IndirectRead, false, args};
                encoder.barrier(std::span{&barrier, 1});
                rhi::RenderingDesc pass;
                pass.colors.push_back({image});
                encoder.beginRendering(pass);
                encoder.bindPipeline(graphics);
                encoder.bindVertexBuffer(0, vb);
                encoder.bindIndexBuffer(ib);
                encoder.drawIndexedIndirect(args, 0);
                encoder.endRendering();
                device.wait(device.submit(encoder.finish()));
                auto pixels = device.readTexture(image);
                require(pixels.pixels[(48 * 96 + 48) * 4] > 245,
                        "GPU-generated indirect drawing failed");
                device.destroy(args);
                bool stale = false;
                try {
                    device.readBuffer(args);
                } catch (const std::exception &) {
                    stale = true;
                }
                require(stale, "Destroyed RHI handle was accepted");
                device.destroy(image);
                device.destroy(vb);
                device.destroy(ib);
                device.destroy(cs);
                device.destroy(graphics);
                device.destroy(argsGroup);
            }
            std::cout << "PASS GPU-generated indirect draw and stale RHI handles\n";
            {
                auto image = device.createTexture({4, 4, 3, rhi::Format::Rgba8});
                std::vector<uint8_t> pixels(64, 255);
                device.upload(image, pixels);
                device.generateMipmaps(image);
                auto view = device.createTextureView(image, 1, 1);
                device.destroy(image);
                auto result = device.readTexture(view);
                require(result.width == 2 && result.height == 2 && result.pixels[0] == 255,
                        "Texture view lost its backing storage");
                auto copied = device.createTexture({2, 2, 1, rhi::Format::Rgba8});
                rhi::CommandEncoder copy;
                copy.copyTexture(view, copied, {.width = 2, .height = 2});
                device.wait(device.submit(copy.finish()));
                require(device.readTexture(copied).pixels[0] == 255,
                        "Texture transfer copy failed");
                device.destroy(copied);
                device.destroy(view);
            }
            device.destroy(group);
            device.destroy(storage);
            device.destroy(readback);
            device.destroy(pipeline);
            device.destroy(times);
            device.waitIdle();
            require(device.pendingDeletions() == 0 && device.debugErrorCount() == 0,
                    "RHI resource cleanup/debug errors");
            std::cout << "PASS texture views, backing lifetime and deferred resource cleanup\n";
        }
        {
            auto window = graphicsTestWindow({.width = 256,
                                              .height = 256,
                                              .title = "Material tests",
                                              .visible = false,
                                              .vsync = false});
            auto renderer = graphicsTestRenderer(window);
            AssetStore assets;
            Scene scene;
            RenderExtractor extractor;
            auto cam = camera(scene);
            auto mesh = assets.createMesh(quad());
            auto red = assets.createMaterial(
                {.baseColor = {1, 0, 0, 0.5f}, .unlit = true, .alphaMode = AlphaMode::Blend});
            auto blue = assets.createMaterial(
                {.baseColor = {0, 0, 1, 0.5f}, .unlit = true, .alphaMode = AlphaMode::Blend});
            auto near = object(scene, mesh, red);
            object(scene, mesh, blue, Mat4::translation({0, 0, -0.5f}));
            auto stats =
                renderer.render(extractor.extract(scene, assets),
                                {.cameraNode = cam, .clearColor = {0, 0, 0, 1}, .shadows = false});
            auto center = pixel(renderer.readback(), 128, 128);
            require(std::abs(center.x - 188) < 5 && std::abs(center.z - 137) < 5,
                    "Transparent depth sorting/linear blending incorrect");
            require(stats.transparentDrawCalls == 2, "Transparent draw stats missing");
            auto mask = assets.createMaterial(
                {.baseColor = {1, 0, 0, 0.4f}, .unlit = true, .alphaMode = AlphaMode::Mask});
            scene.setComponent(near, MeshRenderer{mesh, {mask}});
            renderer.render(extractor.extract(scene, assets),
                            {.cameraNode = cam, .clearColor = {0, 0, 0, 1}, .shadows = false});
            center = pixel(renderer.readback(), 128, 128);
            require(center.x < 5 && center.z > 180, "Alpha-mask discard failed");
            auto bright = assets.createMaterial({.baseColor = {4, 4, 4, 1}, .unlit = true});
            scene.setComponent(near, MeshRenderer{mesh, {bright}});
            renderer.render(extractor.extract(scene, assets),
                            {.cameraNode = cam,
                             .shadows = false,
                             .exposure = 1,
                             .toneMapping = ToneMapping::Reinhard});
            center = pixel(renderer.readback(), 128, 128);
            require(center.x > 225 && center.x < 235, "HDR Reinhard tone mapping incorrect");
            for (int i = 0; i < 5; ++i)
                stats = renderer.render(
                    extractor.extract(scene, assets),
                    {.cameraNode = cam, .shadows = false, .toneMapping = ToneMapping::ACES});
            require(stats.gpuTimingValid && stats.passes.size() == 5 && stats.gpuMilliseconds > 0,
                    "Asynchronous GPU pass timing unavailable");
            require(renderer.debugErrorCount() == 0, "Material rendering GL errors");
            std::cout << "PASS transparent depth order, linear blending, alpha mask, HDR tone "
                         "mapping and GPU pass timings\n";
        }
        {
            auto window = graphicsTestWindow({.width = 128,
                                              .height = 128,
                                              .title = "Weighted OIT tests",
                                              .visible = false,
                                              .vsync = false});
            auto renderer =
                graphicsTestRenderer(window, {.framesInFlight = 2, .renderThread = true});
            AssetStore assets;
            Scene scene;
            RenderExtractor extractor;
            auto cam = camera(scene);
            auto mesh = assets.createMesh(quad());
            MaterialDesc redDesc{
                .baseColor = {1, 0, 0, 0.5f}, .unlit = true, .alphaMode = AlphaMode::Blend};
            MaterialDesc blueDesc = redDesc;
            blueDesc.baseColor = {0, 0, 1, 0.5f};
            auto red = assets.createMaterial(redDesc), blue = assets.createMaterial(blueDesc);
            auto a = object(scene, mesh, red, Mat4::rotationY(0.55f));
            auto b = object(scene, mesh, blue, Mat4::rotationY(-0.55f));
            RenderView view{.cameraNode = cam,
                            .clearColor = {0, 0, 0, 1},
                            .shadows = false,
                            .transparency = TransparencyMode::WeightedBlendedOIT};
            auto stats = renderer.render(extractor.extract(scene, assets), view);
            auto reference = renderer.readback();
            auto center = centerPixel(reference);
            // 两层 alpha=0.5 的同深度颜色：总覆盖率 0.75，红/蓝各贡献约 0.375。
            if (!(std::abs(center.x - 165) < 7 && std::abs(center.z - 165) < 7 && center.y < 3))
                throw std::runtime_error(
                    "OIT accumulation/revealage or linear composition incorrect: " +
                    std::to_string(center.x) + ", " + std::to_string(center.y) + ", " +
                    std::to_string(center.z));
            require(stats.transparentDrawCalls == 2 && stats.transparencyResolveDrawCalls == 1 &&
                        stats.drawCalls == stats.sceneDrawCalls + stats.shadowDrawCalls + 2,
                    "OIT resolve draw statistics incorrect");

            // 交换材质创建顺序会反转按材质排序的 GPU 绘制顺序，几何/颜色保持不变。
            auto blueFirst = assets.createMaterial(blueDesc),
                 redSecond = assets.createMaterial(redDesc);
            scene.setComponent(a, MeshRenderer{mesh, {redSecond}});
            scene.setComponent(b, MeshRenderer{mesh, {blueFirst}});
            renderer.render(extractor.extract(scene, assets), view);
            auto reversed = renderer.readback();
            for (size_t i = 0; i < reference.pixels.size(); ++i)
                require(std::abs(int(reference.pixels[i]) - int(reversed.pixels[i])) <= 2,
                        "OIT output changes when transparent draw order is reversed");

            view.transparency = TransparencyMode::SortedAlpha;
            stats = renderer.render(extractor.extract(scene, assets), view);
            center = centerPixel(renderer.readback());
            require(std::abs(center.x - center.z) > 30 && stats.transparencyResolveDrawCalls == 0,
                    "Switching back to sorted alpha retained OIT blend/resolve state");
            view.transparency = TransparencyMode::WeightedBlendedOIT;

            auto green = assets.createMaterial({.baseColor = {0, 1, 0, 1}, .unlit = true});
            auto occluder = object(scene, mesh, green, Mat4::translation({0, 0, 0.8f}));
            renderer.render(extractor.extract(scene, assets), view);
            center = centerPixel(renderer.readback());
            require(center.y > 250 && center.x < 3 && center.z < 3,
                    "OIT transparent layers ignored opaque depth occlusion");
            scene.setVisible(occluder, false);

            auto zero = assets.createMaterial(
                {.baseColor = {1000, 1000, 1000, 0}, .unlit = true, .alphaMode = AlphaMode::Blend});
            auto zeroNode = object(scene, mesh, zero, Mat4::translation({0, 0, 1}));
            renderer.render(extractor.extract(scene, assets), view);
            center = centerPixel(renderer.readback());
            require(std::abs(center.x - 165) < 7 && std::abs(center.z - 165) < 7 && center.y < 3,
                    "Zero-alpha OIT surface changed the image");
            scene.setVisible(zeroNode, false);
            scene.setVisible(b, false);
            redDesc.baseColor.w = 1;
            scene.setComponent(a, MeshRenderer{mesh, {assets.createMaterial(redDesc)}});
            renderer.render(extractor.extract(scene, assets), view);
            center = centerPixel(renderer.readback());
            require(center.x > 250 && center.y < 3 && center.z < 3,
                    "Fully covered OIT surface did not replace the background");

            // 高亮且深度重叠的多层表面验证累积精度，合成后才进行曝光与 ToneMap。
            scene.setVisible(a, false);
            auto bright = assets.createMaterial(
                {.baseColor = {1024, 256, 64, 0.5f}, .unlit = true, .alphaMode = AlphaMode::Blend});
            auto layers = scene.createNode(scene.root());
            for (int i = 0; i < 64; ++i) {
                auto node = scene.createNode(layers);
                scene.setComponent(node, MeshRenderer{mesh, {bright}});
            }
            view.exposure = 1.0f / 128;
            view.toneMapping = ToneMapping::Reinhard;
            for (int i = 0; i < 5; ++i)
                stats = renderer.render(extractor.extract(scene, assets), view);
            center = centerPixel(renderer.readback());
            require(std::abs(center.x - 242) < 4 && std::abs(center.y - 213) < 4 &&
                        std::abs(center.z - 156) < 4,
                    "Dense HDR OIT layers overflowed or were tone mapped before composition");
            bool resolveTiming = false;
            for (const auto &pass : stats.passes)
                resolveTiming |= pass.name == "OIT Resolve";
            require(stats.transparentDrawCalls == 1 && stats.gpuTimingValid &&
                        stats.passes.size() == 6 && resolveTiming,
                    "OIT instancing or asynchronous pass timing unavailable");
            renderer.present();
            window.resize(160, 96);
            window.pollEvents();
            renderer.render(extractor.extract(scene, assets), view);
            auto resized = renderer.readback();
            auto [width, height] = window.framebufferSize();
            require(resized.width == static_cast<uint32_t>(width) &&
                        resized.height == static_cast<uint32_t>(height),
                    "OIT resize retained stale render targets");

            scene.setVisible(layers, false);
            renderer.render(extractor.extract(scene, assets), view);
            center = pixel(renderer.readback(), resized.width / 2, resized.height / 2);
            require(center.x < 2 && center.y < 2 && center.z < 2,
                    "Empty OIT frame retained accumulation from the previous frame");
            renderer.waitIdle();
            require(renderer.debugErrorCount() == 0, "Weighted OIT GL errors");
            std::cout << "PASS weighted OIT order independence, coverage, depth, mode switching, "
                         "HDR, instancing, timings, resize and render thread\n";
        }
        {
            auto window = graphicsTestWindow({.width = 256,
                                              .height = 256,
                                              .title = "Shadow tests",
                                              .visible = false,
                                              .vsync = false});
            auto renderer = graphicsTestRenderer(window);
            AssetStore assets;
            Scene scene;
            RenderExtractor extractor;
            auto cam = camera(scene, {3, 5, 6}, {0, 0, 0});
            auto cube = assets.createMesh(geometry::cube()),
                 plane = assets.createMesh(geometry::plane(8));
            auto white =
                assets.createMaterial({.baseColor = {0.75f, 0.75f, 0.75f, 1}, .roughness = 0.8f});
            object(scene, plane, white);
            auto body =
                object(scene, cube, white,
                       Mat4::translation({0, 0.75f, 0}) * Mat4::scaling({1.5f, 1.5f, 1.5f}));
            auto snapshot = extractor.extract(scene, assets);
            RenderView view{.cameraNode = cam,
                            .lightDirection = {0.8f, -1, 0.2f},
                            .ambientLight = 0.05f,
                            .shadows = false,
                            .shadowResolution = 512};
            renderer.render(snapshot, view);
            auto noShadow = renderer.readback();
            view.shadows = true;
            renderer.render(snapshot, view);
            auto shadow = renderer.readback();
            size_t darker = 0;
            for (size_t i = 0; i < shadow.pixels.size(); i += 4)
                if (int(noShadow.pixels[i]) - int(shadow.pixels[i]) > 12)
                    ++darker;
            require(darker > 100, "Directional shadow map did not darken receiving surfaces");
            auto metallic = assets.createMaterial(
                {.baseColor = {0.75f, 0.75f, 0.75f, 1}, .metallic = 1, .roughness = 0.1f});
            scene.setComponent(body, MeshRenderer{cube, {metallic}});
            renderer.render(extractor.extract(scene, assets), view);
            auto metalImage = renderer.readback();
            size_t different = 0;
            for (size_t i = 0; i < shadow.pixels.size(); i += 4)
                if (std::abs(int(metalImage.pixels[i]) - int(shadow.pixels[i])) > 10)
                    ++different;
            require(different > 100, "Metallic/roughness PBR factors had no visible effect");
            scene.setVisible(body, false);
            auto groundOnly = extractor.extract(scene, assets);
            view.shadows = false;
            renderer.render(groundOnly, view);
            auto reference = renderer.readback();
            view.shadows = true;
            renderer.render(groundOnly, view);
            auto selfShadow = renderer.readback();
            size_t acne = 0;
            for (size_t i = 0; i < reference.pixels.size(); i += 4)
                if (int(reference.pixels[i]) - int(selfShadow.pixels[i]) > 8)
                    ++acne;
            require(acne < 20,
                    "An unoccluded receiver self-shadows; shadow bias/gradient regression");
            require(renderer.debugErrorCount() == 0, "Shadow/PBR GL errors");
            std::cout << "PASS directional shadow receiving and metallic/roughness PBR\n";
        }
        {
            auto window =
                std::make_unique<Window>(graphicsTestConfig(WindowConfig{.width = 96,
                                                                         .height = 96,
                                                                         .title = "Thread tests",
                                                                         .visible = false,
                                                                         .vsync = false}));
            auto renderer = std::make_unique<Renderer>(
                graphicsTestRenderer(*window, {.renderThread = true, .maxQueuedFrames = 2}));
            AssetStore assets;
            Scene scene;
            RenderExtractor extractor;
            auto cam = camera(scene);
            auto mesh = assets.createMesh(geometry::cube());
            auto red = assets.createMaterial({.baseColor = {1, 0, 0, 1}, .unlit = true});
            object(scene, mesh, red);
            std::vector<std::future<RenderStats>> frames;
            for (int i = 0; i < 6; ++i)
                frames.push_back(renderer->submit(extractor.extract(scene, assets),
                                                  {.cameraNode = cam, .shadows = false}));
            for (auto &frame : frames)
                require(frame.get().visibleInstances == 1, "Async snapshot was not rendered");
            renderer->present();
            window->resize(128, 80);
            window->pollEvents();
            renderer->render(extractor.extract(scene, assets),
                             {.cameraNode = cam, .shadows = false});
            auto image = renderer->readback();
            auto size = window->framebufferSize();
            require(image.width == static_cast<uint32_t>(size.first) &&
                        image.height == static_cast<uint32_t>(size.second),
                    "Resize did not recreate graph targets");
            window.reset();
            renderer->waitIdle();
            require(renderer->debugErrorCount() == 0, "Render-thread context errors");
            renderer.reset();
            std::cout << "PASS bounded async submission, render-thread presentation, resize and "
                         "context lifetime\n";
        }
        std::cout << "All advanced GPU tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "ADVANCED GPU TEST FAILED: " << e.what() << '\n';
        return 1;
    }
}
