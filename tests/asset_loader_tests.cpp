/** @file @brief 验证后台解码、主线程发布预算、取消、失败、资源包卸载和快照内存保留。 */
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <renderer/asset_loader.hpp>
#include <renderer/renderer.hpp>
#include <thread>
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
} // namespace
int main() {
    try {
        const std::filesystem::path dir = RENDERER_TEST_OUTPUT;
        std::filesystem::create_directories(dir);
        auto image = dir / std::filesystem::path(u8"测试🍃图片.png");
        Image{8, 8, std::vector<uint8_t>(8 * 8 * 4, 255)}.savePng(image);
        auto obj = dir / std::filesystem::path(u8"三角形.obj");
        std::ofstream(dir / std::filesystem::path(u8"🌲材质.mtl"))
            << "newmtl leaf\nmap_Kd 测试🍃图片.png\n";
        std::ofstream(obj)
            << "mtllib 🌲材质.mtl\nv 0 0 -3\nv 1 0 -3\nv 0 1 -3\nusemtl leaf\nf 1 2 3\n";
        AssetLoader loader({1, 8, 1024 * 1024});
        AssetStore store;
        const auto texture = loader.loadTexture(image);
        const auto model = loader.loadModel(obj);
        const auto cancelled = loader.loadTexture(image);
        require(loader.cancel(cancelled), "Cancel did not change state");
        const auto missing = loader.loadTexture(dir / "absent.png");
        std::vector<AssetLoadResult> results;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (results.size() < 4 && std::chrono::steady_clock::now() < deadline) {
            auto events = loader.publish(store, {1, 1});
            unsigned published = 0;
            for (auto &e : events) {
                published += e.info.state == AssetLoadState::Published;
                results.push_back(std::move(e));
            }
            require(published <= 1, "Publication count budget ignored");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(results.size() == 4, "Asset tasks did not terminate");
        AssetLoadResult textureResult, modelResult;
        for (const auto &r : results) {
            if (r.info.id == texture)
                textureResult = r;
            if (r.info.id == model)
                modelResult = r;
            if (r.info.id == cancelled)
                require(r.info.state == AssetLoadState::Cancelled, "Cancelled request published");
            if (r.info.id == missing)
                require(r.info.state == AssetLoadState::Failed && !r.info.error.empty(),
                        "Missing asset not diagnosed");
        }
        require(textureResult.info.state == AssetLoadState::Published &&
                    modelResult.info.state == AssetLoadState::Published,
                "Unicode resource decoding failed");
        require(store.textureDescription(textureResult.texture).pixels.size() == 256,
                "Published texture not adopted");
        auto material = store.createMaterial({.baseColorTexture = textureResult.texture});
        Scene scene;
        RenderExtractor extractor;
        auto instance = modelResult.model.instantiate(scene, scene.root());
        auto pinned = extractor.extract(scene, store);
        require(pinned.instanceCount() == 1, "Adopted model handles are invalid");
        auto before = store.memoryStatistics();
        require(before.meshHandles == 1 && before.textureBytes == 512,
                "CPU payload statistics wrong");
        store.releasePackage(modelResult.package);
        require(store.memoryStatistics().meshHandles == 0 && store.memoryStatistics().meshBytes > 0,
                "Snapshot-retained mesh omitted");
        throws([&] { extractor.extract(scene, store); });
        pinned = {};
        require(store.memoryStatistics().meshBytes == 0, "Unloaded mesh payload leaked");
        scene.destroySubtree(instance.root);
        store.releasePackage(textureResult.package);
        require(store.memoryStatistics().textureBytes == 256, "Material-retained texture omitted");
        store.destroyMaterial(material);
        require(store.memoryStatistics().textureBytes == 0, "Texture payload leaked");
        throws([&] { store.textureDescription(textureResult.texture); });
        for (const auto &r : results)
            loader.forget(r.info.id);
        require(loader.statistics().records == 0, "Terminal records not reusable");
        auto foreign =
            std::async(std::launch::async, [&] { throws([&] { loader.statistics(); }); });
        foreign.get();
        AssetLoader small({1, 2, 16});
        auto over = small.loadTexture(image);
        while (small.status(over).state == AssetLoadState::Queued ||
               small.status(over).state == AssetLoadState::Loading)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        require(small.status(over).state == AssetLoadState::Failed, "Ready memory bound ignored");
        throws([&] { store.adopt(std::move(store)); });
        std::cout << "Asset loading, budgets, packages and memory passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
