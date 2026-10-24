/** @file @brief 森林图片精灵性能测试：数量扫描、双后端及线程对比、预热与 CSV 统计。 */
#include "../common/forest_assets.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <renderer/renderer.hpp>
#include <renderer/sprite_snapshot.hpp>
#include <renderer/window.hpp>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using namespace renderer;
namespace {
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::time_point begin) {
    return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}
void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
/** @brief 应用私有的测试选项，所有组合共用同一份参数。 */
struct Options {
    std::vector<uint32_t> counts{1000, 5000, 10000, 20000};
    std::vector<uint32_t> atlasImages{25};
    std::string backend = "all", threads = "all", transparency = "sorted";
    uint32_t frames = 240, warmup = 60, width = 1280, height = 800, seed = 42, repeats = 1;
    float sceneSize = 28, spriteScale = 1, targetFps = 60;
    bool hidden = false, fxaa = true, animate = true, validation = false, selfTest = false;
    std::filesystem::path csv = "sprite-benchmark.csv", capture;
};
uint32_t integer(const std::string &text, uint32_t minimum, uint32_t maximum) {
    uint32_t result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
                result >= minimum && result <= maximum,
            "Invalid integer: " + text);
    return result;
}
float real(const std::string &text, float minimum, float maximum) {
    size_t consumed = 0;
    const auto result = std::stof(text, &consumed);
    require(consumed == text.size() && std::isfinite(result) && result >= minimum &&
                result <= maximum,
            "Invalid number: " + text);
    return result;
}
std::vector<uint32_t> integerList(const std::string &list, uint32_t minimum, uint32_t maximum) {
    std::vector<uint32_t> result;
    size_t begin = 0;
    for (;;) {
        const auto end = list.find(',', begin);
        result.push_back(integer(list.substr(begin, end - begin), minimum, maximum));
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
void help() {
    std::cout
        << "Sprite forest benchmark (fixed step 1/60, vsync off)\n"
           "  --backend all|opengl|vulkan   Available compiled backends, default all\n"
           "  --threads all|single|render   Compare dedicated render thread, default all\n"
           "  --counts 1000,5000,10000      Exact sprite counts including hats/shadows\n"
           "  --atlas-images 25,100,400     Atlas image-kind sweep (25..1024), default 25\n"
           "  --frames 240 --warmup 60      Measured / excluded warmup frames per case\n"
           "  --repeats 1                   Repeat the matrix, rotating case order\n"
           "  --width 1280 --height 800     Requested window size; CSV records pixels\n"
           "  --scene-size 28               Fixed square world area (higher count is denser)\n"
           "  --sprite-scale 1 --seed 42    Image scale and deterministic layout seed\n"
           "  --transparency sorted|oit|oit-ordered\n"
           "  --no-fxaa --static            Disable FXAA / actor movement and flipbooks\n"
           "  --hidden --validation         Hidden window / enable available validation\n"
           "  --target-fps 60               FPS and p95 budget for tested-count summary\n"
           "  --csv sprite-benchmark.csv    One row per completed case\n"
           "  --capture directory           Save each case after timing, as PPM\n"
           "  --self-test                   Check counts, visibility, image and GPU errors\n"
           "  --help                        Print this help; Escape cancels a running test\n";
}
Options parse(int argc, char **argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        const auto value = [&]() -> std::string {
            require(++i < argc, "Missing value for " + argument);
            return argv[i];
        };
        if (argument == "--backend")
            o.backend = value();
        else if (argument == "--threads")
            o.threads = value();
        else if (argument == "--transparency")
            o.transparency = value();
        else if (argument == "--frames")
            o.frames = integer(value(), 1, 1000000);
        else if (argument == "--warmup")
            o.warmup = integer(value(), 0, 100000);
        else if (argument == "--width")
            o.width = integer(value(), 64, 8192);
        else if (argument == "--height")
            o.height = integer(value(), 64, 8192);
        else if (argument == "--seed")
            o.seed = integer(value(), 0, UINT32_MAX);
        else if (argument == "--repeats")
            o.repeats = integer(value(), 1, 20);
        else if (argument == "--scene-size")
            o.sceneSize = real(value(), 4, 256);
        else if (argument == "--sprite-scale")
            o.spriteScale = real(value(), .1f, 4);
        else if (argument == "--target-fps")
            o.targetFps = real(value(), 1, 10000);
        else if (argument == "--csv")
            o.csv = value();
        else if (argument == "--capture")
            o.capture = value();
        else if (argument == "--hidden")
            o.hidden = true;
        else if (argument == "--validation")
            o.validation = true;
        else if (argument == "--self-test")
            o.selfTest = true;
        else if (argument == "--no-fxaa")
            o.fxaa = false;
        else if (argument == "--static")
            o.animate = false;
        else if (argument == "--counts")
            o.counts = integerList(value(), 1, 1000000);
        else if (argument == "--atlas-images")
            o.atlasImages = integerList(value(), 25, 1024);
        else
            throw std::invalid_argument("Unknown option: " + argument);
    }
    require(o.backend == "all" || o.backend == "opengl" || o.backend == "vulkan",
            "--backend must be all, opengl or vulkan");
    require(o.threads == "all" || o.threads == "single" || o.threads == "render",
            "--threads must be all, single or render");
    require(o.transparency == "sorted" || o.transparency == "oit" ||
                o.transparency == "oit-ordered",
            "--transparency must be sorted, oit or oit-ordered");
    require(!o.csv.empty(), "--csv must specify a file");
    return o;
}

/** @brief 保存一组有限测量值，采用 nearest-rank 分位数，单位由调用者决定。 */
struct Samples {
    std::vector<double> values;
    void add(double value) {
        require(std::isfinite(value) && value >= 0, "Non-finite benchmark sample");
        values.push_back(value);
    }
    double mean() const {
        return values.empty() ? 0
                              : std::accumulate(values.begin(), values.end(), 0.0) /
                                    static_cast<double>(values.size());
    }
    double percentile(double fraction) const {
        if (values.empty())
            return 0;
        auto sorted = values;
        std::sort(sorted.begin(), sorted.end());
        const auto rank = std::max<size_t>(
            1, static_cast<size_t>(std::ceil(fraction * static_cast<double>(sorted.size()))));
        return sorted[std::min(rank, sorted.size()) - 1];
    }
};
/** @brief 可运动的角色及其分组；帽子和软阴影随父节点移动。 */
struct Actor {
    NodeHandle root, body;
    Vec3 origin;
    float phase;
    uint32_t variety;
    SpriteRenderer component;
};
/** @brief 仅使用公开核心接口搭建森林测试场景，无动画/拾取模块依赖。 */
struct Forest {
    AssetStore assets;
    Scene scene;
    RenderExtractor extractor;
    std::vector<SpriteHandle> sprites;
    std::vector<Actor> actors;
    RenderView view;
    uint32_t spriteCount = 0, trees = 0, rocks = 0, flowers = 0, hats = 0, shadows = 0;
    const Options &options;

    Forest(const Options &o, uint32_t count, uint32_t imageCount, uint32_t width, uint32_t height,
           const TextureDesc &image)
        : options(o) {
        const auto texture = assets.createTexture(image);
        const auto entries = forest_assets::makeEntries(imageCount);
        const SpriteAtlas atlas(assets, texture, entries);
        sprites.resize(imageCount);
        for (size_t i = 0; i < sprites.size(); ++i)
            sprites[i] = atlas.sprite(std::to_string(i));
        auto ground = geometry::checkerboard(128, 24);
        for (size_t i = 0; i < ground.pixels.size(); i += 4) {
            const bool light = ground.pixels[i] > 100;
            ground.pixels[i] = light ? 101 : 97;
            ground.pixels[i + 1] = light ? 115 : 111;
            ground.pixels[i + 2] = light ? 70 : 66;
        }
        const auto material = assets.createMaterial(
            {.baseColorTexture = assets.createTexture(ground), .unlit = true});
        const auto floor = scene.createNode(scene.root(), "Forest ground (one mesh)");
        scene.setComponent(floor, MeshRenderer{assets.createMesh(geometry::plane(o.sceneSize + 4)),
                                               {material},
                                               0xFFFFFFFFu,
                                               false,
                                               false});
        const auto camera = scene.createNode(scene.root(), "Fixed benchmark camera");
        Camera cameraData;
        cameraData.projection = Camera::Projection::Orthographic;
        // 测试整个场景，避免大量离屏精灵被裁剪后形成虚高的容量数据。
        const float aspect = float(width) / float(height);
        cameraData.orthographicHeight =
            (o.sceneSize * 1.55f + 8 * o.spriteScale) * std::max(1.0f, 1 / aspect);
        cameraData.farPlane = std::max(1000.0f, o.sceneSize * 10);
        scene.setComponent(camera, cameraData);
        scene.setLocalTransform(
            camera,
            *Mat4::lookAt({o.sceneSize * .48f, o.sceneSize * .9f, o.sceneSize * 1.2f}, {0, 0, 0})
                 .inverse());
        view.cameraNode = camera;
        view.clearColor = {.08f, .10f, .055f, 1};
        view.shadows = false; // 与交互森林一致，使用图片软阴影，不启用方向光阴影图。
        view.antiAliasing = o.fxaa ? AntiAliasing::FXAA : AntiAliasing::None;
        view.transparency = o.transparency == "sorted" ? TransparencyMode::SortedAlpha
                                                       : TransparencyMode::WeightedBlendedOIT;
        std::mt19937 random(o.seed);
        std::uniform_real_distribution<float> coordinate(-o.sceneSize * .43f, o.sceneSize * .43f);
        uint32_t object = 0;
        while (spriteCount < count) {
            const Vec3 position{coordinate(random), 0, coordinate(random)};
            const auto kind = object++ % 12;
            const auto remaining = count - spriteCount;
            if ((kind == 0 || kind == 10) && remaining >= 3) {
                const auto root = scene.createNode(scene.root(), "Actor group");
                scene.setLocalTransform(root, Mat4::translation(position));
                scene.setComponent(root, SortingGroup{});
                const auto body = add(root, 0, {}, o.spriteScale);
                const auto hat = add(root, 22, {}, o.spriteScale);
                auto component = *scene.component<SpriteRenderer>(hat);
                component.sortingOrder = 1;
                scene.setComponent(hat, component);
                ++hats;
                shadow(root, {}, .75f * o.spriteScale);
                actors.push_back({root, body, position, float(object) * .37f, object,
                                  *scene.component<SpriteRenderer>(body)});
            } else if (kind != 2 && kind != 6 && kind != 11 && remaining >= 2) {
                const bool tree = kind != 4 && kind != 9;
                const auto index = tree ? 16 + random() % 3 : 19 + random() % 2;
                const float scale = o.spriteScale * (.85f + float(random() % 25) / 100);
                const auto node = add(scene.root(), int(index), position, scale);
                auto component = *scene.component<SpriteRenderer>(node);
                component.flipX = random() % 2 == 0;
                scene.setComponent(node, component);
                shadow(scene.root(), position, (tree ? 1.05f : .65f) * scale);
                tree ? ++trees : ++rocks;
            } else {
                add(scene.root(), 24, position, .55f * o.spriteScale);
                ++flowers;
            }
        }
    }
    NodeHandle add(NodeHandle parent, int image, Vec3 position, float scale) {
        const auto node = scene.createNode(parent, "Forest sprite");
        SpriteRenderer component;
        component.sprite = imageKind(uint32_t(image), spriteCount * 7919u);
        component.sizeMultiplier = {scale, scale};
        component.transparency = options.transparency == "oit" ? SpriteTransparency::FollowView
                                                               : SpriteTransparency::Sorted;
        scene.setComponent(node, component);
        scene.setLocalTransform(node, Mat4::translation(position));
        ++spriteCount;
        return node;
    }
    void shadow(NodeHandle parent, Vec3 position, float scale) {
        const auto node = add(parent, 21, position + Vec3{0, .012f, 0}, scale);
        auto component = *scene.component<SpriteRenderer>(node);
        component.billboard = BillboardMode::Fixed;
        component.sizeMultiplier = {scale, scale * .6f};
        component.sortingLayer = -10;
        component.sortingOrder = -1; // 位于角色 SortingGroup 内时，也先于身体和帽子绘制。
        scene.setComponent(node, component);
        scene.setLocalTransform(node, Mat4::translation(position + Vec3{0, .012f, 0}) *
                                          Mat4::rotationX(radians(-90)));
        ++shadows;
    }
    SpriteHandle imageKind(uint32_t base, uint32_t salt) const {
        const auto variants = 1 + (uint32_t(sprites.size()) - 1 - base) / 25;
        if (variants == 1)
            return sprites[base];
        // 打散顺序，避免场景每两/三张精灵一组时只命中偶数或少数配色变体。
        salt ^= salt >> 16;
        salt *= 0x7feb352du;
        salt ^= salt >> 15;
        salt *= 0x846ca68bu;
        salt ^= salt >> 16;
        return sprites[base + salt % variants * 25];
    }
    void update(uint32_t frame) {
        if (!options.animate)
            return;
        const float time = float(frame) / 60;
        for (size_t i = 0; i < actors.size(); ++i) {
            auto &actor = actors[i];
            const float phase = time * .8f + actor.phase;
            const Vec3 offset{std::sin(phase) * .55f * options.spriteScale, 0,
                              std::cos(phase) * .55f * options.spriteScale};
            scene.setLocalTransform(actor.root, Mat4::translation(actor.origin + offset));
            const int direction = std::abs(std::cos(phase)) > std::abs(std::sin(phase))
                                      ? (std::cos(phase) > 0 ? 1 : 3)
                                      : (std::sin(phase) > 0 ? 2 : 0);
            actor.component.sprite =
                imageKind(uint32_t(direction * 4 + (frame / 8 + i) % 4), actor.variety);
            scene.setComponent(actor.body, actor.component);
        }
    }
};

/** @brief 一组后端、线程和精灵数量的完整测量结果。CPU 阶段重叠时不能相加。 */
struct Result {
    DeviceInfo device;
    bool threaded = false;
    uint32_t count = 0, visible = 0, width = 0, height = 0, repeat = 0, errors = 0;
    uint32_t atlasImages = 0, visibleImageKinds = 0, atlasWidth = 0, atlasHeight = 0;
    uint32_t trees = 0, rocks = 0, flowers = 0, actors = 0, hats = 0, shadows = 0;
    double elapsedMs = 0, drainMs = 0;
    Samples frame, update, extract, prepare, submit, wait, present, gpu, draws, sceneDraws,
        triangles, upload;
    std::map<std::string, Samples> passes;
    double fps() const {
        return 1000 * static_cast<double>(frame.values.size()) / elapsedMs;
    }
    bool meets(float target) const {
        return fps() >= target && frame.percentile(.95) <= 1000 / target;
    }
};
Renderer createRenderer(Window &window, GraphicsApi api, const Options &o, bool threaded) {
    const RendererConfig config{.debug = o.validation, .renderThread = threaded};
#ifdef RENDERER_BENCHMARK_OPENGL
    if (api == GraphicsApi::OpenGL)
        return Renderer::createOpenGL(window, config);
#endif
#ifdef RENDERER_BENCHMARK_VULKAN
    if (api == GraphicsApi::Vulkan)
        return Renderer::createVulkan(window, config);
#endif
    throw std::runtime_error("Requested backend was not compiled");
}
Result run(const Options &o, GraphicsApi api, bool threaded, uint32_t count, uint32_t imageCount,
           uint32_t repeat, const TextureDesc &atlas) {
    Window window({.width = int(o.width),
                   .height = int(o.height),
                   .title = "Sprite benchmark / " + std::to_string(count) + " sprites / " +
                            (threaded ? "render thread" : "single thread") + " / Escape cancels",
                   .visible = !o.hidden,
                   .vsync = false,
                   .debugContext = o.validation,
                   .graphicsApi = api});
    const auto keys = window.subscribe<KeyEvent>([&](const KeyEvent &event) {
        if (event.key == Key::Escape && event.action == InputAction::Press)
            window.requestClose();
    });
    auto engine = createRenderer(window, api, o, threaded);
    const auto [width, height] = window.framebufferSize();
    require(width > 0 && height > 0, "Benchmark requires a nonzero framebuffer");
    Forest forest(o, count, imageCount, uint32_t(width), uint32_t(height), atlas);
    Result result;
    result.device = engine.deviceInfo();
    result.threaded = threaded;
    result.count = count;
    result.atlasImages = imageCount;
    result.atlasWidth = atlas.width;
    result.atlasHeight = atlas.height;
    result.width = uint32_t(width);
    result.height = uint32_t(height);
    result.repeat = repeat;
    result.trees = forest.trees;
    result.rocks = forest.rocks;
    result.flowers = forest.flowers;
    result.actors = uint32_t(forest.actors.size());
    result.hats = forest.hats;
    result.shadows = forest.shadows;
    std::cout << "RUN " << result.device.backend << '/' << (threaded ? "render" : "single")
              << " sprites=" << count << " atlas_images=" << imageCount << " repeat=" << repeat
              << " pixels=" << width << 'x' << height << " device=" << result.device.device
              << std::endl;
    forest.update(0);
    auto ready = forest.extractor.extract(forest.scene, forest.assets);
    RenderSnapshot displayed;
    uint32_t frame = 0;
    const auto step = [&](bool measure) {
        const auto begin = Clock::now();
        window.pollEvents();
        require(!window.shouldClose(), "Benchmark cancelled; completed CSV rows are retained");
        require(window.framebufferSize() == std::pair{width, height},
                "Framebuffer changed during benchmark; keep window size fixed");
        displayed = ready;
        auto completion = engine.submit(std::move(ready), forest.view);
        // 先提交 N 帧，再准备 N+1 帧。专用线程才能与主线程的更新/快照提取重叠。
        const auto updateStart = Clock::now();
        forest.update(frame + 1);
        const auto updateMs = milliseconds(updateStart);
        const auto extractStart = Clock::now();
        ready = forest.extractor.extract(forest.scene, forest.assets);
        const auto extractMs = milliseconds(extractStart);
        const auto waitStart = Clock::now();
        const auto stats = completion.get();
        const auto waitMs = milliseconds(waitStart);
        const auto presentStart = Clock::now();
        engine.present();
        const auto presentMs = milliseconds(presentStart);
        const auto frameMs = milliseconds(begin);
        if (measure) {
            result.frame.add(frameMs);
            result.update.add(updateMs);
            result.extract.add(extractMs);
            result.prepare.add(stats.prepareMilliseconds);
            result.submit.add(stats.submitMilliseconds);
            result.wait.add(waitMs);
            result.present.add(presentMs);
            result.draws.add(stats.drawCalls);
            result.sceneDraws.add(stats.sceneDrawCalls);
            result.triangles.add(double(stats.triangles));
            result.upload.add(double(stats.uploadedBytes));
            if (stats.gpuTimingValid) {
                result.gpu.add(stats.gpuMilliseconds);
                for (const auto &pass : stats.passes)
                    result.passes[pass.name].add(pass.gpuMilliseconds);
            }
            if (o.selfTest)
                require(stats.sceneInstances == count + 1 && stats.visibleInstances == count + 1,
                        "Sprite benchmark scene/visible instance count mismatch");
        }
        ++frame;
    };
    for (uint32_t i = 0; i < o.warmup; ++i)
        step(false);
    engine.waitIdle();
    const auto start = Clock::now();
    for (uint32_t i = 0; i < o.frames; ++i)
        step(true);
    const auto drain = Clock::now();
    engine.waitIdle();
    result.drainMs = milliseconds(drain);
    result.elapsedMs = milliseconds(start); // 包含最后一次 GPU 排空，避免遗漏尾帧工作。
    // 视图展开和截图放在计时区间外，防止查询/回读改变测得的吞吐。
    const auto spriteView = displayed.spriteView(forest.view, result.width, result.height);
    result.visible = uint32_t(spriteView.sprites().size());
    std::set<std::pair<uint32_t, uint32_t>> imageKinds;
    for (const auto &item : spriteView.sprites()) {
        const auto &uv = item.uvRect;
        // 还原图块左下角，翻转后的同一图片仍只计一种；避免浮点 UV 舍入导致重复计数。
        imageKinds.emplace(uint32_t(std::lround(std::min(uv.x, uv.x + uv.z) * atlas.width)),
                           uint32_t(std::lround(std::min(uv.y, uv.y + uv.w) * atlas.height)));
    }
    result.visibleImageKinds = uint32_t(imageKinds.size());
    if (!o.capture.empty() || o.selfTest) {
        const auto image = engine.readback();
        if (o.selfTest) {
            require(image.width == result.width && image.height == result.height &&
                        displayed.spriteCount() == count && result.visible == count &&
                        result.visibleImageKinds > 0 && result.visibleImageKinds <= imageCount,
                    "Benchmark output size or sprite count mismatch");
            std::set<uint32_t> colors;
            for (size_t i = 0; i < image.pixels.size() && colors.size() < 32; i += 4)
                colors.insert(uint32_t(image.pixels[i]) | uint32_t(image.pixels[i + 1]) << 8 |
                              uint32_t(image.pixels[i + 2]) << 16);
            require(colors.size() > 8, "Benchmark produced an empty image");
        }
        if (!o.capture.empty()) {
            std::filesystem::create_directories(o.capture);
            const auto name = result.device.backend + "-" + (threaded ? "render-" : "single-") +
                              std::to_string(count) + "-atlas" + std::to_string(imageCount) + "-r" +
                              std::to_string(repeat) + ".ppm";
            image.savePpm((o.capture / name).string());
        }
    }
    result.errors = engine.debugErrorCount();
    require(result.errors == 0, "Graphics errors during benchmark");
    return result;
}

std::string csvText(const std::string &value) {
    std::string escaped = "\"";
    for (char c : value) {
        if (c == '"')
            escaped += '"';
        escaped += c;
    }
    return escaped + '"';
}
const std::array<std::string, 9> passNames{"Shadow",           "Opaque PBR",  "Transparent",
                                           "OIT Accumulation", "OIT Resolve", "Ordered Sprites",
                                           "ToneMap",          "FXAA",        "Present"};
void csvHeader(std::ostream &out) {
    out << "backend,device,api_version,build,threads,repeat,sprites,visible_sprites,atlas_images,"
           "visible_image_kinds,atlas_width,atlas_height,atlas_rgba8_bytes,width,height,"
           "seed,scene_size,sprite_scale,transparency,fxaa,animated,validation,hidden,warmup_"
           "frames,"
           "frames,"
           "trees,rocks,flowers,actors,hats,blob_shadows,fps,frame_mean_ms,frame_p50_ms,frame_p95_"
           "ms,"
           "frame_p99_ms,frame_max_ms,update_mean_ms,extract_mean_ms,prepare_mean_ms,submit_mean_"
           "ms,"
           "wait_mean_ms,present_mean_ms,gpu_graph_mean_ms,gpu_graph_p95_ms,gpu_samples,"
           "scene_draws_mean,total_draws_mean,triangles_mean,uploaded_bytes_mean,elapsed_ms,"
           "gpu_drain_ms,target_fps,target_met,graphics_errors";
    for (const auto &name : passNames)
        out << ',' << csvText("gpu_pass_" + name + "_mean_ms");
    out << '\n';
}
void writeCsv(std::ostream &out, const Options &o, const Result &r) {
#ifdef NDEBUG
    const char *build = "Release";
#else
    const char *build = "Debug";
#endif
    out << std::fixed << std::setprecision(6) << csvText(r.device.backend) << ','
        << csvText(r.device.device) << ',' << csvText(r.device.version) << ',' << build << ','
        << (r.threaded ? "render" : "single") << ',' << r.repeat << ',' << r.count << ','
        << r.visible << ',' << r.atlasImages << ',' << r.visibleImageKinds << ',' << r.atlasWidth
        << ',' << r.atlasHeight << ',' << uint64_t(r.atlasWidth) * r.atlasHeight * 4 << ','
        << r.width << ',' << r.height << ',' << o.seed << ',' << o.sceneSize << ',' << o.spriteScale
        << ',' << o.transparency << ',' << o.fxaa << ',' << o.animate << ',' << o.validation << ','
        << o.hidden << ',' << o.warmup << ',' << o.frames << ',' << r.trees << ',' << r.rocks << ','
        << r.flowers << ',' << r.actors << ',' << r.hats << ',' << r.shadows << ',' << r.fps()
        << ',' << r.frame.mean() << ',' << r.frame.percentile(.5) << ',' << r.frame.percentile(.95)
        << ',' << r.frame.percentile(.99) << ',' << r.frame.percentile(1) << ',' << r.update.mean()
        << ',' << r.extract.mean() << ',' << r.prepare.mean() << ',' << r.submit.mean() << ','
        << r.wait.mean() << ',' << r.present.mean() << ',';
    if (!r.gpu.values.empty())
        out << r.gpu.mean();
    out << ',';
    if (!r.gpu.values.empty())
        out << r.gpu.percentile(.95);
    out << ',' << r.gpu.values.size() << ',' << r.sceneDraws.mean() << ',' << r.draws.mean() << ','
        << r.triangles.mean() << ',' << r.upload.mean() << ',' << r.elapsedMs << ',' << r.drainMs
        << ',' << o.targetFps << ',' << r.meets(o.targetFps) << ',' << r.errors;
    for (const auto &name : passNames) {
        out << ',';
        if (const auto it = r.passes.find(name); it != r.passes.end())
            out << it->second.mean();
    }
    out << '\n';
    out.flush(); // 每组完成立即保存，取消后保留已完成的数据。
}
void printResult(const Result &r) {
    std::cout << std::fixed << std::setprecision(3) << "RESULT " << r.device.backend << '/'
              << (r.threaded ? "render" : "single") << " sprites=" << r.count
              << " visible=" << r.visible << " atlas_images=" << r.atlasImages
              << " visible_image_kinds=" << r.visibleImageKinds << " fps=" << r.fps()
              << " frame(ms) mean/p95/p99=" << r.frame.mean() << '/' << r.frame.percentile(.95)
              << '/' << r.frame.percentile(.99)
              << " CPU update/extract/prepare/submit=" << r.update.mean() << '/' << r.extract.mean()
              << '/' << r.prepare.mean() << '/' << r.submit.mean() << " GPU(graph)=";
    if (r.gpu.values.empty())
        std::cout << "unavailable";
    else
        std::cout << r.gpu.mean();
    std::cout << " draws(scene/total)=" << r.sceneDraws.mean() << '/' << r.draws.mean()
              << " errors=" << r.errors << std::endl;
}
void comparisons(const Options &o, const std::vector<Result> &results) {
    using Key = std::tuple<uint32_t, uint32_t, std::string, bool>;
    std::map<Key, Samples> rates;
    std::map<Key, bool> qualified;
    for (const auto &r : results) {
        const Key key{r.count, r.atlasImages, r.device.backend, r.threaded};
        rates[key].add(r.fps());
        const auto [it, inserted] = qualified.try_emplace(key, true);
        (void)inserted;
        it->second = it->second && r.meets(o.targetFps);
    }
    const auto ratio = [&](uint32_t count, uint32_t images, const char *label, const std::string &a,
                           bool at, const std::string &b, bool bt) {
        const auto first = rates.find({count, images, a, at}),
                   second = rates.find({count, images, b, bt});
        if (first != rates.end() && second != rates.end())
            std::cout << "COMPARE sprites=" << count << " atlas_images=" << images << ' ' << label
                      << '=' << first->second.mean() / second->second.mean() << "x\n";
    };
    for (const auto count : o.counts)
        for (const auto images : o.atlasImages) {
            ratio(count, images, "OpenGL render/single", "OpenGL", true, "OpenGL", false);
            ratio(count, images, "Vulkan render/single", "Vulkan", true, "Vulkan", false);
            ratio(count, images, "single Vulkan/OpenGL", "Vulkan", false, "OpenGL", false);
            ratio(count, images, "render Vulkan/OpenGL", "Vulkan", true, "OpenGL", true);
        }
    for (const auto &[key, current] : rates) {
        const auto &[count, images, backend, threaded] = key;
        if (images == o.atlasImages.front())
            continue;
        const auto baseline = rates.find({count, o.atlasImages.front(), backend, threaded});
        if (baseline != rates.end())
            std::cout << "COMPARE sprites=" << count << ' ' << backend << '/'
                      << (threaded ? "render" : "single") << " atlas_images=" << images << '/'
                      << o.atlasImages.front()
                      << " fps_ratio=" << current.mean() / baseline->second.mean() << "x\n";
    }
    std::map<std::tuple<uint32_t, std::string, bool>, uint32_t> largest;
    for (const auto &[key, ratesForCase] : rates) {
        (void)ratesForCase;
        const auto &[count, images, backend, threaded] = key;
        auto &capacity = largest[{images, backend, threaded}];
        if (qualified.at(key))
            capacity = std::max(capacity, count);
    }
    for (const auto &[key, count] : largest) {
        const auto &[images, backend, threaded] = key;
        std::cout << "CAPACITY " << backend << '/' << (threaded ? "render" : "single")
                  << " atlas_images=" << images
                  << " largest_tested_sprites_meeting_fps_and_p95=" << count
                  << " target_fps=" << o.targetFps << '\n';
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        for (int i = 1; i < argc; ++i)
            if (std::string_view(argv[i]) == "--help") {
                help();
                return 0;
            }
        const auto options = parse(argc, argv);
        std::vector<std::pair<std::string, GraphicsApi>> backends;
#ifdef RENDERER_BENCHMARK_OPENGL
        if (options.backend == "all" || options.backend == "opengl")
            backends.emplace_back("OpenGL", GraphicsApi::OpenGL);
#endif
#ifdef RENDERER_BENCHMARK_VULKAN
        if (options.backend == "all" || options.backend == "vulkan")
            backends.emplace_back("Vulkan", GraphicsApi::Vulkan);
#endif
        require(!backends.empty(), "Requested backend is absent from this build/SDK");
#ifndef NDEBUG
        std::cout << "Debug build: use Release for performance conclusions.\n";
#endif
        std::cout << "Fixed layout seed=" << options.seed
                  << " transparency=" << options.transparency << " FXAA=" << options.fxaa
                  << " animation=" << options.animate << " validation=" << options.validation
                  << " hidden=" << options.hidden
                  << "; CPU stages overlap in render-thread mode.\n";
        if (options.csv.has_parent_path())
            std::filesystem::create_directories(options.csv.parent_path());
        std::ofstream csv(options.csv);
        csv.exceptions(std::ios::failbit | std::ios::badbit);
        csvHeader(csv);
        std::map<uint32_t, TextureDesc> atlases;
        for (const auto images : options.atlasImages)
            atlases.emplace(images, forest_assets::makeAtlas(images));
        std::vector<std::pair<GraphicsApi, bool>> cases;
        for (const auto &[name, api] : backends) {
            (void)name;
            if (options.threads == "all" || options.threads == "single")
                cases.emplace_back(api, false);
            if (options.threads == "all" || options.threads == "render")
                cases.emplace_back(api, true);
        }
        std::vector<Result> results;
        for (uint32_t repeat = 1; repeat <= options.repeats; ++repeat) {
            for (const auto count : options.counts)
                for (const auto images : options.atlasImages)
                    for (size_t i = 0; i < cases.size(); ++i) {
                        const auto [api, threaded] = cases[(i + repeat - 1) % cases.size()];
                        auto result =
                            run(options, api, threaded, count, images, repeat, atlases.at(images));
                        printResult(result);
                        writeCsv(csv, options, result);
                        results.push_back(std::move(result));
                    }
        }
        comparisons(options, results);
        std::cout << "CSV: " << std::filesystem::absolute(options.csv).string() << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Sprite benchmark: " << error.what() << '\n';
        return 1;
    }
}
