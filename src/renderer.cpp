#include "renderer/renderer.hpp"
#include "internal/backend.hpp"
#include "internal/frame.hpp"
#include "internal/frustum.hpp"
#include "internal/scene.hpp"
#include "internal/snapshot.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

namespace renderer {
RenderSnapshot::RenderSnapshot() = default;
RenderSnapshot::~RenderSnapshot() = default;
RenderSnapshot::RenderSnapshot(const RenderSnapshot &) = default;
RenderSnapshot &RenderSnapshot::operator=(const RenderSnapshot &) = default;
RenderSnapshot::RenderSnapshot(RenderSnapshot &&) noexcept = default;
RenderSnapshot &RenderSnapshot::operator=(RenderSnapshot &&) noexcept = default;
size_t RenderSnapshot::instanceCount() const {
    return impl_ ? impl_->scene().instances.size() + impl_->scene().sprites.size() +
                       impl_->batchSpriteCount
                 : 0;
}
size_t RenderSnapshot::spriteCount() const {
    return impl_ ? impl_->scene().sprites.size() + impl_->batchSpriteCount : 0;
}
size_t RenderSnapshot::cameraCount() const {
    return impl_ ? impl_->scene().cameras.size() : 0;
}
uint64_t RenderSnapshot::revision() const {
    return impl_ ? impl_->revision : 0;
}
std::optional<Mat4> RenderSnapshot::instanceTransform(NodeHandle h) const {
    if (impl_) {
        for (const auto &i : impl_->scene().instances)
            if (i.node == h)
                return i.world;
        for (const auto &s : impl_->scene().sprites)
            if (s.node == h)
                return s.world;
    }
    return std::nullopt;
}
struct RenderExtractor::Impl {
    struct SpatialCache {
        NodeHandle node;
        uint64_t version = 0, meshId = 0, seen = 0;
        Mat4 normal;
        AABB bounds;
        bool invertible = false;
        uint64_t spriteWorldVersion = 0;
        SpriteRenderer spriteComponent{};
        AABB spriteBounds;
        std::shared_ptr<const detail::SpriteGeometry> fixedGeometry;
    };
    uint64_t owner = 0, extraction = 0;
    bool caching = true, spatialEnabled = true;
    std::pair<uint64_t, uint64_t> assetsStamp{};
    std::shared_ptr<const RenderSnapshot::Impl> cachedScene;

