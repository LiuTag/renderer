#include "../../internal/render_backend.hpp"
#include "../../internal/rhi_backend.hpp"
#include "../../internal/window.hpp"
#include "portable_shaders.hpp"
#include "renderer/render_graph.hpp"
#include "renderer/renderer.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <tuple>
#include <unordered_map>

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
};
struct TextureGpu {
    rhi::TextureHandle texture;
    rhi::SamplerHandle sampler;
    std::weak_ptr<const detail::TextureData> owner;
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
};
struct RhiRenderBackend final : detail::Backend {
    rhi::Device device;
    RendererConfig config;
    std::shared_ptr<detail::WindowState> window;
    DeviceInfo info;
    ShaderCompiler compiler;
    ShaderArtifact sceneVertex, sceneFragment, oitFragment, shadowVertex, shadowFragment,
        postVertex, postFragment;
    std::map<std::tuple<bool, bool, bool, bool, bool>, rhi::PipelineHandle> pipelines;
    rhi::PipelineHandle postPipeline, postEncodedPipeline, fxaaPipeline, oitResolvePipeline;
    rhi::SamplerHandle sampler, shadowSampler, postSampler;
    rhi::TextureHandle white, normal, output;
    rhi::BindingGroupHandle postGroup, fxaaGroup, oitGroup;
    uint32_t width = 0, height = 0, shadowSize = 0;
    TransparencyMode transparency = TransparencyMode::SortedAlpha;
    AntiAliasing antiAliasing = AntiAliasing::None;
    bool orderedSprites = false;
    std::optional<CompiledRenderGraph> graph;
    std::vector<FrameSlot> slots;
    size_t nextSlot = 0;
    std::unordered_map<uint64_t, MeshGpu> meshes;
    std::unordered_map<uint64_t, TextureGpu> textures;
    std::unordered_map<uint64_t, MaterialGpu> materials;
    detail::PreparedFrame *currentFrame = nullptr;
    FrameSlot *currentSlot = nullptr;
    RenderStats lastTiming;
    explicit RhiRenderBackend(rhi::Device input, Window &w, const RendererConfig &c)
        : device(std::move(input)), config(c), window(detail::WindowAccess::state(w)) {
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
        device.waitIdle();
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
        return meshes.emplace(asset->id, MeshGpu{vertex, index, asset}).first->second;
    }
    rhi::TextureHandle texture(const std::shared_ptr<const detail::TextureData> &asset,
                               rhi::TextureHandle fallback) {
        if (!asset)
            return fallback;
        if (auto it = textures.find(asset->id); it != textures.end())
            return it->second.texture;
        const auto &d = asset->description;
        const auto levels =
            1 + static_cast<uint32_t>(std::floor(std::log2(std::max(d.width, d.height))));
        auto handle = device.createTexture({d.width, d.height, levels,
                                            d.srgb ? rhi::Format::Rgba8Srgb : rhi::Format::Rgba8,
                                            "Asset texture"});
        device.upload(handle, d.pixels);
        device.generateMipmaps(handle);
        rhi::SamplerDesc sampling;
        sampling.minFilter = static_cast<rhi::Filter>(d.sampling.minFilter);
        sampling.magFilter = static_cast<rhi::Filter>(d.sampling.magFilter);
        sampling.mipFilter = static_cast<rhi::Filter>(d.sampling.mipFilter);
        sampling.addressU = static_cast<rhi::AddressMode>(d.sampling.addressU);
        sampling.addressV = static_cast<rhi::AddressMode>(d.sampling.addressV);
        sampling.mipmapped = d.sampling.mipmapped;
        textures.emplace(asset->id, TextureGpu{handle, device.createSampler(sampling), asset});
        currentFrame->stats.uploadedBytes += d.pixels.size();
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
            {d.unlit ? 1.0f : 0.0f, static_cast<float>(d.alphaMode), d.occlusionStrength, 0}};
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
        return materials.emplace(asset->id, MaterialGpu{uniform, handle, asset}).first->second;
    }
    rhi::PipelineHandle pipeline(const detail::DrawBatch &batch, bool shadow,
                                 bool weightedOit = false) {
        const auto &mat = batch.material->description;
        const bool alpha = !shadow && mat.alphaMode == AlphaMode::Blend;
        const bool oit = alpha && weightedOit;
        auto key = std::tuple{shadow, alpha, oit, mat.doubleSided, batch.mirrored};
        if (auto it = pipelines.find(key); it != pipelines.end())
            return it->second;
        rhi::GraphicsPipelineDesc desc;
        desc.vertex = shadow ? shadowVertex : sceneVertex;
        desc.fragment = shadow ? shadowFragment : (oit ? oitFragment : sceneFragment);
        desc.colorFormats = shadow ? std::vector<rhi::Format>{}
                                   : std::vector<rhi::Format>{rhi::Format::Rgba16Float};
        desc.depthFormat = rhi::Format::Depth32Float;
        desc.depthWrite = !alpha;
        desc.blend = alpha ? rhi::BlendMode::Alpha : rhi::BlendMode::Opaque;
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
        auto handle = device.createGraphicsPipeline(desc);
        pipelines.emplace(key, handle);
        return handle;
    }
    void collect() {
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
            if (i->second.owner.expired()) {
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
                    AntiAliasing aa, bool hasOrderedSprites) {
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
        // FXAA 必须在 ToneMap 后的感知颜色空间过滤：普通 RGBA8 保存手工编码值，
        // 采样时不会被 sRGB 纹理解码；关闭时直接沿用原有 ToneMap -> 显示附件路径。
        const auto toneTarget =
            aa == AntiAliasing::FXAA
                ? builder.createTexture({w, h, 1, rhi::Format::Rgba8, "FXAA input (encoded LDR)"})
                : display;
        builder.addPass(
            "ToneMap",
            {{hdr, rhi::Access::SampledRead},
             {toneTarget, rhi::Access::ColorWrite, rhi::Stage::Attachment}},
            [this, hdr, toneTarget, aa](auto &encoder, const auto &resources) {
                if (!postGroup)
                    postGroup = device.createBindingGroup({2,
                                                           {{.binding = 0,
                                                             .kind = BindingKind::SampledTexture,
                                                             .texture = resources.texture(hdr),
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
        window->checkThread();
        int w = 0, h = 0;
        glfwGetFramebufferSize(window->native, &w, &h);
        window->framebufferWidth = w;
        window->framebufferHeight = h;
        return {w, h};
    }
    RenderStats draw(detail::PreparedFrame frame, int w, int h) override {
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
        collect();
        ensureCapacity(slot, std::max<size_t>(1, frame.instances.size()));
        const auto resolution = frame.shadows ? frame.shadowResolution : 1;
        if (!graph || width != static_cast<uint32_t>(w) || height != static_cast<uint32_t>(h) ||
            shadowSize != resolution || transparency != frame.transparency ||
            antiAliasing != frame.antiAliasing ||
            orderedSprites != !frame.orderedSpriteBatches.empty())
            buildGraph(static_cast<uint32_t>(w), static_cast<uint32_t>(h), resolution,
                       frame.transparency, frame.antiAliasing, !frame.orderedSpriteBatches.empty());
        currentFrame = &frame;
        currentSlot = &slot;
        device.upload(slot.uniform, 0, bytes(frame.uniforms));
        device.upload(slot.instances, 0, bytes(frame.instances));
        device.upload(slot.lights, 0, bytes(frame.lights));
        frame.stats.uploadedBytes += sizeof(frame.uniforms) +
                                     frame.instances.size() * sizeof(detail::InstanceGpu) +
                                     frame.lights.size() * sizeof(detail::LightGpu);
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
        collect();
        device.waitIdle();
    }
    void present() override {
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
        window->rendererThreaded = enabled;
    }
};
} // namespace
namespace detail {
std::unique_ptr<Backend> makeRenderBackend(rhi::Device device, Window &window,
                                           const RendererConfig &config) {
    return std::make_unique<RhiRenderBackend>(std::move(device), window, config);
}
} // namespace detail
} // namespace renderer
