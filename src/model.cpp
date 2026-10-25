#include "renderer/model.hpp"
#include "renderer/texture.hpp"
#define TINYGLTF_IMPLEMENTATION
#define STBI_MAX_DIMENSIONS 16384
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tiny_gltf.h>

namespace renderer {
struct Model::Impl {
    struct Node {
        std::string name;
        int parent = -1;
        Mat4 transform = Mat4::identity();
        std::optional<MeshRenderer> mesh;
        std::optional<Camera> camera;
        std::optional<Light> light;
    };
    std::vector<Node> nodes;
    std::vector<std::string> warnings;
};
Model::Model() = default;
Model::~Model() = default;
Model::Model(const Model &) = default;
Model &Model::operator=(const Model &) = default;
Model::Model(Model &&) noexcept = default;
Model &Model::operator=(Model &&) noexcept = default;
size_t Model::nodeCount() const {
    return impl_ ? impl_->nodes.size() : 0;
}
std::vector<std::string> Model::warnings() const {
    return impl_ ? impl_->warnings : std::vector<std::string>{};
}
ModelInstance Model::instantiate(Scene &scene, NodeHandle parent, std::string_view name) const {
    if (!impl_)
        throw std::logic_error("Empty model");
    ModelInstance instance;
    instance.root = scene.createNode(parent, name);
    try {
        for (const auto &description : impl_->nodes) {
            auto p = description.parent < 0
                         ? instance.root
                         : instance.nodes.at(static_cast<size_t>(description.parent));
            auto n = scene.createNode(p, description.name);
            instance.nodes.push_back(n);
            scene.setLocalTransform(n, description.transform);
            if (description.mesh)
                scene.setComponent(n, *description.mesh);
            if (description.camera) {
                scene.setComponent(n, *description.camera);
                instance.cameras.push_back(n);
            }
            if (description.light) {
                scene.setComponent(n, *description.light);
                instance.lights.push_back(n);
            }
        }
    } catch (...) {
        // 实例化也采用事务式清理，不把半棵导入子树遗留在调用者场景中。
        scene.destroySubtree(instance.root);
        throw;
    }
    return instance;
}
namespace {
std::string pathUtf8(const std::filesystem::path &p) {
    auto s = p.u8string();
    return {s.begin(), s.end()};
}
TextureDesc imageFile(const std::filesystem::path &path, bool srgb) {
    auto extension = path.extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](char8_t c) { return c >= u8'A' && c <= u8'Z' ? char8_t(c + 32) : c; });
    if (extension == u8".dds")
        return texture::loadDds(path);

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() < 0 || file.tellg() > 256 * 1024 * 1024)
        throw std::runtime_error("Cannot read image or encoded image exceeds 256 MiB: " +
                                 pathUtf8(path));
    std::vector<stbi_uc> encoded(size_t(file.tellg()));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char *>(encoded.data()), std::streamsize(encoded.size())))
        throw std::runtime_error("Cannot read image: " + pathUtf8(path));
    int width = 0, height = 0, channels = 0;
    stbi_uc *pixels =
        stbi_load_from_memory(encoded.data(), int(encoded.size()), &width, &height, &channels, 4);
    if (!pixels)
        throw std::runtime_error("Image load failed: " + pathUtf8(path) + ": " +
                                 (stbi_failure_reason() ? stbi_failure_reason() : "unknown"));
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> storage(pixels, stbi_image_free);
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384)
        throw std::invalid_argument("Image dimensions exceed texture profile");
    TextureDesc result{static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                       std::vector<uint8_t>(size_t{static_cast<uint32_t>(width)} *
                                            static_cast<uint32_t>(height) * 4),
                       srgb};
    // 独立图片从解码器的顶行顺序转换为手工纹理约定；glTF 走自己的配套 UV 路径。
    const size_t row = static_cast<size_t>(width) * 4;
    for (int y = 0; y < height; ++y)
        std::memcpy(result.pixels.data() + static_cast<size_t>(y) * row,
                    pixels + static_cast<size_t>(height - 1 - y) * row, row);
    return result;
}
// 记录本次导入创建的资源；解析全部成功后提交，任意异常触发逆向依赖清理。
struct Import {
    AssetStore &store;
    bool committed = false;
    std::vector<MeshHandle> meshes;
    std::vector<TextureHandle> textures;
    std::vector<MaterialInstanceHandle> materials;
    std::map<std::pair<std::string, bool>, TextureHandle> imageCache;
    ~Import() {
        if (committed)
            return;
        try {
            for (auto h : materials)
                store.destroyMaterial(h);
            for (auto h : meshes)
                store.destroyMesh(h);
            for (auto h : textures)
                store.destroyTexture(h);
        } catch (...) {
        }
    }
    MeshHandle mesh(MeshDesc d) {
        auto h = store.createMesh(std::move(d));
        meshes.push_back(h);
        return h;
    }
    TextureHandle texture(TextureDesc d) {
        auto h = store.createTexture(std::move(d));
        textures.push_back(h);
        return h;
    }
    MaterialInstanceHandle material(const MaterialDesc &d) {
        auto h = store.createMaterial(d);
        materials.push_back(h);
        return h;
    }
    TextureHandle image(const std::filesystem::path &path, bool srgb) {
        auto key = std::pair{pathUtf8(std::filesystem::absolute(path).lexically_normal()), srgb};
        if (auto i = imageCache.find(key); i != imageCache.end())
            return i->second;
        auto h = texture(imageFile(path, srgb));
        imageCache.emplace(key, h);
        return h;
    }
};
template <class T> const T &at(const std::vector<T> &values, int index, const char *what) {
    if (index < 0 || static_cast<size_t>(index) >= values.size())
        throw std::invalid_argument(std::string("Invalid glTF ") + what + " index");
    return values[static_cast<size_t>(index)];
}
double scalar(const uint8_t *p, int type, bool normalized) {
    switch (type) {
    case TINYGLTF_COMPONENT_TYPE_FLOAT: {
        float v;
        std::memcpy(&v, p, 4);
        return v;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
        uint32_t v;
        std::memcpy(&v, p, 4);
        return normalized ? double(v) / 4294967295.0 : v;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
        uint16_t v;
        std::memcpy(&v, p, 2);
        return normalized ? double(v) / 65535.0 : v;
    }
    case TINYGLTF_COMPONENT_TYPE_SHORT: {
        int16_t v;
        std::memcpy(&v, p, 2);
        return normalized ? std::max(-1.0, double(v) / 32767.0) : v;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        return normalized ? double(*p) / 255 : *p;
    case TINYGLTF_COMPONENT_TYPE_BYTE: {
        int8_t v;
        std::memcpy(&v, p, 1);
        return normalized ? std::max(-1.0, double(v) / 127) : v;
    }
    default:
        throw std::invalid_argument("Unsupported glTF component type");
    }
}
struct AccessorValues {
    size_t count = 0;
    int components = 0;
    std::vector<double> values;
};
// 将交错、归一化及稀疏 accessor 统一展开，先校验字节范围再读取源缓冲。
AccessorValues accessor(const tinygltf::Model &model, int index, int components) {
    const auto &a = at(model.accessors, index, "accessor");
    if (tinygltf::GetNumComponentsInType(static_cast<uint32_t>(a.type)) != components ||
        a.count > 10000000)
        throw std::invalid_argument("Unsupported accessor shape/size");
    const int scalarSize =
        tinygltf::GetComponentSizeInBytes(static_cast<uint32_t>(a.componentType));
    if (scalarSize <= 0)
        throw std::invalid_argument("Invalid accessor component size");
    AccessorValues result{a.count, components,
                          std::vector<double>(a.count * static_cast<size_t>(components))};
    const auto readView = [&](int viewIndex, size_t offset, size_t count, size_t stride,
                              int componentType, bool norm, int dimensions, auto &&output) {
        const auto &view = at(model.bufferViews, viewIndex, "buffer view");
        const auto &buffer = at(model.buffers, view.buffer, "buffer");
        const size_t element = static_cast<size_t>(tinygltf::GetComponentSizeInBytes(
                                   static_cast<uint32_t>(componentType))) *
                               static_cast<size_t>(dimensions);
        if (!element || stride < element || view.byteOffset > buffer.data.size() ||
            view.byteLength > buffer.data.size() - view.byteOffset || offset > view.byteLength ||
            (count && ((count - 1) > (view.byteLength - offset) / stride ||
                       element > view.byteLength - offset - (count - 1) * stride)))
            throw std::invalid_argument("Accessor exceeds its buffer view");
        for (size_t n = 0; n < count; ++n)
            for (int c = 0; c < dimensions; ++c) {
                const auto *p = buffer.data.data() + view.byteOffset + offset + n * stride +
                                static_cast<size_t>(c) * tinygltf::GetComponentSizeInBytes(
                                                             static_cast<uint32_t>(componentType));
                double value = scalar(p, componentType, norm);
                if (!std::isfinite(value))
                    throw std::invalid_argument("Non-finite accessor value");
                output(n, c, value);
            }
    };
    if (a.bufferView >= 0) {
        const auto &view = at(model.bufferViews, a.bufferView, "buffer view");
        const int stride = a.ByteStride(view);
        if (stride < 0)
            throw std::invalid_argument("Invalid accessor stride");
        readView(a.bufferView, a.byteOffset, a.count, static_cast<size_t>(stride), a.componentType,
                 a.normalized, components, [&](size_t n, int c, double value) {
                     result.values[n * static_cast<size_t>(components) + static_cast<size_t>(c)] =
                         value;
                 });
    }
    if (a.sparse.isSparse) {
        if (a.sparse.count < 0 || static_cast<size_t>(a.sparse.count) > a.count)
            throw std::invalid_argument("Invalid sparse accessor count");
        std::vector<size_t> indices(static_cast<size_t>(a.sparse.count));
        const int type = a.sparse.indices.componentType;
        if (type != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
            type != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT &&
            type != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
            throw std::invalid_argument("Invalid sparse index type");
        readView(
            a.sparse.indices.bufferView, a.sparse.indices.byteOffset, indices.size(),
            static_cast<size_t>(tinygltf::GetComponentSizeInBytes(static_cast<uint32_t>(type))),
            type, false, 1, [&](size_t n, int, double value) {
                if (value >= static_cast<double>(a.count) ||
                    (n && value <= static_cast<double>(indices[n - 1])))
                    throw std::invalid_argument("Sparse indices must be in range and increasing");
                indices[n] = static_cast<size_t>(value);
            });
        readView(a.sparse.values.bufferView, a.sparse.values.byteOffset, indices.size(),
                 static_cast<size_t>(scalarSize * components), a.componentType, a.normalized,
                 components, [&](size_t n, int c, double value) {
                     result.values[indices[n] * static_cast<size_t>(components) +
                                   static_cast<size_t>(c)] = value;
                 });
    }
    return result;
}
void normals(MeshDesc &mesh, size_t startVertex = 0) {
    std::vector<Vec3> sums(mesh.vertices.size());
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        auto a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        auto n = cross(mesh.vertices[b].position - mesh.vertices[a].position,
                       mesh.vertices[c].position - mesh.vertices[a].position);
        sums[a] = sums[a] + n;
        sums[b] = sums[b] + n;
        sums[c] = sums[c] + n;
    }
    for (size_t i = startVertex; i < mesh.vertices.size(); ++i)
        mesh.vertices[i].normal =
            dot(sums[i], sums[i]) > 1e-12f ? normalized(sums[i]) : Vec3{0, 1, 0};
}
Mat4 transform(const tinygltf::Node &node) {
    Mat4 result = Mat4::identity();
    if (!node.matrix.empty()) {
        if (node.matrix.size() != 16 || !node.translation.empty() || !node.rotation.empty() ||
            !node.scale.empty())
            throw std::invalid_argument("Invalid glTF matrix/TRS combination");
        for (size_t i = 0; i < 16; ++i)
            result.elements[i] = static_cast<float>(node.matrix[i]);
    } else {
        if (!node.translation.empty()) {
            if (node.translation.size() != 3)
                throw std::invalid_argument("Invalid glTF translation");
            result = result * Mat4::translation({static_cast<float>(node.translation[0]),
                                                 static_cast<float>(node.translation[1]),
                                                 static_cast<float>(node.translation[2])});
        }
        if (!node.rotation.empty()) {
            if (node.rotation.size() != 4)
                throw std::invalid_argument("Invalid glTF quaternion");
            result = result * Mat4::quaternion(static_cast<float>(node.rotation[0]),
                                               static_cast<float>(node.rotation[1]),
                                               static_cast<float>(node.rotation[2]),
                                               static_cast<float>(node.rotation[3]));
        }
        if (!node.scale.empty()) {
            if (node.scale.size() != 3)
                throw std::invalid_argument("Invalid glTF scale");
            result = result * Mat4::scaling({static_cast<float>(node.scale[0]),
                                             static_cast<float>(node.scale[1]),
                                             static_cast<float>(node.scale[2])});
        }
    }
    if (!result.isAffine())
        throw std::invalid_argument("Invalid glTF transform");
    return result;
}
} // namespace
TextureHandle AssetStore::loadTexture(const std::filesystem::path &path, bool srgb) {
    return createTexture(imageFile(path, srgb));
}
TextureHandle AssetStore::loadTexture(const std::filesystem::path &path, bool srgb,
                                      const TextureSampling &sampling) {
    auto description = imageFile(path, srgb);
    description.sampling = sampling;
    return createTexture(std::move(description));
}

