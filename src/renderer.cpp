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
    return impl_ ? impl_->instances.size() + impl_->sprites.size() + impl_->batchSpriteCount : 0;
}
size_t RenderSnapshot::spriteCount() const {
    return impl_ ? impl_->sprites.size() + impl_->batchSpriteCount : 0;
}
size_t RenderSnapshot::cameraCount() const {
    return impl_ ? impl_->cameras.size() : 0;
}
uint64_t RenderSnapshot::revision() const {
    return impl_ ? impl_->revision : 0;
}
std::optional<Mat4> RenderSnapshot::instanceTransform(NodeHandle h) const {
    if (impl_) {
        for (const auto &i : impl_->instances)
            if (i.node == h)
                return i.world;
        for (const auto &s : impl_->sprites)
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
    };
    uint64_t owner = 0, extraction = 0;
    std::unordered_map<uint32_t, SpatialCache> spatial;
};
RenderExtractor::RenderExtractor() : impl_(std::make_unique<Impl>()) {}
RenderExtractor::~RenderExtractor() = default;
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
        impl_->owner = scene.root().owner;
    }
    ++impl_->extraction;
    auto data = std::make_shared<RenderSnapshot::Impl>();
    data->revision = scene.revision();
    for (const auto &batch : batches)
        if (auto dataBatch = detail::SpriteBatchAccess::data(batch)) {
            data->batchSpriteCount += dataBatch->sprites.size();
            data->batches.push_back(std::move(dataBatch));
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
    RenderSnapshot snapshot;
    snapshot.impl_ = std::move(data);
    return snapshot;
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
