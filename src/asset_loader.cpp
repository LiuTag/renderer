/** @file @brief 用独立 CPU 仓库解码，取消过期结果并在所有者线程接收完整资源包。 */
#include "renderer/asset_loader.hpp"
#include "internal/pool.hpp"
#include <algorithm>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
namespace renderer {
struct AssetLoader::Impl {
    struct Request {
        AssetLoadInfo info;
        std::filesystem::path path;
        int priority = 0;
        bool image = false, srgb = true, delivered = false;
        TextureSampling sampling;
        std::unique_ptr<AssetStore> store;
        TextureHandle texture;
        Model model;
        uint64_t package = 0;
    };
    AssetLoaderConfig config;
    std::thread::id owner = std::this_thread::get_id();
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::map<AssetRequestId, std::shared_ptr<Request>> requests;
    std::vector<std::thread> workers;
    uint64_t readyBytes = 0;
    bool stopping = false;
    explicit Impl(const AssetLoaderConfig &c) : config(c) {
        if (!c.workers || c.workers > 16 || !c.maxRequests || c.maxRequests > 65536 ||
            !c.maxReadyBytes)
            throw std::invalid_argument("Invalid asset loader capacity");
        try {
            for (uint32_t i = 0; i < c.workers; ++i)
                workers.emplace_back([this] { run(); });
        } catch (...) {
            stop();
            throw;
        }
    }
    ~Impl() {
        stop();
    }
    void stop() {
        {
            std::lock_guard lock(mutex);
            stopping = true;
        }
        changed.notify_all();
        for (auto &w : workers)
            if (w.joinable())
                w.join();
    }
    void check() const {
        if (owner != std::this_thread::get_id())
            throw std::logic_error("AssetLoader control thread mismatch");
    }
    std::shared_ptr<Request> get(AssetRequestId id) const {
        auto it = requests.find(id);
        if (it == requests.end())
            throw std::invalid_argument("Unknown asset request");
        return it->second;
    }
    void run() {
        for (;;) {
            std::shared_ptr<Request> job;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, [&] {
                    return stopping || std::any_of(requests.begin(), requests.end(), [](auto &p) {
                               return p.second->info.state == AssetLoadState::Queued;
                           });
                });
                if (stopping)
                    return;
                for (auto &[id, r] : requests) {
                    (void)id;
                    if (r->info.state == AssetLoadState::Queued &&
                        (!job || r->priority > job->priority))
                        job = r;
                }
                job->info.state = AssetLoadState::Loading;
            }
            std::unique_ptr<AssetStore> store;
            TextureHandle texture;
            Model model;
            std::string error;
            uint64_t bytes = 0;
            try {
                store = std::make_unique<AssetStore>();
                if (job->image)
                    texture = store->loadTexture(job->path, job->srgb, job->sampling);
                else
                    model = store->loadModel(job->path);
                auto m = store->memoryStatistics();
                bytes = m.meshBytes + m.textureBytes;
            } catch (const std::exception &e) {
                error = e.what();
            } catch (...) {
                error = "Unknown asset decoding failure";
            }
            {
                std::lock_guard lock(mutex);
                if (stopping)
                    return;
                if (job->info.state == AssetLoadState::Cancelled)
                    continue;
                job->info.bytes = bytes;
                if (error.empty() && bytes > config.maxReadyBytes - readyBytes)
                    error = "Decoded result exceeds ready memory budget; publish other results and "
                            "retry";
                if (!error.empty()) {
                    job->info.state = AssetLoadState::Failed;
                    job->info.error = std::move(error);
                } else {
                    readyBytes += bytes;
                    job->store = std::move(store);
                    job->texture = texture;
                    job->model = std::move(model);
                    job->info.state = AssetLoadState::Ready;
                }
            }
        }
    }
    AssetRequestId add(const std::filesystem::path &path, bool image, bool srgb, int priority,
                       TextureSampling sampling) {
        check();
        if (path.empty())
            throw std::invalid_argument("Empty asset path");
        auto r = std::make_shared<Request>();
        r->path = std::filesystem::absolute(path);
        r->image = image;
        r->srgb = srgb;
        r->priority = priority;
        r->sampling = sampling;
        std::lock_guard lock(mutex);
        if (requests.size() >= config.maxRequests)
            throw std::length_error("Asset request capacity reached; forget consumed records");
        r->info.id = detail::uniqueId();
        requests.emplace(r->info.id, r);
        changed.notify_one();
        return r->info.id;
    }
};
AssetLoader::AssetLoader(const AssetLoaderConfig &c) : impl_(std::make_unique<Impl>(c)) {}
AssetLoader::~AssetLoader() = default;
AssetRequestId AssetLoader::loadTexture(const std::filesystem::path &p, bool s, int priority,
                                        TextureSampling sampling) {
    return impl_->add(p, true, s, priority, sampling);
}
AssetRequestId AssetLoader::loadModel(const std::filesystem::path &p, int priority) {
    return impl_->add(p, false, true, priority, {});
}
bool AssetLoader::cancel(AssetRequestId id) {
    impl_->check();
    std::lock_guard lock(impl_->mutex);
    auto r = impl_->get(id);
    if (r->info.state == AssetLoadState::Published || r->info.state == AssetLoadState::Cancelled ||
        r->info.state == AssetLoadState::Failed)
        return false;
    if (r->info.state == AssetLoadState::Ready)
        impl_->readyBytes -= r->info.bytes;
    r->store.reset();
    r->model = {};
    r->texture = {};
    r->info.state = AssetLoadState::Cancelled;
    return true;
}
std::vector<AssetLoadResult> AssetLoader::publish(AssetStore &destination,
                                                  AssetPublishBudget budget) {
    impl_->check();
    if (!budget.maxRequests || !budget.maxBytes)
        throw std::invalid_argument("Publish budget must be positive");
    std::vector<AssetLoadResult> out;
    uint64_t publishedBytes = 0;
    uint32_t published = 0;
    std::lock_guard lock(impl_->mutex);
    for (auto &[id, r] : impl_->requests) {
        (void)id;
        if (r->info.state == AssetLoadState::Ready && published < budget.maxRequests &&
            (!published ||
             r->info.bytes <= budget.maxBytes - std::min(publishedBytes, budget.maxBytes))) {
            try {
                r->package = destination.adopt(std::move(*r->store));
                r->info.state = AssetLoadState::Published;
                ++published;
                publishedBytes += r->info.bytes;
            } catch (const std::exception &e) {
                r->info.state = AssetLoadState::Failed;
                r->info.error = e.what();
            }
            impl_->readyBytes -= r->info.bytes;
            r->store.reset();
        }
        if (!r->delivered && (r->info.state == AssetLoadState::Published ||
                              r->info.state == AssetLoadState::Failed ||
                              r->info.state == AssetLoadState::Cancelled)) {
            out.push_back(
                {r->info, r->info.state == AssetLoadState::Published ? r->texture : TextureHandle{},
                 r->info.state == AssetLoadState::Published ? r->model : Model{}, r->package});
            r->delivered = true;
        }
    }
    return out;
}
AssetLoadInfo AssetLoader::status(AssetRequestId id) const {
    impl_->check();
    std::lock_guard lock(impl_->mutex);
    return impl_->get(id)->info;
}
void AssetLoader::forget(AssetRequestId id) {
    impl_->check();
    std::lock_guard lock(impl_->mutex);
    auto r = impl_->get(id);
    if (!r->delivered)
        throw std::logic_error("Consume terminal event before forgetting request");
    impl_->requests.erase(id);
}
AssetLoaderStats AssetLoader::statistics() const {
    impl_->check();
    std::lock_guard lock(impl_->mutex);
    AssetLoaderStats s;
    s.records = uint32_t(impl_->requests.size());
    s.readyBytes = impl_->readyBytes;
    for (auto &[id, r] : impl_->requests) {
        (void)id;
        s.queued += r->info.state == AssetLoadState::Queued;
        s.loading += r->info.state == AssetLoadState::Loading;
        s.ready += r->info.state == AssetLoadState::Ready;
    }
    return s;
}
} // namespace renderer
