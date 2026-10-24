/** @file @brief 受容量限制的后台区块任务、不可变版本、滞回驻留与保存后卸载实现。 */
#include "chunk_data.hpp"
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <thread>
#include <utility>
namespace renderer {
namespace {
using Key = std::pair<int32_t, int32_t>;
Key key(TileChunkCoord c) {
    return {c.x, c.z};
}
TileChunkCoord coord(Key k) {
    return {k.first, k.second};
}
int64_t distance(Key a, Key b) {
    return std::max(std::abs(int64_t(a.first) - b.first), std::abs(int64_t(a.second) - b.second));
}
int32_t divide(int32_t value, int32_t n) {
    return value / n - (value % n < 0 ? 1 : 0);
}
} // namespace
struct TileStreamer::Impl {
    enum class State { Pending, Ready, Resident, Failed };
    struct Entry {
        State state = State::Pending;
        std::shared_ptr<const TileChunkData> data;
        uint64_t loadTicket = 0, saveTicket = 0, revision = 1;
        bool dirty = false, saving = false, saveFailed = false, saveRequested = false;
    };
    struct Task {
        Key key;
        uint64_t ticket = 0, revision = 0;
        bool saving = false;
        std::shared_ptr<const TileChunkData> data;
        std::stop_source cancel;
    };
    struct Result {
        Task task;
        std::shared_ptr<const TileChunkData> data;
        std::string error;
        bool cancelled = false;
    };
    TileStreamConfig config;
    TileStreamSource source;
    TileMap map;
    std::map<Key, Entry> entries;
    std::map<uint64_t, Task> outstanding;
    std::vector<TileStreamEvent> events;
    uint64_t next = 1, loaded = 0, unloaded = 0, failures = 0;
    bool capacityLimited = false;
    std::mutex mutex;
    std::condition_variable_any ready;
    std::deque<Task> tasks;
    std::vector<Result> results;
    std::vector<std::jthread> workers;
    Impl(const TileStreamConfig &c, TileStreamSource s)
        : config(c), source(std::move(s)), map(c.map) {
        const uint64_t side = 2 * uint64_t(c.unloadRadius) + 1;
        if (!source.load || !source.resolve || c.loadRadius > c.unloadRadius ||
            c.unloadRadius > 32 || !c.workerCount || c.workerCount > 16 || !c.maxPendingRequests ||
            c.maxPendingRequests > c.maxResidentChunks || c.maxResidentChunks < side * side ||
            c.maxResidentChunks > 16384 || !c.maxCommitsPerUpdate || !c.maxUnloadsPerUpdate)
            throw std::invalid_argument("Invalid tile streaming configuration/source");
        workers.reserve(c.workerCount);
        for (uint32_t i = 0; i < c.workerCount; ++i)
            workers.emplace_back([this](std::stop_token stop) { work(stop); });
    }
    ~Impl() {
        for (auto &[id, task] : outstanding) {
            (void)id;
            task.cancel.request_stop();
        }
        for (auto &worker : workers)
            worker.request_stop();
        ready.notify_all();
        workers.clear();
    }
    void work(std::stop_token stop) {
        while (!stop.stop_requested()) {
            Task task;
            {
                std::unique_lock lock(mutex);
                if (!ready.wait(lock, stop, [&] { return !tasks.empty(); }))
                    return;
                task = std::move(tasks.front());
                tasks.pop_front();
            }
            Result result{task};
            try {
                const auto token = task.cancel.get_token();
                tile_detail::cancelled(token);
                if (task.saving)
                    source.save(coord(task.key), *task.data, token);
                else {
                    auto data = source.load(coord(task.key), token);
                    tile_detail::validate(data);
                    if (data.chunkSize != config.map.chunkSize)
                        throw std::invalid_argument(
                            "Loaded chunk size differs from map configuration");
                    result.data = std::make_shared<TileChunkData>(std::move(data));
                }
            } catch (const std::exception &error) {
                result.error = error.what();
            } catch (...) {
                result.error = "Unknown tile data source error";
            }
            result.cancelled = task.cancel.stop_requested();
            std::lock_guard lock(mutex);
            results.push_back(std::move(result));
        }
    }
    void enqueue(Task task) {
        if (next == std::numeric_limits<uint64_t>::max())
            throw std::overflow_error("Tile streaming ticket exhausted");
        task.ticket = next++;
        auto &entry = entries.at(task.key);
        if (task.saving) {
            entry.saveTicket = task.ticket;
            entry.saving = true;
        } else
            entry.loadTicket = task.ticket;
        outstanding.emplace(task.ticket, task);
        {
            std::lock_guard lock(mutex);
            // 保存优先，避免远处脏块长时间占用驻留容量。
            if (task.saving)
                tasks.push_front(std::move(task));
            else
                tasks.push_back(std::move(task));
        }
        ready.notify_one();
    }
    void consume() {
        std::vector<Result> done;
        {
            std::lock_guard lock(mutex);
            done.swap(results);
        }
        for (auto &result : done) {
            const auto &task = result.task;
            outstanding.erase(task.ticket);
            auto found = entries.find(task.key);
            if (found == entries.end())
                continue;
            auto &entry = found->second;
            if (task.saving) {
                if (entry.saveTicket != task.ticket)
                    continue;
                entry.saving = false;
                if (result.cancelled)
                    continue;
                if (!result.error.empty()) {
                    entry.saveFailed = true;
                    ++failures;
                    events.push_back({TileStreamEventKind::SaveFailed, coord(task.key), task.data,
                                      result.error});
                } else {
                    if (entry.revision == task.revision) {
                        entry.dirty = false;
                        entry.saveRequested = false;
                    }
                    events.push_back({TileStreamEventKind::Saved, coord(task.key), task.data, {}});
                }
            } else if (entry.loadTicket == task.ticket && entry.state == State::Pending) {
                if (result.cancelled) {
                    entries.erase(found);
                    continue;
                }
                if (!result.error.empty()) {
                    entry.state = State::Failed;
                    ++failures;
                    events.push_back(
                        {TileStreamEventKind::LoadFailed, coord(task.key), {}, result.error});
                } else {
                    entry.state = State::Ready;
                    entry.data = std::move(result.data);
                }
            }
        }
    }
    bool validKey(Key k) const {
        const int64_t n = config.map.chunkSize;
        return int64_t(k.first) * n >= INT32_MIN && int64_t(k.first) * n + n - 1 <= INT32_MAX &&
               int64_t(k.second) * n >= INT32_MIN && int64_t(k.second) * n + n - 1 <= INT32_MAX;
    }
    Key focusKey(Vec3 focus) const {
        if (!finite(focus))
            throw std::invalid_argument("Invalid streaming focus");
        const double span = double(config.map.tileSize) * config.map.chunkSize;
        const double x = std::floor((double(focus.x) - config.map.origin.x) / span),
                     z = std::floor((double(focus.z) - config.map.origin.z) / span);
        if (x < INT32_MIN || x > INT32_MAX || z < INT32_MIN || z > INT32_MAX)
            throw std::out_of_range("Streaming focus exceeds coordinate range");
        const Key k{int32_t(x), int32_t(z)};
        if (!validKey(k))
            throw std::out_of_range("Streaming chunk exceeds tile coordinate range");
        return k;
    }
    void publish(Key k, const TileChunkData &data, AssetStore &assets) {
        std::map<uint32_t, SpriteHandle> types;
        std::vector<TileChunkCell> cells;
        cells.reserve(data.tiles.size());
        for (const auto &tile : data.tiles) {
            auto [found, inserted] = types.try_emplace(tile.type);
            if (inserted)
                found->second = source.resolve(tile.type, assets);
            cells.push_back({tile.x, tile.z, found->second});
        }
        map.replaceChunk(k.first, k.second, assets, cells);
    }
    TileStreamStats stats() const {
        TileStreamStats s;
        for (const auto &[k, entry] : entries) {
            (void)k;
            s.residentChunks += entry.state == State::Resident;
            s.readyChunks += entry.state == State::Ready;
            s.dirtyChunks += entry.dirty;
            s.failedChunks += entry.state == State::Failed || entry.saveFailed;
        }
        for (const auto &[id, task] : outstanding) {
            (void)id;
            task.saving ? ++s.pendingSaves : ++s.pendingLoads;
        }
        s.loaded = loaded;
        s.unloaded = unloaded;
        s.failures = failures;
        s.capacityLimited = capacityLimited;
        return s;
    }
};
TileStreamer::TileStreamer(const TileStreamConfig &config, TileStreamSource source)
    : impl_(std::make_unique<Impl>(config, std::move(source))) {}
TileStreamer::~TileStreamer() = default;
TileStreamer::TileStreamer(TileStreamer &&) noexcept = default;
TileStreamer &TileStreamer::operator=(TileStreamer &&) noexcept = default;
TileStreamStats TileStreamer::statistics() const {
    return impl_->stats();
}
std::shared_ptr<const TileChunkData> TileStreamer::chunk(TileChunkCoord c) const {
    auto found = impl_->entries.find(key(c));
    return found != impl_->entries.end() && found->second.state == Impl::State::Resident
               ? found->second.data
               : nullptr;
}
std::vector<SpriteBatch> TileStreamer::batches(const AssetStore &assets) {
    return impl_->map.batches(assets);
}
std::vector<TileStreamEvent> TileStreamer::takeEvents() {
    return std::exchange(impl_->events, {});
}
void TileStreamer::requestSave() {
    for (auto &[k, entry] : impl_->entries) {
        (void)k;
        if (entry.dirty)
            entry.saveRequested = true;
    }
}
bool TileStreamer::retry(TileChunkCoord c) {
    auto found = impl_->entries.find(key(c));
    if (found == impl_->entries.end())
        return false;
    if (found->second.state == Impl::State::Failed) {
        impl_->entries.erase(found);
        return true;
    }
    if (found->second.saveFailed) {
        found->second.saveFailed = false;
        found->second.saveRequested = true;
        return true;
    }
    return false;
}
void TileStreamer::setTile(int32_t x, int32_t z, uint32_t type, AssetStore &assets) {
    if (!impl_->source.save)
        throw std::logic_error("Editing streamed tiles requires a save callback");
    const auto n = int32_t(impl_->config.map.chunkSize);
    const Key k{divide(x, n), divide(z, n)};
    auto found = impl_->entries.find(k);
    if (found == impl_->entries.end() || found->second.state != Impl::State::Resident)
        throw std::out_of_range("Tile chunk is not resident");
    auto &entry = found->second;
    auto next = std::make_shared<TileChunkData>(*entry.data);
    const uint32_t lx = uint32_t(int64_t(x) - int64_t(k.first) * n),
                   lz = uint32_t(int64_t(z) - int64_t(k.second) * n);
    auto tile = std::find_if(next->tiles.begin(), next->tiles.end(),
                             [&](const auto &cell) { return cell.x == lx && cell.z == lz; });
    if (tile != next->tiles.end()) {
        if (tile->type == type)
            return;
        if (type)
            tile->type = type;
        else
            next->tiles.erase(tile);
    } else if (type)
        next->tiles.push_back({lx, lz, type});
    else
        return;
    if (entry.revision == std::numeric_limits<uint64_t>::max())
        throw std::overflow_error("Tile chunk revision exhausted");
    impl_->publish(k, *next, assets);
    entry.data = std::move(next);
    ++entry.revision;
    entry.dirty = true;
    entry.saveRequested = entry.saveRequested || entry.saving;
}
void TileStreamer::setPayload(TileChunkCoord c, std::span<const uint8_t> payload) {
    if (!impl_->source.save)
        throw std::logic_error("Editing streamed state requires a save callback");
    auto found = impl_->entries.find(key(c));
    if (found == impl_->entries.end() || found->second.state != Impl::State::Resident)
        throw std::out_of_range("Tile chunk is not resident");
    if (payload.size() > tile_detail::maxPayload)
        throw std::length_error("Tile chunk payload exceeds 16 MiB");
    auto &entry = found->second;
    if (entry.data->payload.size() == payload.size() &&
        std::equal(payload.begin(), payload.end(), entry.data->payload.begin()))
        return;
    if (entry.revision == std::numeric_limits<uint64_t>::max())
        throw std::overflow_error("Tile chunk revision exhausted");
    auto next = std::make_shared<TileChunkData>(*entry.data);
    next->payload.assign(payload.begin(), payload.end());
    entry.data = std::move(next);
    ++entry.revision;
    entry.dirty = true;
    entry.saveRequested = entry.saveRequested || entry.saving;
}
TileStreamStats TileStreamer::update(Vec3 focus, AssetStore &assets) {
    auto &p = *impl_;
    const auto center = p.focusKey(focus);
    p.capacityLimited = false;
    p.consume();
    std::vector<Key> unload, publish;
    for (auto i = p.entries.begin(); i != p.entries.end();) {
        const auto d = distance(i->first, center);
        if (i->second.state != Impl::State::Resident && d > p.config.unloadRadius) {
            if (auto job = p.outstanding.find(i->second.loadTicket); job != p.outstanding.end())
                job->second.cancel.request_stop();
            i = p.entries.erase(i);
            continue;
        }
        if (i->second.state == Impl::State::Resident && d > p.config.unloadRadius &&
            !i->second.dirty && !i->second.saving)
            unload.push_back(i->first);
        if (i->second.state == Impl::State::Ready)
            publish.push_back(i->first);
        ++i;
    }
    auto nearest = [&](Key a, Key b) {
        const auto da = distance(a, center), db = distance(b, center);
        return da != db ? da < db : a < b;
    };
    std::sort(unload.begin(), unload.end(), [&](Key a, Key b) { return nearest(b, a); });
    if (unload.size() > p.config.maxUnloadsPerUpdate)
        unload.resize(p.config.maxUnloadsPerUpdate);
    for (auto k : unload) {
        p.map.unloadChunk(k.first, k.second);
        p.events.push_back({TileStreamEventKind::Unloaded, coord(k), p.entries.at(k).data, {}});
        p.entries.erase(k);
        ++p.unloaded;
    }
    for (auto &[k, entry] : p.entries)
        if (entry.state == Impl::State::Resident && entry.dirty && !entry.saving &&
            !entry.saveFailed &&
            (entry.saveRequested || distance(k, center) > p.config.unloadRadius) &&
            p.outstanding.size() < p.config.maxPendingRequests)
            p.enqueue({k, 0, entry.revision, true, entry.data});
    std::sort(publish.begin(), publish.end(), nearest);
    if (publish.size() > p.config.maxCommitsPerUpdate)
        publish.resize(p.config.maxCommitsPerUpdate);
    for (auto k : publish) {
        auto &entry = p.entries.at(k);
        try {
            p.publish(k, *entry.data, assets);
            entry.state = Impl::State::Resident;
            ++p.loaded;
            p.events.push_back({TileStreamEventKind::Loaded, coord(k), entry.data, {}});
        } catch (...) {
            std::string message = "Unknown tile resource resolution error";
            try {
                throw;
            } catch (const std::exception &error) {
                message = error.what();
            } catch (...) {
            }
            entry.state = Impl::State::Failed;
            entry.data.reset();
            ++p.failures;
            p.events.push_back({TileStreamEventKind::LoadFailed, coord(k), {}, std::move(message)});
        }
    }
    std::vector<Key> desired;
    const int64_t r = p.config.loadRadius;
    for (int64_t z = int64_t(center.second) - r; z <= int64_t(center.second) + r; ++z)
        for (int64_t x = int64_t(center.first) - r; x <= int64_t(center.first) + r; ++x)
            if (x >= INT32_MIN && x <= INT32_MAX && z >= INT32_MIN && z <= INT32_MAX &&
                p.validKey({int32_t(x), int32_t(z)}))
                desired.emplace_back(int32_t(x), int32_t(z));
    std::sort(desired.begin(), desired.end(), nearest);
    size_t reserved = std::count_if(p.entries.begin(), p.entries.end(), [](const auto &item) {
        return item.second.state != Impl::State::Failed;
    });
    for (auto k : desired) {
        if (p.entries.contains(k))
            continue;
        if (reserved >= p.config.maxResidentChunks) {
            p.capacityLimited = true;
            break;
        }
        if (p.outstanding.size() >= p.config.maxPendingRequests)
            break;
        p.entries.emplace(k, Impl::Entry{});
        p.enqueue({k});
        ++reserved;
    }
    return p.stats();
}
} // namespace renderer
