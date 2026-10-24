#include "../../internal/pool.hpp"
#include "../../internal/rhi_backend.hpp"
#include "../../internal/window.hpp"
#include "renderer/rhi.hpp"
#include "renderer/window.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <glad/gl.h>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <tuple>

namespace renderer::rhi {
namespace {
void checked(const char *operation) {
    const auto e = glGetError();
    if (e != GL_NO_ERROR)
        throw std::runtime_error(std::string(operation) + ": OpenGL error " + std::to_string(e));
}
GLenum format(Format f) {
    switch (f) {
    case Format::Rgba8:
        return GL_RGBA8;
    case Format::Rgba8Srgb:
        return GL_SRGB8_ALPHA8;
    case Format::Rgba16Float:
        return GL_RGBA16F;
    case Format::Rgba32Float:
        return GL_RGBA32F;
    case Format::R32Float:
        return GL_R32F;
    case Format::Depth32Float:
        return GL_DEPTH_COMPONENT32F;
    }
    throw std::invalid_argument("Invalid format");
}
GLenum compare(Compare c) {
    switch (c) {
    case Compare::Never:
        return GL_NEVER;
    case Compare::Less:
        return GL_LESS;
    case Compare::LessEqual:
        return GL_LEQUAL;
    case Compare::Equal:
        return GL_EQUAL;
    case Compare::Greater:
        return GL_GREATER;
    case Compare::Always:
        return GL_ALWAYS;
    }
    throw std::invalid_argument("Invalid comparison");
}
GLenum address(AddressMode a) {
    switch (a) {
    case AddressMode::Repeat:
        return GL_REPEAT;
    case AddressMode::Clamp:
        return GL_CLAMP_TO_EDGE;
    case AddressMode::Mirror:
        return GL_MIRRORED_REPEAT;
    case AddressMode::Border:
        return GL_CLAMP_TO_BORDER;
    }
    throw std::invalid_argument("Invalid sampler address");
}
GLenum blendFactor(BlendFactor factor) {
    switch (factor) {
    case BlendFactor::Zero:
        return GL_ZERO;
    case BlendFactor::One:
        return GL_ONE;
    case BlendFactor::SourceColor:
        return GL_SRC_COLOR;
    case BlendFactor::OneMinusSourceColor:
        return GL_ONE_MINUS_SRC_COLOR;
    case BlendFactor::SourceAlpha:
        return GL_SRC_ALPHA;
    case BlendFactor::OneMinusSourceAlpha:
        return GL_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DestinationColor:
        return GL_DST_COLOR;
    case BlendFactor::OneMinusDestinationColor:
        return GL_ONE_MINUS_DST_COLOR;
    case BlendFactor::DestinationAlpha:
        return GL_DST_ALPHA;
    case BlendFactor::OneMinusDestinationAlpha:
        return GL_ONE_MINUS_DST_ALPHA;
    }
    throw std::invalid_argument("Invalid blend factor");
}
void range(size_t size, size_t offset, size_t bytes) {
    if (offset > size || bytes > size - offset)
        throw std::out_of_range("Buffer range out of bounds");
}
// 原生对象的共享所有权：纹理视图/程序变体可引用底层存储，提交标记控制延迟回收。
struct Native {
    enum Kind { Buffer, Texture, Sampler, Pipeline, Queries, Framebuffer } kind;
    GLuint id = 0, vao = 0;
    void *mapping = nullptr;
    uint64_t lastUse = 0;
    std::vector<GLuint> queries;
    std::shared_ptr<Native> parent;
    uint32_t baseMip = 0;
    std::shared_ptr<std::vector<std::weak_ptr<Native>>> pipelines;
    std::map<uint32_t, GLuint> vertexSources;
    GLuint indexSource = 0;
    explicit Native(Kind k) : kind(k) {}
    ~Native() {
        if (kind == Buffer && id) {
            if (pipelines)
                for (auto &weak : *pipelines)
                    if (auto p = weak.lock()) {
                        for (auto &[binding, source] : p->vertexSources)
                            if (source == id) {
                                glVertexArrayVertexBuffer(p->vao, binding, 0, 0, 0);
                                source = 0;
                            }
                        if (p->indexSource == id) {
                            glVertexArrayElementBuffer(p->vao, 0);
                            p->indexSource = 0;
                        }
                    }
            if (mapping)
                glUnmapNamedBuffer(id);
            glDeleteBuffers(1, &id);
        } else if (kind == Texture && id)
            glDeleteTextures(1, &id);
        else if (kind == Sampler && id)
            glDeleteSamplers(1, &id);
        else if (kind == Pipeline) {
            if (vao)
                glDeleteVertexArrays(1, &vao);
            if (id && !parent)
                glDeleteProgram(id);
        } else if (kind == Queries && !queries.empty())
            glDeleteQueries(static_cast<GLsizei>(queries.size()), queries.data());
        else if (kind == Framebuffer && id)
            glDeleteFramebuffers(1, &id);
    }
};
struct Buffer {
    std::shared_ptr<Native> native;
    BufferDesc desc;
};
struct Texture {
    std::shared_ptr<Native> native;
    TextureDesc desc;
};
struct Sampler {
    std::shared_ptr<Native> native;
    SamplerDesc desc;
};
struct Slot {
    ShaderBinding binding;
    uint32_t physical;
};
struct Pipeline {
    std::shared_ptr<Native> native;
    bool compute = false;
    GraphicsPipelineDesc desc;
    std::vector<Slot> slots;
};
struct Queries {
    std::shared_ptr<Native> native;
    std::vector<bool> written;
};
struct Fbo {
    std::shared_ptr<Native> native;
    std::vector<TextureHandle> attachments;
    int width = 0, height = 0;
};
} // namespace
struct OpenGLDevice final : detail::DeviceBackend {
    std::shared_ptr<renderer::detail::WindowState> window;
    // 成员按声明的逆序析构；资源池先清空，再解绑上下文，最后释放窗口。
    struct Detach {
        bool enabled = false;
        ~Detach() {
            if (enabled)
                glfwMakeContextCurrent(nullptr);
        }
    } detach;
    std::thread::id thread = std::this_thread::get_id();
    uint64_t owner = renderer::detail::uniqueId(), submitted = 0, completed = 0;
    DeviceCaps caps;
    std::atomic<uint32_t> errors{0};
    bool loaded = false;
    std::shared_ptr<std::vector<std::weak_ptr<Native>>> registry =
        std::make_shared<std::vector<std::weak_ptr<Native>>>();
    renderer::detail::Pool<Buffer, BufferTag> buffers;
    renderer::detail::Pool<Texture, TextureTag> textures;
    renderer::detail::Pool<Sampler, SamplerTag> samplers;
    renderer::detail::Pool<Pipeline, PipelineTag> pipelines;
    renderer::detail::Pool<BindingGroupDesc, GroupTag> groups;
    renderer::detail::Pool<Queries, QueryTag> queries;
    std::map<std::string, Fbo> framebuffers;
    std::vector<std::shared_ptr<Native>> retired;
    std::deque<std::pair<uint64_t, GLsync>> fences;
    ShaderCompiler compiler;
    std::map<std::string, std::weak_ptr<Native>> programCache;
    OpenGLDevice(std::shared_ptr<renderer::detail::WindowState> state) : window(std::move(state)) {
        window->checkThread();
        if (window->graphicsApi != GraphicsApi::OpenGL)
            throw std::invalid_argument("OpenGL requires an OpenGL window");
        if (window->rendererAttached)
            throw std::logic_error("Window already has a graphics device");
        window->rendererAttached = true;
    }
    ~OpenGLDevice() override {
        if (loaded) {
            glfwMakeContextCurrent(window->native);
            glFinish();
            glDebugMessageCallback(nullptr, nullptr);
            for (auto [serial, f] : fences) {
                (void)serial;
                glDeleteSync(f);
            }
            framebuffers.clear();
            retired.clear();
        }
        window->rendererAttached = false;
    }
    void check() const {
        if (std::this_thread::get_id() != thread)
            throw std::logic_error("RHI operation must run on the context owner thread");
    }
    void current() const {
        check();
        glfwMakeContextCurrent(window->native);
    }
    static void GLAD_API_PTR debug(GLenum, GLenum type, GLuint, GLenum severity, GLsizei,
                                   const GLchar *text, const void *user) {
        if (type == GL_DEBUG_TYPE_ERROR || severity == GL_DEBUG_SEVERITY_HIGH) {
            ++static_cast<OpenGLDevice *>(const_cast<void *>(user))->errors;
            std::cerr << "OpenGL: " << text << '\n';
        }
    }
    void init(bool enableDebug) {
        current();
        static std::once_flag loader;
        static bool ready = false;
        std::call_once(loader,
                       [] { ready = gladLoadGL(glfwGetProcAddress) != 0 && GLAD_GL_VERSION_4_5; });
        if (!ready)
            throw std::runtime_error("OpenGL 4.5 required");
        loaded = true;
        detach.enabled = true;
        caps.backend = "OpenGL";
        caps.vendor = reinterpret_cast<const char *>(glGetString(GL_VENDOR));
        caps.device = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
        caps.version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
        const auto query = [](GLenum name) {
            GLint value = 0;
            glGetIntegerv(name, &value);
            return static_cast<uint32_t>(value);
        };
        caps.maxTextureSize = query(GL_MAX_TEXTURE_SIZE);
        caps.maxColorAttachments = query(GL_MAX_COLOR_ATTACHMENTS);
        caps.maxVertexAttributes = query(GL_MAX_VERTEX_ATTRIBS);
        caps.maxVertexStride = query(GL_MAX_VERTEX_ATTRIB_STRIDE);
        caps.maxSampledTextures =
            std::min(query(GL_MAX_TEXTURE_IMAGE_UNITS), query(GL_MAX_COMPUTE_TEXTURE_IMAGE_UNITS));
        caps.maxUniformBindings = query(GL_MAX_UNIFORM_BUFFER_BINDINGS);
        caps.maxStorageBindings = query(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS);
        caps.maxImageBindings = query(GL_MAX_IMAGE_UNITS);
        caps.uniformAlignment = query(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT);
        caps.storageAlignment = query(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT);
        caps.maxComputeInvocations = query(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS);
        if (enableDebug) {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(debug, this);
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0,
                                  nullptr, GL_FALSE);
        }
        glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
        glDisable(GL_DITHER);
        checked("RHI initialization");
    }
    DeviceCaps capabilities() const override {
        check();
        return caps;
    }
    FormatFeatures formatFeatures(Format f) const override {
        current();
        GLint supported = 0, renderable = 0, store = 0;
        glGetInternalformativ(GL_TEXTURE_2D, format(f), GL_INTERNALFORMAT_SUPPORTED, 1, &supported);
        glGetInternalformativ(GL_TEXTURE_2D, format(f), GL_FRAMEBUFFER_RENDERABLE, 1, &renderable);
        if (f != Format::Depth32Float && f != Format::Rgba8Srgb)
            glGetInternalformativ(GL_TEXTURE_2D, format(f), GL_SHADER_IMAGE_STORE, 1, &store);
        return {supported != 0, f != Format::Depth32Float && renderable != GL_NONE,
                f == Format::Depth32Float && renderable != GL_NONE, store != GL_NONE};
    }
    void label(GLenum type, GLuint id, const std::string &name) {
        if (!name.empty())
            glObjectLabel(type, id, static_cast<GLsizei>(std::min<size_t>(name.size(), 240)),
                          name.c_str());
    }
    BufferHandle createBuffer(const BufferDesc &d, std::span<const std::byte> initial) override {
        current();
        if (d.size == 0 || d.size > static_cast<size_t>(std::numeric_limits<GLsizeiptr>::max()) ||
            initial.size() > d.size)
            throw std::invalid_argument("Invalid buffer size");
        auto n = std::make_shared<Native>(Native::Buffer);
        n->pipelines = registry;
        glCreateBuffers(1, &n->id);
        GLbitfield flags = GL_DYNAMIC_STORAGE_BIT;
        if (d.memory == Memory::Upload)
            flags |= GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        else if (d.memory == Memory::Readback)
            flags |= GL_MAP_READ_BIT;
        glNamedBufferStorage(n->id, static_cast<GLsizeiptr>(d.size),
                             initial.size() == d.size ? initial.data() : nullptr, flags);
        if (!initial.empty() && initial.size() != d.size)
            glNamedBufferSubData(n->id, 0, static_cast<GLsizeiptr>(initial.size()), initial.data());
        if (d.memory == Memory::Upload)
            n->mapping = glMapNamedBufferRange(n->id, 0, static_cast<GLsizeiptr>(d.size),
                                               GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT |
                                                   GL_MAP_COHERENT_BIT);
        checked("Buffer creation");
        if (d.memory == Memory::Upload && !n->mapping)
            throw std::runtime_error("Upload buffer mapping failed");
        label(GL_BUFFER, n->id, d.name);
        return buffers.create(Buffer{std::move(n), d});
    }
    TextureHandle createTexture(const TextureDesc &d) override {
        current();
        if (!d.width || !d.height || d.width > caps.maxTextureSize ||
            d.height > caps.maxTextureSize || !d.mipLevels ||
            d.mipLevels >
                1 + static_cast<uint32_t>(std::floor(std::log2(std::max(d.width, d.height)))))
            throw std::invalid_argument("Invalid texture dimensions/mips");
        auto n = std::make_shared<Native>(Native::Texture);
        glCreateTextures(GL_TEXTURE_2D, 1, &n->id);
        glTextureStorage2D(n->id, static_cast<GLsizei>(d.mipLevels), format(d.format),
                           static_cast<GLsizei>(d.width), static_cast<GLsizei>(d.height));
        label(GL_TEXTURE, n->id, d.name);
        checked("Texture creation");
        return textures.create(Texture{std::move(n), d});
    }
    TextureHandle createTextureView(TextureHandle h, uint32_t first, uint32_t count) override {
        current();
        auto &t = textures.get(h);
        if (!count || first >= t.desc.mipLevels || count > t.desc.mipLevels - first)
            throw std::invalid_argument("Invalid view mip range");
        auto n = std::make_shared<Native>(Native::Texture);
        n->parent = t.native;
        n->baseMip = t.native->baseMip + first;
        glGenTextures(1, &n->id);
        glTextureView(n->id, GL_TEXTURE_2D, t.native->id, format(t.desc.format), first, count, 0,
                      1);
        auto d = t.desc;
        d.width = std::max(1u, d.width >> first);
        d.height = std::max(1u, d.height >> first);
        d.mipLevels = count;
        checked("Texture view creation");
        return textures.create(Texture{std::move(n), d});
    }
    SamplerHandle createSampler(const SamplerDesc &d) override {
        current();
        auto n = std::make_shared<Native>(Native::Sampler);
        glCreateSamplers(1, &n->id);
        glSamplerParameteri(
            n->id, GL_TEXTURE_MIN_FILTER,
            d.mipmapped ? (d.minFilter == Filter::Linear
                               ? (d.mipFilter == Filter::Linear ? GL_LINEAR_MIPMAP_LINEAR
                                                                : GL_LINEAR_MIPMAP_NEAREST)
                               : (d.mipFilter == Filter::Linear ? GL_NEAREST_MIPMAP_LINEAR
                                                                : GL_NEAREST_MIPMAP_NEAREST))
                        : (d.minFilter == Filter::Linear ? GL_LINEAR : GL_NEAREST));
        glSamplerParameteri(n->id, GL_TEXTURE_MAG_FILTER,
                            d.magFilter == Filter::Linear ? GL_LINEAR : GL_NEAREST);
        glSamplerParameteri(n->id, GL_TEXTURE_WRAP_S, address(d.addressU));
        glSamplerParameteri(n->id, GL_TEXTURE_WRAP_T, address(d.addressV));
        glSamplerParameterfv(n->id, GL_TEXTURE_BORDER_COLOR, &d.borderColor.x);
        glSamplerParameteri(n->id, GL_TEXTURE_COMPARE_MODE,
                            d.compare ? GL_COMPARE_REF_TO_TEXTURE : GL_NONE);
        glSamplerParameteri(n->id, GL_TEXTURE_COMPARE_FUNC, compare(d.compareFunction));
        checked("Sampler creation");
        return samplers.create(Sampler{std::move(n), d});
    }
    std::vector<Slot> layout(std::span<const ShaderArtifact> shaders) {
        // 合并阶段反射后，分别在 UBO、SSBO、采样纹理、存储图像命名空间紧凑编号。
        std::map<std::pair<uint32_t, uint32_t>, ShaderBinding> bindings;
        for (const auto &shader : shaders)
            for (const auto &b : shader.bindings) {
                if (b.arraySize != 1)
                    throw std::invalid_argument("Descriptor arrays are outside this RHI profile");
                auto [it, inserted] = bindings.emplace(std::pair{b.group, b.binding}, b);
                if (!inserted && (it->second.kind != b.kind || it->second.byteSize != b.byteSize))
                    throw std::invalid_argument("Shader binding layouts disagree between stages");
            }
        uint32_t counts[4]{};
        const uint32_t maximum[] = {caps.maxUniformBindings, caps.maxStorageBindings,
                                    caps.maxSampledTextures, caps.maxImageBindings};
        std::vector<Slot> slots;
        for (auto &[key, binding] : bindings) {
            (void)key;
            const auto kind = static_cast<size_t>(binding.kind);
            if (counts[kind] >= maximum[kind])
                throw std::invalid_argument("Pipeline exceeds device binding limits");
            slots.push_back({binding, counts[kind]++});
        }
        return slots;
    }
    void link(Pipeline &pipeline, std::span<const ShaderArtifact> shaders) {
        pipeline.slots = layout(shaders);
        std::vector<BindingRemap> remaps;
        for (auto &s : pipeline.slots)
            remaps.push_back({s.binding.group, s.binding.binding, s.physical});
        std::string key;
        for (const auto &artifact : shaders) {
            key += std::to_string(artifact.spirv.size()) + ":";
            key.append(reinterpret_cast<const char *>(artifact.spirv.data()),
                       artifact.spirv.size() * sizeof(uint32_t));
        }
        if (auto found = programCache.find(key); found != programCache.end())
            if (auto program = found->second.lock()) {
                auto native = std::make_shared<Native>(Native::Pipeline);
                native->id = program->id;
                native->parent = program;
                pipeline.native = std::move(native);
                return;
            }
        auto n = std::make_shared<Native>(Native::Pipeline);
        n->id = glCreateProgram();
        std::vector<GLuint> stages;
        try {
            for (const auto &artifact : shaders) {
                auto source = compiler.toOpenGL(artifact, remaps);
                const char *text = source.c_str();
                const GLenum type = artifact.stage == ShaderStage::Vertex     ? GL_VERTEX_SHADER
                                    : artifact.stage == ShaderStage::Fragment ? GL_FRAGMENT_SHADER
                                                                              : GL_COMPUTE_SHADER;
                const GLuint shader = glCreateShader(type);
                stages.push_back(shader);
                glShaderSource(shader, 1, &text, nullptr);
                glCompileShader(shader);
                GLint ok = 0;
                glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
                if (!ok) {
                    GLint length = 0;
                    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
                    std::string log(static_cast<size_t>(std::max(1, length)), '\0');
                    glGetShaderInfoLog(shader, length, nullptr, log.data());
                    throw std::runtime_error(artifact.sourceName + ": " + log);
                }
                glAttachShader(n->id, shader);
            }
            glLinkProgram(n->id);
            GLint ok = 0;
            glGetProgramiv(n->id, GL_LINK_STATUS, &ok);
            if (!ok) {
                GLint length = 0;
                glGetProgramiv(n->id, GL_INFO_LOG_LENGTH, &length);
                std::string log(static_cast<size_t>(std::max(1, length)), '\0');
                glGetProgramInfoLog(n->id, length, nullptr, log.data());
                throw std::runtime_error("Pipeline link: " + log);
            }
            for (auto s : stages) {
                glDetachShader(n->id, s);
                glDeleteShader(s);
            }
        } catch (...) {
            for (auto s : stages)
                glDeleteShader(s);
            throw;
        }
        programCache[key] = n;
        auto native = std::make_shared<Native>(Native::Pipeline);
        native->id = n->id;
        native->parent = std::move(n);
        pipeline.native = std::move(native);
        checked("Pipeline link");
    }
    PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc &d) override {
        current();
        if (!std::isfinite(d.depthBiasConstant) || !std::isfinite(d.depthBiasSlope))
            throw std::invalid_argument("Depth bias must be finite");
        if (d.vertex.stage != ShaderStage::Vertex || d.fragment.stage != ShaderStage::Fragment ||
            d.colorFormats.size() > caps.maxColorAttachments)
            throw std::invalid_argument("Invalid graphics pipeline stages/attachments");
        for (auto f : d.colorFormats)
            if (f == Format::Depth32Float)
                throw std::invalid_argument("Depth format used as color");
        if (d.depthFormat && *d.depthFormat != Format::Depth32Float)
            throw std::invalid_argument("Invalid depth format");
        if (!d.colorBlends.empty() && d.colorBlends.size() != d.colorFormats.size())
            throw std::invalid_argument("Color blend states must match color attachments");
        for (const auto &blend : d.colorBlends) {
            (void)blendFactor(blend.sourceColor);
            (void)blendFactor(blend.destinationColor);
            (void)blendFactor(blend.sourceAlpha);
            (void)blendFactor(blend.destinationAlpha);
        }
        Pipeline p;
        p.desc = d;
        const ShaderArtifact stages[] = {d.vertex, d.fragment};
        link(p, stages);
        glCreateVertexArrays(1, &p.native->vao);
        registry->push_back(p.native);
        std::set<uint32_t> locations, bindings;
        for (const auto &b : d.vertexBindings) {
            if (b.binding >= caps.maxVertexAttributes || !b.stride ||
                b.stride > caps.maxVertexStride || !bindings.insert(b.binding).second)
                throw std::invalid_argument("Invalid vertex binding");
            glVertexArrayBindingDivisor(p.native->vao, b.binding, b.instanced ? 1 : 0);
        }
        for (const auto &a : d.attributes) {
            const auto b = std::find_if(d.vertexBindings.begin(), d.vertexBindings.end(),
                                        [&](const auto &v) { return v.binding == a.binding; });
            if (a.location >= caps.maxVertexAttributes || !a.components || a.components > 4 ||
                b == d.vertexBindings.end() || a.offset + a.components * 4 > b->stride ||
                !locations.insert(a.location).second)
                throw std::invalid_argument("Invalid vertex attribute");
            glEnableVertexArrayAttrib(p.native->vao, a.location);
            glVertexArrayAttribFormat(p.native->vao, a.location, static_cast<GLint>(a.components),
                                      GL_FLOAT, GL_FALSE, a.offset);
            glVertexArrayAttribBinding(p.native->vao, a.location, a.binding);
        }
        label(GL_PROGRAM, p.native->id, d.name);
        checked("Graphics pipeline creation");
        return pipelines.create(std::move(p));
    }
    PipelineHandle createComputePipeline(const ShaderArtifact &artifact,
                                         const std::string &name) override {
        current();
        if (artifact.stage != ShaderStage::Compute)
            throw std::invalid_argument("Compute stage required");
        Pipeline p;
        p.compute = true;
        link(p, std::span{&artifact, 1});
        label(GL_PROGRAM, p.native->id, name);
        return pipelines.create(std::move(p));
    }
    void validateBinding(const Binding &b) {
        if (b.kind == BindingKind::UniformBuffer || b.kind == BindingKind::StorageBuffer) {
            const auto &buffer = buffers.get(b.buffer);
            const auto alignment = b.kind == BindingKind::UniformBuffer ? caps.uniformAlignment
                                                                        : caps.storageAlignment;
            if (b.offset >= buffer.desc.size)
                throw std::invalid_argument("Buffer binding must contain at least one byte");
            if (b.offset % alignment)
                throw std::invalid_argument("Unaligned buffer binding");
            range(buffer.desc.size, b.offset, b.size ? b.size : buffer.desc.size - b.offset);
        } else {
            const auto &texture = textures.get(b.texture);
            if (b.mipLevel >= texture.desc.mipLevels)
                throw std::invalid_argument("Invalid image mip");
            if (b.kind == BindingKind::SampledTexture)
                samplers.get(b.sampler);
            else if (texture.desc.format == Format::Depth32Float ||
                     texture.desc.format == Format::Rgba8Srgb)
                throw std::invalid_argument("Unsupported storage image format");
        }
    }
    BindingGroupHandle createBindingGroup(const BindingGroupDesc &d) override {
        current();
        std::set<uint32_t> seen;
        for (const auto &b : d.entries) {
            if (!seen.insert(b.binding).second)
                throw std::invalid_argument("Duplicate group binding");
            validateBinding(b);
        }
        return groups.create(d);
    }
    QueryHandle createTimestampQueries(uint32_t count) override {
        current();
        if (!count || count > 4096)
            throw std::invalid_argument("Invalid timestamp count");
        auto n = std::make_shared<Native>(Native::Queries);
        n->queries.resize(count);
        glCreateQueries(GL_TIMESTAMP, static_cast<GLsizei>(count), n->queries.data());
        checked("Timestamp query creation");
        return queries.create(Queries{n, std::vector<bool>(count)});
    }
    template <class P, class H> void remove(P &pool, H h) {
        current();
        auto native = pool.get(h).native;
        retired.push_back(std::move(native));
        pool.erase(h);
        collect();
    }
    void destroy(BufferHandle h) override {
        remove(buffers, h);
    }
    void destroy(SamplerHandle h) override {
        remove(samplers, h);
    }
    void destroy(PipelineHandle h) override {
        remove(pipelines, h);
    }
    void destroy(QueryHandle h) override {
        remove(queries, h);
    }
    void destroy(TextureHandle h) override {
        current();
        textures.get(h);
        for (auto i = framebuffers.begin(); i != framebuffers.end();)
            if (std::find(i->second.attachments.begin(), i->second.attachments.end(), h) !=
                i->second.attachments.end()) {
                retired.push_back(i->second.native);
                i = framebuffers.erase(i);
            } else
                ++i;
        remove(textures, h);
    }
    void destroy(BindingGroupHandle h) override {
        current();
        groups.erase(h);
    }
    void *map(BufferHandle h) override {
        current();
        auto &b = buffers.get(h);
        if (b.desc.memory != Memory::Upload)
            throw std::invalid_argument("Only upload buffers have persistent mappings");
        return b.native->mapping;
    }
    void upload(BufferHandle h, size_t offset, std::span<const std::byte> bytes) override {
        current();
        auto &b = buffers.get(h);
        range(b.desc.size, offset, bytes.size());
        if (b.desc.memory == Memory::Upload) {
            if (b.native->lastUse > completed && !complete({owner, b.native->lastUse}))
                throw std::logic_error("Upload buffer is in flight; wait before overwriting it");
            std::memcpy(static_cast<std::byte *>(b.native->mapping) + offset, bytes.data(),
                        bytes.size());
        } else
            glNamedBufferSubData(b.native->id, static_cast<GLintptr>(offset),
                                 static_cast<GLsizeiptr>(bytes.size()), bytes.data());
        checked("Buffer upload");
    }
    void upload(TextureHandle h, std::span<const uint8_t> pixels, uint32_t mip) override {
        current();
        auto &t = textures.get(h);
        if (mip >= t.desc.mipLevels ||
            (t.desc.format != Format::Rgba8 && t.desc.format != Format::Rgba8Srgb))
            throw std::invalid_argument("RGBA8 upload needs an RGBA8 texture/mip");
        const auto w = std::max(1u, t.desc.width >> mip),
                   height = std::max(1u, t.desc.height >> mip);
        if (pixels.size() != size_t{w} * height * 4)
            throw std::invalid_argument("Image byte size mismatch");
        glTextureSubImage2D(t.native->id, static_cast<GLint>(mip), 0, 0, static_cast<GLsizei>(w),
                            static_cast<GLsizei>(height), GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        checked("Texture upload");
    }
    void generateMipmaps(TextureHandle h) override {
        current();
        glGenerateTextureMipmap(textures.get(h).native->id);
        checked("Mipmap generation");
    }
    std::vector<std::byte> readBuffer(BufferHandle h, size_t bytes, size_t offset) override {
        current();
        auto &b = buffers.get(h);
        if (offset > b.desc.size)
            throw std::out_of_range("Read offset out of bounds");
        if (!bytes)
            bytes = b.desc.size - offset;
        range(b.desc.size, offset, bytes);
        std::vector<std::byte> data(bytes);
        glGetNamedBufferSubData(b.native->id, static_cast<GLintptr>(offset),
                                static_cast<GLsizeiptr>(bytes), data.data());
        checked("Buffer readback");
        return data;
    }
    RgbaImage readTexture(TextureHandle h) override {
        current();
        auto &t = textures.get(h);
        if (t.desc.format == Format::Depth32Float)
            throw std::invalid_argument("Color readback needs a color texture");
        const size_t bytes = size_t{t.desc.width} * t.desc.height * 4;
        if (bytes > static_cast<size_t>(std::numeric_limits<GLsizei>::max()))
            throw std::length_error("Readback too large");
        RgbaImage image{t.desc.width, t.desc.height, std::vector<uint8_t>(bytes)};
        glGetTextureImage(t.native->id, 0, GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(bytes),
                          image.pixels.data());
        for (size_t y = 0; y < t.desc.height / 2; ++y) {
            auto a = image.pixels.begin() + static_cast<ptrdiff_t>(y * t.desc.width * 4),
                 b = image.pixels.begin() +
                     static_cast<ptrdiff_t>((t.desc.height - 1 - y) * t.desc.width * 4);
            std::swap_ranges(a, a + static_cast<ptrdiff_t>(t.desc.width * 4), b);
        }
        checked("Texture readback");
        return image;
    }
    void collect() {
        std::erase_if(retired, [&](const auto &n) { return n->lastUse <= completed; });
        std::erase_if(*registry, [](const auto &p) { return p.expired(); });
    }
    bool complete(CompletionToken token) override {
        current();
        if (!token.owner && !token.value)
            return true;
        if (token.owner != owner || token.value > submitted)
            throw std::invalid_argument("Invalid device completion token");
        while (!fences.empty()) {
            auto [serial, fence] = fences.front();
            const auto status = glClientWaitSync(fence, 0, 0);
            if (status == GL_WAIT_FAILED)
                throw std::runtime_error("GPU fence poll failed");
            if (status == GL_TIMEOUT_EXPIRED)
                break;
            completed = serial;
            glDeleteSync(fence);
            fences.pop_front();
        }
        collect();
        return token.value <= completed;
    }
    void wait(CompletionToken token) override {
        current();
        while (!complete(token)) {
            const auto status =
                glClientWaitSync(fences.front().second, GL_SYNC_FLUSH_COMMANDS_BIT, 100000000);
            if (status == GL_WAIT_FAILED)
                throw std::runtime_error("GPU wait failed");
        }
    }
    void waitIdle() override {
        current();
        glFinish();
        completed = submitted;
        for (auto [serial, fence] : fences) {
            (void)serial;
            glDeleteSync(fence);
        }
        fences.clear();
        collect();
    }
    std::optional<uint64_t> timestampResult(QueryHandle h, uint32_t index,
                                            bool waitResult) override {
        current();
        auto &q = queries.get(h);
        if (index >= q.written.size())
            throw std::out_of_range("Timestamp index");
        if (!q.written[index])
            return std::nullopt;
        GLint available = 0;
        glGetQueryObjectiv(q.native->queries[index], GL_QUERY_RESULT_AVAILABLE, &available);
        if (!available && !waitResult)
            return std::nullopt;
        GLuint64 value = 0;
        glGetQueryObjectui64v(q.native->queries[index], GL_QUERY_RESULT, &value);
        return value;
    }
    size_t pendingDeletions() const override {
        check();
        return retired.size();
    }
    uint32_t debugErrorCount() const override {
        return errors.load();
    }
    void releaseContext() override {
        check();
        glfwMakeContextCurrent(nullptr);
    }
    void acquireContext() override {
        thread = std::this_thread::get_id();
        glfwMakeContextCurrent(window->native);
    }
    void swap() override {
        current();
        glfwSwapBuffers(window->native);
    }
    std::shared_ptr<renderer::detail::WindowState> windowState() const override {
        return window;
    }

    Fbo &framebuffer(const RenderingDesc &desc, uint64_t serial) {
        std::string key;
        std::vector<TextureHandle> attachments;
        int width = 0, height = 0;
        auto append = [&](TextureHandle h, uint32_t mip) {
            auto &t = textures.get(h);
            if (mip >= t.desc.mipLevels)
                throw std::invalid_argument("Attachment mip out of range");
            const int w = static_cast<int>(std::max(1u, t.desc.width >> mip)),
                      heightValue = static_cast<int>(std::max(1u, t.desc.height >> mip));
            if (width && (width != w || height != heightValue))
                throw std::invalid_argument("Attachment dimensions disagree");
            width = w;
            height = heightValue;
            t.native->lastUse = serial;
            key += std::to_string(t.native->id) + ":" + std::to_string(mip) + ",";
            attachments.push_back(h);
        };
        if (desc.colors.size() > caps.maxColorAttachments)
            throw std::invalid_argument("Too many color attachments");
        for (auto &a : desc.colors) {
            if (textures.get(a.texture).desc.format == Format::Depth32Float)
                throw std::invalid_argument("Depth texture used as color");
            append(a.texture, a.mipLevel);
        }
        key += "|";
        if (desc.depth) {
            if (textures.get(desc.depth->texture).desc.format != Format::Depth32Float)
                throw std::invalid_argument("Invalid depth attachment");
            append(desc.depth->texture, desc.depth->mipLevel);
        }
        auto it = framebuffers.find(key);
        if (it != framebuffers.end()) {
            it->second.native->lastUse = serial;
            return it->second;
        }
        auto n = std::make_shared<Native>(Native::Framebuffer);
        glCreateFramebuffers(1, &n->id);
        std::vector<GLenum> drawBuffers;
        for (uint32_t i = 0; i < desc.colors.size(); ++i) {
            const auto &a = desc.colors[i];
            glNamedFramebufferTexture(n->id, GL_COLOR_ATTACHMENT0 + i,
                                      textures.get(a.texture).native->id,
                                      static_cast<GLint>(a.mipLevel));
            drawBuffers.push_back(GL_COLOR_ATTACHMENT0 + i);
        }
        if (desc.depth)
            glNamedFramebufferTexture(n->id, GL_DEPTH_ATTACHMENT,
                                      textures.get(desc.depth->texture).native->id,
                                      static_cast<GLint>(desc.depth->mipLevel));
        if (drawBuffers.empty()) {
            glNamedFramebufferDrawBuffer(n->id, GL_NONE);
            glNamedFramebufferReadBuffer(n->id, GL_NONE);
        } else
            glNamedFramebufferDrawBuffers(n->id, static_cast<GLsizei>(drawBuffers.size()),
                                          drawBuffers.data());
        if (glCheckNamedFramebufferStatus(n->id, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("RHI framebuffer incomplete");
        n->lastUse = serial;
        return framebuffers.emplace(key, Fbo{std::move(n), std::move(attachments), width, height})
            .first->second;
    }
    void bindings(Pipeline &p, const std::map<uint32_t, BindingGroupHandle> &active,
                  uint64_t serial) {
        for (const auto &slot : p.slots) {
            auto g = active.find(slot.binding.group);
            if (g == active.end())
                throw std::invalid_argument("Missing pipeline binding group");
            const auto &group = groups.get(g->second);
            auto found =
                std::find_if(group.entries.begin(), group.entries.end(),
                             [&](const auto &b) { return b.binding == slot.binding.binding; });
            if (found == group.entries.end() || found->kind != slot.binding.kind)
                throw std::invalid_argument("Missing/incompatible pipeline binding");
            const auto &b = *found;
            validateBinding(b);
            if (b.kind == BindingKind::UniformBuffer || b.kind == BindingKind::StorageBuffer) {
                auto &buffer = buffers.get(b.buffer);
                const size_t bytes = b.size ? b.size : buffer.desc.size - b.offset;
                if (bytes < slot.binding.byteSize)
                    throw std::invalid_argument("Buffer smaller than reflected shader layout");
                glBindBufferRange(b.kind == BindingKind::UniformBuffer ? GL_UNIFORM_BUFFER
                                                                       : GL_SHADER_STORAGE_BUFFER,
                                  slot.physical, buffer.native->id, static_cast<GLintptr>(b.offset),
                                  static_cast<GLsizeiptr>(bytes));
                buffer.native->lastUse = serial;
            } else {
                auto &t = textures.get(b.texture);
                t.native->lastUse = serial;
                if (b.kind == BindingKind::SampledTexture) {
                    auto &sampler = samplers.get(b.sampler);
                    sampler.native->lastUse = serial;
                    glBindTextureUnit(slot.physical, t.native->id);
                    glBindSampler(slot.physical, sampler.native->id);
                } else
                    glBindImageTexture(slot.physical, t.native->id, static_cast<GLint>(b.mipLevel),
                                       GL_FALSE, 0, b.writable ? GL_READ_WRITE : GL_READ_ONLY,
                                       format(t.desc.format));
            }
        }
    }
    CompletionToken submit(const std::vector<detail::Command> &commands) override;
};

CompletionToken OpenGLDevice::submit(const std::vector<detail::Command> &commands) {
    current();
    const auto serial = ++submitted;
    Pipeline *pipeline = nullptr;
    bool rendering = false;
    RenderingDesc attachments;
    Fbo *fbo = nullptr;
    std::map<uint32_t, BindingGroupHandle> activeGroups;
    std::map<uint32_t, detail::VertexBuffer> vertexBuffers;
    std::optional<detail::IndexBuffer> indexBuffer;
    auto prepare = [&](bool compute, uint32_t count = 0, uint32_t instances = 0,
                       uint32_t firstInstance = 0) {
        if (!pipeline || pipeline->compute != compute || rendering == compute)
            throw std::logic_error("Pipeline/pass kind incompatible with command");
        bindings(*pipeline, activeGroups, serial);
        pipeline->native->lastUse = serial;
        if (compute)
            return;
        for (const auto &layout : pipeline->desc.vertexBindings) {
            auto entry = vertexBuffers.find(layout.binding);
            if (entry == vertexBuffers.end())
                throw std::invalid_argument("Missing vertex buffer");
            auto &buffer = buffers.get(entry->second.handle);
            const uint64_t elements =
                layout.instanced ? uint64_t{firstInstance} + instances : std::max(1u, count);
            range(buffer.desc.size, entry->second.offset,
                  static_cast<size_t>(elements * layout.stride));
            glVertexArrayVertexBuffer(pipeline->native->vao, layout.binding, buffer.native->id,
                                      static_cast<GLintptr>(entry->second.offset),
                                      static_cast<GLsizei>(layout.stride));
            pipeline->native->vertexSources[layout.binding] = buffer.native->id;
            buffer.native->lastUse = serial;
        }
        if (indexBuffer) {
            auto &b = buffers.get(indexBuffer->handle);
            glVertexArrayElementBuffer(pipeline->native->vao, b.native->id);
            pipeline->native->indexSource = b.native->id;
            b.native->lastUse = serial;
        }
    };
    auto end = [&] {
        if (!rendering)
            return;
        std::vector<GLenum> discard;
        for (uint32_t i = 0; i < attachments.colors.size(); ++i)
            if (attachments.colors[i].store == StoreOp::DontCare)
                discard.push_back(GL_COLOR_ATTACHMENT0 + i);
        if (attachments.depth && attachments.depth->store == StoreOp::DontCare)
            discard.push_back(GL_DEPTH_ATTACHMENT);
        if (!discard.empty())
            glInvalidateNamedFramebufferData(fbo->native->id, static_cast<GLsizei>(discard.size()),
                                             discard.data());
        rendering = false;
    };
    try {
        for (const auto &command : commands)
            std::visit(
                [&](const auto &c) {
                    using T = std::decay_t<decltype(c)>;
                    if constexpr (std::is_same_v<T, detail::Begin>) {
                        attachments = c.desc;
                        fbo = &framebuffer(c.desc, serial);
                        rendering = true;
                        pipeline = nullptr;
                        glBindFramebuffer(GL_FRAMEBUFFER, fbo->native->id);
                        glViewport(0, 0, fbo->width, fbo->height);
                        glDisable(GL_SCISSOR_TEST);
                        glDepthMask(GL_TRUE);
                        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                        glEnable(GL_FRAMEBUFFER_SRGB);
                        for (uint32_t i = 0; i < c.desc.colors.size(); ++i)
                            if (c.desc.colors[i].load == LoadOp::Clear)
                                glClearNamedFramebufferfv(fbo->native->id, GL_COLOR,
                                                          static_cast<GLint>(i),
                                                          &c.desc.colors[i].clearColor.x);
                        if (c.desc.depth && c.desc.depth->load == LoadOp::Clear)
                            glClearNamedFramebufferfv(fbo->native->id, GL_DEPTH, 0,
                                                      &c.desc.depth->clearDepth);
                    } else if constexpr (std::is_same_v<T, detail::End>)
                        end();
                    else if constexpr (std::is_same_v<T, detail::Pipeline>) {
                        pipeline = &pipelines.get(c.handle);
                        glUseProgram(pipeline->native->id);
                        pipeline->native->lastUse = serial;
                        if (!pipeline->compute) {
                            const auto &d = pipeline->desc;
                            if (!rendering || d.colorFormats.size() != attachments.colors.size() ||
                                d.depthFormat.has_value() != attachments.depth.has_value())
                                throw std::invalid_argument("Pipeline attachment layout mismatch");
                            for (size_t i = 0; i < d.colorFormats.size(); ++i)
                                if (d.colorFormats[i] !=
                                    textures.get(attachments.colors[i].texture).desc.format)
                                    throw std::invalid_argument("Color format mismatch");
                            if (d.depthFormat &&
                                *d.depthFormat !=
                                    textures.get(attachments.depth->texture).desc.format)
                                throw std::invalid_argument("Depth format mismatch");
                            glBindVertexArray(pipeline->native->vao);
                            if (d.depthTest)
                                glEnable(GL_DEPTH_TEST);
                            else
                                glDisable(GL_DEPTH_TEST);
                            glDepthMask(d.depthWrite ? GL_TRUE : GL_FALSE);
                            glDepthFunc(compare(d.depthCompare));
                            if (d.depthBiasConstant != 0 || d.depthBiasSlope != 0) {
                                glEnable(GL_POLYGON_OFFSET_FILL);
                                glPolygonOffset(d.depthBiasSlope, d.depthBiasConstant);
                            } else
                                glDisable(GL_POLYGON_OFFSET_FILL);
                            if (d.cull == CullMode::None)
                                glDisable(GL_CULL_FACE);
                            else {
                                glEnable(GL_CULL_FACE);
                                glCullFace(d.cull == CullMode::Back ? GL_BACK : GL_FRONT);
                            }
                            glFrontFace(d.clockwise ? GL_CW : GL_CCW);
                            // 每次绑定均覆盖所有活动附件，防止 OIT 的独立混合状态泄漏。
                            for (uint32_t i = 0; i < d.colorFormats.size(); ++i) {
                                ColorBlendState blend;
                                if (!d.colorBlends.empty())
                                    blend = d.colorBlends[i];
                                else if (d.blend != BlendMode::Opaque) {
                                    blend.enabled = true;
                                    blend.sourceColor = BlendFactor::SourceAlpha;
                                    blend.destinationColor = d.blend == BlendMode::Alpha
                                                                 ? BlendFactor::OneMinusSourceAlpha
                                                                 : BlendFactor::One;
                                    blend.destinationAlpha = BlendFactor::OneMinusSourceAlpha;
                                }
                                if (blend.enabled) {
                                    glEnablei(GL_BLEND, i);
                                    glBlendEquationi(i, GL_FUNC_ADD);
                                    glBlendFuncSeparatei(i, blendFactor(blend.sourceColor),
                                                         blendFactor(blend.destinationColor),
                                                         blendFactor(blend.sourceAlpha),
                                                         blendFactor(blend.destinationAlpha));
                                } else
                                    glDisablei(GL_BLEND, i);
                            }
                        }
                    } else if constexpr (std::is_same_v<T, detail::Group>) {
                        auto &group = groups.get(c.handle);
                        activeGroups[group.group] = c.handle;
                    } else if constexpr (std::is_same_v<T, detail::VertexBuffer>) {
                        buffers.get(c.handle);
                        vertexBuffers.insert_or_assign(c.binding, c);
                    } else if constexpr (std::is_same_v<T, detail::IndexBuffer>) {
                        auto &buffer = buffers.get(c.handle);
                        range(buffer.desc.size, c.offset, 0);
                        if (c.offset % 4)
                            throw std::invalid_argument("Index buffer offset must align to uint32");
                        indexBuffer = c;
                    } else if constexpr (std::is_same_v<T, detail::Viewport>) {
                        if (!c.width || !c.height ||
                            c.width > static_cast<uint32_t>(std::numeric_limits<GLsizei>::max()) ||
                            c.height > static_cast<uint32_t>(std::numeric_limits<GLsizei>::max()))
                            throw std::invalid_argument("Invalid viewport");
                        glViewport(0, 0, static_cast<GLsizei>(c.width),
                                   static_cast<GLsizei>(c.height));
                    } else if constexpr (std::is_same_v<T, detail::Scissor>) {
                        glEnable(GL_SCISSOR_TEST);
                        glScissor(c.x, c.y, static_cast<GLsizei>(c.width),
                                  static_cast<GLsizei>(c.height));
                    } else if constexpr (std::is_same_v<T, detail::Draw>) {
                        prepare(false, c.first + c.count, c.instances, c.firstInstance);
                        glDrawArraysInstancedBaseInstance(GL_TRIANGLES, static_cast<GLint>(c.first),
                                                          static_cast<GLsizei>(c.count),
                                                          static_cast<GLsizei>(c.instances),
                                                          c.firstInstance);
                    } else if constexpr (std::is_same_v<T, detail::DrawIndexed>) {
                        prepare(false, 0, c.instances, c.firstInstance);
                        if (!indexBuffer)
                            throw std::invalid_argument("Missing index buffer");
                        auto &b = buffers.get(indexBuffer->handle);
                        const size_t offset = indexBuffer->offset + size_t{c.first} * 4;
                        range(b.desc.size, offset, size_t{c.count} * 4);
                        glDrawElementsInstancedBaseVertexBaseInstance(
                            GL_TRIANGLES, static_cast<GLsizei>(c.count), GL_UNSIGNED_INT,
                            reinterpret_cast<const void *>(offset),
                            static_cast<GLsizei>(c.instances), c.base, c.firstInstance);
                    } else if constexpr (std::is_same_v<T, detail::Indirect>) {
                        auto &b = buffers.get(c.buffer);
                        range(b.desc.size, c.offset,
                              c.dispatch ? 12
                                         : (c.count ? size_t{c.count - 1} * c.stride + 20 : 0));
                        b.native->lastUse = serial;
                        prepare(c.dispatch);
                        if (c.dispatch) {
                            glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, b.native->id);
                            glDispatchComputeIndirect(static_cast<GLintptr>(c.offset));
                        } else {
                            if (!indexBuffer)
                                throw std::invalid_argument("Missing indirect index buffer");
                            if (indexBuffer->offset)
                                throw std::invalid_argument(
                                    "Indirect index buffer must start at offset zero");
                            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, b.native->id);
                            glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT,
                                                        reinterpret_cast<const void *>(c.offset),
                                                        static_cast<GLsizei>(c.count),
                                                        static_cast<GLsizei>(c.stride));
                        }
                    } else if constexpr (std::is_same_v<T, detail::Dispatch>) {
                        prepare(true);
                        const uint32_t values[] = {c.x, c.y, c.z};
                        for (GLuint i = 0; i < 3; ++i) {
                            GLint maximum = 0;
                            glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, i, &maximum);
                            if (values[i] > static_cast<uint32_t>(maximum))
                                throw std::invalid_argument(
                                    "Compute group count exceeds device limit");
                        }
                        glDispatchCompute(c.x, c.y, c.z);
                    } else if constexpr (std::is_same_v<T, detail::Copy>) {
                        auto &a = buffers.get(c.source);
                        auto &b = buffers.get(c.destination);
                        range(a.desc.size, c.sourceOffset, c.size);
                        range(b.desc.size, c.destinationOffset, c.size);
                        if (a.native == b.native && c.sourceOffset < c.destinationOffset + c.size &&
                            c.destinationOffset < c.sourceOffset + c.size)
                            throw std::invalid_argument("Overlapping buffer copy");
                        a.native->lastUse = serial;
                        b.native->lastUse = serial;
                        glCopyNamedBufferSubData(a.native->id, b.native->id,
                                                 static_cast<GLintptr>(c.sourceOffset),
                                                 static_cast<GLintptr>(c.destinationOffset),
                                                 static_cast<GLsizeiptr>(c.size));
                    } else if constexpr (std::is_same_v<T, detail::CopyTexture>) {
                        auto &source = textures.get(c.source);
                        auto &destination = textures.get(c.destination);
                        const auto &r = c.region;
                        if (source.desc.format != destination.desc.format ||
                            r.sourceMip >= source.desc.mipLevels ||
                            r.destinationMip >= destination.desc.mipLevels)
                            throw std::invalid_argument("Texture copy formats/mips differ");
                        const auto sw = std::max(1u, source.desc.width >> r.sourceMip),
                                   sh = std::max(1u, source.desc.height >> r.sourceMip),
                                   dw = std::max(1u, destination.desc.width >> r.destinationMip),
                                   dh = std::max(1u, destination.desc.height >> r.destinationMip);
                        range(sw, r.sourceX, r.width);
                        range(sh, r.sourceY, r.height);
                        range(dw, r.destinationX, r.width);
                        range(dh, r.destinationY, r.height);
                        auto storage = [](auto native) {
                            while (native->parent && native->parent->kind == Native::Texture)
                                native = native->parent;
                            return native;
                        };
                        if (storage(source.native) == storage(destination.native) &&
                            source.native->baseMip + r.sourceMip ==
                                destination.native->baseMip + r.destinationMip &&
                            r.sourceX < r.destinationX + r.width &&
                            r.destinationX < r.sourceX + r.width &&
                            r.sourceY < r.destinationY + r.height &&
                            r.destinationY < r.sourceY + r.height)
                            throw std::invalid_argument("Overlapping texture copy");
                        source.native->lastUse = destination.native->lastUse = serial;
                        glCopyImageSubData(
                            source.native->id, GL_TEXTURE_2D, static_cast<GLint>(r.sourceMip),
                            static_cast<GLint>(r.sourceX), static_cast<GLint>(r.sourceY), 0,
                            destination.native->id, GL_TEXTURE_2D,
                            static_cast<GLint>(r.destinationMip),
                            static_cast<GLint>(r.destinationX), static_cast<GLint>(r.destinationY),
                            0, static_cast<GLsizei>(r.width), static_cast<GLsizei>(r.height), 1);
                    } else if constexpr (std::is_same_v<T, detail::Barriers>) {
                        GLbitfield bits = 0;
                        for (auto b : c.barriers) {
                            if (b.buffer) {
                                if (b.texture)
                                    throw std::invalid_argument("Buffer barrier marked as texture");
                                auto &resource = buffers.get(b.buffer);
                                range(resource.desc.size, b.offset,
                                      b.size ? b.size : resource.desc.size - b.offset);
                                resource.native->lastUse = serial;
                            }
                            if (b.image) {
                                if (!b.texture)
                                    throw std::invalid_argument("Image barrier marked as buffer");
                                auto &resource = textures.get(b.image);
                                if (b.firstMip >= resource.desc.mipLevels ||
                                    (b.mipCount &&
                                     b.mipCount > resource.desc.mipLevels - b.firstMip))
                                    throw std::invalid_argument("Invalid barrier mip range");
                                resource.native->lastUse = serial;
                            }
                            // 着色器存储写需要按后续用途刷新可见性；读后写也必须同步。
                            // 常规附件/拷贝顺序由对应 OpenGL 操作保证。
                            if (b.before == Access::StorageWrite ||
                                b.before == Access::StorageReadWrite ||
                                b.after == Access::StorageWrite ||
                                b.after == Access::StorageReadWrite) {
                                switch (b.after) {
                                case Access::None:
                                    bits |= GL_ALL_BARRIER_BITS; // 物理存储复用的完整依赖。
                                    break;
                                case Access::VertexRead:
                                    bits |= GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT;
                                    break;
                                case Access::IndexRead:
                                    bits |= GL_ELEMENT_ARRAY_BARRIER_BIT;
                                    break;
                                case Access::UniformRead:
                                    bits |= GL_UNIFORM_BARRIER_BIT;
                                    break;
                                case Access::SampledRead:
                                    bits |= GL_TEXTURE_FETCH_BARRIER_BIT;
                                    break;
                                case Access::IndirectRead:
                                    bits |= GL_COMMAND_BARRIER_BIT;
                                    break;
                                case Access::TransferRead:
                                case Access::TransferWrite:
                                    bits |= b.texture ? GL_TEXTURE_UPDATE_BARRIER_BIT
                                                      : GL_BUFFER_UPDATE_BARRIER_BIT;
                                    break;
                                case Access::ColorWrite:
                                case Access::DepthWrite:
                                case Access::DepthRead:
                                    bits |= GL_FRAMEBUFFER_BARRIER_BIT;
                                    break;
                                default:
                                    bits |= b.texture ? GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
                                                      : GL_SHADER_STORAGE_BARRIER_BIT;
                                    break;
                                }
                            }
                        }
                        if (bits)
                            glMemoryBarrier(bits);
                    } else if constexpr (std::is_same_v<T, detail::Timestamp>) {
                        auto &q = queries.get(c.query);
                        if (c.index >= q.written.size())
                            throw std::out_of_range("Timestamp index");
                        q.written[c.index] = true;
                        q.native->lastUse = serial;
                        glQueryCounter(q.native->queries[c.index], GL_TIMESTAMP);
                    } else if constexpr (std::is_same_v<T, detail::Present>) {
                        RenderingDesc desc;
                        desc.colors.push_back({c.texture, LoadOp::Load});
                        auto &source = framebuffer(desc, serial);
                        // 输出纹理已经过 sRGB 编码，呈现拷贝避免再次编码。
                        glDisable(GL_FRAMEBUFFER_SRGB);
                        glBindFramebuffer(GL_READ_FRAMEBUFFER, source.native->id);
                        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                        const int w = window->framebufferWidth.load(),
                                  h = window->framebufferHeight.load();
                        glBlitFramebuffer(0, 0, source.width, source.height, 0, 0, w, h,
                                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
                    }
                },
                command);
        checked("RHI submission");
    } catch (...) {
        end();
        glFinish();
        completed = serial;
        for (auto [value, f] : fences) {
            (void)value;
            glDeleteSync(f);
        }
        fences.clear();
        collect();
        throw;
    }
    // 提交后插入 Fence，将单调序号关联到真实 GPU 完成点，用于帧槽和延迟销毁。
    auto fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!fence) {
        glFinish();
        completed = serial;
        throw std::runtime_error("Fence creation failed");
    }
    fences.emplace_back(serial, fence);
    glFlush();
    complete({owner, completed});
    return {owner, serial};
}
Device Device::createOpenGL(Window &window, bool debug) {
    auto backend = std::make_unique<OpenGLDevice>(renderer::detail::WindowAccess::state(window));
    backend->init(debug);
    auto impl = std::make_unique<Impl>();
    impl->backend = std::move(backend);
    return Device(std::move(impl));
}
} // namespace renderer::rhi
