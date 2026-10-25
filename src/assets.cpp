#include "renderer/assets.hpp"
#include "internal/assets.hpp"
#include "internal/pool.hpp"
#include "internal/sprite.hpp"
#include "renderer/texture.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_map>

namespace renderer {
struct AssetStore::Impl {
    uint64_t revision = 1;
    uint64_t package = detail::uniqueId();
    std::vector<std::unique_ptr<Impl>> imported;
    std::unordered_map<uint64_t, Impl *> owners;
    std::vector<std::weak_ptr<const detail::MeshData>> allMeshes;
    std::vector<std::weak_ptr<const detail::TextureData>> allTextures;

    detail::Pool<std::shared_ptr<const detail::MeshData>, MeshTag> meshes;
    detail::Pool<std::shared_ptr<const detail::MaterialData>, MaterialTag> materials;
    detail::Pool<std::shared_ptr<const detail::TextureData>, TextureTag> textures;
    detail::Pool<std::shared_ptr<const detail::SpriteData>, SpriteTag> sprites;
    std::shared_ptr<const detail::MeshData> spriteQuad;
    std::map<std::tuple<uint64_t, AlphaMode, float, bool, MaterialBlend>,
             std::weak_ptr<const detail::MaterialData>>
        spriteMaterials;
    Impl() {
        for (auto id : {meshes.owner(), materials.owner(), textures.owner(), sprites.owner()})
            owners.emplace(id, this);
    }
    Impl &owner(uint64_t id) const {
        auto it = owners.find(id);
        if (it == owners.end())
            throw std::invalid_argument("Resource belongs to another store");
        return *it->second;
    }
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
    auto data = std::make_shared<const detail::MeshData>(
        detail::MeshData{detail::uniqueId(), std::move(d), bounds});
    if (impl_->allMeshes.size() % 1024 == 0)
        std::erase_if(impl_->allMeshes, [](const auto &p) { return p.expired(); });
    impl_->allMeshes.push_back(data);
    return impl_->meshes.create(std::move(data));
}
TextureHandle AssetStore::createTexture(TextureDesc d) {
    texture::validate(d);
    auto data = std::make_shared<const detail::TextureData>(
        detail::TextureData{detail::uniqueId(), std::move(d)});
    if (impl_->allTextures.size() % 1024 == 0)
        std::erase_if(impl_->allTextures, [](const auto &p) { return p.expired(); });
    impl_->allTextures.push_back(data);
    return impl_->textures.create(std::move(data));
}
TextureHandle AssetStore::updateTexture(TextureHandle source, TextureRegion r,
                                        std::span<const uint8_t> pixels) {
    const auto old = detail::AssetAccess::texture(*this, source);
    const auto &d = old->description;
    if (d.renderTexture.size().first || d.layers != 1 || d.mipLevels != 1 || d.sampling.mipmapped ||
        d.compression != TextureCompression::None || !r.width || !r.height || r.x >= d.width ||
        r.y >= d.height || r.width > d.width - r.x || r.height > d.height - r.y)
        throw std::invalid_argument("Texture update requires a single uncompressed non-mipmapped "
                                    "CPU image and valid region");
    const size_t channels = d.channels == TextureChannels::Alpha8 ? 1 : 4;
    if (pixels.size() != size_t(r.width) * r.height * channels)
        throw std::invalid_argument("Texture patch length mismatch");
    auto next = d;
    auto &payload = next.data.empty() ? next.pixels : next.data;
    for (uint32_t y = 0; y < r.height; ++y)
        std::copy_n(pixels.begin() + size_t(y) * r.width * channels, size_t(r.width) * channels,
                    payload.begin() + (size_t(r.y + y) * d.width + r.x) * channels);
    auto resource = std::make_shared<const detail::TextureData>(
        detail::TextureData{detail::uniqueId(), std::move(next), old->id, r});
    if (impl_->allTextures.size() % 1024 == 0)
        std::erase_if(impl_->allTextures, [](const auto &p) { return p.expired(); });
    impl_->allTextures.push_back(resource);
    return impl_->textures.create(std::move(resource));
}
TextureHandle AssetStore::createTexture(const RenderTexture &t) {
    if (!t)
        throw std::invalid_argument("Invalid render texture");
    TextureDesc d;
    auto [w, h] = t.size();
    d.width = w;
    d.height = h;
    d.renderTexture = t;
    d.sampling.mipmapped = false;
    d.sampling.addressU = d.sampling.addressV = TextureAddress::Clamp;
    return createTexture(std::move(d));
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
    if ((d.blend != MaterialBlend::Alpha && d.blend != MaterialBlend::PremultipliedAlpha &&
         d.blend != MaterialBlend::Additive) ||
        d.effect.vertex.size() > 65536 || d.effect.surface.size() > 65536 ||
        !std::isfinite(d.effect.boundsPadding) || d.effect.boundsPadding < 0)
        throw std::invalid_argument("Invalid material effect or blend mode");
    for (float p : {d.effect.parameters.x, d.effect.parameters.y, d.effect.parameters.z,
                    d.effect.parameters.w})
        if (!std::isfinite(p))
            throw std::invalid_argument("Invalid effect parameters");
    auto texture = d.baseColorTexture
                       ? impl_->owner(d.baseColorTexture.owner).textures.get(d.baseColorTexture)
                       : nullptr;
    if (d.baseTextureLayer >= (texture ? texture->description.layers : 1))
        throw std::invalid_argument("Material texture layer out of bounds");
    for (auto h :
         {d.normalTexture, d.metallicRoughnessTexture, d.occlusionTexture, d.emissiveTexture})
        if (h) {
            const auto &t = impl_->owner(h.owner).textures.get(h)->description;
            if (t.layers != 1 || t.arrayTexture)
                throw std::invalid_argument("Auxiliary material maps currently require 2D views");
        }
    return impl_->materials.create(
        std::make_shared<const detail::MaterialData>(detail::MaterialData{
            detail::uniqueId(), d.baseColor, std::move(texture), d.unlit, d,
            d.normalTexture ? impl_->owner(d.normalTexture.owner).textures.get(d.normalTexture)
                            : nullptr,
            d.metallicRoughnessTexture ? impl_->owner(d.metallicRoughnessTexture.owner)
                                             .textures.get(d.metallicRoughnessTexture)
                                       : nullptr,
            d.occlusionTexture
                ? impl_->owner(d.occlusionTexture.owner).textures.get(d.occlusionTexture)
                : nullptr,
            d.emissiveTexture
                ? impl_->owner(d.emissiveTexture.owner).textures.get(d.emissiveTexture)
                : nullptr}));
}
void AssetStore::destroyMesh(MeshHandle h) {
    ++impl_->revision;
    impl_->owner(h.owner).meshes.erase(h);
}
void AssetStore::destroyTexture(TextureHandle h) {
    ++impl_->revision;
    impl_->owner(h.owner).textures.erase(h);
}
void AssetStore::destroyMaterial(MaterialInstanceHandle h) {
    ++impl_->revision;
    impl_->owner(h.owner).materials.erase(h);
}
SpriteHandle AssetStore::createSprite(const SpriteDesc &description) {
    auto d = description;
    const auto texture = impl_->owner(d.texture.owner).textures.get(d.texture);
    const auto custom =
        d.material ? impl_->owner(d.material.owner).materials.get(d.material) : nullptr;
    if (custom) {
        if (custom->texture != texture)
            throw std::invalid_argument("Sprite material texture mismatch");
        d.alphaMode = custom->description.alphaMode;
        d.alphaCutoff = custom->description.alphaCutoff;
        d.unlit = custom->description.unlit;
        d.blend = custom->description.blend;
    }
    if (d.blend != MaterialBlend::Alpha && d.blend != MaterialBlend::PremultipliedAlpha &&
        d.blend != MaterialBlend::Additive)
        throw std::invalid_argument("Invalid sprite blend mode");
    if (d.textureLayer >= texture->description.layers || !std::isfinite(d.distanceFieldRange) ||
        d.distanceFieldRange < 0 || d.distanceFieldRange > 64)
        throw std::invalid_argument("Invalid sprite layer/SDF range");
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
        impl_->allMeshes.push_back(impl_->spriteQuad);
    }
    const auto key = std::tuple{texture->id, d.alphaMode, d.alphaCutoff, d.unlit, d.blend};
    auto material = custom ? custom : impl_->spriteMaterials[key].lock();
    if (!material) {
        MaterialDesc md;
        md.baseColorTexture = d.texture;
        md.unlit = d.unlit;
        md.alphaMode = d.alphaMode;
        md.alphaCutoff = d.alphaCutoff;
        md.doubleSided = true;
        md.blend = d.blend;
        material = std::make_shared<const detail::MaterialData>(detail::MaterialData{
            detail::uniqueId(), md.baseColor, texture, md.unlit, md, {}, {}, {}, {}});
        impl_->spriteMaterials[key] = material;
        std::erase_if(impl_->spriteMaterials, [](const auto &p) { return p.second.expired(); });
    }
    return impl_->sprites.create(std::make_shared<const detail::SpriteData>(
        detail::SpriteData{d, impl_->spriteQuad, std::move(material)}));
}
SpriteDesc AssetStore::spriteDescription(SpriteHandle sprite) const {
    return impl_->owner(sprite.owner).sprites.get(sprite)->description;
}
void AssetStore::destroySprite(SpriteHandle sprite) {
    ++impl_->revision;
    impl_->owner(sprite.owner).sprites.erase(sprite);
}
std::shared_ptr<const detail::SpriteData> detail::AssetAccess::sprite(const AssetStore &store,
                                                                      SpriteHandle sprite) {
    return store.impl_->owner(sprite.owner).sprites.get(sprite);
}
std::shared_ptr<const detail::MeshData> detail::AssetAccess::mesh(const AssetStore &store,
                                                                  MeshHandle h) {
    return store.impl_->owner(h.owner).meshes.get(h);
}
std::shared_ptr<const detail::MaterialData>
detail::AssetAccess::material(const AssetStore &store, MaterialInstanceHandle h) {
    return store.impl_->owner(h.owner).materials.get(h);
}

