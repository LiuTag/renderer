#include "renderer/shader.hpp"
#include <SPIRV/GlslangToSpv.h>
#include <algorithm>
#include <fstream>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <mutex>
#include <spirv_glsl.hpp>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

namespace renderer {
namespace {
std::string readText(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("Cannot read shader: " + path.string());
    return {std::istreambuf_iterator<char>(stream), {}};
}
struct Process {
    Process() {
        if (!glslang::InitializeProcess())
            throw std::runtime_error("glslang initialization failed");
    }
    ~Process() {
        glslang::FinalizeProcess();
    }
};
struct Includer : glslang::TShader::Includer {
    std::vector<std::filesystem::path> directories;
    IncludeResult *includeLocal(const char *name, const char *origin, size_t depth) override {
        if (depth > 64)
            return nullptr;
        auto paths = directories;
        paths.insert(paths.begin(), std::filesystem::path(origin).parent_path());
        for (const auto &base : paths) {
            const auto path = base / name;
            if (std::filesystem::is_regular_file(path)) {
                auto text = std::make_unique<std::string>(readText(path));
                auto *result =
                    new IncludeResult(path.string(), text->data(), text->size(), text.get());
                text.release();
                return result;
            }
        }
        return nullptr;
    }
    IncludeResult *includeSystem(const char *name, const char *origin, size_t depth) override {
        return includeLocal(name, origin, depth);
    }
    void releaseInclude(IncludeResult *result) override {
        if (result) {
            delete static_cast<std::string *>(result->userData);
            delete result;
        }
    }
};
EShLanguage language(ShaderStage stage) {
    switch (stage) {
    case ShaderStage::Vertex:
        return EShLangVertex;
    case ShaderStage::Fragment:
        return EShLangFragment;
    case ShaderStage::Compute:
        return EShLangCompute;
    }
    throw std::invalid_argument("Invalid shader stage");
}
void environment(glslang::TShader &shader, EShLanguage stage) {
    // 使用统一的 Vulkan 着色器语义生成可移植 SPIR-V，不代表创建 Vulkan 设备。
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 450);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);
}
std::vector<ShaderBinding> reflect(const std::vector<uint32_t> &code) {
    spirv_cross::Compiler compiler(code);
    const auto resources = compiler.get_shader_resources();
    if (!resources.push_constant_buffers.empty() || !resources.separate_images.empty() ||
        !resources.separate_samplers.empty() || !resources.subpass_inputs.empty() ||
        !resources.acceleration_structures.empty())
        throw std::invalid_argument("Portable shader profile uses uniform/storage buffers and "
                                    "combined sampled textures/images");
    std::vector<ShaderBinding> result;
    const auto append = [&](const auto &list, BindingKind kind) {
        for (const auto &r : list) {
            ShaderBinding binding;
            binding.name = r.name;
            binding.group = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
            binding.binding = compiler.get_decoration(r.id, spv::DecorationBinding);
            binding.kind = kind;
            const auto &type = compiler.get_type(r.type_id);
            for (uint32_t count : type.array)
                binding.arraySize *= count;
            if (kind == BindingKind::UniformBuffer || kind == BindingKind::StorageBuffer) {
                const auto &block = compiler.get_type(r.base_type_id);
                binding.byteSize = static_cast<uint32_t>(compiler.get_declared_struct_size(block));
                for (uint32_t i = 0; i < block.member_types.size(); ++i)
                    binding.members.push_back(
                        {compiler.get_member_name(r.base_type_id, i),
                         compiler.type_struct_member_offset(block, i),
                         static_cast<uint32_t>(
                             compiler.get_declared_struct_member_size(block, i))});
            }
            result.push_back(std::move(binding));
        }
    };
    append(resources.uniform_buffers, BindingKind::UniformBuffer);
    append(resources.storage_buffers, BindingKind::StorageBuffer);
    append(resources.sampled_images, BindingKind::SampledTexture);
    append(resources.storage_images, BindingKind::StorageImage);
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return std::tie(a.group, a.binding) < std::tie(b.group, b.binding);
    });
    return result;
}
} // namespace
struct ShaderCompiler::Impl {
    mutable std::mutex mutex;
    std::unordered_map<std::string, ShaderArtifact> cache;
};
ShaderCompiler::ShaderCompiler() : impl_(std::make_unique<Impl>()) {
    static Process process;
}
ShaderCompiler::~ShaderCompiler() = default;
ShaderCompiler::ShaderCompiler(ShaderCompiler &&) noexcept = default;
ShaderCompiler &ShaderCompiler::operator=(ShaderCompiler &&) noexcept = default;
ShaderArtifact ShaderCompiler::compile(ShaderStage stage, const std::string &source,
                                       const std::string &sourceName,
                                       const ShaderCompileOptions &options) {
    const auto type = language(stage);
    const char *text = source.c_str();
    const char *name = sourceName.c_str();
    glslang::TShader preprocessor(type);
    environment(preprocessor, type);
    preprocessor.setStringsWithLengthsAndNames(&text, nullptr, &name, 1);
    std::string preamble;
    for (const auto &define : options.defines) {
        if (define.find_first_of("\r\n") != std::string::npos)
            throw std::invalid_argument("Shader define must fit on one line");
        auto macro = define;
        if (auto equal = macro.find('='); equal != std::string::npos)
            macro[equal] = ' ';
        preamble += "#define " + macro + "\n";
    }
    preprocessor.setPreamble(preamble.c_str());
    Includer includer;
    includer.directories = options.includeDirectories;
    std::string processed;
    const auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    if (!preprocessor.preprocess(GetDefaultResources(), 450, ECoreProfile, false, false, messages,
                                 &processed, includer))
        throw std::runtime_error(sourceName + ": " + preprocessor.getInfoLog());
    // 先解析宏和 include 再缓存，保证修改依赖文件后不会错误复用旧产物。
    const std::string key = std::to_string(static_cast<int>(stage)) + processed;
    std::lock_guard lock(impl_->mutex);
    if (const auto it = impl_->cache.find(key); it != impl_->cache.end())
        return it->second;
    text = processed.c_str();
    glslang::TShader shader(type);
    environment(shader, type);
    shader.setStringsWithLengthsAndNames(&text, nullptr, &name, 1);
    if (!shader.parse(GetDefaultResources(), 450, false, messages))
        throw std::runtime_error(sourceName + ": " + shader.getInfoLog());
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages))
        throw std::runtime_error(sourceName + ": " + program.getInfoLog());
    ShaderArtifact result;
    result.stage = stage;
    result.sourceName = sourceName;
    glslang::SpvOptions spvOptions;
    spvOptions.disableOptimizer = true;
    glslang::GlslangToSpv(*program.getIntermediate(type), result.spirv, &spvOptions);
    result.bindings = reflect(result.spirv);
    impl_->cache.emplace(key, result);
    return result;
}
ShaderArtifact ShaderCompiler::compileFile(ShaderStage stage, const std::filesystem::path &path,
                                           const ShaderCompileOptions &options) {
    return compile(stage, readText(path), path.string(), options);
}
std::string ShaderCompiler::toOpenGL(const ShaderArtifact &artifact,
                                     std::span<const BindingRemap> remaps) const {
    if (artifact.spirv.empty())
        throw std::invalid_argument("Empty SPIR-V shader");
    spirv_cross::CompilerGLSL compiler(artifact.spirv);
    const auto resources = compiler.get_shader_resources();
    const auto remap = [&](const auto &list) {
        for (const auto &resource : list) {
            const auto group = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            const auto binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
            const auto match = std::find_if(remaps.begin(), remaps.end(), [&](const auto &r) {
                return r.group == group && r.binding == binding;
            });
            if (match == remaps.end() && group != 0)
                throw std::invalid_argument(
                    "Nonzero descriptor groups require an explicit OpenGL binding map");
            // OpenGL 没有 descriptor set，移除 set 后将逻辑绑定改写为分配的绑定点。
            compiler.unset_decoration(resource.id, spv::DecorationDescriptorSet);
            compiler.set_decoration(resource.id, spv::DecorationBinding,
                                    match == remaps.end() ? binding : match->physicalBinding);
        }
    };
    remap(resources.uniform_buffers);
    remap(resources.storage_buffers);
    remap(resources.sampled_images);
    remap(resources.storage_images);
    auto options = compiler.get_common_options();
    options.version = 450;
    options.es = false;
    options.vulkan_semantics = false;
    compiler.set_common_options(options);
    return compiler.compile();
}
size_t ShaderCompiler::cachedShaderCount() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->cache.size();
}
} // namespace renderer
