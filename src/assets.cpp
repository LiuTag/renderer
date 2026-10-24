#include "renderer/assets.hpp"
#include "internal/assets.hpp"
#include "internal/pool.hpp"
#include "internal/sprite.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

namespace renderer {
struct AssetStore::Impl {
    detail::Pool<std::shared_ptr<const detail::MeshData>, MeshTag> meshes;
    detail::Pool<std::shared_ptr<const detail::MaterialData>, MaterialTag> materials;
    detail::Pool<std::shared_ptr<const detail::TextureData>, TextureTag> textures;
    detail::Pool<std::shared_ptr<const detail::SpriteData>, SpriteTag> sprites;
    std::shared_ptr<const detail::MeshData> spriteQuad;
    std::map<std::tuple<uint64_t, AlphaMode, float, bool>,
             std::weak_ptr<const detail::MaterialData>>
        spriteMaterials;
};
AssetStore::AssetStore() : impl_(std::make_unique<Impl>()) {}
AssetStore::~AssetStore() = default;
AssetStore::AssetStore(AssetStore &&) noexcept = default;
AssetStore &AssetStore::operator=(AssetStore &&) noexcept = default;

MeshHandle AssetStore::createMesh(MeshDesc d) {
    if (d.vertices.empty() || d.indices.empty() || d.indices.size() % 3 != 0 ||
        d.indices.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max()))
        throw std::invalid_argument("A mesh needs indexed triangles within the draw-count limit");
    std::vector<Vec3> positions;
    positions.reserve(d.vertices.size());
    for (const auto &v : d.vertices) {
        if (!finite(v.normal) || !std::isfinite(v.uv.x) || !std::isfinite(v.uv.y))
            throw std::invalid_argument("Invalid vertex normal or UV");
        positions.push_back(v.position);
    }
    for (uint32_t index : d.indices)
        if (index >= d.vertices.size())
            throw std::invalid_argument("Mesh index out of bounds");
    if (d.submeshes.empty())
        d.submeshes.push_back({0, static_cast<uint32_t>(d.indices.size()), 0});
    for (const auto &s : d.submeshes)
        if (s.indexCount == 0 || s.indexCount % 3 != 0 || s.firstIndex > d.indices.size() ||
            s.indexCount > d.indices.size() - s.firstIndex || s.materialSlot >= 64)
            throw std::invalid_argument(
                "Invalid submesh range or material slot (maximum 64 slots)");
    const AABB bounds = AABB::fromPoints(positions);
    return impl_->meshes.create(std::make_shared<const detail::MeshData>(
        detail::MeshData{detail::uniqueId(), std::move(d), bounds}));
}
TextureHandle AssetStore::createTexture(TextureDesc d) {
    const uint64_t bytes = uint64_t{d.width} * d.height * 4;
    if (d.width == 0 || d.height == 0 || d.width > 16384 || d.height > 16384 ||
        bytes != d.pixels.size())
        throw std::invalid_argument(
            "Texture needs valid dimensions (at most 16384) and width*height*4 bytes");
    return impl_->textures.create(std::make_shared<const detail::TextureData>(
        detail::TextureData{detail::uniqueId(), std::move(d)}));
}
MaterialInstanceHandle AssetStore::createMaterial(const MaterialDesc &d) {
    if (!std::isfinite(d.baseColor.x) || !std::isfinite(d.baseColor.y) ||
        !std::isfinite(d.baseColor.z) || d.baseColor.x < 0 || d.baseColor.y < 0 ||
        d.baseColor.z < 0 || !std::isfinite(d.baseColor.w) || d.baseColor.w < 0 ||
        d.baseColor.w > 1)
        throw std::invalid_argument(
            "Material needs finite non-negative linear RGB and alpha in [0,1]");
    for (float factor : {d.metallic, d.roughness, d.alphaCutoff, d.occlusionStrength})
        if (!std::isfinite(factor) || factor < 0 || factor > 1)
            throw std::invalid_argument("PBR factors must be in [0,1]");
    if (!finite(d.emissive) || d.emissive.x < 0 || d.emissive.y < 0 || d.emissive.z < 0 ||
        !std::isfinite(d.normalScale) || d.normalScale < 0)
        throw std::invalid_argument("Invalid emissive/normal material factors");
    auto texture = d.baseColorTexture ? impl_->textures.get(d.baseColorTexture) : nullptr;
    return impl_->materials.create(
        std::make_shared<const detail::MaterialData>(detail::MaterialData{
            detail::uniqueId(), d.baseColor, std::move(texture), d.unlit, d,
            d.normalTexture ? impl_->textures.get(d.normalTexture) : nullptr,
            d.metallicRoughnessTexture ? impl_->textures.get(d.metallicRoughnessTexture) : nullptr,
            d.occlusionTexture ? impl_->textures.get(d.occlusionTexture) : nullptr,
            d.emissiveTexture ? impl_->textures.get(d.emissiveTexture) : nullptr}));
}
void AssetStore::destroyMesh(MeshHandle h) {
    impl_->meshes.erase(h);
}
void AssetStore::destroyTexture(TextureHandle h) {
    impl_->textures.erase(h);
}
void AssetStore::destroyMaterial(MaterialInstanceHandle h) {
    impl_->materials.erase(h);
}
SpriteHandle AssetStore::createSprite(const SpriteDesc &description) {
    auto d = description;
    const auto texture = impl_->textures.get(d.texture);
    const auto &t = texture->description;
    const auto &r = d.rect;
    if (!r.width || !r.height || r.x > t.width || r.y > t.height || r.width > t.width - r.x ||
        r.height > t.height - r.y)
        throw std::invalid_argument("Sprite rectangle is outside its texture");
    if (d.originalSize.x == 0 && d.originalSize.y == 0)
        d.originalSize = {float(r.width), float(r.height)};
    for (float v : {d.originalSize.x, d.originalSize.y, d.trimOffset.x, d.trimOffset.y, d.pivot.x,
                    d.pivot.y, d.pixelsPerUnit, d.alphaCutoff})
        if (!std::isfinite(v))
            throw std::invalid_argument("Sprite metadata must be finite");
    if (d.originalSize.x <= 0 || d.originalSize.y <= 0 || d.trimOffset.x < 0 ||
        d.trimOffset.y < 0 || d.trimOffset.x + float(r.width) > d.originalSize.x ||
        d.trimOffset.y + float(r.height) > d.originalSize.y || d.pivot.x < 0 || d.pivot.x > 1 ||
        d.pivot.y < 0 || d.pivot.y > 1 || d.pixelsPerUnit <= 0 ||
        !std::isfinite(d.originalSize.x / d.pixelsPerUnit) ||
        !std::isfinite(d.originalSize.y / d.pixelsPerUnit) || d.alphaCutoff < 0 ||
        d.alphaCutoff > 1 ||
        (d.alphaMode != AlphaMode::Opaque && d.alphaMode != AlphaMode::Mask &&
         d.alphaMode != AlphaMode::Blend))
        throw std::invalid_argument("Invalid sprite size, trim, pivot or material");
    if (!impl_->spriteQuad) {
        MeshDesc quad{{{{-0.5f, -0.5f, 0}, {0, 0, 1}, {0, 0}},
                       {{0.5f, -0.5f, 0}, {0, 0, 1}, {1, 0}},
                       {{0.5f, 0.5f, 0}, {0, 0, 1}, {1, 1}},
                       {{-0.5f, 0.5f, 0}, {0, 0, 1}, {0, 1}}},
                      {0, 1, 2, 0, 2, 3},
                      {{0, 6, 0}}};
        impl_->spriteQuad = std::make_shared<const detail::MeshData>(detail::MeshData{
            detail::uniqueId(), std::move(quad), {{-0.5f, -0.5f, 0}, {0.5f, 0.5f, 0}}});
    }
    const auto key = std::tuple{texture->id, d.alphaMode, d.alphaCutoff, d.unlit};
    auto material = impl_->spriteMaterials[key].lock();
    if (!material) {
        MaterialDesc md;
        md.baseColorTexture = d.texture;
        md.unlit = d.unlit;
        md.alphaMode = d.alphaMode;
        md.alphaCutoff = d.alphaCutoff;
        md.doubleSided = true;
        material = std::make_shared<const detail::MaterialData>(detail::MaterialData{
            detail::uniqueId(), md.baseColor, texture, md.unlit, md, {}, {}, {}, {}});
        impl_->spriteMaterials[key] = material;
        std::erase_if(impl_->spriteMaterials, [](const auto &p) { return p.second.expired(); });
    }
    return impl_->sprites.create(std::make_shared<const detail::SpriteData>(
        detail::SpriteData{d, impl_->spriteQuad, std::move(material)}));
}
SpriteDesc AssetStore::spriteDescription(SpriteHandle sprite) const {
    return impl_->sprites.get(sprite)->description;
}
void AssetStore::destroySprite(SpriteHandle sprite) {
    impl_->sprites.erase(sprite);
}
std::shared_ptr<const detail::SpriteData> detail::AssetAccess::sprite(const AssetStore &store,
                                                                      SpriteHandle sprite) {
    return store.impl_->sprites.get(sprite);
}
std::shared_ptr<const detail::MeshData> detail::AssetAccess::mesh(const AssetStore &store,
                                                                  MeshHandle h) {
    return store.impl_->meshes.get(h);
}
std::shared_ptr<const detail::MaterialData>
detail::AssetAccess::material(const AssetStore &store, MaterialInstanceHandle h) {
    return store.impl_->materials.get(h);
}

