#include "graphics_fixture.hpp"
#include "renderer/renderer.hpp"
#include "renderer/window.hpp"
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace renderer;
namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
constexpr std::array<Vec2, 3> triangle{{{-0.83f, -0.76f}, {0.81f, -0.3f}, {-0.17f, 0.79f}}};
float side(Vec2 a, Vec2 b, Vec2 p) {
    return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}
bool inside(const std::array<Vec2, 3> &vertices, Vec2 point) {
    const float a = side(vertices[0], vertices[1], point);
    const float b = side(vertices[1], vertices[2], point);
    const float c = side(vertices[2], vertices[0], point);
    return (a >= 0 && b >= 0 && c >= 0) || (a <= 0 && b <= 0 && c <= 0);
}
double coverageError(const Image &image) {
    std::array<Vec2, 3> pixels{};
    for (size_t i = 0; i < triangle.size(); ++i)
        pixels[i] = {(triangle[i].x * 0.5f + 0.5f) * float(image.width),
                     (0.5f - triangle[i].y * 0.5f) * float(image.height)};
    double error = 0;
    // 独立的 8×8 几何覆盖参考，使用 FXAA 所处理的显示编码颜色进行比较。
    for (uint32_t y = 0; y < image.height; ++y)
        for (uint32_t x = 0; x < image.width; ++x) {
            unsigned covered = 0;
            for (unsigned sy = 0; sy < 8; ++sy)
                for (unsigned sx = 0; sx < 8; ++sx)
                    covered += inside(pixels, {float(x) + (float(sx) + 0.5f) / 8,
                                               float(y) + (float(sy) + 0.5f) / 8});
            const double expected = 255.0 * covered / 64;
            const double difference = image.pixels[(size_t{y} * image.width + x) * 4] - expected;
            error += difference * difference;
        }
    return error / double(uint64_t{image.width} * image.height);
}
size_t fractionalPixels(const Image &image, uint8_t maximum = 255) {
    size_t count = 0;
    for (size_t i = 0; i < image.pixels.size(); i += 4)
        count += image.pixels[i] > 4 && image.pixels[i] + 4 < maximum;
    return count;
}
void nearImage(const Image &a, const Image &b, int tolerance, const char *message) {
    require(a.width == b.width && a.height == b.height && a.pixels.size() == b.pixels.size(),
            message);
    for (size_t i = 0; i < a.pixels.size(); ++i)
        require(std::abs(int(a.pixels[i]) - int(b.pixels[i])) <= tolerance, message);
}
void checkInterior(const Image &a, const Image &b) {
    const uint32_t x = static_cast<uint32_t>(float(a.width) * 0.47f);
    const uint32_t y = static_cast<uint32_t>(float(a.height) * 0.55f);
    const size_t center = (size_t{y} * a.width + x) * 4;
    for (size_t channel = 0; channel < 4; ++channel)
        require(std::abs(int(a.pixels[center + channel]) - int(b.pixels[center + channel])) <= 1 &&
                    std::abs(int(a.pixels[channel]) - int(b.pixels[channel])) <= 1,
                "FXAA changed solid interiors or background encoding");
    for (size_t i = 3; i < b.pixels.size(); i += 4)
        require(b.pixels[i] == 255, "FXAA output alpha is not opaque");
}
} // namespace
int main(int argc, char **argv) {
    try {
        for (bool threaded : {false, true}) {
            auto window = graphicsTestWindow({.width = 256,
                                              .height = 192,
                                              .title = "FXAA tests",
                                              .visible = false,
                                              .vsync = false});
            auto renderer = graphicsTestRenderer(
                window,
                {.framesInFlight = 2, .initialInstanceCapacity = 1, .renderThread = threaded});
            const auto [width, height] = window.framebufferSize();
            AssetStore assets;
            Scene scene;
            RenderExtractor extractor;
            const auto camera = scene.createNode(scene.root());
            Camera cameraData;
            cameraData.projection = Camera::Projection::Orthographic;
            cameraData.orthographicHeight = 2;
            scene.setComponent(camera, cameraData);
            scene.setLocalTransform(camera, Mat4::translation({0, 0, 3}));
            MeshDesc description;
            for (const auto point : triangle)
                description.vertices.push_back(
                    {{point.x * float(width) / float(height), point.y, 0}, {0, 0, 1}, {0, 0}});
            description.indices = {0, 1, 2};
            const auto mesh = assets.createMesh(std::move(description));
            const auto white = assets.createMaterial({.baseColor = {1, 1, 1, 1}, .unlit = true});
            const auto node = scene.createNode(scene.root());
            scene.setComponent(node, MeshRenderer{mesh, {white}});
            const auto snapshot = extractor.extract(scene, assets);
            RenderView view{.cameraNode = camera, .clearColor = {0, 0, 0, 1}, .shadows = false};
            const auto baselineStats = renderer.render(snapshot, view);
            const auto baseline = renderer.readback();
            require(baselineStats.antiAliasingDrawCalls == 0,
                    "Anti-aliasing unexpectedly enabled by default");
            view.antiAliasing = AntiAliasing::FXAA;
            auto stats = renderer.render(snapshot, view);
            const auto filtered = renderer.readback();
            const auto baselineError = coverageError(baseline),
                       filteredError = coverageError(filtered);
            std::cout << "Coverage MSE " << baselineError << " -> " << filteredError
                      << ", filtered pixels " << fractionalPixels(filtered)
                      << ", threaded=" << threaded << '\n';
            require(filteredError < baselineError * 0.95 && fractionalPixels(filtered) > 100,
                    "FXAA did not improve slanted-edge coverage error");
            require(stats.antiAliasingDrawCalls == 1 &&
                        stats.drawCalls == baselineStats.drawCalls + 1 &&
                        stats.triangles == baselineStats.triangles &&
                        stats.sceneDrawCalls == baselineStats.sceneDrawCalls,
                    "FXAA pass or geometry statistics are incorrect");
            checkInterior(baseline, filtered);
            if (argc == 2 && !threaded) {
                const auto folder = std::filesystem::path(argv[1]);
                std::filesystem::create_directories(folder);
                baseline.savePpm((folder / "none.ppm").string());
                filtered.savePpm((folder / "fxaa.ppm").string());
            }
            for (int i = 0; i < 3; ++i) {
                view.antiAliasing = AntiAliasing::None;
                renderer.render(snapshot, view);
                nearImage(baseline, renderer.readback(), 0,
                          "Disabled FXAA did not restore original image");
                view.antiAliasing = AntiAliasing::FXAA;
                renderer.render(snapshot, view);
                nearImage(filtered, renderer.readback(), 0,
                          "FXAA toggle retained stale graph bindings");
            }

            // 平坦画面与不同 ToneMap/曝光组合不能因额外编码而变亮或变暗。
            scene.setVisible(node, false);
            const auto empty = extractor.extract(scene, assets);
            for (const auto tone : {ToneMapping::None, ToneMapping::ACES, ToneMapping::Reinhard}) {
                view.clearColor = {0.18f, 0.41f, 0.72f, 1};
                view.exposure = 0.7f;
                view.toneMapping = tone;
                view.antiAliasing = AntiAliasing::None;
                renderer.render(empty, view);
                const auto flat = renderer.readback();
                view.antiAliasing = AntiAliasing::FXAA;
                renderer.render(empty, view);
                nearImage(flat, renderer.readback(), 1, "FXAA changed flat-image brightness");
            }
            scene.setVisible(node, true);
            for (const auto mode :
                 {TransparencyMode::SortedAlpha, TransparencyMode::WeightedBlendedOIT}) {
                const auto material = assets.createMaterial(
                    {.baseColor = {1, 1, 1, 0.5f}, .unlit = true, .alphaMode = AlphaMode::Blend});
                scene.setComponent(node, MeshRenderer{mesh, {material}});
                const auto transparent = extractor.extract(scene, assets);
                view.clearColor = {0, 0, 0, 1};
                view.exposure = 1;
                view.toneMapping = ToneMapping::None;
                view.transparency = mode;
                view.antiAliasing = AntiAliasing::None;
                const auto before = renderer.render(transparent, view);
                const auto alphaBaseline = renderer.readback();
                view.antiAliasing = AntiAliasing::FXAA;
                stats = renderer.render(transparent, view);
                const auto alphaFiltered = renderer.readback();
                require(stats.drawCalls == before.drawCalls + 1 &&
                            stats.transparencyResolveDrawCalls ==
                                before.transparencyResolveDrawCalls &&
                            fractionalPixels(alphaFiltered, 188) > 100,
                        "FXAA failed to filter sorted-alpha or OIT edges");
                checkInterior(alphaBaseline, alphaFiltered);
            }

            // 最终 HDR 曝光及 ToneMap 必须先于 FXAA，内部区域颜色应保持一致。
            const auto hdrMaterial =
                assets.createMaterial({.baseColor = {8, 0.3f, 2, 1}, .unlit = true});
            scene.setComponent(node, MeshRenderer{mesh, {hdrMaterial}});
            const auto hdrSnapshot = extractor.extract(scene, assets);
            view.toneMapping = ToneMapping::ACES;
            view.exposure = 0.7f;
            view.antiAliasing = AntiAliasing::None;
            renderer.render(hdrSnapshot, view);
            const auto hdrBaseline = renderer.readback();
            view.antiAliasing = AntiAliasing::FXAA;
            for (int i = 0; i < 6; ++i)
                stats = renderer.render(hdrSnapshot, view);
            checkInterior(hdrBaseline, renderer.readback());
            bool fxaaTiming = false;
            for (const auto &pass : stats.passes)
                fxaaTiming |= pass.name == "FXAA" && pass.gpuMilliseconds >= 0;
            require(stats.gpuTimingValid && stats.passes.size() == 7 && fxaaTiming,
                    "OIT plus FXAA pass timings were not recorded");
            window.resize(320, 224);
            renderer.render(hdrSnapshot, view);
            auto resized = renderer.readback();
            auto actual = window.framebufferSize();
            require(resized.width == uint32_t(actual.first) &&
                        resized.height == uint32_t(actual.second),
                    "FXAA used stale target dimensions after resize");
            window.resize(256, 1);
            renderer.render(empty, view);
            resized = renderer.readback();
            actual = window.framebufferSize();
            require(resized.width == uint32_t(actual.first) &&
                        resized.height == uint32_t(actual.second),
                    "FXAA failed with a narrow framebuffer");
            renderer.present();
            renderer.waitIdle();
            require(renderer.debugErrorCount() == 0, "FXAA generated graphics errors");
        }
        std::cout << "PASS FXAA coverage, color encoding, toggles, transparency, HDR, timing, "
                     "resize and threads\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "ANTI-ALIASING TEST FAILED: " << error.what() << '\n';
        return 1;
    }
}
