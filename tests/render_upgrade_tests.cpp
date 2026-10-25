/** @file @brief 在双后端真实像素中验证上传预算、增量实例、自定义材质、混合与多相机 GPU 纹理。 */
#include <algorithm>
#include <iostream>
#include <renderer/renderer.hpp>
#include <renderer/sprite.hpp>
using namespace renderer;
namespace {
void require(bool b, const char *s) {
    if (!b)
        throw std::runtime_error(s);
}
template <class F> void throws(F f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception &) {
        failed = true;
    }
    require(failed, "Expected rejection");
}
Renderer create(bool threaded, uint64_t budget = 0) {
    RendererConfig c;
    c.framesInFlight = 2;
    c.renderThread = threaded;
    c.assetUploadBudgetBytes = budget;
#ifdef RENDERER_TEST_VULKAN
    return Renderer::createVulkanOffscreen({256, 192}, c);
#else
    return Renderer::createOpenGLOffscreen({256, 192}, c);
#endif
}
struct World {
    AssetStore assets;
    Scene scene;
    RenderExtractor extractor;
    NodeHandle camera;
    World() {
        camera = scene.createNode(scene.root());
        scene.setComponent(camera, Camera{.projection = Camera::Projection::Orthographic,
                                          .orthographicHeight = 4});
        scene.setLocalTransform(camera, Mat4::translation({0, 0, 5}));
    }
    NodeHandle sprite(MaterialDesc material, TextureHandle texture = {}, bool shadow = false) {
        if (!texture)
            texture = assets.createTexture({2, 2, std::vector<uint8_t>(16, 255), false});
        material.baseColorTexture = texture;
        material.doubleSided = true;
        auto mat = assets.createMaterial(material);
        const auto d = assets.textureDescription(texture);
        SpriteDesc desc;
        desc.texture = texture;
        desc.rect = {0, 0, d.width, d.height};
        desc.pivot = {.5f, .5f};
        desc.pixelsPerUnit = float(d.height) / 2;
        desc.material = mat;
        auto n = scene.createNode(scene.root());
        scene.setComponent(n, SpriteRenderer{.sprite = assets.createSprite(desc),
                                             .billboard = BillboardMode::Fixed,
                                             .transparency = SpriteTransparency::FollowView,
                                             .castShadow = shadow});
        return n;
    }
    RenderSnapshot snapshot() {
        return extractor.extract(scene, assets);
    }
    RenderView view() {
        RenderView v;
        v.cameraNode = camera;
        v.shadows = false;
        v.clearColor = {0, 0, 0, 1};
        return v;
    }
};
unsigned channel(const Image &image, int x, int y, int c) {
    return image.pixels[(size_t(y) * image.width + size_t(x)) * 4 + size_t(c)];
}
} // namespace
int main() {
    try {
        for (bool threaded : {false, true}) {
            auto other = create(true);
            auto gpu = create(threaded, 1);
            World world;
            auto node = world.sprite({.baseColor = {1, 0, 0, 1}, .unlit = true});
            auto snapshot = world.snapshot();
            auto view = world.view();
            auto first = gpu.render(snapshot, view);
            require(first.pendingResourceDraws > 0,
                    "Upload budget did not defer incomplete material");
            RenderStats steady;
            for (int i = 0; i < 8; ++i)
                steady = gpu.render(snapshot, view);
            require(!steady.pendingResourceDraws && !steady.instanceUploadedBytes &&
                        steady.instanceReusedBytes > 0,
                    "Static slot data uploaded again");
            auto image = gpu.readback();
            require(channel(image, 128, 96, 0) > 240, "Deferred assets never became visible");
            world.scene.setLocalTransform(node, Mat4::translation({.2f, 0, 0}));
            auto moved = gpu.render(world.snapshot(), view);
            require(moved.instanceUploadedBytes == 224 && moved.instanceUploadRanges == 1,
                    "Changed instance was not uploaded as one range");
            require(gpu.memoryStatistics().meshBytes > 0 && gpu.memoryStatistics().textureBytes > 0,
                    "GPU memory payload absent");
            World effect;
            MaterialDesc m;
            m.unlit = true;
            m.effect.surface = "base.rgb=effectParameters.rgb;";
            m.effect.vertex = "position.x+=effectParameters.w;";
            m.effect.parameters = {0, 1, 0, .15f};
            m.effect.boundsPadding = .2f;
            effect.sprite(m);
            for (int i = 0; i < 5; ++i)
                gpu.render(effect.snapshot(), effect.view());
            image = gpu.readback();
            require(channel(image, 128, 96, 1) > 240 && channel(image, 128, 96, 0) < 5,
                    "Custom material effect not executed");
            World cutout;
            MaterialDesc mask;
            mask.unlit = true;
            mask.alphaMode = AlphaMode::Mask;
            mask.effect.vertex = "position.y+=effectParameters.y;";
            mask.effect.surface = "if(uv.x<effectParameters.x)base.a=0.0;";
            mask.effect.parameters = {.5f, 0, 0, 0};
            mask.baseColor = {0, 1, 0, 1};
            cutout.sprite(mask, {}, true);
            auto shadowView = cutout.view();
            shadowView.shadows = true;
            for (int i = 0; i < 6; ++i)
                gpu.render(cutout.snapshot(), shadowView);
            image = gpu.readback();
            require(channel(image, 104, 96, 1) < 5 && channel(image, 152, 96, 1) > 240,
                    "Custom Alpha Mask/shadow shader did not preserve coverage");
            World blend;
            MaterialDesc a;
            a.unlit = true;
            a.alphaMode = AlphaMode::Blend;
            a.baseColor = {.5f, 0, 0, .5f};
            a.blend = MaterialBlend::Alpha;
            auto alpha = blend.sprite(a);
            for (int i = 0; i < 5; ++i)
                gpu.render(blend.snapshot(), blend.view());
            auto straight = gpu.readback();
            blend.scene.destroySubtree(alpha);
            a.blend = MaterialBlend::PremultipliedAlpha;
            blend.sprite(a);
            for (int i = 0; i < 5; ++i)
                gpu.render(blend.snapshot(), blend.view());
            auto premultiplied = gpu.readback();
            require(std::abs(int(channel(straight, 128, 96, 0)) -
                             int(channel(premultiplied, 128, 96, 0))) <= 1,
                    "Premultiplied output changed alpha appearance");
            World additive;
            a.blend = MaterialBlend::Additive;
            additive.sprite(a);
            additive.sprite(a);
            auto av = additive.view();
            av.transparency = TransparencyMode::WeightedBlendedOIT;
            for (int i = 0; i < 8; ++i)
                gpu.render(additive.snapshot(), av);
            image = gpu.readback();
            require(channel(image, 128, 96, 0) > channel(straight, 128, 96, 0) + 30,
                    "Additive particles were mixed into OIT revealage");
            // 多相机目标使用相同设备，可供后续场景采样，纹理内容随下一次输出更新。
            auto target = gpu.createRenderTexture({80, 60});
            World source;
            auto sv = source.view();
            sv.clearColor = {0, 1, 0, 1};
            gpu.renderToTexture(source.snapshot(), sv, target);
            World monitor;
            auto registered = monitor.assets.createTexture(target);
            monitor.sprite({.unlit = true}, registered);
            for (int i = 0; i < 5; ++i)
                gpu.render(monitor.snapshot(), monitor.view());
            image = gpu.readback();
            require(channel(image, 128, 96, 1) > 240 && channel(image, 128, 96, 0) < 5,
                    "GPU camera texture did not sample rendered output");
            sv.clearColor = {0, 0, 1, 1};
            gpu.renderToTexture(source.snapshot(), sv, target);
            gpu.render(monitor.snapshot(), monitor.view());
            image = gpu.readback();
            require(channel(image, 128, 96, 2) > 240 && channel(image, 128, 96, 1) < 5,
                    "GPU camera texture did not update in queue order");
            auto red = source.sprite({.baseColor = {1, 0, 0, 1}, .unlit = true});
            auto blue = source.sprite({.baseColor = {0, 0, 1, 1}, .unlit = true});
            source.scene.setLocalTransform(red, Mat4::translation({0, .65f, 0}) *
                                                    Mat4::scaling({.45f, .45f, 1}));
            source.scene.setLocalTransform(blue, Mat4::translation({0, -.65f, 0}) *
                                                     Mat4::scaling({.45f, .45f, 1}));
            sv.clearColor = {0, 0, 0, 1};
            for (int i = 0; i < 8; ++i)
                gpu.renderToTexture(source.snapshot(), sv, target);
            gpu.render(monitor.snapshot(), monitor.view());
            image = gpu.readback();
            require(channel(image, 128, 80, 0) > 240 && channel(image, 128, 80, 2) < 5 &&
                        channel(image, 128, 112, 2) > 240,
                    "Camera texture vertical orientation differs from regular textures");
            throws([&] { gpu.renderToTexture(monitor.snapshot(), monitor.view(), target); });
            gpu.render(monitor.snapshot(), monitor.view());
            throws([&] { other.render(monitor.snapshot(), monitor.view()); });
            gpu.waitIdle();
            require(!gpu.debugErrorCount() && !other.debugErrorCount(),
                    "Graphics validation failed");
        }
        World stale;
        RenderTexture expired;
        {
            auto gpu = create(false);
            expired = gpu.createRenderTexture({4, 4});
            stale.sprite({.unlit = true}, stale.assets.createTexture(expired));
        }
        require(!expired, "Texture remained valid after renderer destruction");
        {
            auto gpu = create(false);
            throws([&] { gpu.render(stale.snapshot(), stale.view()); });
        }
        std::cout << "GPU upload, incremental, effects, blend and camera texture passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
