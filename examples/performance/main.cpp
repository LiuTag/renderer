/** @file @brief 以公开 SDK 对静态/动态精灵、分块、压缩数组加载和动态中文字形进行可复现对照。 */
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <fstream>
#include <iostream>
#include <numeric>
#include <renderer/interaction/picking.hpp>
#include <renderer/renderer.hpp>
#include <renderer/text/text.hpp>
#include <renderer/texture.hpp>
using namespace renderer;
namespace {
using Clock = std::chrono::steady_clock;
double ms(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}
void need(bool b, const char *s) {
    if (!b)
        throw std::runtime_error(s);
}
struct Samples {
    std::vector<double> values;
    void add(double v) {
        need(std::isfinite(v) && v >= 0, "Invalid measurement");
        values.push_back(v);
    }
    double mean() const {
        return values.empty()
                   ? 0
                   : std::accumulate(values.begin(), values.end(), 0.) / double(values.size());
    }
    double percentile(double f) const {
        if (values.empty())
            return 0;
        auto s = values;
        std::sort(s.begin(), s.end());
        return s[std::min(s.size() - 1, size_t(std::ceil(s.size() * f)) - 1)];
    }
};
struct Options {
    std::string backend = "all", threads = "all", workload = "all";
    uint32_t count = 5000, frames = 120, warmup = 24, repeats = 1;
    bool check = false, reference = false;
    std::filesystem::path csv = "performance.csv", capture, font;
};
std::filesystem::path path(const std::string &s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}
TextureDesc picture(unsigned n) {
    TextureDesc d{64, 64, std::vector<uint8_t>(64 * 64 * 4, 0), true};
    d.sampling.addressU = d.sampling.addressV = TextureAddress::Clamp;
    for (unsigned y = 0; y < 64; ++y)
        for (unsigned x = 0; x < 64; ++x) {
            auto p = (y * 64 + x) * 4;
            d.pixels[p] = uint8_t(60 + n * 21);
            d.pixels[p + 1] = uint8_t(200 - n * 12);
            d.pixels[p + 2] = uint8_t(90 + n * 13);
            d.pixels[p + 3] =
                (int(x) - 32) * (int(x) - 32) + (int(y) - 32) * (int(y) - 32) < 850 ? 255 : 0;
        }
    return d;
}
Renderer make(const std::string &backend, bool threaded, bool reference) {
    RendererConfig c;
    c.debug = false;
    c.renderThread = threaded;
    c.batchResourceUploads = !reference;
    if (backend == "vulkan") {
#ifdef RENDERER_PERFORMANCE_VULKAN
        return Renderer::createVulkanOffscreen({960, 640}, c);
#else
        throw std::runtime_error("Vulkan unavailable");
#endif
    }
#ifdef RENDERER_PERFORMANCE_OPENGL
    return Renderer::createOpenGLOffscreen({960, 640}, c);
#else
    throw std::runtime_error("OpenGL unavailable");
#endif
}
void runCase(const Options &o, const std::string &backend, bool threaded,
             const std::string &workload, unsigned repeat, std::ofstream &out) {
    auto gpu = make(backend, threaded, o.reference);
    AssetStore assets;
    Scene scene;
    RenderExtractor extractor;
    extractor.setCachingEnabled(!o.reference);
    extractor.setSpatialIndexEnabled(!o.reference);
    TextSystemConfig tc;
    tc.compactAtlas = !o.reference;
    tc.incrementalAtlas = !o.reference;
    TextSystem text(tc);
    auto cam = scene.createNode(scene.root());
    scene.setComponent(
        cam, Camera{.projection = Camera::Projection::Orthographic, .orthographicHeight = 24});
    scene.setLocalTransform(cam, Mat4::translation({0, 0, 20}));
    auto raw = picture(0);
    auto tex = assets.createTexture(raw);
    SpriteDesc sd;
    sd.texture = tex;
    sd.rect = {0, 0, 64, 64};
    sd.pivot = {.5f, .5f};
    sd.pixelsPerUnit = 64;
    sd.alphaMode = AlphaMode::Mask;
    auto sprite = assets.createSprite(sd);
    std::vector<NodeHandle> nodes;
    std::vector<BatchSprite> items;
    std::vector<SpriteBatch> chunks;
    const unsigned side = unsigned(std::ceil(std::sqrt(double(o.count))));
    for (unsigned i = 0; i < o.count; ++i) {
        Vec3 p{(float(i % side) - float(side / 2)) * 1.3f,
               (float(i / side) - float(side / 2)) * 1.3f, 0};
        auto transform = Mat4::translation(p);
        if (workload == "batch") {
            items.push_back(
                {{.sprite = sprite, .billboard = BillboardMode::Fixed}, transform, i + 1});
        } else {
            auto n = scene.createNode(scene.root());
            scene.setLocalTransform(n, transform);
            scene.setComponent(n,
                               SpriteRenderer{.sprite = sprite, .billboard = BillboardMode::Fixed});
            nodes.push_back(n);
        }
    }
    if (!items.empty()) {
        for (size_t first = 0; first < items.size(); first += 128) {
            SpriteBatch b;
            b.replace(assets,
                      std::span(items).subspan(first, std::min<size_t>(128, items.size() - first)));
            chunks.push_back(std::move(b));
        }
    }
    std::array<TextureDesc, 8> pictures;
    for (unsigned i = 0; i < 8; ++i)
        pictures[i] = picture(i);
    auto bc = texture::compress(texture::makeArray(pictures), TextureCompression::BC3);
    std::vector<WorldTextId> labels;
    auto fontPath = o.font;
#ifdef _WIN32
    if (fontPath.empty() && std::filesystem::exists("C:/Windows/Fonts/msyh.ttc"))
        fontPath = "C:/Windows/Fonts/msyh.ttc";
#endif
    if (fontPath.empty())
        fontPath = RENDERER_PERFORMANCE_FONT;
    if (workload == "text") {
        auto f = text.loadFont(fontPath);
        for (unsigned i = 0; i < 24; ++i) {
            WorldTextDesc d;
            d.style.font = f;
            d.style.fontSize = 24;
            d.style.rasterMode = TextRasterMode::Sdf;
            d.text = "Loading";
            d.sizeMode = TextSizeMode::ScreenPixels;
            d.depthTest = false;
            d.transform = Mat4::translation({-12.f + float(i % 4) * 7, 8.f - float(i / 4) * 3, 1});
            labels.push_back(text.create(d));
        }
    }
    Samples frame, update, extract, prepare, submit, pick, gpuTime;
    uint64_t transferCalls = 0, transferSubmissions = 0, transferWaits = 0, allocations = 0,
             stagingReuses = 0, peakPool = 0;
    uint64_t uploads = 0, reused = 0, draws = 0, pending = 0;
    double cold = 0;
    uint64_t peak = 0;
    std::deque<RenderSnapshot> old;
    RenderView view;
    view.cameraNode = cam;
    view.spatialCulling = !o.reference;
    view.shadows = false;
    view.clearColor = {.02f, .035f, .05f, 1};
    if (workload == "oit")
        view.transparency = TransparencyMode::WeightedBlendedOIT;
    if (workload == "oit") {
        sd.alphaMode = AlphaMode::Blend;
        auto blended = assets.createSprite(sd);
        for (auto n : nodes)
            scene.setComponent(n, SpriteRenderer{.sprite = blended,
                                                 .tint = {1, 1, 1, .6f},
                                                 .billboard = BillboardMode::Fixed,
                                                 .transparency = SpriteTransparency::FollowView});
    }
    auto sampleStart = Clock::now();
    for (unsigned f = 0; f < o.warmup + o.frames; ++f) {
        if (f == o.warmup)
            sampleStart = Clock::now();
        const auto begin = Clock::now();
        if (workload == "dynamic") {
            for (size_t i = 0; i < nodes.size(); i += 20) {
                const auto p = Vec3{(float(i % side) - float(side / 2)) * 1.3f +
                                        std::sin(float(f) * .08f) * .3f,
                                    (float(i / side) - float(side / 2)) * 1.3f, 0};
                scene.setLocalTransform(nodes[i], Mat4::translation(p));
            }
        }
        if (workload == "stream" && f % 8 == 0) {
            auto t = assets.createTexture(bc);
            auto d = sd;
            d.texture = t;
            auto s = assets.createSprite(d);
            for (size_t i = 0; i < nodes.size(); ++i) {
                auto c = *scene.component<SpriteRenderer>(nodes[i]);
                c.sprite = s;
                c.textureLayer = uint32_t(i % 8);
                scene.setComponent(nodes[i], c);
            }
            assets.destroySprite(sprite);
            assets.destroyTexture(tex);
            sprite = s;
            tex = t;
        }
        if (workload == "text") {
            for (size_t i = 0; i < labels.size(); ++i) {
                std::string value = "Player " + std::to_string(i) + " HP " + std::to_string(f + i);
                const unsigned code = 0x4e00 + (f * 23 + unsigned(i) * 11) % 1600;
                value += ' ';
                value += char(0xe0 | (code >> 12));
                value += char(0x80 | ((code >> 6) & 63));
                value += char(0x80 | (code & 63));
                text.setText(labels[i], value);
            }
        }
        auto local = chunks;
        if (!labels.empty())
            local.push_back(text.batch(scene, cam, 960, 640));
        const auto u = ms(begin);
        auto t = Clock::now();
        auto snapshot = extractor.extract(scene, assets, local);
        const auto e = ms(t);
        double query = 0;
        if (f % 8 == 0) {
            t = Clock::now();
            SceneQuery q(snapshot, view, 960, 640);
            for (unsigned n = 0; n < 32; ++n)
                (void)q.pickSprite({float((n * 71) % 960), float((n * 43) % 640)});
            query = ms(t);
        }
        auto stats = gpu.render(snapshot, view);
        if (f == 0)
            cold = ms(begin);
        const auto elapsed = ms(begin);
        if (workload == "stream" || workload == "text") {
            old.push_back(snapshot);
            if (old.size() > 3)
                old.pop_front();
        }
        if (f >= o.warmup) {
            frame.add(elapsed);
            update.add(u);
            extract.add(e);
            prepare.add(stats.prepareMilliseconds);
            submit.add(stats.submitMilliseconds);
            if (query)
                pick.add(query);
            uploads += stats.assetUploadedBytes;
            reused += stats.instanceReusedBytes;
            draws += stats.sceneDrawCalls;
            pending += stats.pendingResourceDraws;
            auto memory = gpu.memoryStatistics();
            peak = std::max(peak, memory.estimatedGpuBytes());
            peakPool = std::max(peakPool, memory.pooledUploadBytes);
            if (stats.gpuTimingValid)
                gpuTime.add(stats.gpuMilliseconds);
            transferCalls += stats.uploadCalls;
            transferSubmissions += stats.uploadSubmissions;
            transferWaits += stats.uploadWaits;
            allocations += stats.stagingAllocations;
            stagingReuses += stats.stagingReuses;
        }
        if (o.check)
            need(stats.visibleInstances > 0 && !gpu.debugErrorCount(),
                 "Benchmark produced no visible instances or a graphics error");
    }
    auto drainStart = Clock::now();
    gpu.waitIdle();
    const auto drain = ms(drainStart), sampleWall = ms(sampleStart);
    auto image = gpu.readback();
    if (o.check)
        need(!image.pixels.empty() && std::any_of(image.pixels.begin(), image.pixels.end(),
                                                  [](uint8_t p) { return p > 120; }),
             "No rendered content");
    if (!o.capture.empty())
        image.savePng(o.capture / (backend + "-" + (threaded ? "render" : "single") + "-" +
                                   workload + "-" + std::to_string(repeat) + ".png"));
    const auto ts = text.statistics();
    {
        auto metaPath = o.csv;
        metaPath += ".environment.txt";
        std::ofstream meta(metaPath, std::ios::app);
        const auto info = gpu.deviceInfo();
        auto f = fontPath.u8string();
        meta << info.backend << " | " << info.vendor << " | " << info.device << " | "
             << info.version << " | reference=" << o.reference
             << " | 960x640 | shadows=0 | validation=0 | warmup=" << o.warmup
             << " | font=" << std::string(f.begin(), f.end()) << " | "
#ifdef NDEBUG
             << "Release"
#else
             << "Debug"
#endif
             << '\n';
    }

    out << backend << ',' << (threaded ? "render" : "single") << ',' << workload << ',' << repeat
        << ',' << o.count << ',' << o.frames << ',' << frame.mean() << ',' << frame.percentile(.95)
        << ',' << frame.percentile(.99) << ',' << cold << ',' << update.mean() << ','
        << extract.mean() << ',' << prepare.mean() << ',' << submit.mean() << ',' << pick.mean()
        << ',' << uploads << ',' << reused << ',' << double(draws) / o.frames << ',' << pending
        << ',' << peak << ',' << ts.atlasPixelBytes << ',' << ts.cacheMisses << ',' << o.reference
        << ',' << gpuTime.mean() << ',' << gpuTime.values.size() << ',' << transferCalls << ','
        << transferSubmissions << ',' << transferWaits << ',' << allocations << ',' << stagingReuses
        << ',' << peakPool << ',' << drain << ',' << sampleWall << ','
        << 1000. * o.frames / sampleWall << ',' << ts.pageRevisions << ',' << ts.dirtyPixelBytes
        << '\n';
    out.flush();
    need(bool(out), "Cannot write CSV");
    std::cout << backend << ' ' << (threaded ? "render" : "single") << ' ' << workload
              << " mean=" << frame.mean() << " p95=" << frame.percentile(.95) << " cold=" << cold
              << " ms\n";
}
int run(const std::vector<std::string> &args) {
    Options o;
    for (size_t i = 1; i < args.size(); ++i) {
        const auto &a = args[i];
        auto val = [&]() {
            need(i + 1 < args.size(), "Missing argument");
            return args[++i];
        };
        if (a == "--backend")
            o.backend = val();
        else if (a == "--threads")
            o.threads = val();
        else if (a == "--workload")
            o.workload = val();
        else if (a == "--count")
            o.count = unsigned(std::stoul(val()));
        else if (a == "--frames")
            o.frames = unsigned(std::stoul(val()));
        else if (a == "--warmup")
            o.warmup = unsigned(std::stoul(val()));
        else if (a == "--repeats")
            o.repeats = unsigned(std::stoul(val()));
        else if (a == "--csv")
            o.csv = path(val());
        else if (a == "--capture")
            o.capture = path(val());
        else if (a == "--font")
            o.font = path(val());
        else if (a == "--reference")
            o.reference = true;
        else if (a == "--self-test")
            o.check = true;
        else if (a == "--help") {
            std::cout
                << "--backend all|opengl|vulkan --threads all|single|render --workload "
                   "all|static|dynamic|batch|stream|text|oit --count N --frames N --warmup N "
                   "--repeats N --csv FILE --capture DIR --font FONT --reference --self-test\n";
            return 0;
        } else
            throw std::invalid_argument("Unknown option " + a);
    }
    need(o.count && o.count <= 1000000 && o.frames && o.frames <= 100000 && o.warmup <= 100000 &&
             o.repeats && o.repeats <= 20,
         "Invalid benchmark size");
    need(o.backend == "all" || o.backend == "opengl" || o.backend == "vulkan", "Invalid backend");
    need(o.threads == "all" || o.threads == "single" || o.threads == "render", "Invalid threads");
    std::vector<std::string> workloads{"static", "dynamic", "batch", "stream", "text", "oit"};
    need(o.workload == "all" ||
             std::find(workloads.begin(), workloads.end(), o.workload) != workloads.end(),
         "Invalid workload");
    if (o.workload != "all")
        workloads = {o.workload};
    if (o.csv.has_parent_path())
        std::filesystem::create_directories(o.csv.parent_path());
    if (!o.capture.empty())
        std::filesystem::create_directories(o.capture);
    {
        auto metaPath = o.csv;
        metaPath += ".environment.txt";
        std::ofstream clear(metaPath);
    }
    std::ofstream out(o.csv);
    need(bool(out), "Cannot open CSV");
    out << "backend,threads,workload,repeat,count,frames,frame_mean_ms,frame_p95_ms,frame_p99_ms,"
           "cold_frame_ms,update_ms,extract_ms,prepare_ms,submit_ms,pick_32_queries_ms,asset_"
           "uploaded_bytes,instance_reused_bytes,scene_draws,pending_draws,peak_gpu_payload_bytes,"
           "text_atlas_bytes,text_misses,reference,gpu_mean_ms,gpu_samples,upload_calls,upload_"
           "submissions,upload_waits,staging_allocations,staging_reuses,peak_staging_bytes,gpu_"
           "drain_ms,sample_wall_ms,fps,text_page_revisions,text_dirty_bytes\n";
    for (unsigned r = 0; r < o.repeats; ++r)
        for (const auto &backend : std::array<std::string, 2>{"opengl", "vulkan"}) {
            if (o.backend != "all" && backend != o.backend)
                continue;
#ifndef RENDERER_PERFORMANCE_OPENGL
            if (backend == "opengl")
                continue;
#endif
#ifndef RENDERER_PERFORMANCE_VULKAN
            if (backend == "vulkan")
                continue;
#endif
            for (bool thread : {false, true}) {
                if (o.threads != "all" && thread != (o.threads == "render"))
                    continue;
                for (const auto &workload : workloads)
                    runCase(o, backend, thread, workload, r, out);
            }
        }
    return 0;
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    try {
        std::vector<std::string> args;
        for (int i = 0; i < argc; ++i) {
            auto s = std::filesystem::path(argv[i]).u8string();
            args.emplace_back(s.begin(), s.end());
        }
        return run(args);
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
#else
int main(int argc, char **argv) {
    try {
        return run({argv, argv + argc});
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
#endif
