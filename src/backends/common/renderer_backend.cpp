#include "../../internal/overlay.hpp"
#include "../../internal/pool.hpp"
#include "../../internal/render_backend.hpp"
#include "../../internal/render_texture.hpp"
#include "../../internal/rhi_backend.hpp"
#include "../../internal/texture_format.hpp"
#include "../../internal/window.hpp"
#include "portable_shaders.hpp"
#include "renderer/render_graph.hpp"
#include "renderer/renderer.hpp"
#include "renderer/texture.hpp"
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace renderer {
namespace {
template <class T> std::span<const std::byte> bytes(const T &value) {
    return std::as_bytes(std::span{&value, 1});
}
template <class T> std::span<const std::byte> bytes(const std::vector<T> &values) {
    return std::as_bytes(std::span{values});
}
struct MeshGpu {
    rhi::BufferHandle vertices, indices;
    std::weak_ptr<const detail::MeshData> owner;
    uint64_t bytes = 0;
};
struct TextureGpu {
    rhi::TextureHandle texture;
    rhi::SamplerHandle sampler;
    std::weak_ptr<const detail::TextureData> owner;
    rhi::BindingGroupHandle overlayGroup;
    uint64_t bytes = 0;
    bool external = false;
    bool compressed = false;
    uint32_t layers = 1;
};
struct MaterialGpu {
    rhi::BufferHandle uniform;
    rhi::BindingGroupHandle group;
    std::weak_ptr<const detail::MaterialData> owner;
};
struct FrameSlot {
    rhi::BufferHandle uniform, instances, lights;
    rhi::BindingGroupHandle group;
    rhi::QueryHandle timestamps;
    rhi::CompletionToken completion;
    size_t capacity = 0;
    std::vector<std::string> passNames;
    std::vector<detail::InstanceGpu> instanceMirror;
    rhi::BufferHandle overlayVertices, overlayIndices;
    size_t overlayVertexCapacity = 0, overlayIndexCapacity = 0;
};
struct TargetGpu {
    rhi::TextureHandle texture;
    std::weak_ptr<detail::RenderTextureAccess::State> owner;
    uint64_t bytes = 0;
};
struct GraphState {
    std::optional<CompiledRenderGraph> graph;
    rhi::TextureHandle output;
    rhi::BindingGroupHandle postGroup, fxaaGroup, oitGroup;
    uint32_t width = 0, height = 0, shadowSize = 0;
    TransparencyMode transparency = TransparencyMode::SortedAlpha;
    AntiAliasing antiAliasing = AntiAliasing::None;
    bool orderedSprites = false, unoccludedSprites = false, hasOverlay = false;
};
// 异常路径也结束已记录的有效传输，避免下一帧遗留嵌套批次；资源由提交保持引用。
struct UploadScope {
    rhi::Device &device;
    bool active;
    UploadScope(rhi::Device &d, bool enabled) : device(d), active(enabled) {
        if (active)
            device.beginUploadBatch();
    }
    void finish() {
        if (active) {
            active = false;
            device.endUploadBatch();
        }
    }
    ~UploadScope() {
        if (active)
            try {
                finish();
            } catch (...) {
            }
    }
};
struct RhiRenderBackend final : detail::Backend {
    rhi::Device device;
    uint64_t deviceId = detail::uniqueId(), activeTarget = 0, currentGraphId = 0;
    std::map<uint64_t, TargetGpu> targets;
    std::map<uint64_t, GraphState> targetGraphs;
    RendererConfig config;
    std::shared_ptr<detail::WindowState> window;
    std::atomic<uint64_t> targetSize{0};
    DeviceInfo info;
    ShaderCompiler compiler;
    ShaderArtifact sceneVertex, sceneFragment, oitFragment, shadowVertex, shadowFragment,
        postVertex, postFragment;
    std::map<std::tuple<bool, bool, bool, bool, bool, bool, bool, MaterialBlend, std::string,
                        std::string>,
             rhi::PipelineHandle>
        pipelines;
    rhi::PipelineHandle postPipeline, postEncodedPipeline, fxaaPipeline, oitResolvePipeline;
    rhi::SamplerHandle sampler, shadowSampler, postSampler;
    rhi::TextureHandle white, normal, output;
    rhi::BindingGroupHandle postGroup, fxaaGroup, oitGroup;
    uint32_t width = 0, height = 0, shadowSize = 0;
    TransparencyMode transparency = TransparencyMode::SortedAlpha;
    AntiAliasing antiAliasing = AntiAliasing::None;
    bool orderedSprites = false;
    bool unoccludedSprites = false;
    bool hasOverlay = false;
    rhi::PipelineHandle overlayPipeline;
    rhi::BindingGroupHandle whiteOverlayGroup;
    std::optional<CompiledRenderGraph> graph;
    std::vector<FrameSlot> slots;
    size_t nextSlot = 0;
    std::unordered_map<uint64_t, MeshGpu> meshes;
    std::unordered_map<uint64_t, TextureGpu> textures;
    std::unordered_set<uint64_t> patchParents;
    std::unordered_map<uint64_t, MaterialGpu> materials;
    detail::PreparedFrame *currentFrame = nullptr;
    FrameSlot *currentSlot = nullptr;
    RenderStats lastTiming;
    explicit RhiRenderBackend(rhi::Device input, std::shared_ptr<detail::WindowState> state,
                              const RendererConfig &c, const OffscreenConfig &target = {})
        : device(std::move(input)), config(c), window(std::move(state)) {
        if (!window)
            resizeOffscreen(target.width, target.height);
        if (!c.framesInFlight || c.framesInFlight > 8 || !c.initialInstanceCapacity ||
            c.initialInstanceCapacity > 1000000 || !c.maxQueuedFrames || c.maxQueuedFrames > 8)
            throw std::invalid_argument("Invalid renderer configuration");
        const auto caps = device.capabilities();
        info = {caps.backend, caps.vendor, caps.device, caps.version};
        sceneVertex =
            compiler.compile(ShaderStage::Vertex, detail::sceneVertex(false), "scene.vert");
        sceneFragment = compiler.compile(ShaderStage::Fragment, detail::pbrFragment(), "pbr.frag");
        oitFragment =
            compiler.compile(ShaderStage::Fragment, detail::pbrFragment(true), "pbr-oit.frag");
        shadowVertex =
            compiler.compile(ShaderStage::Vertex, detail::sceneVertex(true), "shadow.vert");
        shadowFragment =
            compiler.compile(ShaderStage::Fragment, detail::shadowFragment(), "shadow.frag");
        postVertex = compiler.compile(ShaderStage::Vertex, detail::postVertex(), "fullscreen.vert");
        postFragment =
            compiler.compile(ShaderStage::Fragment, detail::postFragment(), "tonemap.frag");
        sampler = device.createSampler();
        shadowSampler = device.createSampler({.addressU = rhi::AddressMode::Border,
                                              .addressV = rhi::AddressMode::Border,
                                              .mipmapped = false,
                                              .compare = true});
        postSampler = device.createSampler({.addressU = rhi::AddressMode::Clamp,
                                            .addressV = rhi::AddressMode::Clamp,
                                            .mipmapped = false});
        white = device.createTexture({1, 1, 1, rhi::Format::Rgba8, "White"});
        normal = device.createTexture({1, 1, 1, rhi::Format::Rgba8, "FlatNormal"});
        const uint8_t wPixel[] = {255, 255, 255, 255}, nPixel[] = {128, 128, 255, 255};
        device.upload(white, wPixel);
        device.upload(normal, nPixel);
        rhi::GraphicsPipelineDesc post;
        post.vertex = postVertex;
        post.fragment = postFragment;
        post.colorFormats = {rhi::Format::Rgba8Srgb};
        post.depthTest = false;
        post.depthWrite = false;
        post.cull = rhi::CullMode::None;
        post.name = "ToneMap";
        postPipeline = device.createGraphicsPipeline(post);
        post.fragment =
            compiler.compile(ShaderStage::Fragment, detail::postFragment(true), "tonemap-ldr.frag");
        post.colorFormats = {rhi::Format::Rgba8};
        post.name = "ToneMap encoded LDR";
        postEncodedPipeline = device.createGraphicsPipeline(post);
        post.fragment =
            compiler.compile(ShaderStage::Fragment, detail::fxaaFragment(), "fxaa.frag");
        post.colorFormats = {rhi::Format::Rgba8Srgb};
        post.name = "FXAA";
        fxaaPipeline = device.createGraphicsPipeline(post);
        post.fragment = compiler.compile(ShaderStage::Fragment, detail::oitResolveFragment(),
                                         "oit-resolve.frag");
        post.colorFormats = {rhi::Format::Rgba16Float};
        post.blend = rhi::BlendMode::Alpha;
        post.name = "OIT Resolve";
        oitResolvePipeline = device.createGraphicsPipeline(post);
        for (uint32_t flags = 0; flags < 8; ++flags) {
            auto mat = std::make_shared<detail::MaterialData>();
            mat->description.alphaMode = (flags & 1) ? AlphaMode::Blend : AlphaMode::Opaque;
            mat->description.doubleSided = (flags & 2) != 0;
            detail::DrawBatch batch{};
            batch.material = mat;
            batch.mirrored = (flags & 4) != 0;
            pipeline(batch, false);
            if (flags & 1)
                pipeline(batch, false, true);
            if (!(flags & 1))
                pipeline(batch, true);
        }
        for (uint32_t i = 0; i < c.framesInFlight; ++i) {
            FrameSlot slot;
            slot.uniform =
                device.createBuffer({sizeof(detail::FrameUniforms), rhi::Memory::Upload, "Frame"});
            slot.lights =
                device.createBuffer({sizeof(detail::LightGpu) * 64, rhi::Memory::Upload, "Lights"});
            slot.timestamps = device.createTimestampQueries(32);
            ensureCapacity(slot, c.initialInstanceCapacity);
            slots.push_back(slot);
        }
    }
    ~RhiRenderBackend() override {
        // 原生资源全部归 RHI 设备管理；先释放图持有的引用，再释放设备。
        graph.reset();
        targetGraphs.clear();
        for (auto &[id, t] : targets) {
            (void)id;
            if (auto state = t.owner.lock())
                state->alive = false;
        }
        device.waitIdle();
        if (window)
            window->rendererThreaded = false;
    }
    void ensureCapacity(FrameSlot &slot, size_t required) {
        if (slot.capacity >= required)
            return;
        size_t capacity = std::max<size_t>(config.initialInstanceCapacity, slot.capacity);
        while (capacity < required)
            capacity *= 2;
        if (slot.instances)
            device.destroy(slot.instances);
        slot.instances = device.createBuffer(
            {capacity * sizeof(detail::InstanceGpu), rhi::Memory::Upload, "Instances"});
        slot.capacity = capacity;
        slot.instanceMirror.clear();
    }
    MeshGpu &mesh(const std::shared_ptr<const detail::MeshData> &asset) {
        if (auto it = meshes.find(asset->id); it != meshes.end())
            return it->second;
        const auto &d = asset->description;
        auto vertex = device.createBuffer(
            {d.vertices.size() * sizeof(Vertex), rhi::Memory::Device, "Mesh vertices"},
            bytes(d.vertices));
        auto index = device.createBuffer(
            {d.indices.size() * sizeof(uint32_t), rhi::Memory::Device, "Mesh indices"},
            bytes(d.indices));
        currentFrame->stats.uploadedBytes +=
            d.vertices.size() * sizeof(Vertex) + d.indices.size() * 4;
        const auto size = d.vertices.size() * sizeof(Vertex) + d.indices.size() * sizeof(uint32_t);
        currentFrame->stats.assetUploadedBytes += size;
        return meshes.emplace(asset->id, MeshGpu{vertex, index, asset, size}).first->second;
    }
    rhi::Format assetFormat(const TextureDesc &d) const {
        if (d.channels == TextureChannels::Alpha8)
            return rhi::Format::R8;
        using F = rhi::Format;
        switch (d.compression) {
        case TextureCompression::None:
            return d.srgb ? F::Rgba8Srgb : F::Rgba8;
        case TextureCompression::BC1:
            return d.srgb ? F::Bc1Srgb : F::Bc1;
        case TextureCompression::BC2:
            return d.srgb ? F::Bc2Srgb : F::Bc2;
        case TextureCompression::BC3:
            return d.srgb ? F::Bc3Srgb : F::Bc3;
        case TextureCompression::BC4:
            return F::Bc4;
        case TextureCompression::BC5:
            return F::Bc5;
        case TextureCompression::BC7:
            return d.srgb ? F::Bc7Srgb : F::Bc7;
        }
        throw std::invalid_argument("Invalid asset texture format");
    }
    uint64_t assetUploadBytes(const TextureDesc &d) const {
        if (d.renderTexture.size().first)
            return 0;
        if (!(config.forceTextureDecompression && d.compression != TextureCompression::None) &&
            device.formatFeatures(assetFormat(d)).sampled)
            return d.pixels.size() + d.data.size();
        uint64_t count = 0;
        for (uint32_t mip = 0; mip < d.mipLevels; ++mip)
            count += uint64_t(std::max(1u, d.width >> mip)) * std::max(1u, d.height >> mip) * 4 *
                     d.layers;
        return count;
    }
    rhi::TextureHandle texture(const std::shared_ptr<const detail::TextureData> &asset,
                               rhi::TextureHandle fallback) {
        if (!asset)
            return fallback;
        if (auto it = textures.find(asset->id); it != textures.end())
            return it->second.texture;
        const auto &d = asset->description;
        if (d.renderTexture.size().first) {
            auto &source = target(d.renderTexture);
            rhi::SamplerDesc desc;
            desc.addressU = desc.addressV = rhi::AddressMode::Clamp;
            desc.mipmapped = false;
            textures.emplace(
                asset->id,
                TextureGpu{source.texture, device.createSampler(desc), asset, {}, 0, true});
            return source.texture;
        }
        const auto levels =
            !d.data.empty()
                ? d.mipLevels
                : (d.sampling.mipmapped ? uint32_t(std::bit_width(std::max(d.width, d.height)))
                                        : 1u);
        const auto requested = assetFormat(d);
        const bool forced =
            config.forceTextureDecompression && d.compression != TextureCompression::None;
        const bool unsupported = !device.formatFeatures(requested).sampled;
        const bool decompressed = forced || unsupported;
        if (unsupported && !forced && !config.textureCompressionFallback)
            throw std::runtime_error(
                "Texture compression format is unsupported and fallback is disabled");
        const auto actual =
            decompressed ? (d.srgb ? rhi::Format::Rgba8Srgb : rhi::Format::Rgba8) : requested;
        auto handle = device.createTexture(
            {d.width, d.height, levels, actual, "Asset texture", d.layers, d.arrayTexture});
        try {
            auto parent = textures.find(asset->parentId);
            const bool patch = !decompressed && asset->parentId && parent != textures.end() &&
                               !parent->second.external && parent->second.layers == 1;
            if (patch) {
                device.copyTexture(parent->second.texture, handle,
                                   {.width = d.width, .height = d.height});
                const auto r = asset->dirty;
                const auto channels = d.channels == TextureChannels::Alpha8 ? 1u : 4u;
                auto data = texture::subresource(d);
                std::vector<uint8_t> pixels(size_t(r.width) * r.height * channels);
                for (uint32_t y = 0; y < r.height; ++y)
                    std::copy_n(data.begin() + (size_t(r.y + y) * d.width + r.x) * channels,
                                size_t(r.width) * channels,
                                pixels.begin() + size_t(y) * r.width * channels);
                device.uploadRegion(handle, r, pixels);
                currentFrame->stats.assetUploadedBytes += pixels.size();
                currentFrame->stats.uploadedBytes += pixels.size();
            } else
                for (uint32_t layer = 0; layer < d.layers; ++layer)
                    for (uint32_t mip = 0; mip < (!d.data.empty() ? d.mipLevels : 1u); ++mip) {
                        if (decompressed) {
                            auto raw = texture::decode(d, layer, mip);
                            if (d.origin == TextureOrigin::TopLeft)
                                for (uint32_t y = 0; y < raw.height / 2; ++y)
                                    for (uint32_t x = 0; x < raw.width * 4; ++x)
                                        std::swap(
                                            raw.pixels[size_t(y) * raw.width * 4 + x],
                                            raw.pixels[size_t(raw.height - 1 - y) * raw.width * 4 +
                                                       x]);
                            device.upload(handle, raw.pixels, mip, layer);
                            currentFrame->stats.assetUploadedBytes += raw.pixels.size();
                            currentFrame->stats.uploadedBytes += raw.pixels.size();
                        } else {
                            auto data = texture::subresource(d, layer, mip);
                            device.upload(handle, data, mip, layer);
                            currentFrame->stats.assetUploadedBytes += data.size();
                            currentFrame->stats.uploadedBytes += data.size();
                        }
                    }
            if (d.data.empty() && levels > 1)
                device.generateMipmaps(handle);
        } catch (...) {
            device.destroy(handle);
            throw;
        }
        rhi::SamplerDesc sampling;
        sampling.minFilter = static_cast<rhi::Filter>(d.sampling.minFilter);
        sampling.magFilter = static_cast<rhi::Filter>(d.sampling.magFilter);
        sampling.mipFilter = static_cast<rhi::Filter>(d.sampling.mipFilter);
        sampling.addressU = static_cast<rhi::AddressMode>(d.sampling.addressU);
        sampling.addressV = static_cast<rhi::AddressMode>(d.sampling.addressV);
        sampling.mipmapped = d.sampling.mipmapped;
        uint64_t allocation = 0;
        for (uint32_t mip = 0; mip < levels; ++mip)
            allocation += rhi::detail::imageBytes(actual, std::max(1u, d.width >> mip),
                                                  std::max(1u, d.height >> mip)) *
                          d.layers;
        textures.emplace(asset->id, TextureGpu{handle,
                                               device.createSampler(sampling),
                                               asset,
                                               {},
                                               allocation,
                                               false,
                                               rhi::detail::compressed(actual),
                                               d.layers});
        return handle;
    }
    MaterialGpu &material(const std::shared_ptr<const detail::MaterialData> &asset) {
        if (auto it = materials.find(asset->id); it != materials.end())
            return it->second;
        const auto &d = asset->description;
        const detail::MaterialGpu values{
            d.baseColor,
            {d.metallic, d.roughness, d.alphaCutoff, d.normalScale},
            {d.emissive.x, d.emissive.y, d.emissive.z, 0},
            {d.unlit ? 1.0f : 0.0f, static_cast<float>(d.alphaMode), d.occlusionStrength,
             static_cast<float>(d.blend)},
            d.effect.parameters,
            {float(d.baseTextureLayer),
             asset->texture && asset->texture->description.origin == TextureOrigin::TopLeft ? 1.f
                                                                                            : 0.f,
             asset->normalTexture &&
                     asset->normalTexture->description.compression == TextureCompression::BC5
                 ? 1.f
                 : 0.f,
             asset->texture && asset->texture->description.channels == TextureChannels::Alpha8 &&
                     device.formatFeatures(rhi::Format::R8).sampled
                 ? 1.f
                 : 0.f},
            {asset->normalTexture &&
                     asset->normalTexture->description.origin == TextureOrigin::TopLeft
                 ? 1.f
                 : 0.f,
             asset->metallicRoughnessTexture &&
                     asset->metallicRoughnessTexture->description.origin == TextureOrigin::TopLeft
                 ? 1.f
                 : 0.f,
             asset->occlusionTexture &&
                     asset->occlusionTexture->description.origin == TextureOrigin::TopLeft
                 ? 1.f
                 : 0.f,
             asset->emissiveTexture &&
                     asset->emissiveTexture->description.origin == TextureOrigin::TopLeft
                 ? 1.f
                 : 0.f}};
        auto uniform =
            device.createBuffer({sizeof(values), rhi::Memory::Device, "Material"}, bytes(values));
        rhi::BindingGroupDesc group;
        group.group = 1;
        group.entries.push_back(
            {.binding = 0, .kind = BindingKind::UniformBuffer, .buffer = uniform});
        const rhi::TextureHandle images[] = {
            texture(asset->texture, white), texture(asset->normalTexture, normal),
            texture(asset->metallicRoughnessTexture, white),
            texture(asset->occlusionTexture, white), texture(asset->emissiveTexture, white)};
        const std::shared_ptr<const detail::TextureData> assets[] = {
            asset->texture, asset->normalTexture, asset->metallicRoughnessTexture,
            asset->occlusionTexture, asset->emissiveTexture};
        for (uint32_t i = 0; i < 5; ++i)
            group.entries.push_back(
                {.binding = i + 1,
                 .kind = BindingKind::SampledTexture,
                 .texture = images[i],
                 .sampler = assets[i] ? textures.at(assets[i]->id).sampler : sampler});
        auto handle = device.createBindingGroup(group);
        currentFrame->stats.uploadedBytes += sizeof(values);
        currentFrame->stats.assetUploadedBytes += sizeof(values);
        return materials.emplace(asset->id, MaterialGpu{uniform, handle, asset}).first->second;
    }
    rhi::PipelineHandle pipeline(const detail::DrawBatch &batch, bool shadow,
                                 bool weightedOit = false) {
        const auto &mat = batch.material->description;
        const auto &base = batch.material->texture;
        const bool arrayBase =
            base && (base->description.layers > 1 || base->description.arrayTexture);
        const bool alpha = !shadow && mat.alphaMode == AlphaMode::Blend;
        const bool oit = alpha && weightedOit && mat.blend != MaterialBlend::Additive;
        auto key = std::tuple{
            shadow,          alpha,     oit,       mat.doubleSided,   batch.mirrored,
            batch.depthTest, arrayBase, mat.blend, mat.effect.vertex, mat.effect.surface};
        if (auto it = pipelines.find(key); it != pipelines.end())
            return it->second;
        rhi::GraphicsPipelineDesc desc;
        desc.vertex = shadow ? shadowVertex : sceneVertex;
        desc.fragment = shadow ? shadowFragment : (oit ? oitFragment : sceneFragment);
        if (arrayBase || !mat.effect.vertex.empty())
            desc.vertex = compiler.compile(
                ShaderStage::Vertex, detail::sceneVertex(shadow, mat.effect.vertex, arrayBase),
                "material-effect.vert");
        if (arrayBase || !mat.effect.surface.empty())
            desc.fragment =
                compiler.compile(ShaderStage::Fragment,
                                 shadow ? detail::shadowFragment(mat.effect.surface, arrayBase)
                                        : detail::pbrFragment(oit, mat.effect.surface, arrayBase),
                                 "material-effect.frag");
        desc.colorFormats = shadow ? std::vector<rhi::Format>{}
                                   : std::vector<rhi::Format>{rhi::Format::Rgba16Float};
        desc.depthFormat = rhi::Format::Depth32Float;
        desc.depthTest = shadow || batch.depthTest;
        desc.depthWrite = !alpha && desc.depthTest;
        desc.blend = alpha ? (mat.blend == MaterialBlend::Additive ? rhi::BlendMode::Additive
                                                                   : rhi::BlendMode::Alpha)
                           : rhi::BlendMode::Opaque;
        if (alpha && !oit && mat.blend == MaterialBlend::PremultipliedAlpha) {
            rhi::ColorBlendState premultiplied;
            premultiplied.enabled = true;
            premultiplied.destinationColor = premultiplied.destinationAlpha =
                rhi::BlendFactor::OneMinusSourceAlpha;
            desc.colorBlends = {premultiplied};
        }
        if (oit) {
            // FP32 累积避免高亮 HDR 与多层权重相加时超出 FP16 范围。
            desc.colorFormats = {rhi::Format::Rgba32Float, rhi::Format::R32Float};
            rhi::ColorBlendState accumulation;
            accumulation.enabled = true;
            accumulation.destinationColor = accumulation.destinationAlpha = rhi::BlendFactor::One;
            rhi::ColorBlendState revealage;
            revealage.enabled = true;
            revealage.sourceColor = revealage.sourceAlpha = rhi::BlendFactor::Zero;
            revealage.destinationColor = rhi::BlendFactor::OneMinusSourceColor;
            revealage.destinationAlpha = rhi::BlendFactor::One;
            desc.colorBlends = {accumulation, revealage};
        }
        desc.cull = mat.doubleSided ? rhi::CullMode::None : rhi::CullMode::Back;
        desc.clockwise = batch.mirrored;
        if (shadow) {
            desc.depthBiasConstant = 2;
            desc.depthBiasSlope = 1.5f;
        }
        desc.name = shadow ? "Shadow" : "PBR";
        desc.vertexBindings = {{0, sizeof(Vertex), false}, {1, sizeof(detail::InstanceGpu), true}};
        desc.attributes = {{0, 0, 3, 0}, {1, 0, 3, 12}, {2, 0, 2, 24}};
        for (uint32_t i = 0; i < 4; ++i)
            desc.attributes.push_back({3 + i, 1, 4, i * 16});
        for (uint32_t i = 0; i < 3; ++i)
            desc.attributes.push_back({7 + i, 1, 3, 64 + i * 16});
        desc.attributes.push_back({10, 1, 4, 128});
        desc.attributes.push_back({11, 1, 4, 144});
        desc.attributes.push_back({12, 1, 4, 160});
        desc.attributes.push_back({13, 1, 4, 176});
        desc.attributes.push_back({14, 1, 4, 192});
        desc.attributes.push_back({15, 1, 4, 208});
        auto handle = device.createGraphicsPipeline(desc);
        pipelines.emplace(key, handle);
        return handle;
    }
    void uploadOverlay(FrameSlot &slot, const OverlaySnapshot &overlay) {
        if (overlay.draws().empty())
            return;
        const auto ensure = [&](rhi::BufferHandle &handle, size_t &capacity, size_t required,
                                const char *name) {
            if (capacity >= required)
                return;
            if (handle)
                device.destroy(handle);
            capacity = std::max(required, capacity * 2);
            handle = device.createBuffer({capacity, rhi::Memory::Upload, name});
        };
        const auto v = std::as_bytes(overlay.vertices()), i = std::as_bytes(overlay.indices());
        if (v.empty() || i.empty())
            return;
        ensure(slot.overlayVertices, slot.overlayVertexCapacity, v.size(), "Overlay vertices");
        ensure(slot.overlayIndices, slot.overlayIndexCapacity, i.size(), "Overlay indices");
        device.upload(slot.overlayVertices, 0, v);
        device.upload(slot.overlayIndices, 0, i);
        currentFrame->stats.uploadedBytes += v.size() + i.size();
    }
    void drawOverlay(rhi::CommandEncoder &encoder) {
        const auto &overlay = currentFrame->overlay;
        if (overlay.vertices().empty() || overlay.indices().empty())
            return;
        if (!overlayPipeline) {
            rhi::GraphicsPipelineDesc desc;
            desc.vertex =
                compiler.compile(ShaderStage::Vertex, "#version 450\n" + detail::frameBlock + R"(
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 color;
layout(location=0) out vec2 texcoord;
layout(location=1) out vec4 tint;
void main(){ gl_Position=vec4(position.x*frame.counts.y-1.0,1.0-position.y*frame.counts.z,0,1); texcoord=uv; tint=color; }
)",
                                 "overlay.vert");
            desc.fragment = compiler.compile(ShaderStage::Fragment, R"(#version 450
layout(set=3,binding=0) uniform sampler2D image;
layout(location=0) in vec2 texcoord;
layout(location=1) in vec4 tint;
layout(location=0) out vec4 result;
void main(){result=texture(image,texcoord)*tint;}
)",
                                             "overlay.frag");
            desc.colorFormats = {rhi::Format::Rgba8Srgb};
            desc.depthTest = desc.depthWrite = false;
            desc.cull = rhi::CullMode::None;
            rhi::ColorBlendState blend;
            blend.enabled = true;
            blend.destinationColor = blend.destinationAlpha = rhi::BlendFactor::OneMinusSourceAlpha;
            desc.colorBlends = {blend};
            desc.vertexBindings = {{0, sizeof(OverlayVertex), false}};
            desc.attributes = {{0, 0, 2, offsetof(OverlayVertex, position)},
                               {1, 0, 2, offsetof(OverlayVertex, uv)},
                               {2, 0, 4, offsetof(OverlayVertex, color)}};
            desc.name = "Screen Overlay (linear premultiplied alpha)";
            overlayPipeline = device.createGraphicsPipeline(desc);
        }
        const auto group = [&](rhi::TextureHandle image, rhi::SamplerHandle sampling) {
            return device.createBindingGroup({3,
                                              {{.binding = 0,
                                                .kind = BindingKind::SampledTexture,
                                                .texture = image,
                                                .sampler = sampling}}});
        };
        encoder.bindPipeline(overlayPipeline);
        encoder.bindGroup(currentSlot->group);
        encoder.bindVertexBuffer(0, currentSlot->overlayVertices);
        encoder.bindIndexBuffer(currentSlot->overlayIndices);
        const auto [canvasWidth, canvasHeight] = overlay.size();
        for (size_t n = 0; n < overlay.draws().size(); ++n) {
            const auto &draw = overlay.draws()[n];
            // RHI 使用左下原点；逻辑裁剪先在画布内求交，再换算到实际目标。
            const double x0 = std::clamp<double>(draw.clipX, 0, canvasWidth),
                         y0 = std::clamp<double>(draw.clipY, 0, canvasHeight);
            const double x1 = std::clamp<double>(double(draw.clipX) + draw.clipWidth, x0,
                                                 canvasWidth),
                         y1 = std::clamp<double>(double(draw.clipY) + draw.clipHeight, y0,
                                                 canvasHeight);
            if (!draw.indexCount || x0 >= x1 || y0 >= y1)
                continue;
            auto asset = detail::OverlayAccess::texture(overlay, n);
            if (asset) {
                if (!prepareTexture(asset)) {
                    ++currentFrame->stats.pendingResourceDraws;
                    continue;
                }
                auto &gpu = textures.at(asset->id);
                if (!gpu.overlayGroup)
                    gpu.overlayGroup = group(gpu.texture, gpu.sampler);
                encoder.bindGroup(gpu.overlayGroup);
            } else {
                if (!whiteOverlayGroup)
                    whiteOverlayGroup = group(white, postSampler);
                encoder.bindGroup(whiteOverlayGroup);
            }
            const auto left = int(std::floor(x0 * width / canvasWidth)),
                       right = int(std::ceil(x1 * width / canvasWidth)),
                       bottom = int(std::floor((canvasHeight - y1) * height / canvasHeight)),
                       top = int(std::ceil((canvasHeight - y0) * height / canvasHeight));
            encoder.scissor(left, bottom, uint32_t(right - left), uint32_t(top - bottom));
            encoder.drawIndexed(draw.indexCount, 1, draw.firstIndex);
            ++currentFrame->stats.overlayDrawCalls;
            ++currentFrame->stats.drawCalls;
            currentFrame->stats.overlayTriangles += draw.indexCount / 3;
        }
    }
    void collect() {
        for (auto it = targets.begin(); it != targets.end();) {
            if (it->second.owner.expired() && it->first != currentGraphId &&
                it->first != activeTarget) {
                if (auto g = targetGraphs.find(it->first); g != targetGraphs.end()) {
                    destroyGraph(g->second);
                    targetGraphs.erase(g);
                }
                device.destroy(it->second.texture);
                it = targets.erase(it);
            } else
                ++it;
        }
        for (auto i = materials.begin(); i != materials.end();)
            if (i->second.owner.expired()) {
                device.destroy(i->second.group);
                device.destroy(i->second.uniform);
                i = materials.erase(i);
            } else
                ++i;
        for (auto i = meshes.begin(); i != meshes.end();)
            if (i->second.owner.expired()) {
                device.destroy(i->second.vertices);
                device.destroy(i->second.indices);
                i = meshes.erase(i);
            } else
                ++i;
        for (auto i = textures.begin(); i != textures.end();)
            if (i->second.owner.expired() && !patchParents.contains(i->first)) {
                if (i->second.overlayGroup)
                    device.destroy(i->second.overlayGroup);
                if (!i->second.external)
                    device.destroy(i->second.texture);
                device.destroy(i->second.sampler);
                i = textures.erase(i);
            } else
                ++i;
    }
    void frameBindings(rhi::TextureHandle shadow) {
        if (currentSlot->group)
            device.destroy(currentSlot->group);
        currentSlot->group = device.createBindingGroup(
            {0,
             {{.binding = 0, .kind = BindingKind::UniformBuffer, .buffer = currentSlot->uniform},
              {.binding = 1, .kind = BindingKind::StorageBuffer, .buffer = currentSlot->lights},
              {.binding = 2,
               .kind = BindingKind::SampledTexture,
               .texture = shadow,
               .sampler = shadowSampler}}});
    }
    TargetGpu &target(const RenderTexture &t) {
        auto state = detail::RenderTextureAccess::state(t);
        if (!state || !state->alive || state->owner != deviceId || !targets.contains(state->id))
            throw std::invalid_argument("Render texture belongs to another or expired renderer");
        if (state->id == activeTarget)
            throw std::logic_error(
                "Render texture feedback is forbidden; use two ping-pong targets");
        return targets.at(state->id);
    }
    void swapGraph(GraphState &s) {
        using std::swap;
        swap(graph, s.graph);
        swap(output, s.output);
        swap(postGroup, s.postGroup);
        swap(fxaaGroup, s.fxaaGroup);
        swap(oitGroup, s.oitGroup);
        swap(width, s.width);
        swap(height, s.height);
        swap(shadowSize, s.shadowSize);
        swap(transparency, s.transparency);
        swap(antiAliasing, s.antiAliasing);
        swap(orderedSprites, s.orderedSprites);
        swap(unoccludedSprites, s.unoccludedSprites);
        swap(hasOverlay, s.hasOverlay);
    }
    void selectGraph() {
        if (currentGraphId == activeTarget)
            return;
        swapGraph(targetGraphs[currentGraphId]);
        swapGraph(targetGraphs[activeTarget]);
        currentGraphId = activeTarget;
    }
    void destroyGraph(GraphState &s) {
        s.graph.reset();
        for (auto h : {s.postGroup, s.fxaaGroup, s.oitGroup})
            if (h)
                device.destroy(h);
        if (s.output)
            device.destroy(s.output);
    }
    bool canUpload(uint64_t count) const {
        const auto spent = currentFrame->stats.assetUploadedBytes;
        return !config.assetUploadBudgetBytes || !spent ||
               count <=
                   config.assetUploadBudgetBytes - std::min(spent, config.assetUploadBudgetBytes);
    }
    bool prepareTexture(const std::shared_ptr<const detail::TextureData> &asset) {
        if (!asset)
            return true;
        if (asset->description.renderTexture.size().first)
            (void)target(asset->description.renderTexture);
        if (textures.contains(asset->id))
            return true;
        const auto &d = asset->description;
        const uint64_t required = asset->parentId && textures.contains(asset->parentId) &&
                                          device.formatFeatures(assetFormat(d)).sampled
                                      ? uint64_t(asset->dirty.width) * asset->dirty.height *
                                            (d.channels == TextureChannels::Alpha8 ? 1 : 4)
                                      : assetUploadBytes(d);
        if (!canUpload(required))
            return false;
        texture(asset, white);
        return true;
    }
    bool prepareResources(const detail::DrawBatch &batch) {
        if (!meshes.contains(batch.mesh->id)) {
            const auto &d = batch.mesh->description;
            if (!canUpload(d.vertices.size() * sizeof(Vertex) +
                           d.indices.size() * sizeof(uint32_t)))
                return false;
            mesh(batch.mesh);
        }
        const auto &m = batch.material;
        for (const auto &t : {m->texture, m->normalTexture, m->metallicRoughnessTexture,
                              m->occlusionTexture, m->emissiveTexture})
            if (!prepareTexture(t))
                return false;
        if (materials.contains(m->id))
            return true;
        if (!canUpload(sizeof(detail::MaterialGpu)))
            return false;
        material(m);
        return true;
    }
    void uploadInstances(FrameSlot &slot, detail::PreparedFrame &frame) {
        const auto &data = frame.instances;
        const size_t previousSize = slot.instanceMirror.size();
        if (slot.instanceMirror.size() < data.size())
            slot.instanceMirror.resize(data.size());
        size_t i = 0;
        while (i < data.size()) {
            const auto changed = [&](size_t n) {
                return !config.incrementalInstanceUploads || n >= previousSize ||
                       std::memcmp(&data[n], &slot.instanceMirror[n],
                                   sizeof(detail::InstanceGpu)) != 0;
            };
            if (!changed(i)) {
                ++i;
                continue;
            }
            const auto first = i++;
            while (i < data.size() && changed(i))
                ++i;
            const auto view = std::span(data).subspan(first, i - first);
            device.upload(slot.instances, first * sizeof(detail::InstanceGpu), std::as_bytes(view));
            std::memcpy(slot.instanceMirror.data() + first, view.data(), view.size_bytes());
            frame.stats.instanceUploadedBytes += view.size_bytes();
            ++frame.stats.instanceUploadRanges;
        }
        frame.stats.instanceReusedBytes =
            data.size() * sizeof(detail::InstanceGpu) - frame.stats.instanceUploadedBytes;
        frame.stats.uploadedBytes += frame.stats.instanceUploadedBytes;
        slot.instanceMirror.resize(data.size());
    }
    void drawBatches(rhi::CommandEncoder &encoder, const std::vector<detail::DrawBatch> &batches,
                     bool shadow, int alphaFilter = -1, bool allowOit = true) {
        for (const auto &batch : batches) {
            const bool blend = batch.material->description.alphaMode == AlphaMode::Blend;
            if (alphaFilter >= 0 && blend != (alphaFilter != 0))
                continue;
            auto &geometry = mesh(batch.mesh);
            auto &binding = material(batch.material);
            encoder.bindPipeline(pipeline(batch, shadow,
                                          allowOit && currentFrame->transparency ==
                                                          TransparencyMode::WeightedBlendedOIT));
            encoder.bindGroup(currentSlot->group);
            encoder.bindGroup(binding.group);
            encoder.bindVertexBuffer(0, geometry.vertices);
            encoder.bindVertexBuffer(1, currentSlot->instances);
            encoder.bindIndexBuffer(geometry.indices);
            const auto &submesh = batch.mesh->description.submeshes[batch.submesh];
            encoder.drawIndexed(submesh.indexCount, batch.instanceCount, submesh.firstIndex, 0,
                                batch.firstInstance);
        }
    }
    void buildGraph(uint32_t w, uint32_t h, uint32_t shadowResolution, TransparencyMode mode,
                    AntiAliasing aa, bool hasOrderedSprites, bool overlayEnabled,
                    bool hasUnoccludedSprites) {
        // 视口、阴影尺寸、透明或抗锯齿模式改变时重建，其余帧复用图和物理纹理。
        graph.reset();
        if (oitGroup) {
            device.destroy(oitGroup);
            oitGroup = {};
        }
        if (postGroup) {
            device.destroy(postGroup);
            postGroup = {};
        }
        if (fxaaGroup) {
            device.destroy(fxaaGroup);
            fxaaGroup = {};
        }
        if (output)
            device.destroy(output);
        width = w;
        height = h;
        shadowSize = shadowResolution;
        transparency = mode;
        antiAliasing = aa;
        orderedSprites = hasOrderedSprites;
        unoccludedSprites = hasUnoccludedSprites;
        hasOverlay = overlayEnabled;
        output = device.createTexture({w, h, 1, rhi::Format::Rgba8Srgb, "Display"});
        RenderGraph builder;
        auto shadow = builder.createTexture(
            {shadowSize, shadowSize, 1, rhi::Format::Depth32Float, "ShadowMap"});
        auto hdr = builder.createTexture({w, h, 1, rhi::Format::Rgba16Float, "HDR"});
        auto depth = builder.createTexture({w, h, 1, rhi::Format::Depth32Float, "SceneDepth"});
        auto display = builder.importTexture(output, {w, h, 1, rhi::Format::Rgba8Srgb, "Display"},
                                             rhi::Access::None);
        builder.addPass("Shadow", {{shadow, rhi::Access::DepthWrite, rhi::Stage::Attachment}},
                        [this, shadow](auto &encoder, const auto &resources) {
                            frameBindings(resources.texture(shadow));
                            rhi::RenderingDesc desc;
                            desc.depth = rhi::DepthAttachment{resources.texture(shadow)};
                            encoder.beginRendering(desc);
                            drawBatches(encoder, currentFrame->shadowBatches, true);
                            encoder.endRendering();
                        });
        builder.addPass("Opaque PBR",
                        {{shadow, rhi::Access::SampledRead},
                         {hdr, rhi::Access::ColorWrite, rhi::Stage::Attachment},
                         {depth, rhi::Access::DepthWrite, rhi::Stage::Attachment}},
                        [this, hdr, depth](auto &encoder, const auto &resources) {
                            rhi::RenderingDesc desc;
                            desc.colors.push_back({resources.texture(hdr), rhi::LoadOp::Clear,
                                                   rhi::StoreOp::Store, currentFrame->clearColor});
                            desc.depth = rhi::DepthAttachment{resources.texture(depth)};
                            encoder.beginRendering(desc);
                            drawBatches(encoder, currentFrame->batches, false, 0);
                            encoder.endRendering();
                        });
        if (mode == TransparencyMode::WeightedBlendedOIT) {
            auto accumulation =
                builder.createTexture({w, h, 1, rhi::Format::Rgba32Float, "OIT Accumulation"});
            auto revealage =
                builder.createTexture({w, h, 1, rhi::Format::R32Float, "OIT Revealage"});
            builder.addPass(
                "OIT Accumulation",
                {{shadow, rhi::Access::SampledRead},
                 {accumulation, rhi::Access::ColorWrite, rhi::Stage::Attachment},
                 {revealage, rhi::Access::ColorWrite, rhi::Stage::Attachment},
                 {depth, rhi::Access::DepthRead, rhi::Stage::Attachment}},
                [this, accumulation, revealage, depth](auto &encoder, const auto &resources) {
                    rhi::RenderingDesc desc;
                    desc.colors.push_back({resources.texture(accumulation)});
                    // 透过率从 1 开始，逐片元乘以 (1-alpha)。
                    desc.colors.push_back({resources.texture(revealage),
                                           rhi::LoadOp::Clear,
                                           rhi::StoreOp::Store,
                                           {1, 1, 1, 1}});
                    desc.depth = rhi::DepthAttachment{resources.texture(depth), rhi::LoadOp::Load};
                    encoder.beginRendering(desc);
                    drawBatches(encoder, currentFrame->batches, false, 1);
                    encoder.endRendering();
                });
            builder.addPass(
                "OIT Resolve",
                {{accumulation, rhi::Access::SampledRead},
                 {revealage, rhi::Access::SampledRead},
                 {hdr, rhi::Access::ColorWrite, rhi::Stage::Attachment}},
                [this, accumulation, revealage, hdr](auto &encoder, const auto &resources) {
                    if (!oitGroup)
                        oitGroup =
                            device.createBindingGroup({2,
                                                       {{.binding = 0,
                                                         .kind = BindingKind::SampledTexture,
                                                         .texture = resources.texture(accumulation),
                                                         .sampler = postSampler},
                                                        {.binding = 1,
                                                         .kind = BindingKind::SampledTexture,
                                                         .texture = resources.texture(revealage),
                                                         .sampler = postSampler}}});
                    rhi::RenderingDesc desc;
                    desc.colors.push_back({resources.texture(hdr), rhi::LoadOp::Load});
                    encoder.beginRendering(desc);
                    encoder.bindPipeline(oitResolvePipeline);
                    encoder.bindGroup(oitGroup);
                    encoder.draw(3);
                    encoder.endRendering();
                });
        } else {
            // 排序 Alpha 加载已有 HDR/深度，保留深度测试并关闭深度写入。
            builder.addPass("Transparent",
                            {{shadow, rhi::Access::SampledRead},
                             {hdr, rhi::Access::ColorWrite, rhi::Stage::Attachment},
                             {depth, rhi::Access::DepthRead, rhi::Stage::Attachment}},
                            [this, hdr, depth](auto &encoder, const auto &resources) {
                                rhi::RenderingDesc desc;
                                desc.colors.push_back({resources.texture(hdr), rhi::LoadOp::Load});
                                desc.depth = rhi::DepthAttachment{resources.texture(depth),
                                                                  rhi::LoadOp::Load};
                                encoder.beginRendering(desc);
                                drawBatches(encoder, currentFrame->batches, false, 1);
                                encoder.endRendering();
                            });
        }
        if (hasOrderedSprites)
            builder.addPass(
                "Ordered Sprites",
                {{shadow, rhi::Access::SampledRead},
                 {hdr, rhi::Access::ColorWrite, rhi::Stage::Attachment},
                 {depth, rhi::Access::DepthRead, rhi::Stage::Attachment}},
                [this, hdr, depth](auto &encoder, const auto &resources) {
                    rhi::RenderingDesc desc;
                    desc.colors.push_back({resources.texture(hdr), rhi::LoadOp::Load});
                    desc.depth = rhi::DepthAttachment{resources.texture(depth), rhi::LoadOp::Load};
                    encoder.beginRendering(desc);
                    drawBatches(encoder, currentFrame->orderedSpriteBatches, false, -1, false);
                    encoder.endRendering();
                });
        if (hasUnoccludedSprites)
            builder.addPass(
                "Unoccluded World Sprites",
                {{shadow, rhi::Access::SampledRead},
                 {hdr, rhi::Access::ColorWrite, rhi::Stage::Attachment},
                 {depth, rhi::Access::DepthRead, rhi::Stage::Attachment}},
                [this, hdr, depth](auto &encoder, const auto &resources) {
                    if (currentFrame->unoccludedSpriteBatches.empty())
                        return;
                    rhi::RenderingDesc desc;
                    desc.colors.push_back({resources.texture(hdr), rhi::LoadOp::Load});
                    desc.depth = rhi::DepthAttachment{resources.texture(depth), rhi::LoadOp::Load};
                    encoder.beginRendering(desc);
                    drawBatches(encoder, currentFrame->unoccludedSpriteBatches, false, -1, false);
                    encoder.endRendering();
                });
        // FXAA 必须在 ToneMap 后的感知颜色空间过滤：普通 RGBA8 保存手工编码值，
        // 采样时不会被 sRGB 纹理解码；关闭时直接沿用原有 ToneMap -> 显示附件路径。
        const auto toneTarget =
            aa == AntiAliasing::FXAA
                ? builder.createTexture({w, h, 1, rhi::Format::Rgba8, "FXAA input (encoded LDR)"})
                : display;
        builder.addPass(
            "ToneMap",
            {{hdr, rhi::Access::SampledRead},
             {depth, rhi::Access::SampledRead},
             {toneTarget, rhi::Access::ColorWrite, rhi::Stage::Attachment}},
            [this, hdr, depth, toneTarget, aa](auto &encoder, const auto &resources) {
                if (!postGroup)
                    postGroup = device.createBindingGroup({2,
                                                           {{.binding = 0,
                                                             .kind = BindingKind::SampledTexture,
                                                             .texture = resources.texture(hdr),
                                                             .sampler = postSampler},
                                                            {.binding = 1,
                                                             .kind = BindingKind::SampledTexture,
                                                             .texture = resources.texture(depth),
                                                             .sampler = postSampler}}});
                rhi::RenderingDesc desc;
                desc.colors.push_back({resources.texture(toneTarget)});
                encoder.beginRendering(desc);
                encoder.bindPipeline(aa == AntiAliasing::FXAA ? postEncodedPipeline : postPipeline);
                encoder.bindGroup(currentSlot->group);
                encoder.bindGroup(postGroup);
                encoder.draw(3);
                encoder.endRendering();
            });
        if (aa == AntiAliasing::FXAA)
            builder.addPass("FXAA",
                            {{toneTarget, rhi::Access::SampledRead},
                             {display, rhi::Access::ColorWrite, rhi::Stage::Attachment}},
                            [this, toneTarget, display](auto &encoder, const auto &resources) {
                                if (!fxaaGroup)
                                    fxaaGroup = device.createBindingGroup(
                                        {2,
                                         {{.binding = 0,
                                           .kind = BindingKind::SampledTexture,
                                           .texture = resources.texture(toneTarget),
                                           .sampler = postSampler}}});
                                rhi::RenderingDesc desc;
                                desc.colors.push_back({resources.texture(display)});
                                encoder.beginRendering(desc);
                                encoder.bindPipeline(fxaaPipeline);
                                encoder.bindGroup(fxaaGroup);
                                encoder.draw(3);
                                encoder.endRendering();
                            });
        if (overlayEnabled)
            builder.addPass(
                "Screen Overlay", {{display, rhi::Access::ColorWrite, rhi::Stage::Attachment}},
                [this, display](auto &encoder, const auto &resources) {
                    rhi::RenderingDesc desc;
                    desc.colors.push_back({resources.texture(display), rhi::LoadOp::Load});
                    encoder.beginRendering(desc);
                    drawOverlay(encoder);
                    encoder.endRendering();
                });
        if (window && !activeTarget)
            builder.addPass(
                "Present", {{display, rhi::Access::Present, rhi::Stage::Transfer}},
                [display](auto &encoder, const auto &resources) {
                    encoder.present(resources.texture(display));
                },
                true);
        builder.exportResource(display);
        graph.emplace(builder.compile());
    }
    std::pair<int, int> framebufferSize() const override {
        if (!window) {
            const auto size = targetSize.load();
            return {int(size >> 32), int(size & 0xffffffffu)};
        }
        window->checkThread();
        int w = 0, h = 0;
        glfwGetFramebufferSize(window->native, &w, &h);
        window->framebufferWidth = w;
        window->framebufferHeight = h;
        return {w, h};
    }
    void resizeOffscreen(uint32_t w, uint32_t h) override {
        if (window)
            throw std::logic_error("resizeOffscreen requires an offscreen renderer");
        detail::validateOffscreen({w, h});
        const auto max = device.capabilities().maxTextureSize;
        if (w > max || h > max)
            throw std::invalid_argument("Offscreen size exceeds device capability");
        targetSize.store((uint64_t(w) << 32) | h);
    }
    RenderStats draw(detail::PreparedFrame frame, int w, int h) override {
        selectGraph();
        const auto start = std::chrono::steady_clock::now();
        auto &slot = slots[nextSlot];
        // 槽位复用前只等待该槽的提交；完成后可安全覆写映射内存及读取旧帧计时。
        if (slot.completion) {
            device.wait(slot.completion);
            RenderStats timing;
            bool valid = true;
            for (uint32_t i = 0; i < slot.passNames.size(); ++i) {
                const auto begin = device.timestampResult(slot.timestamps, i * 2),
                           end = device.timestampResult(slot.timestamps, i * 2 + 1);
                if (!begin || !end || *end < *begin) {
                    valid = false;
                    break;
                }
                const double ms = static_cast<double>(*end - *begin) / 1e6;
                timing.passes.push_back({slot.passNames[i], ms});
                timing.gpuMilliseconds += ms;
            }
            if (valid) {
                timing.gpuTimingValid = true;
                lastTiming = std::move(timing);
            }
        }
        patchParents.clear();
        const auto keepParent = [&](const std::shared_ptr<const detail::TextureData> &t) {
            if (t && t->parentId && !textures.contains(t->id))
                patchParents.insert(t->parentId);
        };
        const auto scan = [&](const auto &batches) {
            for (const auto &b : batches)
                for (const auto &t : {b.material->texture, b.material->normalTexture,
                                      b.material->metallicRoughnessTexture,
                                      b.material->occlusionTexture, b.material->emissiveTexture})
                    keepParent(t);
        };
        scan(frame.batches);
        scan(frame.orderedSpriteBatches);
        scan(frame.unoccludedSpriteBatches);
        scan(frame.shadowBatches);
        for (size_t i = 0; i < frame.overlay.draws().size(); ++i)
            keepParent(detail::OverlayAccess::texture(frame.overlay, i));
        collect();
        ensureCapacity(slot, std::max<size_t>(1, frame.instances.size()));
        const auto resolution = frame.shadows ? frame.shadowResolution : 1;
        if (!graph || width != static_cast<uint32_t>(w) || height != static_cast<uint32_t>(h) ||
            shadowSize != resolution || transparency != frame.transparency ||
            antiAliasing != frame.antiAliasing ||
            orderedSprites != !frame.orderedSpriteBatches.empty() ||
            hasOverlay != !frame.overlay.draws().empty() ||
            (!unoccludedSprites && !frame.unoccludedSpriteBatches.empty()))
            buildGraph(static_cast<uint32_t>(w), static_cast<uint32_t>(h), resolution,
                       frame.transparency, frame.antiAliasing, !frame.orderedSpriteBatches.empty(),
                       !frame.overlay.draws().empty(),
                       unoccludedSprites || !frame.unoccludedSpriteBatches.empty());
        currentFrame = &frame;
        currentSlot = &slot;
        const auto beforeUploads = device.uploadStatistics();
        UploadScope uploads(device, config.batchResourceUploads);
        // 预算不足时保留资源缓存并延后批次；不在图执行中临时上传不完整的材质。
        auto prepare = [&](auto &batches, bool shadow) {
            std::erase_if(batches, [&](const auto &batch) {
                if (prepareResources(batch))
                    return false;
                ++frame.stats.pendingResourceDraws;
                if (shadow)
                    --frame.stats.shadowDrawCalls;
                else {
                    --frame.stats.sceneDrawCalls;
                    --frame.stats.drawCalls;
                    if (batch.material->description.alphaMode == AlphaMode::Blend)
                        --frame.stats.transparentDrawCalls;
                    frame.stats.triangles -=
                        uint64_t(batch.mesh->description.submeshes[batch.submesh].indexCount / 3) *
                        batch.instanceCount;
                }
                return true;
            });
        };
        prepare(frame.batches, false);
        prepare(frame.orderedSpriteBatches, false);
        prepare(frame.unoccludedSpriteBatches, false);
        prepare(frame.shadowBatches, true);
        uploadOverlay(slot, frame.overlay);
        device.upload(slot.uniform, 0, bytes(frame.uniforms));
        uploadInstances(slot, frame);
        device.upload(slot.lights, 0, bytes(frame.lights));
        frame.stats.uploadedBytes +=
            sizeof(frame.uniforms) + frame.lights.size() * sizeof(detail::LightGpu);
        uploads.finish();
        const auto afterUploads = device.uploadStatistics();
        frame.stats.uploadCalls = afterUploads.calls - beforeUploads.calls;
        frame.stats.uploadSubmissions = afterUploads.submissions - beforeUploads.submissions;
        frame.stats.uploadWaits = afterUploads.waits - beforeUploads.waits;
        frame.stats.stagingAllocations =
            afterUploads.stagingAllocations - beforeUploads.stagingAllocations;
        frame.stats.stagingReuses = afterUploads.stagingReuses - beforeUploads.stagingReuses;
        slot.completion = graph->execute(device, slot.timestamps);
        slot.passNames.clear();
        for (const auto &pass : graph->passes())
            slot.passNames.push_back(pass.name);
        nextSlot = (nextSlot + 1) % slots.size();
        frame.stats.transparencyResolveDrawCalls =
            frame.transparency == TransparencyMode::WeightedBlendedOIT ? 1 : 0;
        frame.stats.antiAliasingDrawCalls = frame.antiAliasing == AntiAliasing::FXAA ? 1 : 0;
        frame.stats.drawCalls += frame.stats.shadowDrawCalls + 1 +
                                 frame.stats.transparencyResolveDrawCalls +
                                 frame.stats.antiAliasingDrawCalls;
        frame.stats.gpuMeshes = meshes.size();
        frame.stats.gpuTextures = textures.size();
        frame.stats.retiredResources = device.pendingDeletions();
        frame.stats.gpuTimingValid = lastTiming.gpuTimingValid;
        frame.stats.gpuMilliseconds = lastTiming.gpuMilliseconds;
        frame.stats.passes = lastTiming.passes;
        frame.stats.submitMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        currentFrame = nullptr;
        currentSlot = nullptr;
        return frame.stats;
    }
    DeviceInfo deviceInfo() const override {
        return info;
    }
    RenderTexture createRenderTexture(const OffscreenConfig &desc) override {
        detail::validateOffscreen(desc);
        if (desc.width > device.capabilities().maxTextureSize ||
            desc.height > device.capabilities().maxTextureSize)
            throw std::invalid_argument("Render texture exceeds device limit");
        auto t = detail::RenderTextureAccess::create(deviceId, detail::uniqueId(), desc.width,
                                                     desc.height);
        auto state = detail::RenderTextureAccess::state(t);
        auto handle = device.createTexture(
            {desc.width, desc.height, 1, rhi::Format::Rgba8Srgb, "Camera render texture"});
        try {
            std::vector<uint8_t> black(size_t(desc.width) * desc.height * 4, 0);
            for (size_t i = 3; i < black.size(); i += 4)
                black[i] = 255;
            device.upload(handle, black);
            targets.emplace(state->id, TargetGpu{handle, state, black.size()});
        } catch (...) {
            device.destroy(handle);
            throw;
        }
        return t;
    }
    RenderStats drawToTexture(detail::PreparedFrame frame, const RenderTexture &t) override {
        auto &destination = target(t);
        auto state = detail::RenderTextureAccess::state(t);
        activeTarget = state->id;
        struct Reset {
            uint64_t &value;
            ~Reset() {
                value = 0;
            }
        } reset{activeTarget};
        auto stats = draw(std::move(frame), int(state->width), int(state->height));
        rhi::CommandEncoder copy;
        const rhi::ResourceBarrier before[] = {{.before = rhi::Access::ColorWrite,
                                                .after = rhi::Access::TransferRead,
                                                .texture = true,
                                                .image = output},
                                               {.before = rhi::Access::SampledRead,
                                                .after = rhi::Access::TransferWrite,
                                                .texture = true,
                                                .image = destination.texture}};
        copy.barrier(before);
        copy.copyTexture(output, destination.texture,
                         {.width = state->width, .height = state->height});
        const rhi::ResourceBarrier after[] = {{.before = rhi::Access::TransferWrite,
                                               .after = rhi::Access::SampledRead,
                                               .texture = true,
                                               .image = destination.texture}};
        copy.barrier(after);
        auto done = device.submit(copy.finish());
        slots[(nextSlot + slots.size() - 1) % slots.size()].completion = done;
        return stats;
    }
    RendererMemoryStats memoryStatistics() const override {
        RendererMemoryStats m;
        m.pooledUploadBytes = device.uploadStatistics().pooledStagingBytes;
        const auto native = device.statistics();
        m.nativeMemoryDetailsAvailable = native.available;
        m.nativeMemoryBlocks = native.memoryBlocks;
        m.nativeMemoryAllocations = native.memoryAllocations;
        m.nativeMemoryBlockBytes = native.memoryBlockBytes;
        m.nativeMemoryAllocationBytes = native.memoryAllocationBytes;
        m.cachedMeshes = meshes.size();
        m.cachedTextures = textures.size();
        m.cachedMaterials = materials.size();
        for (const auto &[id, a] : meshes) {
            (void)id;
            m.meshBytes += a.bytes;
        }
        for (const auto &[id, a] : textures) {
            (void)id;
            m.textureBytes += a.bytes;
            if (a.compressed)
                m.compressedTextureBytes += a.bytes;
            if (a.layers > 1)
                m.textureArrayLayers += a.layers;
        }
        m.materialBytes = materials.size() * sizeof(detail::MaterialGpu);
        for (const auto &slot : slots) {
            m.frameBufferBytes += sizeof(detail::FrameUniforms) + 64 * sizeof(detail::LightGpu) +
                                  slot.capacity * sizeof(detail::InstanceGpu) +
                                  slot.overlayVertexCapacity + slot.overlayIndexCapacity;
            m.instanceMirrorBytes += slot.instanceMirror.capacity() * sizeof(detail::InstanceGpu);
        }
        if (graph)
            m.renderTargetBytes =
                uint64_t(width) * height *
                    (16 + (antiAliasing == AntiAliasing::FXAA ? 4 : 0) +
                     (transparency == TransparencyMode::WeightedBlendedOIT ? 20 : 0)) +
                uint64_t(shadowSize) * shadowSize * 4;
        for (const auto &[id, t] : targets) {
            (void)id;
            m.renderTargetBytes += t.bytes;
        }
        for (const auto &[id, g] : targetGraphs) {
            (void)id;
            if (g.graph)
                m.renderTargetBytes +=
                    uint64_t(g.width) * g.height *
                        (16 + (g.antiAliasing == AntiAliasing::FXAA ? 4 : 0) +
                         (g.transparency == TransparencyMode::WeightedBlendedOIT ? 20 : 0)) +
                    uint64_t(g.shadowSize) * g.shadowSize * 4;
        }
        return m;
    }
    uint32_t debugErrorCount() const override {
        return device.debugErrorCount();
    }
    Image readback() override {
        if (!output)
            throw std::logic_error("Render before readback");
        auto image = device.readTexture(output);
        return {image.width, image.height, std::move(image.pixels)};
    }
    void waitIdle() override {
        device.waitIdle();
        patchParents.clear();
        collect();
        device.waitIdle();
    }
    void present() override {
        if (!window)
            throw std::logic_error("Offscreen renderer has no window to present");
        detail::RhiAccess::backend(device).swap();
    }
    void releaseContext() override {
        detail::RhiAccess::backend(device).releaseContext();
    }
    void acquireContext() override {
        detail::RhiAccess::backend(device).acquireContext();
    }
    std::shared_ptr<void> contextLifetime() const override {
        return window;
    }
    void setThreaded(bool enabled) override {
        if (window)
            window->rendererThreaded = enabled;
    }
};
} // namespace
namespace detail {
std::unique_ptr<Backend> makeRenderBackend(rhi::Device device, Window &window,
                                           const RendererConfig &config) {
    return std::make_unique<RhiRenderBackend>(std::move(device), WindowAccess::state(window),
                                              config);
}
std::unique_ptr<Backend> makeOffscreenRenderBackend(rhi::Device device,
                                                    const OffscreenConfig &target,
                                                    const RendererConfig &config) {
    return std::make_unique<RhiRenderBackend>(std::move(device), nullptr, config, target);
}
} // namespace detail
} // namespace renderer