std::shared_ptr<const detail::TextureData> detail::AssetAccess::texture(const AssetStore &store,
                                                                        TextureHandle h) {
    return store.impl_->owner(h.owner).textures.get(h);
}
TextureDesc AssetStore::textureDescription(TextureHandle h) const {
    return impl_->owner(h.owner).textures.get(h)->description;
}
uint64_t AssetStore::adopt(AssetStore &&source) {
    ++impl_->revision;
    if (&source == this || !source.impl_)
        throw std::invalid_argument("Invalid source store");
    auto fresh = std::make_unique<Impl>();
    auto owners = impl_->owners;
    for (const auto &[id, ptr] : source.impl_->owners)
        owners.emplace(id, ptr);
    impl_->imported.reserve(impl_->imported.size() + 1);
    const auto package = source.impl_->package;
    impl_->imported.push_back(std::move(source.impl_));
    impl_->owners.swap(owners);
    source.impl_ = std::move(fresh);
    return package;
}
void AssetStore::releasePackage(uint64_t package) {
    ++impl_->revision;
    auto it = std::find_if(impl_->imported.begin(), impl_->imported.end(),
                           [&](const auto &p) { return p->package == package; });
    if (it == impl_->imported.end())
        throw std::invalid_argument("Unknown asset package");
    auto meshes = impl_->allMeshes;
    auto textures = impl_->allTextures;
    std::erase_if(meshes, [](const auto &w) { return w.expired(); });
    std::erase_if(textures, [](const auto &w) { return w.expired(); });
    auto keep = [&](auto &&self, const Impl &p) -> void {
        meshes.insert(meshes.end(), p.allMeshes.begin(), p.allMeshes.end());
        textures.insert(textures.end(), p.allTextures.begin(), p.allTextures.end());
        for (const auto &child : p.imported)
            self(self, *child);
    };
    keep(keep, **it);
    for (const auto &[owner, ptr] : (*it)->owners) {
        (void)ptr;
        impl_->owners.erase(owner);
    }
    impl_->allMeshes.swap(meshes);
    impl_->allTextures.swap(textures);
    impl_->imported.erase(it);
}
AssetMemoryStats AssetStore::memoryStatistics() const {
    AssetMemoryStats out;
    auto visit = [&](auto &&self, const Impl &store) -> void {
        store.meshes.forEach([&](const auto &) { ++out.meshHandles; });
        store.textures.forEach([&](const auto &) { ++out.textureHandles; });
        store.materials.forEach([&](const auto &) { ++out.materialHandles; });
        store.sprites.forEach([&](const auto &) { ++out.spriteHandles; });
        for (const auto &weak : store.allMeshes)
            if (auto a = weak.lock()) {
                ++out.retainedMeshes;
                out.meshBytes += a->description.vertices.size() * sizeof(Vertex) +
                                 a->description.indices.size() * sizeof(uint32_t);
            }
        for (const auto &weak : store.allTextures)
            if (auto a = weak.lock()) {
                ++out.retainedTextures;
                out.textureBytes += a->description.pixels.size() + a->description.data.size();
            }
        for (const auto &child : store.imported)
            self(self, *child);
    };
    visit(visit, *impl_);
    return out;
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

namespace renderer::detail {
std::pair<uint64_t, uint64_t> AssetAccess::stamp(const AssetStore &a) {
    return {a.impl_->package, a.impl_->revision};
}
} // namespace renderer::detail