namespace geometry {
MeshDesc sphere(uint32_t segments, uint32_t rings) {
    if (segments < 3 || rings < 2 || segments > 256 || rings > 256)
        throw std::invalid_argument("Invalid sphere tessellation");
    MeshDesc mesh;
    for (uint32_t y = 0; y <= rings; ++y)
        for (uint32_t x = 0; x <= segments; ++x) {
            const float theta = radians(180) * static_cast<float>(y) / static_cast<float>(rings),
                        phi = radians(360) * static_cast<float>(x) / static_cast<float>(segments);
            const Vec3 n{std::sin(theta) * std::cos(phi), std::cos(theta),
                         std::sin(theta) * std::sin(phi)};
            mesh.vertices.push_back({n * 0.5f,
                                     n,
                                     {static_cast<float>(x) / static_cast<float>(segments),
                                      1 - static_cast<float>(y) / static_cast<float>(rings)}});
        }
    for (uint32_t y = 0; y < rings; ++y)
        for (uint32_t x = 0; x < segments; ++x) {
            const auto a = y * (segments + 1) + x, b = a + 1, c = a + segments + 1, d = c + 1;
            if (y > 0)
                mesh.indices.insert(mesh.indices.end(), {a, b, c});
            if (y + 1 < rings)
                mesh.indices.insert(mesh.indices.end(), {b, d, c});
        }
    return mesh;
}
MeshDesc cube() {
    MeshDesc d;
    struct Face {
        Vec3 normal, u, v;
    };
    const Face faces[] = {{{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
                          {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
                          {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}}};
    for (const auto &f : faces) {
        const auto base = static_cast<uint32_t>(d.vertices.size());
        const Vec3 center = f.normal * 0.5f;
        d.vertices.push_back({center - f.u * 0.5f - f.v * 0.5f, f.normal, {0, 0}});
        d.vertices.push_back({center + f.u * 0.5f - f.v * 0.5f, f.normal, {1, 0}});
        d.vertices.push_back({center + f.u * 0.5f + f.v * 0.5f, f.normal, {1, 1}});
        d.vertices.push_back({center - f.u * 0.5f + f.v * 0.5f, f.normal, {0, 1}});
        for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
            d.indices.push_back(base + i);
    }
    return d;
}
MeshDesc plane(float size) {
    if (!std::isfinite(size) || size <= 0)
        throw std::invalid_argument("Invalid plane size");
    const float h = size * 0.5f;
    return {{{{-h, 0, h}, {0, 1, 0}, {0, 0}},
             {{h, 0, h}, {0, 1, 0}, {1, 0}},
             {{h, 0, -h}, {0, 1, 0}, {1, 1}},
             {{-h, 0, -h}, {0, 1, 0}, {0, 1}}},
            {0, 1, 2, 0, 2, 3},
            {}};
}
TextureDesc checkerboard(uint32_t size, uint32_t cells) {
    if (size == 0 || size > 4096 || cells == 0 || cells > size)
        throw std::invalid_argument("Invalid checkerboard dimensions");
    TextureDesc t{size, size, std::vector<uint8_t>(size_t{size} * size * 4), true};
    for (uint32_t y = 0; y < size; ++y)
        for (uint32_t x = 0; x < size; ++x) {
            const uint8_t c = (((x * cells / size) + (y * cells / size)) % 2 == 0) ? 220 : 65;
            const size_t i = (size_t{y} * size + x) * 4;
            t.pixels[i] = c;
            t.pixels[i + 1] = c;
            t.pixels[i + 2] = c;
            t.pixels[i + 3] = 255;
        }
    return t;
}
} // namespace geometry
} // namespace renderer