    std::unordered_map<uint32_t, SpatialCache> spatial;
};
RenderExtractor::RenderExtractor() : impl_(std::make_unique<Impl>()) {}
void RenderExtractor::setCachingEnabled(bool enabled) {
    impl_->caching = enabled;
    impl_->cachedScene.reset();
}
void RenderExtractor::setSpatialIndexEnabled(bool enabled) {
    impl_->spatialEnabled = enabled;
    impl_->cachedScene.reset();
}
RenderExtractor::~RenderExtractor() = default;
void RenderExtractor::clearCache() {
    impl_->cachedScene.reset();
    impl_->spatial.clear();
}
RenderExtractor::RenderExtractor(RenderExtractor &&) noexcept = default;
RenderExtractor &RenderExtractor::operator=(RenderExtractor &&) noexcept = default;
RenderSnapshot RenderExtractor::extract(Scene &scene, const AssetStore &assets) {
    return extract(scene, assets, {});
}
RenderSnapshot RenderExtractor::extract(Scene &scene, const AssetStore &assets,
                                        std::span<const SpriteBatch> batches) {
    scene.impl_->updateDerived();
    // 提取器可被不同场景复用，但索引相同的节点不能共享上一场景的空间缓存。
    if (impl_->owner != scene.root().owner) {
        impl_->spatial.clear();
        impl_->cachedScene.reset();
        impl_->owner = scene.root().owner;
    }
    ++impl_->extraction;
    const auto stamp = detail::AssetAccess::stamp(assets);
    auto publish = [&](std::shared_ptr<const RenderSnapshot::Impl> base) {
        auto wrapper = std::make_shared<RenderSnapshot::Impl>();
        wrapper->revision = base->revision;
        wrapper->sourceScene = std::move(base);
        for (const auto &batch : batches)
            if (auto part = detail::SpriteBatchAccess::data(batch)) {
                wrapper->batchSpriteCount += part->sprites.size();
                wrapper->batches.push_back(std::move(part));
            }
        RenderSnapshot result;
        result.impl_ = std::move(wrapper);
        return result;
    };
    if (impl_->caching && impl_->cachedScene && impl_->cachedScene->revision == scene.revision() &&
        impl_->assetsStamp == stamp)
        return publish(impl_->cachedScene);
    auto previous =
        std::move(impl_->cachedScene); // 异常退出后局部引用随栈释放，不在缓存中保留旧包。
    auto data = std::make_shared<RenderSnapshot::Impl>();
    data->revision = scene.revision();
    if (previous) {
        data->sprites.reserve(previous->sprites.size());
        data->instances.reserve(previous->instances.size());
        data->cameras.reserve(previous->cameras.size());
        data->lights.reserve(previous->lights.size());
    }

    struct Group {
        NodeHandle node;
        SortingGroup component;
        Mat4 world;
    };
    std::unordered_map<uint32_t, Group> groups;
    for (auto h : scene.impl_->traversal) {
        const auto &n = scene.impl_->nodes.get(h);
        if (!n.effectiveVisible)
            continue;
        if (n.camera)
            data->cameras.push_back({h, *n.camera, n.world});
        if (n.light)
            data->lights.push_back({*n.light, n.world});
        // 父节点先于子节点；最外层分组一直向下传播，保持多部件对象不可分割。
        std::optional<Group> group;
        if (n.parent) {
            if (const auto it = groups.find(n.parent.index); it != groups.end())
                group = it->second;
        }
        if (!group && n.sortingGroup)
            group = Group{h, *n.sortingGroup, n.world};
        if (group)
            groups.emplace(h.index, *group);
        if (n.spriteRenderer) {
            const auto &c = *n.spriteRenderer;
            detail::SpriteSort sort;
            sort.layer = group ? group->component.sortingLayer : c.sortingLayer;
            sort.order = group ? group->component.sortingOrder : c.sortingOrder;
            sort.memberOrder = group ? c.sortingOrder : 0;
            sort.point = group ? group->world.transformPoint(group->component.sortOffset)
                               : n.world.transformPoint(c.sortOffset);
            const auto groupNode = group ? group->node : h;
            sort.groupIndex = groupNode.index;
            sort.groupGeneration = groupNode.generation;
            if (!finite(sort.point))
                throw std::overflow_error("Sprite sort point overflow");
            data->sprites.push_back(
                {h, detail::AssetAccess::sprite(assets, c.sprite), c, n.world, sort});
            auto &record = data->sprites.back();
            const auto layer = c.textureLayer == UINT32_MAX
                                   ? record.sprite->description.textureLayer
                                   : c.textureLayer;
            if (layer >= record.sprite->material->texture->description.layers)
                throw std::invalid_argument("Sprite array layer out of bounds");
            if (impl_->caching || impl_->spatialEnabled) {
                auto &cache = impl_->spatial[h.index];
                const auto &old = cache.spriteComponent;
                if (cache.node != h || cache.spriteWorldVersion != n.worldVersion ||
                    old.sprite != c.sprite || old.billboard != c.billboard ||
                    old.rotation != c.rotation || old.sizeMultiplier.x != c.sizeMultiplier.x ||
                    old.sizeMultiplier.y != c.sizeMultiplier.y || old.flipX != c.flipX ||
                    old.flipY != c.flipY) {
                    cache.node = h;
                    cache.spriteWorldVersion = n.worldVersion;
                    cache.spriteComponent = c;
                    cache.fixedGeometry.reset();
                    if (c.billboard == BillboardMode::Fixed)
                        if (auto geometry = detail::spriteGeometry(record, Mat4::identity()))
                            cache.fixedGeometry =
                                std::make_shared<const detail::SpriteGeometry>(*geometry);
                    record.fixedGeometry = cache.fixedGeometry;
                    cache.spriteBounds = detail::conservativeSpriteBounds(record);
                }
                cache.seen = impl_->extraction;
                record.fixedGeometry = cache.fixedGeometry;
                record.bounds = cache.spriteBounds;
            }
        }
        if (!n.meshRenderer)
            continue;
        const auto &c = *n.meshRenderer;
        // 在提取阶段解析句柄为不可变共享数据，渲染线程不再访问可变仓库。
        auto mesh = detail::AssetAccess::mesh(assets, c.mesh);
        std::vector<std::shared_ptr<const detail::MaterialData>> materials;
        materials.reserve(c.materials.size());
        for (auto material : c.materials)
            materials.push_back(detail::AssetAccess::material(assets, material));
        for (const auto &submesh : mesh->description.submeshes)
            if (submesh.materialSlot >= materials.size())
                throw std::invalid_argument("MeshRenderer is missing a submesh material slot");
        auto &cache = impl_->spatial[h.index];
        cache.seen = impl_->extraction;
        // 完整句柄包含代际，防止删除后复用同一索引时命中旧节点的缓存。
        if (cache.node != h || cache.version != n.worldVersion || cache.meshId != mesh->id) {
            const auto inverse = n.world.inverse();
            cache.node = h;
            cache.version = n.worldVersion;
            cache.meshId = mesh->id;
            cache.invertible = inverse.has_value();
            if (inverse)
                cache.normal = inverse->transposed();
            cache.bounds = mesh->bounds.transformed(n.world);
        }
        // 退化变换没有可逆法线矩阵，跳过该实例，避免向着色器传入无效值。
        if (!cache.invertible)
            continue;
        data->instances.push_back({h, std::move(mesh), std::move(materials), n.world, cache.normal,
                                   cache.bounds, c.layerMask, n.world.determinant3x3() < 0,
                                   c.castShadow, c.receiveShadow});
    }
    std::erase_if(impl_->spatial,
                  [&](const auto &pair) { return pair.second.seen != impl_->extraction; });
    data->indexed = impl_->spatialEnabled;
    if (data->indexed) {
        std::vector<AABB> boxes;
        boxes.reserve(data->sprites.size());
        for (const auto &r : data->sprites)
            boxes.push_back(r.bounds);
        bool sameSprites = previous && previous->indexed &&
                           previous->sprites.size() == data->sprites.size() &&
                           impl_->extraction % 64 != 0;
        if (sameSprites)
            for (size_t i = 0; i < data->sprites.size(); ++i)
                if (previous->sprites[i].node != data->sprites[i].node) {
                    sameSprites = false;
                    break;
                }
        if (sameSprites) {
            data->spriteIndex = previous->spriteIndex;
            data->spriteIndex.refit(boxes);
        } else
            data->spriteIndex.reset(boxes);
        boxes.clear();
        for (const auto &r : data->instances) {
            auto b = r.bounds;
            float padding = 0;
            for (const auto &m : r.materials)
                padding = std::max(padding, m->description.effect.boundsPadding);
            b.minimum = b.minimum - Vec3{padding, padding, padding};
            b.maximum = b.maximum + Vec3{padding, padding, padding};
            if (!finite(b.minimum) || !finite(b.maximum))
                throw std::overflow_error("Effect bounds overflow");
            boxes.push_back(b);
        }
        bool sameMeshes = previous && previous->indexed &&
                          previous->instances.size() == data->instances.size() &&
                          impl_->extraction % 64 != 0;
        if (sameMeshes)
            for (size_t i = 0; i < data->instances.size(); ++i)
                if (previous->instances[i].node != data->instances[i].node) {
                    sameMeshes = false;
                    break;
                }
        if (sameMeshes) {
            data->meshIndex = previous->meshIndex;
            data->meshIndex.refit(boxes);
        } else
            data->meshIndex.reset(boxes);
    }
    impl_->assetsStamp = stamp;
    if (impl_->caching)
        impl_->cachedScene = data;
    return publish(std::move(data));
}

void Image::savePpm(const std::string &path) const {
    if (width == 0 || height == 0 || uint64_t{width} * height * 4 != pixels.size())
        throw std::invalid_argument("Invalid image dimensions");
    std::ofstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("Cannot open image output: " + path);
    stream << "P6\n" << width << ' ' << height << "\n255\n";
    for (size_t i = 0; i < pixels.size(); i += 4)
        stream.write(reinterpret_cast<const char *>(pixels.data() + i), 3);
    if (!stream)
        throw std::runtime_error("Failed to write image: " + path);
}

Renderer::Renderer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {
    impl_->start();
}
Renderer::~Renderer() = default;
Renderer::Renderer(Renderer &&) noexcept = default;
Renderer &Renderer::operator=(Renderer &&) noexcept = default;
RenderStats Renderer::render(const RenderSnapshot &snapshot, const RenderView &view) {
    return submit(snapshot, view).get();
}
std::future<RenderStats> Renderer::submit(RenderSnapshot snapshot, RenderView view) {
    // GLFW 尺寸查询仍由应用线程执行；排队任务捕获本次尺寸和不可变快照。
    const auto [width, height] = impl_->backend->framebufferSize();
    return impl_->enqueue(
        [backend = impl_->backend.get(), snapshot = std::move(snapshot), view, width, height] {
            if (width <= 0 || height <= 0)
                return RenderStats{};
            return backend->draw(detail::prepareFrame(snapshot, view, static_cast<uint32_t>(width),
                                                      static_cast<uint32_t>(height)),
                                 width, height);
        });
}
DeviceInfo Renderer::deviceInfo() const {
    return impl_->enqueue([&] { return impl_->backend->deviceInfo(); }).get();
}
RendererMemoryStats Renderer::memoryStatistics() const {
    return impl_->enqueue([&] { return impl_->backend->memoryStatistics(); }).get();
}
RenderTexture Renderer::createRenderTexture(const OffscreenConfig &target) {
    return impl_->enqueue([&] { return impl_->backend->createRenderTexture(target); }).get();
}
RenderStats Renderer::renderToTexture(const RenderSnapshot &snapshot, const RenderView &view,
                                      const RenderTexture &target) {
    const auto [w, h] = target.size();
    if (!target)
        throw std::invalid_argument("Invalid render texture");
    return impl_
        ->enqueue([backend = impl_->backend.get(), snapshot, view, target, w, h] {
            return backend->drawToTexture(detail::prepareFrame(snapshot, view, w, h), target);
        })
        .get();
}
uint32_t Renderer::debugErrorCount() const {
    return impl_->backend->debugErrorCount();
}
void Renderer::waitIdle() {
    impl_->enqueue([&] { impl_->backend->waitIdle(); }).get();
}
Image Renderer::readback() {
    return impl_->enqueue([&] { return impl_->backend->readback(); }).get();
}
Image Renderer::renderToImage(const RenderSnapshot &snapshot, const RenderView &view) {
    const auto [width, height] = impl_->backend->framebufferSize();
    if (width <= 0 || height <= 0)
        throw std::logic_error("Cannot capture a zero-sized viewport");
    return impl_
        ->enqueue([backend = impl_->backend.get(), snapshot, view, width, height] {
            backend->draw(detail::prepareFrame(snapshot, view, uint32_t(width), uint32_t(height)),
                          width, height);
            return backend->readback();
        })
        .get();
}
void Renderer::resizeOffscreen(uint32_t width, uint32_t height) {
    impl_->enqueue([&] { impl_->backend->resizeOffscreen(width, height); }).get();
}
void Renderer::present() {
    impl_->enqueue([&] { impl_->backend->present(); }).get();
}
} // namespace renderer
