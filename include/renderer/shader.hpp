/**
 * @file
 * @brief 声明 GLSL 编译、SPIR-V 产物、资源反射与 OpenGL 转译接口。
 */
#ifndef RENDERER_INCLUDE_RENDERER_SHADER_HPP_INCLUDED
#define RENDERER_INCLUDE_RENDERER_SHADER_HPP_INCLUDED
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace renderer {
/** @brief 着色器的顶点、片元或计算阶段。 */
enum class ShaderStage { Vertex, Fragment, Compute };
/** @brief 逻辑着色器资源的 uniform、storage、采样纹理或图像类别。 */
enum class BindingKind { UniformBuffer, StorageBuffer, SampledTexture, StorageImage };
/**
 * @brief 反射出的缓冲块成员名称、字节偏移及大小。
 */
struct ShaderMember {
    std::string name;
    uint32_t offset = 0, size = 0;
};
/**
 * @brief 反射出的逻辑资源绑定、描述符数组与块布局。
 */
struct ShaderBinding {
    std::string name;
    uint32_t group = 0, binding = 0;
    BindingKind kind = BindingKind::UniformBuffer;
    uint32_t byteSize = 0, arraySize = 1; // 反射出的块最小字节数及描述符数量。
    std::vector<ShaderMember> members;
    bool textureArray = false; ///< 采样/存储图像为二维数组，绑定时验证视图维度。
};
/**
 * @brief 一次着色器编译产生的 SPIR-V、资源反射及诊断名称。
 */
struct ShaderArtifact {
    ShaderStage stage = ShaderStage::Vertex;
    std::vector<uint32_t> spirv;
    std::vector<ShaderBinding> bindings;
    std::string sourceName;
};
/**
 * @brief 着色器预定义宏及 include 搜索路径配置。
 */
struct ShaderCompileOptions {
    std::vector<std::string> defines; // 宏格式为 NAME 或 NAME=VALUE。
    std::vector<std::filesystem::path> includeDirectories;
};
/**
 * @brief 逻辑 group/binding 到 OpenGL 同类资源物理绑定点的映射条目。
 */
struct BindingRemap {
    uint32_t group = 0, binding = 0, physicalBinding = 0;
};

// GLSL 450（Vulkan 语义）-> SPIR-V 1.5，并反射布局；无需 Vulkan 运行时。
// OpenGL 后端将同一产物转译为桌面 GLSL，保持前端着色器与绑定约定一致。
/**
 * @brief 以 PImpl 实现 GLSL 编译、反射、缓存与桌面 OpenGL 转译的服务。
 */
class ShaderCompiler {
  public:
    /**
     * @brief 创建着色器编译器及其缓存。
     * @return 无（构造函数）。
     */
    ShaderCompiler();
    /**
     * @brief 释放本对象持有的状态与资源引用。
     * @return 无（析构函数）。
     */
    ~ShaderCompiler();
    /**
     * @brief 转移构造对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 无（构造函数）。
     */
    ShaderCompiler(ShaderCompiler &&other) noexcept;
    /**
     * @brief 转移赋值对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 当前对象的引用。
     */
    ShaderCompiler &operator=(ShaderCompiler &&other) noexcept;
    /**
     * @brief 禁止复制，确保内部可变状态由单个对象拥有。
     * @param other 禁止复制的源对象。
     * @return 无；此接口已删除，调用将编译失败。
     */
    ShaderCompiler(const ShaderCompiler &other) = delete;
    /**
     * @brief 禁止复制，确保内部可变状态由单个对象拥有。
     * @param other 禁止复制的源对象。
     * @return 无；此接口已删除，调用将编译失败。
     */
    ShaderCompiler &operator=(const ShaderCompiler &other) = delete;
    // 按预处理后的源码及阶段缓存，宏和 include 文件内容的改变会进入缓存键。
    /**
     * @brief 按 Vulkan GLSL 450 规则预处理并编译为 SPIR-V 1.5，反射资源并缓存。
     * @param stage 顶点、片元或计算阶段。
     * @param source GLSL 源码文本。
     * @param sourceName 诊断名称及相对 include 的定位依据。
     * @param options 预定义宏及 include 搜索目录。
     * @return SPIR-V、阶段和资源布局反射；编译失败抛出含诊断的异常。
     */
    ShaderArtifact compile(ShaderStage stage, const std::string &source,
                           const std::string &sourceName = "memory.glsl",
                           const ShaderCompileOptions &options = {});
    /**
     * @brief 读取文件并调用相同编译与缓存流程。
     * @param stage 着色器阶段。
     * @param path GLSL 源文件路径。
     * @param options 预定义宏及 include 搜索目录。
     * @return 编译产物；读取或编译失败抛出异常。
     */
    ShaderArtifact compileFile(ShaderStage stage, const std::filesystem::path &path,
                               const ShaderCompileOptions &options = {});
    // remaps 将逻辑 (group,binding) 映射为 OpenGL 同类资源命名空间内的绑定点。
    /**
     * @brief 将 SPIR-V 转译为桌面 OpenGL GLSL。
     * @param artifact 包含有效 SPIR-V 的编译产物。
     * @param remaps 逻辑 group/binding 到 OpenGL 同类资源物理绑定点的映射。
     * @return 可交给 OpenGL 编译器的 GLSL 源码。
     */
    std::string toOpenGL(const ShaderArtifact &artifact,
                         std::span<const BindingRemap> remaps = {}) const;
    /**
     * @brief 读取成功编译并缓存的着色器数量。
     * @return 缓存条目数。
     */
    size_t cachedShaderCount() const;

  private:
    /// 内部实现前置声明，公共头文件仅持有不透明指针。
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace renderer

#endif // RENDERER_INCLUDE_RENDERER_SHADER_HPP_INCLUDED
