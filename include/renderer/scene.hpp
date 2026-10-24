/**
 * @file
 * @brief 声明场景树、节点变换、可见性与网格、相机、灯光组件接口。
 */
#ifndef RENDERER_INCLUDE_RENDERER_SCENE_HPP_INCLUDED
#define RENDERER_INCLUDE_RENDERER_SCENE_HPP_INCLUDED

#include "renderer/assets.hpp"
#include "renderer/sprite.hpp"
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace renderer {
struct NodeTag;
using NodeHandle = Handle<NodeTag>;
// KeepLocal 保留局部矩阵；KeepWorld 重算局部矩阵以维持当前世界姿态。
/** @brief 更换父节点时保留局部矩阵或世界矩阵的策略。 */
enum class ReparentMode { KeepLocal, KeepWorld };
/**
 * @brief 挂在节点上的网格实例组件，引用网格、材质和层/阴影设置。
 */
struct MeshRenderer {
    MeshHandle mesh;
    std::vector<MaterialInstanceHandle> materials; // 必须覆盖网格使用的所有材质槽。
    uint32_t layerMask = 0xFFFFFFFFu;
    bool castShadow = true, receiveShadow = true;
};
/**
 * @brief 透视或正交相机组件；观察方向取节点局部 -Z。
 */
struct Camera {
    float verticalFov = radians(60);
    float nearPlane = 0.1f;
    float farPlane = 1000;
    uint32_t cullingMask = 0xFFFFFFFFu;
    /** @brief 相机的透视或正交投影类型。 */
    enum class Projection { Perspective, Orthographic } projection = Projection::Perspective;
    float orthographicHeight = 10, aspectRatio = 0; // 宽高比为 0 时使用当前视口比例。
};
class RenderExtractor;
/** @brief 方向光、点光源或聚光灯类型。 */
enum class LightType { Directional, Point, Spot };
/**
 * @brief 方向光、点光源或聚光灯组件；方向取节点局部 -Z。
 */
struct Light {
    LightType type = LightType::Directional;
    Vec3 color{1, 1, 1};
    float intensity = 3, range = 20, innerCone = radians(20), outerCone = radians(30);
    bool castShadow = true; // 当前仅首个启用阴影的方向光生成阴影贴图。
};

// 场景树及组件的可变入口，按单写线程使用；世界数据由版本号按需更新。
// 节点的局部 -Z 为相机观察方向及方向光/聚光灯的照射方向。
/**
 * @brief 以 PImpl 管理场景树及组件的可变入口；按单写线程更新派生世界数据。
 */
class Scene {
  public:
    /**
     * @brief 创建包含根节点的空场景。
     * @return 无（构造函数）。
     */
    Scene();
    /**
     * @brief 释放本对象持有的状态与资源引用。
     * @return 无（析构函数）。
     */
    ~Scene();
    /**
     * @brief 转移构造对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 无（构造函数）。
     */
    Scene(Scene &&other) noexcept;
    /**
     * @brief 转移赋值对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 当前对象的引用。
     */
    Scene &operator=(Scene &&other) noexcept;
    /**
     * @brief 禁止复制，确保内部可变状态由单个对象拥有。
     * @param other 禁止复制的源对象。
     * @return 无；此接口已删除，调用将编译失败。
     */
    Scene(const Scene &other) = delete;
    /**
     * @brief 禁止复制，确保内部可变状态由单个对象拥有。
     * @param other 禁止复制的源对象。
     * @return 无；此接口已删除，调用将编译失败。
     */
    Scene &operator=(const Scene &other) = delete;

    /**
     * @brief 获取固定根节点，根节点不能删除或换父节点。
     * @return 当前场景的根节点句柄。
     */
    NodeHandle root() const; // 根节点固定存在，不能删除或更换父节点。
    /**
     * @brief 在指定父节点下创建可见节点，初始局部矩阵为单位矩阵。
     * @param parent 当前场景内有效的父节点。
     * @param name 节点名称，创建时复制字符串。
     * @return 新节点句柄。
     */
    NodeHandle createNode(NodeHandle parent, std::string_view name = {});
    /**
     * @brief 检查节点句柄是否仍属于当前场景且存活。
     * @param node 待检查句柄，可为空或已失效。
     * @return 句柄有效时为 true。
     */
    bool valid(NodeHandle node) const;
    /**
     * @brief 读取节点名称。
     * @param node 当前场景内有效的节点。
     * @return 名称字符串的副本。
     */
    std::string name(NodeHandle node) const;
    /**
     * @brief 查询节点的直接父节点。
     * @param node 当前场景内有效的节点。
     * @return 父节点句柄；根节点返回空句柄。
     */
    NodeHandle parent(NodeHandle node) const;
    /**
     * @brief 删除节点及全部后代，使对应句柄失效；禁止删除根节点。
     * @param node 待删除子树的根节点。
     * @return 无。
     */
    void destroySubtree(NodeHandle node); // 连同后代一起删除，旧句柄随后失效。
    // 无效句柄、成环、修改根节点或 KeepWorld 的新父矩阵不可逆时抛出异常。
    /**
     * @brief 更换父节点；成环、修改根节点或 KeepWorld 无法求逆时抛出异常。
     * @param node 需要移动的非根节点。
     * @param parent 新的有效父节点，不能位于 node 的子树内。
     * @param mode 保留局部矩阵或保留当前世界矩阵。
     * @return 无。
     */
    void reparent(NodeHandle node, NodeHandle parent, ReparentMode mode = ReparentMode::KeepLocal);
    /**
     * @brief 设置局部矩阵并更新场景版本。
     * @param node 目标节点。
     * @param transform 相对于父节点的有限仿射矩阵。
     * @return 无。
     */
    void setLocalTransform(NodeHandle node, const Transform &transform);
    /**
     * @brief 读取节点相对于父节点的变换。
     * @param node 有效节点。
     * @return 局部变换矩阵的副本。
     */
    Transform localTransform(NodeHandle node) const;
    /**
     * @brief 按需更新派生数据并读取世界变换。
     * @param node 有效节点。
     * @return 从节点局部坐标到世界坐标的矩阵。
     */
    Mat4 worldTransform(NodeHandle node) const;
    /**
     * @brief 设置局部可见标记；祖先隐藏时整棵子树均不可见。
     * @param node 目标节点。
     * @param visible 节点自身是否可见。
     * @return 无。
     */
    void setVisible(NodeHandle node, bool visible); // 隐藏父节点会隐藏整棵子树。
    /**
     * @brief 查询合并全部祖先可见性后的结果。
     * @param node 有效节点。
     * @return 节点与全部祖先都可见时为 true。
     */
    bool effectiveVisible(NodeHandle node) const;
    /**
     * @brief 读取场景内容版本号，用于识别修改。
     * @return 当前场景版本号。
     */
    uint64_t revision() const;

    // 支持 MeshRenderer、SpriteRenderer、SortingGroup、Camera、Light。
    /**
     * @brief 添加或替换组件并更新场景版本；不支持的组件类型编译失败。
     * @tparam T MeshRenderer、SpriteRenderer、SortingGroup、Camera 或 Light。
     * @param node 目标节点。
     * @param component 需要复制到节点中的组件值。
     * @return 无。
     */
    template <class T> void setComponent(NodeHandle node, const T &component) {
        if constexpr (std::is_same_v<T, MeshRenderer>)
            setMeshRenderer(node, component);
        else if constexpr (std::is_same_v<T, Camera>)
            setCamera(node, component);
        else if constexpr (std::is_same_v<T, Light>)
            setLight(node, component);
        else if constexpr (std::is_same_v<T, SpriteRenderer>)
            setSpriteRenderer(node, component);
        else if constexpr (std::is_same_v<T, SortingGroup>)
            setSortingGroup(node, component);
        else
            static_assert(!std::is_same_v<T, T>, "Unsupported scene component");
    }
    // 返回副本；修改后需 setComponent 写回，避免外部绕过场景版本管理。
    /**
     * @brief 读取组件副本；修改副本后需调用 setComponent 写回。
     * @tparam T MeshRenderer、SpriteRenderer、SortingGroup、Camera 或 Light。
     * @param node 待查询节点。
     * @return 存在时返回组件副本，不存在时返回空值。
     */
    template <class T> std::optional<T> component(NodeHandle node) const {
        if constexpr (std::is_same_v<T, MeshRenderer>)
            return meshRenderer(node);
        else if constexpr (std::is_same_v<T, Camera>)
            return camera(node);
        else if constexpr (std::is_same_v<T, Light>)
            return light(node);
        else if constexpr (std::is_same_v<T, SpriteRenderer>)
            return spriteRenderer(node);
        else if constexpr (std::is_same_v<T, SortingGroup>)
            return sortingGroup(node);
        else
            static_assert(!std::is_same_v<T, T>, "Unsupported scene component");
    }
    /**
     * @brief 移除指定类型组件。
     * @tparam T MeshRenderer、SpriteRenderer、SortingGroup、Camera 或 Light。
     * @param node 目标节点。
     * @return 无。
     */
    template <class T> void removeComponent(NodeHandle node) {
        if constexpr (std::is_same_v<T, MeshRenderer>)
            removeMeshRenderer(node);
        else if constexpr (std::is_same_v<T, Camera>)
            removeCamera(node);
        else if constexpr (std::is_same_v<T, Light>)
            removeLight(node);
        else if constexpr (std::is_same_v<T, SpriteRenderer>)
            removeSpriteRenderer(node);
        else if constexpr (std::is_same_v<T, SortingGroup>)
            removeSortingGroup(node);
        else
            static_assert(!std::is_same_v<T, T>, "Unsupported scene component");
    }

  private:
    /// 内部实现前置声明，公共头文件仅持有不透明指针。
    struct Impl;
    std::unique_ptr<Impl> impl_;
    /** @brief 设置图片组件。@param node 目标节点。@param component 组件值。@return 无。 */
    void setSpriteRenderer(NodeHandle node, const SpriteRenderer &component);
    /** @brief 设置排序组。@param node 目标节点。@param component 组件值。@return 无。 */
    void setSortingGroup(NodeHandle node, const SortingGroup &component);
    /** @brief 读取图片组件。@param node 目标节点。@return 组件副本，缺少时为空。 */
    std::optional<SpriteRenderer> spriteRenderer(NodeHandle node) const;
    /** @brief 读取排序组。@param node 目标节点。@return 组件副本，缺少时为空。 */
    std::optional<SortingGroup> sortingGroup(NodeHandle node) const;
    /** @brief 移除图片组件。@param node 目标节点。@return 无。 */
    void removeSpriteRenderer(NodeHandle node);
    /** @brief 移除排序组。@param node 目标节点。@return 无。 */
    void removeSortingGroup(NodeHandle node);
    /**
     * @brief 写入具体类型组件。
     * @param node 目标节点。
     * @param component 待保存的组件值。
     * @return 无。
     */
    void setMeshRenderer(NodeHandle node, const MeshRenderer &component);
    /**
     * @brief 写入具体类型组件。
     * @param node 目标节点。
     * @param component 待保存的组件值。
     * @return 无。
     */
    void setCamera(NodeHandle node, const Camera &component);
    /**
     * @brief 读取具体类型组件。
     * @param node 目标节点。
     * @return 组件副本；未添加时为空。
     */
    std::optional<MeshRenderer> meshRenderer(NodeHandle node) const;
    /**
     * @brief 读取具体类型组件。
     * @param node 目标节点。
     * @return 组件副本；未添加时为空。
     */
    std::optional<Camera> camera(NodeHandle node) const;
    /**
     * @brief 移除具体类型组件。
     * @param node 目标节点。
     * @return 无。
     */
    void removeMeshRenderer(NodeHandle node);
    /**
     * @brief 移除具体类型组件。
     * @param node 目标节点。
     * @return 无。
     */
    void removeCamera(NodeHandle node);
    /**
     * @brief 写入具体类型组件。
     * @param node 目标节点。
     * @param component 待保存的组件值。
     * @return 无。
     */
    void setLight(NodeHandle node, const Light &component);
    /**
     * @brief 读取具体类型组件。
     * @param node 目标节点。
     * @return 组件副本；未添加时为空。
     */
    std::optional<Light> light(NodeHandle node) const;
    /**
     * @brief 移除具体类型组件。
     * @param node 目标节点。
     * @return 无。
     */
    void removeLight(NodeHandle node);
    friend class RenderExtractor;
};
} // namespace renderer

#endif // RENDERER_INCLUDE_RENDERER_SCENE_HPP_INCLUDED
