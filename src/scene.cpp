#include "renderer/scene.hpp"
#include "internal/scene.hpp"
#include "internal/sprite.hpp"
#include <cmath>
#include <stdexcept>

namespace renderer {
Scene::Scene() : impl_(std::make_unique<Impl>()) {
    impl_->root = impl_->nodes.create();
    impl_->nodes.get(impl_->root).name = "Root";
}
Scene::~Scene() = default;
Scene::Scene(Scene &&) noexcept = default;
Scene &Scene::operator=(Scene &&) noexcept = default;
NodeHandle Scene::root() const {
    return impl_->root;
}
bool Scene::valid(NodeHandle h) const {
    return impl_->nodes.valid(h);
}
std::string Scene::name(NodeHandle h) const {
    return impl_->nodes.get(h).name;
}
NodeHandle Scene::parent(NodeHandle h) const {
    return impl_->nodes.get(h).parent;
}
uint64_t Scene::revision() const {
    return impl_->revision;
}
NodeHandle Scene::createNode(NodeHandle parent, std::string_view name) {
    impl_->nodes.get(parent);
    Impl::Node n;
    n.name = name;
    const auto h = impl_->nodes.create(std::move(n));
    impl_->attach(h, parent);
    impl_->topologyDirty = true;
    ++impl_->revision;
    return h;
}
void Scene::Impl::attach(NodeHandle h, NodeHandle parent) {
    auto &n = nodes.get(h);
    auto &p = nodes.get(parent);
    n.parent = parent;
    n.nextSibling = p.firstChild;
    n.previousSibling = {};
    if (p.firstChild)
        nodes.get(p.firstChild).previousSibling = h;
    p.firstChild = h;
    n.parentWorldVersion = 0;
}
void Scene::Impl::detach(NodeHandle h) {
    auto &n = nodes.get(h);
    if (n.previousSibling)
        nodes.get(n.previousSibling).nextSibling = n.nextSibling;
    else if (n.parent)
        nodes.get(n.parent).firstChild = n.nextSibling;
    if (n.nextSibling)
        nodes.get(n.nextSibling).previousSibling = n.previousSibling;
    n.parent = {};
    n.nextSibling = {};
    n.previousSibling = {};
}
void Scene::destroySubtree(NodeHandle h) {
    impl_->nodes.get(h);
    if (h == impl_->root)
        throw std::invalid_argument("Cannot destroy the scene root");
    // 先完整收集后代再删除，避免释放节点后继续追踪兄弟链；深树不消耗递归栈。
    std::vector<NodeHandle> pending{h}, destroyed;
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        destroyed.push_back(current);
        for (auto child = impl_->nodes.get(current).firstChild; child;
             child = impl_->nodes.get(child).nextSibling)
            pending.push_back(child);
    }
    impl_->detach(h);
    for (auto current : destroyed)
        impl_->nodes.erase(current);
    impl_->topologyDirty = true;
    ++impl_->revision;
}
void Scene::reparent(NodeHandle h, NodeHandle p, ReparentMode mode) {
    impl_->nodes.get(h);
    impl_->nodes.get(p);
    if (h == impl_->root)
        throw std::invalid_argument("Cannot reparent the scene root");
    for (auto ancestor = p; ancestor; ancestor = impl_->nodes.get(ancestor).parent)
        if (ancestor == h)
            throw std::invalid_argument("Reparent would create a cycle");
    if (impl_->nodes.get(h).parent == p)
        return;
    Mat4 local = impl_->nodes.get(h).local;
    if (mode == ReparentMode::KeepWorld) {
        impl_->updateDerived();
        const auto inverse = impl_->nodes.get(p).world.inverse();
        if (!inverse)
            throw std::invalid_argument("KeepWorld requires an invertible parent transform");
        // world = parentWorld * local，因此换父节点后 local = inverse(parentWorld) * world。
        // 在改动拓扑前完成可逆性校验，失败时原树保持有效。
        local = (*inverse) * impl_->nodes.get(h).world;
        if (!local.isFinite())
            throw std::overflow_error("Reparent transform overflow");
    }
    impl_->detach(h);
    impl_->attach(h, p);
    auto &n = impl_->nodes.get(h);
    n.local = local;
    ++n.localVersion;
    impl_->topologyDirty = true;
    ++impl_->revision;
}
void Scene::setLocalTransform(NodeHandle h, const Transform &t) {
    if (!t.isAffine())
        throw std::invalid_argument("Scene transforms must be finite affine matrices");
    auto &n = impl_->nodes.get(h);
    n.local = t;
    ++n.localVersion;
    ++impl_->revision;
}
Transform Scene::localTransform(NodeHandle h) const {
    return impl_->nodes.get(h).local;
}
Mat4 Scene::worldTransform(NodeHandle h) const {
    impl_->updateDerived();
    return impl_->nodes.get(h).world;
}
void Scene::setVisible(NodeHandle h, bool visible) {
    impl_->nodes.get(h).visible = visible;
    ++impl_->revision;
}
bool Scene::effectiveVisible(NodeHandle h) const {
    impl_->updateDerived();
    return impl_->nodes.get(h).effectiveVisible;
}
void Scene::setMeshRenderer(NodeHandle h, const MeshRenderer &c) {
    if (!c.mesh || c.materials.empty())
        throw std::invalid_argument("MeshRenderer requires a mesh and material slots");
    impl_->nodes.get(h).meshRenderer = c;
    ++impl_->revision;
}
void Scene::setSpriteRenderer(NodeHandle h, const SpriteRenderer &c) {
    detail::validateSprite(c);
    impl_->nodes.get(h).spriteRenderer = c;
    ++impl_->revision;
}
void Scene::setSortingGroup(NodeHandle h, const SortingGroup &c) {
    if (!finite(c.sortOffset))
        throw std::invalid_argument("SortingGroup offset must be finite");
    impl_->nodes.get(h).sortingGroup = c;
    ++impl_->revision;
}
std::optional<SpriteRenderer> Scene::spriteRenderer(NodeHandle h) const {
    return impl_->nodes.get(h).spriteRenderer;
}
std::optional<SortingGroup> Scene::sortingGroup(NodeHandle h) const {
    return impl_->nodes.get(h).sortingGroup;
}
void Scene::removeSpriteRenderer(NodeHandle h) {
    impl_->nodes.get(h).spriteRenderer.reset();
    ++impl_->revision;
}
void Scene::removeSortingGroup(NodeHandle h) {
    impl_->nodes.get(h).sortingGroup.reset();
    ++impl_->revision;
}
void Scene::setCamera(NodeHandle h, const Camera &c) {
    if (!std::isfinite(c.aspectRatio) || c.aspectRatio < 0)
        throw std::invalid_argument("Invalid camera aspect ratio");
    if (c.projection == Camera::Projection::Perspective)
        Mat4::perspective(c.verticalFov, c.aspectRatio > 0 ? c.aspectRatio : 1, c.nearPlane,
                          c.farPlane);
    else {
        const float half = c.orthographicHeight * 0.5f;
        Mat4::orthographic(-half, half, -half, half, c.nearPlane, c.farPlane);
    }
    impl_->nodes.get(h).camera = c;
    ++impl_->revision;
}
std::optional<MeshRenderer> Scene::meshRenderer(NodeHandle h) const {
    return impl_->nodes.get(h).meshRenderer;
}
std::optional<Camera> Scene::camera(NodeHandle h) const {
    return impl_->nodes.get(h).camera;
}
void Scene::removeMeshRenderer(NodeHandle h) {
    impl_->nodes.get(h).meshRenderer.reset();
    ++impl_->revision;
}
void Scene::removeCamera(NodeHandle h) {
    impl_->nodes.get(h).camera.reset();
    ++impl_->revision;
}
void Scene::Impl::updateDerived() {
    if (derivedRevision == revision)
        return;
    // 只有父子关系变化才重建遍历序列；普通组件/变换修改复用现有顺序。
    if (topologyDirty) {
        traversal.clear();
        std::vector<NodeHandle> pending{root};
        while (!pending.empty()) {
            const auto h = pending.back();
            pending.pop_back();
            traversal.push_back(h);
            for (auto child = nodes.get(h).firstChild; child; child = nodes.get(child).nextSibling)
                pending.push_back(child);
        }
        topologyDirty = false;
    }
    for (auto h : traversal) {
        auto &n = nodes.get(h);
        const Node *p = n.parent ? &nodes.get(n.parent) : nullptr;
        const uint64_t parentVersion = p ? p->worldVersion : 0;
        // 父节点已先更新，比较版本即可传播变换变化，无需递归标记整棵子树。
        if (n.appliedLocalVersion != n.localVersion || n.parentWorldVersion != parentVersion) {
            n.world = p ? p->world * n.local : n.local;
            if (!n.world.isFinite())
                throw std::overflow_error("World transform overflow");
            n.appliedLocalVersion = n.localVersion;
            n.parentWorldVersion = parentVersion;
            n.worldVersion = ++worldVersion;
        }
        n.effectiveVisible = n.visible && (!p || p->effectiveVisible);
    }
    derivedRevision = revision;
}
void Scene::setLight(NodeHandle h, const Light &light) {
    if (!finite(light.color) || light.color.x < 0 || light.color.y < 0 || light.color.z < 0 ||
        !std::isfinite(light.intensity) || light.intensity < 0 || !std::isfinite(light.range) ||
        light.range <= 0 || !std::isfinite(light.innerCone) || !std::isfinite(light.outerCone) ||
        light.innerCone < 0 || light.outerCone <= 0 || light.innerCone >= light.outerCone ||
        light.outerCone >= radians(90))
        throw std::invalid_argument("Invalid light parameters");
    impl_->nodes.get(h).light = light;
    ++impl_->revision;
}
std::optional<Light> Scene::light(NodeHandle h) const {
    return impl_->nodes.get(h).light;
}
void Scene::removeLight(NodeHandle h) {
    impl_->nodes.get(h).light.reset();
    ++impl_->revision;
}
} // namespace renderer