Model AssetStore::loadModel(const std::filesystem::path &path) {
    Import transaction{*this};
    auto data = std::make_shared<Model::Impl>();
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension == ".gltf" || extension == ".glb") {
        tinygltf::TinyGLTF loader;
        tinygltf::Model source;
        std::string error, warning;
        const bool ok = extension == ".glb"
                            ? loader.LoadBinaryFromFile(&source, &error, &warning, pathUtf8(path))
                            : loader.LoadASCIIFromFile(&source, &error, &warning, pathUtf8(path));
        if (!ok)
            throw std::runtime_error("glTF load failed: " + error);
        if (!warning.empty())
            data->warnings.push_back(warning);
        for (const auto &e : source.extensionsRequired)
            if (e != "KHR_materials_unlit" && e != "KHR_lights_punctual" &&
                e != "KHR_materials_emissive_strength")
                throw std::invalid_argument("Unsupported required glTF extension: " + e);
        if (!source.animations.empty() || !source.skins.empty())
            throw std::invalid_argument("This importer accepts static models; animation/skinning "
                                        "requires a separate animation system");
        std::map<std::pair<int, bool>, TextureHandle> textureCache;
        auto texture = [&](int index, bool srgb) {
            if (index < 0)
                return TextureHandle{};
            auto key = std::pair{index, srgb};
            if (auto i = textureCache.find(key); i != textureCache.end())
                return i->second;
            const auto &t = at(source.textures, index, "texture");
            const auto &image = at(source.images, t.source, "image");
            if (image.width <= 0 || image.height <= 0 || image.bits != 8 || image.component < 1 ||
                image.component > 4 ||
                image.image.size() != static_cast<size_t>(image.width) *
                                          static_cast<size_t>(image.height) *
                                          static_cast<size_t>(image.component))
                throw std::invalid_argument("glTF image must decode to 8-bit channels");
            TextureDesc desc{static_cast<uint32_t>(image.width),
                             static_cast<uint32_t>(image.height),
                             std::vector<uint8_t>(static_cast<size_t>(image.width) *
                                                  static_cast<size_t>(image.height) * 4),
                             srgb};
            for (size_t pixel = 0; pixel < desc.pixels.size() / 4; ++pixel) {
                auto *destination = desc.pixels.data() + pixel * 4;
                const auto *input =
                    image.image.data() + pixel * static_cast<size_t>(image.component);
                destination[0] = input[0];
                destination[1] = image.component >= 3 ? input[1] : input[0];
                destination[2] = image.component >= 3 ? input[2] : input[0];
                destination[3] =
                    (image.component == 2 ? input[1] : (image.component == 4 ? input[3] : 255));
            }
            if (t.sampler >= 0) {
                const auto &sample = at(source.samplers, t.sampler, "sampler");
                auto wrap = [](int mode) {
                    return mode == TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE     ? TextureAddress::Clamp
                           : mode == TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT ? TextureAddress::Mirror
                                                                           : TextureAddress::Repeat;
                };
                desc.sampling.addressU = wrap(sample.wrapS);
                desc.sampling.addressV = wrap(sample.wrapT);
                desc.sampling.magFilter = sample.magFilter == TINYGLTF_TEXTURE_FILTER_NEAREST
                                              ? TextureFilter::Nearest
                                              : TextureFilter::Linear;
                desc.sampling.minFilter =
                    (sample.minFilter == TINYGLTF_TEXTURE_FILTER_NEAREST ||
                     sample.minFilter == TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST ||
                     sample.minFilter == TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR)
                        ? TextureFilter::Nearest
                        : TextureFilter::Linear;
                desc.sampling.mipFilter =
                    (sample.minFilter == TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST ||
                     sample.minFilter == TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST)
                        ? TextureFilter::Nearest
                        : TextureFilter::Linear;
                desc.sampling.mipmapped = sample.minFilter != TINYGLTF_TEXTURE_FILTER_NEAREST &&
                                          sample.minFilter != TINYGLTF_TEXTURE_FILTER_LINEAR;
            }
            auto h = transaction.texture(std::move(desc));
            textureCache.emplace(key, h);
            return h;
        };
        auto checkUV = [](const auto &info) {
            if (info.index >= 0 &&
                (info.texCoord != 0 || info.extensions.contains("KHR_texture_transform")))
                throw std::invalid_argument(
                    "Material texture requires TEXCOORD_0 without KHR_texture_transform");
        };
        std::vector<MaterialInstanceHandle> materialHandles;
        for (const auto &m : source.materials) {
            MaterialDesc d;
            const auto &p = m.pbrMetallicRoughness;
            if (p.baseColorFactor.size() != 4 || m.emissiveFactor.size() != 3)
                throw std::invalid_argument("Invalid material factors");
            d.baseColor = {
                static_cast<float>(p.baseColorFactor[0]), static_cast<float>(p.baseColorFactor[1]),
                static_cast<float>(p.baseColorFactor[2]), static_cast<float>(p.baseColorFactor[3])};
            d.metallic = static_cast<float>(p.metallicFactor);
            d.roughness = static_cast<float>(p.roughnessFactor);
            d.emissive = {static_cast<float>(m.emissiveFactor[0]),
                          static_cast<float>(m.emissiveFactor[1]),
                          static_cast<float>(m.emissiveFactor[2])};
            d.doubleSided = m.doubleSided;
            d.alphaMode = m.alphaMode == "BLEND"  ? AlphaMode::Blend
                          : m.alphaMode == "MASK" ? AlphaMode::Mask
                                                  : AlphaMode::Opaque;
            d.alphaCutoff = static_cast<float>(m.alphaCutoff);
            d.normalScale = static_cast<float>(m.normalTexture.scale);
            d.occlusionStrength = static_cast<float>(m.occlusionTexture.strength);
            d.unlit = m.extensions.contains("KHR_materials_unlit");
            if (auto e = m.extensions.find("KHR_materials_emissive_strength");
                e != m.extensions.end() && e->second.Has("emissiveStrength"))
                d.emissive =
                    d.emissive *
                    static_cast<float>(e->second.Get("emissiveStrength").GetNumberAsDouble());
            checkUV(p.baseColorTexture);
            checkUV(p.metallicRoughnessTexture);
            checkUV(m.normalTexture);
            checkUV(m.occlusionTexture);
            checkUV(m.emissiveTexture);
            d.baseColorTexture = texture(p.baseColorTexture.index, true);
            d.metallicRoughnessTexture = texture(p.metallicRoughnessTexture.index, false);
            d.normalTexture = texture(m.normalTexture.index, false);
            d.occlusionTexture = texture(m.occlusionTexture.index, false);
            d.emissiveTexture = texture(m.emissiveTexture.index, true);
            materialHandles.push_back(transaction.material(d));
        }
        const auto defaultMaterial = transaction.material({});
        std::vector<MeshRenderer> renderables;
        for (const auto &m : source.meshes) {
            MeshDesc combined;
            std::vector<MaterialInstanceHandle> slots;
            for (const auto &primitive : m.primitives) {
                if (!primitive.targets.empty() ||
                    primitive.extensions.contains("KHR_draco_mesh_compression"))
                    throw std::invalid_argument(
                        "Morph/compressed geometry is unsupported in the static mesh profile");
                auto position = primitive.attributes.find("POSITION");
                if (position == primitive.attributes.end())
                    throw std::invalid_argument("Mesh primitive lacks POSITION");
                auto positions = accessor(source, position->second, 3);
                MeshDesc part;
                part.vertices.resize(positions.count);
                for (size_t i = 0; i < positions.count; ++i)
                    part.vertices[i].position = {static_cast<float>(positions.values[i * 3]),
                                                 static_cast<float>(positions.values[i * 3 + 1]),
                                                 static_cast<float>(positions.values[i * 3 + 2])};
                bool hasNormal = false;
                if (auto n = primitive.attributes.find("NORMAL"); n != primitive.attributes.end()) {
                    auto values = accessor(source, n->second, 3);
                    if (values.count != positions.count)
                        throw std::invalid_argument("Normal count mismatch");
                    for (size_t i = 0; i < values.count; ++i)
                        part.vertices[i].normal = {static_cast<float>(values.values[i * 3]),
                                                   static_cast<float>(values.values[i * 3 + 1]),
                                                   static_cast<float>(values.values[i * 3 + 2])};
                    hasNormal = true;
                }
                if (auto t = primitive.attributes.find("TEXCOORD_0");
                    t != primitive.attributes.end()) {
                    auto values = accessor(source, t->second, 2);
                    if (values.count != positions.count)
                        throw std::invalid_argument("UV count mismatch");
                    for (size_t i = 0; i < values.count; ++i)
                        part.vertices[i].uv = {static_cast<float>(values.values[i * 2]),
                                               static_cast<float>(values.values[i * 2 + 1])};
                }
                std::vector<uint32_t> indices;
                if (primitive.indices >= 0) {
                    const auto &a = at(source.accessors, primitive.indices, "index accessor");
                    if (a.normalized ||
                        (a.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
                         a.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT &&
                         a.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT))
                        throw std::invalid_argument("Invalid triangle index encoding");
                    auto values = accessor(source, primitive.indices, 1);
                    for (double index : values.values) {
                        if (index < 0 || index >= static_cast<double>(positions.count))
                            throw std::invalid_argument("Triangle index out of range");
                        indices.push_back(static_cast<uint32_t>(index));
                    }
                } else {
                    indices.resize(positions.count);
                    std::iota(indices.begin(), indices.end(), 0);
                }
                const int mode = primitive.mode < 0 ? TINYGLTF_MODE_TRIANGLES : primitive.mode;
                if (mode == TINYGLTF_MODE_TRIANGLES) {
                    if (indices.size() % 3)
                        throw std::invalid_argument("Triangle index count mismatch");
                    part.indices = std::move(indices);
                } else if (mode == TINYGLTF_MODE_TRIANGLE_STRIP ||
                           mode == TINYGLTF_MODE_TRIANGLE_FAN) {
                    for (size_t i = 2; i < indices.size(); ++i) {
                        part.indices.push_back(mode == TINYGLTF_MODE_TRIANGLE_FAN
                                                   ? indices[0]
                                                   : indices[i - 2 + (i % 2)]);
                        part.indices.push_back(mode == TINYGLTF_MODE_TRIANGLE_FAN
                                                   ? indices[i - 1]
                                                   : indices[i - 1 - (i % 2)]);
                        part.indices.push_back(indices[i]);
                    }
                } else
                    throw std::invalid_argument("Only triangle primitives are supported");
                if (!hasNormal)
                    normals(part);
                const auto base = static_cast<uint32_t>(combined.vertices.size()),
                           first = static_cast<uint32_t>(combined.indices.size());
                combined.vertices.insert(combined.vertices.end(), part.vertices.begin(),
                                         part.vertices.end());
                for (auto i : part.indices)
                    combined.indices.push_back(base + i);
                combined.submeshes.push_back({first, static_cast<uint32_t>(part.indices.size()),
                                              static_cast<uint32_t>(slots.size())});
                slots.push_back(primitive.material < 0
                                    ? defaultMaterial
                                    : at(materialHandles, primitive.material, "material"));
            }
            renderables.push_back({transaction.mesh(std::move(combined)), std::move(slots)});
        }
        std::vector<int> roots;
        if (!source.scenes.empty())
            roots =
                at(source.scenes, source.defaultScene < 0 ? 0 : source.defaultScene, "scene").nodes;
        else {
            std::vector<bool> child(source.nodes.size());
            for (const auto &node : source.nodes)
                for (int i : node.children) {
                    at(source.nodes, i, "node");
                    child[static_cast<size_t>(i)] = true;
                }
            for (size_t i = 0; i < child.size(); ++i)
                if (!child[i])
                    roots.push_back(static_cast<int>(i));
        }
        std::vector<std::pair<int, int>> pending;
        for (auto i = roots.rbegin(); i != roots.rend(); ++i)
            pending.push_back({*i, -1});
        std::set<int> visited;
        while (!pending.empty()) {
            auto [index, parent] = pending.back();
            pending.pop_back();
            const auto &node = at(source.nodes, index, "node");
            if (!visited.insert(index).second)
                throw std::invalid_argument("glTF hierarchy contains a cycle or shared child");
            if (node.skin >= 0 || !node.weights.empty())
                throw std::invalid_argument("Skinned/morphed nodes require an animation system");
            Model::Impl::Node description;
            description.name = node.name;
            description.parent = parent;
            description.transform = transform(node);
            if (node.mesh >= 0)
                description.mesh = at(renderables, node.mesh, "mesh");
            if (node.camera >= 0) {
                const auto &c = at(source.cameras, node.camera, "camera");
                Camera imported;
                if (c.type == "perspective") {
                    imported.verticalFov = static_cast<float>(c.perspective.yfov);
                    imported.nearPlane = static_cast<float>(c.perspective.znear);
                    imported.farPlane =
                        c.perspective.zfar > 0 ? static_cast<float>(c.perspective.zfar) : 1000;
                    imported.aspectRatio = static_cast<float>(c.perspective.aspectRatio);
                } else if (c.type == "orthographic") {
                    imported.projection = Camera::Projection::Orthographic;
                    imported.orthographicHeight = static_cast<float>(2 * c.orthographic.ymag);
                    imported.aspectRatio =
                        static_cast<float>(c.orthographic.xmag / c.orthographic.ymag);
                    imported.nearPlane = static_cast<float>(c.orthographic.znear);
                    imported.farPlane = static_cast<float>(c.orthographic.zfar);
                } else
                    throw std::invalid_argument("Invalid imported camera type");
                description.camera = imported;
            }
            if (node.light >= 0) {
                const auto &l = at(source.lights, node.light, "light");
                Light light;
                light.type = l.type == "point"  ? LightType::Point
                             : l.type == "spot" ? LightType::Spot
                                                : LightType::Directional;
                if (l.color.size() == 3)
                    light.color = {static_cast<float>(l.color[0]), static_cast<float>(l.color[1]),
                                   static_cast<float>(l.color[2])};
                light.intensity = static_cast<float>(l.intensity);
                light.range = l.range > 0 ? static_cast<float>(l.range) : 1000;
                light.innerCone = static_cast<float>(l.spot.innerConeAngle);
                light.outerCone = static_cast<float>(l.spot.outerConeAngle);
                description.light = light;
            }
            const auto mapped = static_cast<int>(data->nodes.size());
            data->nodes.push_back(std::move(description));
            for (auto i = node.children.rbegin(); i != node.children.rend(); ++i)
                pending.push_back({*i, mapped});
        }
    } else if (extension == ".obj") {
        std::ifstream stream(path);
        if (!stream)
            throw std::runtime_error("Cannot open OBJ: " + pathUtf8(path));
        std::vector<Vec3> positions, normalsList;
        std::vector<Vec2> uvs;
        MeshDesc mesh;
        std::vector<MaterialInstanceHandle> slots;
        std::map<std::string, MaterialInstanceHandle> library;
        auto defaultMaterial = transaction.material({});
        MaterialInstanceHandle active = defaultMaterial;
        std::string line;
        auto loadMtl = [&](const std::filesystem::path &materialPath) {
            std::ifstream input(materialPath);
            if (!input)
                throw std::runtime_error("Cannot open MTL: " + pathUtf8(materialPath));
            std::string row, name;
            MaterialDesc material;
            auto flush = [&] {
                if (!name.empty())
                    library[name] = transaction.material(material);
            };
            while (std::getline(input, row)) {
                std::istringstream fields(row);
                std::string key;
                fields >> key;
                if (key == "newmtl") {
                    flush();
                    fields >> name;
                    material = {};
                } else if (key == "Kd")
                    fields >> material.baseColor.x >> material.baseColor.y >> material.baseColor.z;
                else if (key == "Ke")
                    fields >> material.emissive.x >> material.emissive.y >> material.emissive.z;
                else if (key == "d" || key == "Tr") {
                    float alpha = 1;
                    fields >> alpha;
                    material.baseColor.w = key == "Tr" ? 1 - alpha : alpha;
                    if (material.baseColor.w < 1)
                        material.alphaMode = AlphaMode::Blend;
                } else if (key == "Pr")
                    fields >> material.roughness;
                else if (key == "Pm")
                    fields >> material.metallic;
                else if (key == "Ns") {
                    float exponent = 0;
                    fields >> exponent;
                    material.roughness = std::sqrt(2 / (std::max(0.0f, exponent) + 2));
                } else if (key == "map_Kd" || key == "map_Bump" || key == "bump") {
                    std::string file;
                    std::getline(fields >> std::ws, file);
                    if (file.empty() || file[0] == '-')
                        throw std::invalid_argument(
                            "MTL texture options are unsupported; supply a texture filename");
                    auto h = transaction.image(
                        materialPath.parent_path() /
                            std::filesystem::path(std::u8string(file.begin(), file.end())),
                        key == "map_Kd");
                    if (key == "map_Kd")
                        material.baseColorTexture = h;
                    else
                        material.normalTexture = h;
                }
            }
            flush();
        };
        auto index = [](const std::string &text, size_t count) {
            if (text.empty())
                return -1;
            size_t used = 0;
            int value = std::stoi(text, &used);
            if (used != text.size() || value == 0)
                throw std::invalid_argument("Invalid OBJ index");
            int result = value > 0 ? value - 1 : static_cast<int>(count) + value;
            if (result < 0 || static_cast<size_t>(result) >= count)
                throw std::invalid_argument("OBJ index out of range");
            return result;
        };
        while (std::getline(stream, line)) {
            std::istringstream fields(line);
            std::string key;
            fields >> key;
            if (key == "v") {
                Vec3 value;
                fields >> value.x >> value.y >> value.z;
                if (!fields || !finite(value))
                    throw std::invalid_argument("Invalid OBJ position");
                positions.push_back(value);
            } else if (key == "vn") {
                Vec3 value;
                fields >> value.x >> value.y >> value.z;
                if (!fields)
                    throw std::invalid_argument("Invalid OBJ normal");
                normalsList.push_back(normalized(value));
            } else if (key == "vt") {
                Vec2 value;
                fields >> value.x >> value.y;
                if (!fields)
                    throw std::invalid_argument("Invalid OBJ UV");
                uvs.push_back(value);
            } else if (key == "mtllib") {
                std::string file;
                std::getline(fields >> std::ws, file);
                loadMtl(path.parent_path() /
                        std::filesystem::path(std::u8string(file.begin(), file.end())));
            } else if (key == "usemtl") {
                std::string name;
                fields >> name;
                auto found = library.find(name);
                if (found == library.end())
                    throw std::invalid_argument("OBJ uses unknown material: " + name);
                active = found->second;
            } else if (key == "f") {
                std::vector<Vertex> face;
                std::string corner;
                bool hasNormals = true;
                std::istringstream corners(line.substr(line.find('f') + 1));
                while (corners >> corner) {
                    if (corner[0] == '#')
                        break;
                    const auto a = corner.find('/'), b = a == std::string::npos
                                                             ? std::string::npos
                                                             : corner.find('/', a + 1);
                    const int p = index(corner.substr(0, a), positions.size());
                    if (p < 0)
                        throw std::invalid_argument("OBJ face lacks position");
                    Vertex vertex;
                    vertex.position = positions[static_cast<size_t>(p)];
                    if (a != std::string::npos) {
                        const int t =
                            index(corner.substr(a + 1, b == std::string::npos ? std::string::npos
                                                                              : b - a - 1),
                                  uvs.size());
                        if (t >= 0)
                            vertex.uv = uvs[static_cast<size_t>(t)];
                    }
                    if (b != std::string::npos) {
                        const int n = index(corner.substr(b + 1), normalsList.size());
                        if (n >= 0)
                            vertex.normal = normalsList[static_cast<size_t>(n)];
                        else
                            hasNormals = false;
                    } else
                        hasNormals = false;
                    face.push_back(vertex);
                }
                if (face.size() < 3)
                    throw std::invalid_argument("OBJ face needs three vertices");
                Vec3 normal{};
                for (size_t i = 0; i < face.size(); ++i) {
                    const auto p = face[i].position, q = face[(i + 1) % face.size()].position;
                    normal.x += (p.y - q.y) * (p.z + q.z);
                    normal.y += (p.z - q.z) * (p.x + q.x);
                    normal.z += (p.x - q.x) * (p.y + q.y);
                }
                normal = normalized(normal);
                if (!hasNormals)
                    for (auto &v : face)
                        v.normal = normal;
                int axis = std::abs(normal.x) > std::abs(normal.y) ? 0 : 1;
                if (std::abs(normal.z) > (axis == 0 ? std::abs(normal.x) : std::abs(normal.y)))
                    axis = 2;
                auto point = [&](uint32_t i) {
                    auto p = face[i].position;
                    return axis == 0 ? Vec2{p.y, p.z} : axis == 1 ? Vec2{p.x, p.z} : Vec2{p.x, p.y};
                };
                auto area = [](Vec2 a, Vec2 b, Vec2 c) {
                    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
                };
                float signedArea = 0;
                for (uint32_t i = 0; i < face.size(); ++i) {
                    auto p = point(i), q = point((i + 1) % static_cast<uint32_t>(face.size()));
                    signedArea += p.x * q.y - q.x * p.y;
                }
                const float sign = signedArea > 0 ? 1.0f : -1.0f;
                std::vector<uint32_t> polygon(face.size());
                std::iota(polygon.begin(), polygon.end(), 0);
                const uint32_t base = static_cast<uint32_t>(mesh.vertices.size()),
                               first = static_cast<uint32_t>(mesh.indices.size());
                mesh.vertices.insert(mesh.vertices.end(), face.begin(), face.end());
                while (polygon.size() > 2) {
                    bool clipped = false;
                    for (size_t i = 0; i < polygon.size(); ++i) {
                        const auto a = polygon[(i + polygon.size() - 1) % polygon.size()],
                                   b = polygon[i], c = polygon[(i + 1) % polygon.size()];
                        if (area(point(a), point(b), point(c)) * sign <= 1e-9f)
                            continue;
                        bool inside = false;
                        for (auto v : polygon)
                            if (v != a && v != b && v != c &&
                                area(point(a), point(b), point(v)) * sign >= 0 &&
                                area(point(b), point(c), point(v)) * sign >= 0 &&
                                area(point(c), point(a), point(v)) * sign >= 0) {
                                inside = true;
                                break;
                            }
                        if (inside)
                            continue;
                        mesh.indices.insert(mesh.indices.end(), {base + a, base + b, base + c});
                        polygon.erase(polygon.begin() + static_cast<ptrdiff_t>(i));
                        clipped = true;
                        break;
                    }
                    if (!clipped)
                        throw std::invalid_argument(
                            "OBJ polygon is degenerate or self-intersecting");
                }
                auto slot = std::find(slots.begin(), slots.end(), active);
                if (slot == slots.end()) {
                    slots.push_back(active);
                    slot = slots.end() - 1;
                }
                const auto materialSlot = static_cast<uint32_t>(slot - slots.begin());
                const auto count = static_cast<uint32_t>(mesh.indices.size()) - first;
                if (!mesh.submeshes.empty() && mesh.submeshes.back().materialSlot == materialSlot)
                    mesh.submeshes.back().indexCount += count;
                else
                    mesh.submeshes.push_back({first, count, materialSlot});
            }
        }
        Model::Impl::Node node;
        node.name = pathUtf8(path.stem());
        node.mesh = MeshRenderer{transaction.mesh(std::move(mesh)), std::move(slots)};
        data->nodes.push_back(std::move(node));
    } else
        throw std::invalid_argument("Supported model formats: .gltf, .glb, .obj");
    transaction.committed = true;
    Model result;
    result.impl_ = std::move(data);
    return result;
}
} // namespace renderer
