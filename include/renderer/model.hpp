/**
 * @file
 * @brief 声明可重复实例化的静态模型模板与实例节点集合。
 */
#ifndef RENDERER_INCLUDE_RENDERER_MODEL_HPP_INCLUDED
#define RENDERER_INCLUDE_RENDERER_MODEL_HPP_INCLUDED
#include "renderer/scene.hpp"
#include <memory>
#include <string>
#include <vector>

namespace renderer {
/**
 * @brief 一次模型实例化创建的根节点、模型节点和相机/灯光句柄集合。
 */
struct ModelInstance {
    NodeHandle root;
    std::vector<NodeHandle> nodes, cameras, lights;
};
// 可复制的静态模型模板；instantiate 复用网格/材质句柄，为每次实例创建独立节点。
// 模板保存资源句柄，因此后续提取实例仍要求对应 AssetStore 中的句柄有效。
/**
 * @brief 共享不可变 PImpl 的静态模型模板，实例化时复用资源并创建独立场景节点。
 */
class Model {
  public:
    /**
     * @brief 创建空模型模板。
     * @return 无（构造函数）。
     */
    Model();
    /**
     * @brief 释放本对象持有的状态与资源引用。
     * @return 无（析构函数）。
     */
    ~Model();
    /**
     * @brief 共享复制构造，共享不可变内部数据。
     * @param other 待复制的同类型对象。
     * @return 无（构造函数）。
     */
    Model(const Model &other);
    /**
     * @brief 共享复制赋值，共享不可变内部数据。
     * @param other 待复制的同类型对象。
     * @return 当前对象的引用。
     */
    Model &operator=(const Model &other);
    /**
     * @brief 转移构造对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 无（构造函数）。
     */
    Model(Model &&other) noexcept;
    /**
     * @brief 转移赋值对象，接管 other 的内部状态。
     * @param other 待转移的同类型对象；转移后仅保证可析构或重新赋值。
     * @return 当前对象的引用。
     */
    Model &operator=(Model &&other) noexcept;
    /**
     * @brief 读取模型模板内的节点数。
     * @return 模板节点数量；空模板为零。
     */
    size_t nodeCount() const;
    /**
     * @brief 读取导入过程中产生的非致命提示。
     * @return 提示字符串列表的副本。
     */
    std::vector<std::string> warnings() const;
    // 在 parent 下创建实例根节点；返回的相机/灯光列表可用于选择视图或调整组件。
    /**
     * @brief 在场景中实例化模板，共享网格和材质；失败时回滚新建节点。
     * @param scene 接收实例节点的可写场景。
     * @param parent 该场景中有效的父节点。
     * @param name 本次实例根节点的名称。
     * @return 新建根节点、模型节点及相机/灯光节点句柄列表。
     */
    ModelInstance instantiate(Scene &scene, NodeHandle parent,
                              std::string_view name = "Model") const;

  private:
    /// 内部实现前置声明，公共头文件仅持有不透明指针。
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    friend class AssetStore;
};
} // namespace renderer

#endif // RENDERER_INCLUDE_RENDERER_MODEL_HPP_INCLUDED
