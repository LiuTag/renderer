/** @file @brief 流式区块预算、后台线程、过期请求、错误恢复、持久化和文件损坏检查。 */
#include "sprite_fixture.hpp"
#include <atomic>
#include <chrono>
#include <fstream>
#include <map>
#include <mutex>
#include <renderer/tilemap/streaming.hpp>
#include <thread>
using namespace sprite_test;
namespace {
TileChunkData data(uint32_t type = 1) {
    return {2, {{0, 0, type}}, {9, 8, 7}};
}
TileStreamConfig config() {
    TileStreamConfig c;
    c.map.chunkSize = 2;
    c.loadRadius = c.unloadRadius = 0;
    c.maxPendingRequests = 2;
    c.maxResidentChunks = 2;
    c.maxCommitsPerUpdate = c.maxUnloadsPerUpdate = 1;
    return c;
}
template <class F> void await(F predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate()) {
        require(std::chrono::steady_clock::now() < deadline, "Streaming operation timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
void lifetimeAndSave() {
    Fixture f;
    const auto main = std::this_thread::get_id();
    std::mutex mutex;
    std::map<std::pair<int32_t, int32_t>, TileChunkData> saved;
    std::atomic<int> loads = 0;
    TileStreamSource source;
    source.load = [&](TileChunkCoord c, std::stop_token) {
        require(std::this_thread::get_id() != main, "Loader ran on foreground thread");
        ++loads;
        std::lock_guard lock(mutex);
        auto found = saved.find({c.x, c.z});
        return found == saved.end() ? data() : found->second;
    };
    source.save = [&](TileChunkCoord c, const TileChunkData &d, std::stop_token) {
        require(std::this_thread::get_id() != main, "Saver ran on foreground thread");
        std::lock_guard lock(mutex);
        saved[{c.x, c.z}] = d;
    };
    source.resolve = [&](uint32_t type, AssetStore &) {
        require(std::this_thread::get_id() == main, "Resolver ran in background");
        return type == 1 ? f.first : f.second;
    };
    TileStreamer stream(config(), source);
    await([&] {
        stream.update({-.1f, 0, -.1f}, f.assets);
        return bool(stream.chunk({-1, -1}));
    });
    auto batches = stream.batches(f.assets);
    auto old = f.extractor.extract(f.scene, f.assets, batches);
    auto before = stream.chunk({-1, -1});
    stream.setTile(-2, -2, 2, f.assets);
    const std::vector<uint8_t> payload{1, 2, 3, 4};
    stream.setPayload({-1, -1}, payload);
    require(before->tiles[0].type == 1 && stream.chunk({-1, -1})->tiles[0].type == 2);
    require(before->payload == data().payload && stream.chunk({-1, -1})->payload == payload);
    require(stream.statistics().dirtyChunks == 1);
    // 远离时必须保存成功再卸载，重新进入取得编辑后的数据。
    await([&] {
        const auto s = stream.update({10, 0, 0}, f.assets);
        return !stream.chunk({-1, -1}) && stream.chunk({5, 0}) && s.pendingSaves == 0;
    });
    require(old.spriteCount() == 1);
    await([&] {
        stream.update({-.1f, 0, -.1f}, f.assets);
        return bool(stream.chunk({-1, -1}));
    });
    require(stream.chunk({-1, -1})->tiles[0].type == 2 && loads >= 3);
    require(stream.chunk({-1, -1})->payload == payload);
    bool unloaded = false, savedEvent = false;
    for (const auto &e : stream.takeEvents()) {
        unloaded |= e.kind == TileStreamEventKind::Unloaded;
        savedEvent |= e.kind == TileStreamEventKind::Saved;
    }
    require(unloaded && savedEvent);
    stream.setTile(-2, -2, 0, f.assets);
    stream.requestSave();
    await([&] {
        const auto s = stream.update({-.1f, 0, -.1f}, f.assets);
        return s.dirtyChunks == 0 && s.pendingSaves == 0;
    });
    require(stream.chunk({-1, -1})->tiles.empty() && stream.batches(f.assets).empty());
    throws([&] { stream.setTile(100, 100, 1, f.assets); });
}
void budgetAndHysteresis() {
    Fixture f;
    auto c = config();
    c.loadRadius = 1;
    c.unloadRadius = 2;
    c.maxResidentChunks = 25;
    c.maxPendingRequests = 4;
    TileStreamer stream(c, {.load = [](auto, auto) { return data(); },
                            .resolve = [&](auto, auto &) { return f.first; }});
    uint64_t previous = 0;
    await([&] {
        const auto s = stream.update({}, f.assets);
        require(s.loaded - previous <= 1 && s.pendingLoads <= 4 &&
                s.residentChunks + s.readyChunks <= 25);
        previous = s.loaded;
        return s.residentChunks == 9;
    });
    const auto loaded = stream.statistics().loaded;
    stream.update({2, 0, 0}, f.assets);
    require(stream.chunk({-1, 0}) && stream.statistics().unloaded == 0);
    require(loaded == 9);
    uint64_t unloaded = 0;
    await([&] {
        const auto s = stream.update({30, 0, 0}, f.assets);
        require(s.unloaded - unloaded <= 1);
        unloaded = s.unloaded;
        stream.takeEvents();
        return s.residentChunks == 9 && s.pendingLoads == 0 && s.readyChunks == 0;
    });
    require(stream.statistics().unloaded >= 9);
    throws([&] { stream.setTile(30, 0, 2, f.assets); }); // 只读源禁止丢失式编辑。
}
void cancellation() {
    Fixture f;
    std::atomic<bool> began = false, cancelled = false;
    auto c = config();
    c.maxPendingRequests = 1;
    c.workerCount = 1;
    {
        TileStreamer stream(c,
                            {.load =
                                 [&](TileChunkCoord k, std::stop_token stop) {
                                     if (k.x == 0) {
                                         began = true;
                                         while (!stop.stop_requested())
                                             std::this_thread::yield();
                                         cancelled = true;
                                         return data(2); // 过期回调即使返回正常数据，也不能发布。
                                     }
                                     return data();
                                 },
                             .resolve = [&](auto, auto &) { return f.first; }});
        stream.update({}, f.assets);
        await([&] { return began.load(); });
        await([&] {
            stream.update({10, 0, 0}, f.assets);
            return bool(stream.chunk({5, 0}));
        });
        require(cancelled && !stream.chunk({0, 0}) && stream.statistics().loaded == 1);
    }
    // 析构取消正在执行的任务，避免回调持有已释放的状态。
    began = false;
    cancelled = false;
    {
        TileStreamer stream(c, {.load =
                                    [&](auto, std::stop_token stop) {
                                        began = true;
                                        while (!stop.stop_requested())
                                            std::this_thread::yield();
                                        cancelled = true;
                                        return data();
                                    },
                                .resolve = [&](auto, auto &) { return f.first; }});
        stream.update({}, f.assets);
        await([&] { return began.load(); });
    }
    require(cancelled);
}
void failuresAndRevisions() {
    Fixture f;
    std::atomic<bool> failLoad = true, failSave = true, saveStarted = false, finishSave = false;
    std::atomic<uint32_t> savedType = 0;
    auto c = config();
    c.maxResidentChunks = 1;
    c.maxPendingRequests = 1;
    c.workerCount = 1;
    TileStreamer stream(c, {.load =
                                [&](auto, auto) {
                                    if (failLoad.exchange(false))
                                        throw std::runtime_error("load failed");
                                    return data();
                                },
                            .save =
                                [&](auto, const TileChunkData &d, std::stop_token stop) {
                                    if (failSave.exchange(false))
                                        throw std::runtime_error("save failed");
                                    saveStarted = true;
                                    while (!finishSave && !stop.stop_requested())
                                        std::this_thread::yield();
                                    savedType = d.tiles[0].type;
                                },
                            .resolve = [&](uint32_t type, auto &) -> SpriteHandle {
                                if (type == 99)
                                    throw std::runtime_error("unknown tile");
                                return type == 1 ? f.first : f.second;
                            }});
    await([&] { return stream.update({}, f.assets).failedChunks == 1; });
    require(!stream.chunk({0, 0}) && stream.retry({0, 0}));
    await([&] {
        stream.update({}, f.assets);
        return bool(stream.chunk({0, 0}));
    });
    throws([&] { stream.setTile(0, 0, 99, f.assets); });
    require(stream.chunk({0, 0})->tiles[0].type == 1);
    stream.setTile(0, 0, 2, f.assets);
    await([&] { return stream.update({10, 0, 0}, f.assets).failedChunks == 1; });
    require(stream.chunk({0, 0}) && stream.statistics().dirtyChunks == 1 &&
            stream.statistics().capacityLimited);
    require(stream.retry({0, 0}));
    stream.requestSave();
    stream.update({}, f.assets);
    await([&] { return saveStarted.load(); });
    stream.setTile(0, 0, 1, f.assets); // 保存进行中继续编辑，旧任务不得清除新的脏版本。
    finishSave = true;
    await([&] {
        const auto s = stream.update({}, f.assets);
        return s.dirtyChunks == 0 && s.pendingSaves == 0;
    });
    require(savedType == 1);
    require(!stream.retry({22, 22}));
    throws([] {
        auto c = config();
        c.maxResidentChunks = 0;
        TileStreamer bad(c, {});
    });
}
void files() {
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("renderer-stream-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto path = directory / L"地图.rtile";
    try {
        writeTileChunk(path, data());
        require(readTileChunk(path).payload == data().payload);
        writeTileChunk(path, data(2));
        require(readTileChunk(path).tiles[0].type == 2);
        auto invalid = data();
        invalid.tiles.push_back(invalid.tiles[0]);
        throws([&] { writeTileChunk(path, invalid); });
        require(readTileChunk(path).tiles[0].type == 2);
        {
            std::fstream bytes(path, std::ios::binary | std::ios::in | std::ios::out);
            bytes.seekp(20);
            bytes.put(char(99));
        }
        throws([&] { readTileChunk(path); });
        {
            std::ofstream bytes(path, std::ios::binary | std::ios::trunc);
            bytes << "RTILE";
        }
        throws([&] { readTileChunk(path); });
        std::stop_source stop;
        stop.request_stop();
        throws([&] { writeTileChunk(path, data(), stop.get_token()); });
        std::filesystem::remove(path);
        std::filesystem::remove(directory);
    } catch (...) {
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::remove(directory, error);
        throw;
    }
}
} // namespace
int main() {
    try {
        lifetimeAndSave();
        budgetAndHysteresis();
        cancellation();
        failuresAndRevisions();
        files();
        std::cout << "5 streaming scenarios passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
