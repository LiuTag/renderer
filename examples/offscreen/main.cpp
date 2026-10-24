/** @file @brief 不创建应用窗口，直接把森林 Billboard 场景渲染为 PNG；仅依赖 core 与图形后端。 */
#include "../common/forest_assets.hpp"
#include <filesystem>
#include <iostream>
#include <random>
#include <renderer/renderer.hpp>
#include <stdexcept>
using namespace renderer;
static int run(int argc, char **argv) {
    try {
#ifdef RENDERER_OFFSCREEN_VULKAN
        std::string backend = "vulkan"; // Vulkan 路径完全不初始化窗口系统。
#else
        std::string backend = "opengl";
#endif
        OffscreenConfig target{1280, 720};
        RendererConfig config;
        RenderView view{.shadows = false, .antiAliasing = AntiAliasing::FXAA};
        std::filesystem::path output = "offscreen.png";
        bool selfTest = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto value = [&] {
                if (++i >= argc)
                    throw std::invalid_argument("Missing option value");
                return std::string(argv[i]);
            };
            auto dimension = [&] {
                const auto input = value();
                size_t end = 0;
                const auto n = std::stoul(input, &end);
                if (end != input.size() || n < 1 || n > 16384)
                    throw std::invalid_argument("Dimension must be in [1,16384]");
                return uint32_t(n);
            };
            if (arg == "--backend")
                backend = value();
            else if (arg == "--output") {
                const auto text = value();
                output = std::filesystem::path(std::u8string(text.begin(), text.end()));
            } else if (arg == "--width")
                target.width = dimension();
            else if (arg == "--height")
                target.height = dimension();
            else if (arg == "--threaded")
                config.renderThread = true;
            else if (arg == "--oit")
                view.transparency = TransparencyMode::WeightedBlendedOIT;
            else if (arg == "--no-fxaa")
                view.antiAliasing = AntiAliasing::None;
            else if (arg == "--self-test")
                selfTest = true;
            else if (arg == "--help") {
                std::cout
                    << "renderer_offscreen_demo [--backend vulkan|opengl] [--output file.png] "
                       "[--width 1280] [--height 720] [--threaded] [--oit] [--no-fxaa] "
                       "[--self-test]\n";
                return 0;
            } else
                throw std::invalid_argument("Unknown option: " + arg);
        }
        auto engine = [&]() -> Renderer {
#ifdef RENDERER_OFFSCREEN_VULKAN
            if (backend == "vulkan")
                return Renderer::createVulkanOffscreen(target, config);
#endif
#ifdef RENDERER_OFFSCREEN_OPENGL
            if (backend == "opengl")
                return Renderer::createOpenGLOffscreen(target, config);
#endif
            throw std::invalid_argument("Backend is unavailable: " + backend);
        }();
        AssetStore assets;
        Scene scene;
        RenderExtractor extractor;
        auto camera = scene.createNode(scene.root(), "Camera");
        scene.setComponent(camera, Camera{.projection = Camera::Projection::Orthographic,
                                          .orthographicHeight = 13});
        scene.setLocalTransform(camera, *Mat4::lookAt({7, 11, 16}, {0, 0, 0}).inverse());
        view.cameraNode = camera;
        const auto texture = assets.createTexture(forest_assets::makeAtlas());
        SpriteAtlas atlas(assets, texture, forest_assets::makeEntries());
        // 图集、图片角色、竖直 Billboard 和脚底阴影与交互森林示例使用相同资源约定。
        auto add = [&](int index, Vec3 p) {
            auto node = scene.createNode(scene.root());
            scene.setLocalTransform(node, Mat4::translation(p));
            scene.setComponent(node, SpriteRenderer{.sprite = atlas.sprite(std::to_string(index))});
            return node;
        };
        const auto ground = scene.createNode(scene.root(), "Ground");
        auto groundImage = geometry::checkerboard(128, 32);
        for (size_t i = 0; i < groundImage.pixels.size(); i += 4) {
            const auto bright = groundImage.pixels[i] > 128;
            groundImage.pixels[i] = bright ? 97 : 91;
            groundImage.pixels[i + 1] = bright ? 121 : 115;
            groundImage.pixels[i + 2] = bright ? 66 : 61;
        }
        auto material = assets.createMaterial(
            {.baseColorTexture = assets.createTexture(groundImage), .unlit = true});
        scene.setComponent(ground,
                           MeshRenderer{assets.createMesh(geometry::plane(70)), {material}});
        std::mt19937 random(42);
        std::uniform_real_distribution<float> x(-11, 11), z(-7, 7);
        for (int i = 0; i < 135; ++i) {
            Vec3 p{x(random), 0, z(random)};
            if (p.x * p.x + p.z * p.z < 1.8f * 1.8f)
                continue;
            const bool tree = i < 38, rock = !tree && i < 65;
            add(tree ? 16 + i % 3 : rock ? 19 + i % 2 : 24, p);
            if (tree || rock) {
                auto shadow = add(21, p + Vec3{0, .012f, 0});
                scene.setLocalTransform(shadow, Mat4::translation(p + Vec3{0, .012f, 0}) *
                                                    Mat4::rotationX(radians(-90)));
                auto sprite = *scene.component<SpriteRenderer>(shadow);
                sprite.billboard = BillboardMode::Fixed;
                sprite.sortingLayer = -10;
                sprite.sizeMultiplier = {tree ? 1.1f : .7f, tree ? .7f : .4f};
                scene.setComponent(shadow, sprite);
            }
        }
        auto player = scene.createNode(scene.root(), "Player");
        scene.setComponent(player, SortingGroup{});
        for (int index : {0, 22}) {
            auto part = add(index, {});
            scene.reparent(part, player);
        }
        const auto snapshot = extractor.extract(scene, assets);
        const auto image = engine.renderToImage(snapshot, view);
        image.savePng(output);
        if (selfTest) {
            const auto stats = engine.render(snapshot, view);
            size_t colored = 0;
            for (size_t i = 0; i < image.pixels.size(); i += 4)
                colored +=
                    image.pixels[i] != image.pixels[0] || image.pixels[i + 1] != image.pixels[1];
            if (image.width != target.width || image.height != target.height ||
                colored < size_t(target.width) * target.height / 20 ||
                stats.visibleInstances < 30 || !std::filesystem::is_regular_file(output))
                throw std::runtime_error("Offscreen forest self-test failed");
        }
        engine.waitIdle();
        if (engine.debugErrorCount())
            throw std::runtime_error("GPU validation reported errors");
        const auto path = output.u8string();
        std::cout << engine.deviceInfo().backend << " " << image.width << 'x' << image.height
                  << " sprites=" << snapshot.spriteCount() << " -> "
                  << std::string(path.begin(), path.end()) << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
#ifdef _WIN32
// Windows 原始命令行是 UTF-16，先转 UTF-8，避免中文路径受系统 ANSI 代码页影响。
int wmain(int argc, wchar_t **argv) {
    std::vector<std::string> arguments;
    std::vector<char *> pointers;
    arguments.reserve(size_t(argc));
    for (int i = 0; i < argc; ++i) {
        const auto text = std::filesystem::path(argv[i]).u8string();
        arguments.emplace_back(text.begin(), text.end());
    }
    for (auto &argument : arguments)
        pointers.push_back(argument.data());
    return run(argc, pointers.data());
}
#else
int main(int argc, char **argv) {
    return run(argc, argv);
}
#endif
