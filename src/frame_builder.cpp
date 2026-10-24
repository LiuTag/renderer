#include "internal/frame.hpp"
#include "internal/frustum.hpp"
#include "internal/snapshot.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace renderer::detail {
PreparedFrame prepareFrame(const RenderSnapshot &snapshot, const RenderView &view, uint32_t width,
                           uint32_t height) {
    const auto start = std::chrono::steady_clock::now();
    if (view.transparency != TransparencyMode::SortedAlpha &&
        view.transparency != TransparencyMode::WeightedBlendedOIT)
        throw std::invalid_argument("Invalid transparency mode");
    if (view.antiAliasing != AntiAliasing::None && view.antiAliasing != AntiAliasing::FXAA)
        throw std::invalid_argument("Invalid anti-aliasing mode");
    if (!width || !height || !std::isfinite(view.ambientLight) || view.ambientLight < 0 ||
        view.ambientLight > 1 || !std::isfinite(view.exposure) || view.exposure <= 0 ||
        view.shadowResolution < 64 || view.shadowResolution > 8192)
        throw std::invalid_argument("Invalid render view parameters");
    for (float c : {view.clearColor.x, view.clearColor.y, view.clearColor.z, view.clearColor.w})
        if (!std::isfinite(c) || c < 0 || c > 1)
            throw std::invalid_argument("Clear color must be in [0,1]");
    const auto &data = SnapshotAccess::get(snapshot);
    const auto camera = std::find_if(data.cameras.begin(), data.cameras.end(),
                                     [&](const auto &c) { return c.node == view.cameraNode; });
    if (camera == data.cameras.end())
        throw std::invalid_argument("Camera is absent or hidden in snapshot");
    // 相机节点保存相机到世界的变换，渲染需要它的逆矩阵作为视图矩阵。
    const auto inverse = camera->world.inverse();
    if (!inverse)
        throw std::invalid_argument("Camera transform is singular");
    PreparedFrame frame;
    frame.transparency = view.transparency;
    frame.antiAliasing = view.antiAliasing;
    const auto &cameraData = camera->camera;
    const auto projection = cameraProjection(cameraData, width, height);
    frame.uniforms.viewProjection = projection * (*inverse);
    if (!finite(view.colorMultiply) || !finite(view.colorAdd) || !std::isfinite(view.saturation) ||
        view.saturation < 0 || view.saturation > 4 || !std::isfinite(view.contactShadowStrength) ||
        view.contactShadowStrength < 0 || view.contactShadowStrength > 1 ||
        !std::isfinite(view.contactShadowDistance) || view.contactShadowDistance <= 0 ||
        !std::isfinite(view.contactShadowThickness) || view.contactShadowThickness <= 0 ||
        view.contactShadowSteps < 1 || view.contactShadowSteps > 32)
        throw std::invalid_argument("Invalid color grading or contact shadow parameters");
    const auto inverseViewProjection = frame.uniforms.viewProjection.inverse();
    if (!inverseViewProjection)
        throw std::invalid_argument("View-projection matrix is singular");
    frame.uniforms.inverseViewProjection = *inverseViewProjection;
    frame.uniforms.colorMultiplySaturation = {view.colorMultiply.x, view.colorMultiply.y,
                                              view.colorMultiply.z, view.saturation};
    frame.uniforms.colorAdd = {view.colorAdd.x, view.colorAdd.y, view.colorAdd.z, 0};
    frame.uniforms.contactParameters = {view.contactShadows ? view.contactShadowStrength : 0,
                                        view.contactShadowDistance, view.contactShadowThickness,
                                        float(view.contactShadowSteps)};
    const Vec3 eye = camera->world.transformPoint({0, 0, 0});
    frame.uniforms.cameraPosition = {eye.x, eye.y, eye.z, 1};
    Vec3 direction = normalized(view.lightDirection);
    int shadowLight = -1;
    for (const auto &source : data.lights) {
        if (frame.lights.size() >= 64)
            throw std::length_error("This lighting profile supports 64 lights per view");
        const auto p = source.world.transformPoint({0, 0, 0});
        const auto d = normalized(source.world.transformPoint({0, 0, -1}) - p);
        const auto &light = source.light;
        // 当前阴影 Profile 只选择首个开启投影的方向光，其余灯光仍参与 PBR 光照。
        if (shadowLight < 0 && light.type == LightType::Directional && light.castShadow) {
            shadowLight = static_cast<int>(frame.lights.size());
            direction = d;
        }
        frame.lights.push_back({{p.x, p.y, p.z, static_cast<float>(light.type)},
                                {d.x, d.y, d.z, light.range},
                                {light.color.x, light.color.y, light.color.z, light.intensity},
                                {std::cos(light.innerCone), std::cos(light.outerCone), 0, 0}});
    }
    if (frame.lights.empty()) {
        frame.lights.push_back(
            {{0, 0, 0, 0}, {direction.x, direction.y, direction.z, 0}, {1, 1, 1, 3}, {0, 0, 0, 0}});
        shadowLight = 0;
    }
    frame.shadows = view.shadows && shadowLight >= 0;
    frame.shadowResolution = view.shadowResolution;
    frame.uniforms.lightDirection = {direction.x, direction.y, direction.z, 0};
    frame.uniforms.ambient = {view.ambientLight, 0, 0, 0};
    frame.uniforms.postParameters = {view.exposure, static_cast<float>(view.toneMapping), 0, 0};
    frame.uniforms.counts = {static_cast<float>(frame.lights.size()), 0, 0, 0};
    frame.uniforms.shadowParameters = {frame.shadows ? 1.0f : 0.0f, static_cast<float>(shadowLight),
                                       1.0f / static_cast<float>(view.shadowResolution), 0.0008f};
    frame.uniforms.lightViewProjection = Mat4::identity();
    frame.clearColor = view.clearColor;
    if (snapshot.instanceCount() > std::numeric_limits<uint32_t>::max())
        throw std::length_error("Too many instances");
    frame.stats.sceneInstances = static_cast<uint32_t>(snapshot.instanceCount());
    std::vector<InstanceRecord> spriteInstances;
    spriteInstances.reserve(data.sprites.size() + data.batchSpriteCount);
    const Frustum frustum(frame.uniforms.viewProjection);
    const auto appendSprite = [&](const SpriteRecord &s) {
        const auto geometry = spriteGeometry(s, camera->world);
        if (!geometry) {
            ++frame.stats.culledInstances;
            return;
        }
        const auto &c = s.component;
        InstanceRecord instance{s.node,
                                s.sprite->mesh,
                                {s.sprite->material},
                                geometry->world,
                                geometry->normal,
                                geometry->bounds,
                                c.layerMask,
                                geometry->world.determinant3x3() < 0,
                                c.castShadow,
                                c.receiveShadow};
        instance.uvRect = geometry->uvRect;
        instance.tint = c.tint;
        instance.colorOverlay = c.colorOverlay;
        instance.outlineColor = c.outlineColor;
        instance.effects = {c.outlineWidth, 0, 0, 0};
        instance.spriteSort = s.sort;
        instance.orderedSprite = c.transparency == SpriteTransparency::Sorted;
        spriteInstances.push_back(std::move(instance));
    };
    for (const auto &s : data.sprites)
        appendSprite(s);
    for (const auto &batch : data.batches) {
        if (view.frustumCulling && !frame.shadows && !frustum.intersects(batch->bounds)) {
            frame.stats.culledInstances += uint32_t(batch->sprites.size());
            continue;
        }
        for (const auto &s : batch->sprites)
            appendSprite(s);
    }
    std::vector<const InstanceRecord *> instances;
    instances.reserve(data.instances.size() + spriteInstances.size());
    for (const auto &i : data.instances)
        instances.push_back(&i);
    for (const auto &i : spriteInstances)
        instances.push_back(&i);

    struct Item {
        const InstanceRecord *instance;
        uint32_t submesh;
        float depth;
        SpriteSort sort;
    };
    std::vector<Item> opaque, transparent, orderedSprites, casters;
    bool hasCaster = false;
    AABB casterBounds{};
    for (const auto *source : instances) {
        const auto &instance = *source;
        const bool visible = (instance.layerMask & camera->camera.cullingMask) != 0 &&
                             (!view.frustumCulling || frustum.intersects(instance.bounds));
        if (visible)
            ++frame.stats.visibleInstances;
        else
            ++frame.stats.culledInstances;
        const Vec3 center = (instance.bounds.minimum + instance.bounds.maximum) * 0.5f;
        const auto sort = instance.spriteSort.value_or(
            SpriteSort{0, 0, 0, center, instance.node.index, instance.node.generation});
        const float depth = -inverse->transformPoint(sort.point).z;
        for (uint32_t submesh = 0; submesh < instance.mesh->description.submeshes.size();
             ++submesh) {
            const auto &material =
                *instance.materials[instance.mesh->description.submeshes[submesh].materialSlot];
            const Item item{&instance, submesh, depth, sort};
            if (visible) {
                if (material.description.alphaMode != AlphaMode::Blend)
                    opaque.push_back(item);
                else if (instance.orderedSprite &&
                         view.transparency == TransparencyMode::WeightedBlendedOIT)
                    orderedSprites.push_back(item);
                else
                    transparent.push_back(item);
            }
            // 视锥外物体也可能把阴影投到视锥内；投影者只受可见性、层和材质约束。
            if (frame.shadows && (instance.layerMask & camera->camera.cullingMask) != 0 &&
                instance.castShadow && material.description.alphaMode != AlphaMode::Blend) {
                casters.push_back(item);
                if (!hasCaster) {
                    casterBounds = instance.bounds;
                    hasCaster = true;
                } else {
                    casterBounds.minimum = {
                        std::min(casterBounds.minimum.x, instance.bounds.minimum.x),
                        std::min(casterBounds.minimum.y, instance.bounds.minimum.y),
                        std::min(casterBounds.minimum.z, instance.bounds.minimum.z)};
                    casterBounds.maximum = {
                        std::max(casterBounds.maximum.x, instance.bounds.maximum.x),
                        std::max(casterBounds.maximum.y, instance.bounds.maximum.y),
                        std::max(casterBounds.maximum.z, instance.bounds.maximum.z)};
                }
            }
        }
    }
    if (frame.shadows && hasCaster) {
        const Vec3 center = (casterBounds.minimum + casterBounds.maximum) * 0.5f,
                   extent = (casterBounds.maximum - casterBounds.minimum) * 0.5f;
        const float radius = std::max(1.0f, std::sqrt(dot(extent, extent)));
        const auto lightView =
            Mat4::lookAt(center - direction * (radius + 2), center,
                         std::abs(direction.y) > 0.95f ? Vec3{0, 0, 1} : Vec3{0, 1, 0});
        const auto bounds = casterBounds.transformed(lightView);
        const float padding = 0.5f;
        // 将光空间投影中心对齐到阴影纹素，减少小幅移动造成的采样抖动。
        const float texel = 2 * (radius + padding) / static_cast<float>(view.shadowResolution);
        const float x = std::floor((bounds.minimum.x + bounds.maximum.x) * 0.5f / texel) * texel,
                    y = std::floor((bounds.minimum.y + bounds.maximum.y) * 0.5f / texel) * texel;
        frame.uniforms.lightViewProjection =
            Mat4::orthographic(x - radius - padding, x + radius + padding, y - radius - padding,
                               y + radius + padding, std::max(0.01f, -bounds.maximum.z - padding),
                               -bounds.minimum.z + padding) *
            lightView;
    } else
        frame.uniforms.shadowParameters.x = 0;
    const auto key = [](const Item &item) {
        const auto &i = *item.instance;
        return std::tuple{i.materials[i.mesh->description.submeshes[item.submesh].materialSlot]->id,
                          i.mesh->id, item.submesh, i.mirrored};
    };
    // 排序 Alpha 保持从远到近；加权 OIT 的累积可交换，允许直接按状态聚类合批。
    std::sort(opaque.begin(), opaque.end(), [&](auto &a, auto &b) { return key(a) < key(b); });
    if (view.transparency == TransparencyMode::WeightedBlendedOIT)
        std::sort(transparent.begin(), transparent.end(),
                  [&](auto &a, auto &b) { return key(a) < key(b); });
    else
        std::stable_sort(transparent.begin(), transparent.end(), [](auto &a, auto &b) {
            return spriteSortLess(a.sort, a.depth, b.sort, b.depth);
        });
    std::stable_sort(orderedSprites.begin(), orderedSprites.end(), [](auto &a, auto &b) {
        return spriteSortLess(a.sort, a.depth, b.sort, b.depth);
    });
    std::sort(casters.begin(), casters.end(), [&](auto &a, auto &b) { return key(a) < key(b); });
    const auto append = [&](const std::vector<Item> &items, std::vector<DrawBatch> &batches,
                            bool shadow) {
        for (size_t i = 0; i < items.size(); ++i) {
            const auto &item = items[i];
            const auto &instance = *item.instance;
            const auto &submesh = instance.mesh->description.submeshes[item.submesh];
            if (frame.instances.size() >= static_cast<size_t>(std::numeric_limits<int32_t>::max()))
                throw std::length_error("Too many GPU instance entries");
            // 保持输入队列顺序合并；排序 Alpha 保留深度顺序，OIT 已按状态聚类。
            if (i == 0 || key(item) != key(items[i - 1]))
                batches.push_back({instance.mesh, instance.materials[submesh.materialSlot],
                                   item.submesh, instance.mirrored,
                                   static_cast<uint32_t>(frame.instances.size()), 0});
            ++batches.back().instanceCount;
            frame.instances.push_back({instance.world,
                                       instance.normal,
                                       {instance.receiveShadow ? 1.0f : 0.0f, 0, 0, 0},
                                       instance.uvRect,
                                       instance.tint,
                                       instance.colorOverlay,
                                       instance.outlineColor,
                                       instance.effects});
            if (!shadow)
                frame.stats.triangles += submesh.indexCount / 3;
        }
    };
    append(opaque, frame.batches, false);
    const auto opaqueBatches = frame.batches.size();
    append(transparent, frame.batches, false);
    append(orderedSprites, frame.orderedSpriteBatches, false);
    frame.stats.transparentDrawCalls = static_cast<uint32_t>(frame.batches.size() - opaqueBatches +
                                                             frame.orderedSpriteBatches.size());
    append(casters, frame.shadowBatches, true);
    frame.stats.sceneDrawCalls = frame.stats.drawCalls =
        static_cast<uint32_t>(frame.batches.size() + frame.orderedSpriteBatches.size());
    frame.stats.shadowDrawCalls = static_cast<uint32_t>(frame.shadowBatches.size());
    frame.stats.prepareMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return frame;
}
} // namespace renderer::detail
