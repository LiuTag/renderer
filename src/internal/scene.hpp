/**
 * @file
 * @brief 定义场景节点存储、树形链接和派生数据版本缓存，不作为 SDK 导出。
 */
#ifndef RENDERER_SRC_INTERNAL_SCENE_HPP_INCLUDED
#define RENDERER_SRC_INTERNAL_SCENE_HPP_INCLUDED
#include "pool.hpp"
#include "renderer/scene.hpp"

namespace renderer {
/**
 * @brief 保存节点池、拓扑遍历和场景派生数据缓存的私有实现。
 */
struct Scene::Impl {
    /**
     * @brief 带树形链接、组件、局部/世界变换及版本缓存的内部场景节点。
     */
    struct Node {
        std::string name;
        NodeHandle parent, firstChild, nextSibling, previousSibling;
        Mat4 local = Mat4::identity(), world = Mat4::identity();
        std::optional<MeshRenderer> meshRenderer;
        std::optional<SpriteRenderer> spriteRenderer;
        std::optional<SortingGroup> sortingGroup;
        std::optional<Camera> camera;
        std::optional<Light> light;
        bool visible = true, effectiveVisible = true;
        // 本地版本或父世界版本改变才重算矩阵，避免每次提取都求逆/连乘。
        uint64_t localVersion = 1, appliedLocalVersion = 0, parentWorldVersion = 0,
                 worldVersion = 0;
    };
    detail::Pool<Node, NodeTag> nodes;
    NodeHandle root;
    uint64_t revision = 1, derivedRevision = 0, worldVersion = 0;
    bool topologyDirty = true;
    std::vector<NodeHandle> traversal; // 拓扑变化后重建，保证父节点先于子节点。
    /**
     * @brief 按拓扑与版本更新世界矩阵、继承可见性及遍历缓存。
     * @return 无。
     */
    void updateDerived();
    /**
     * @brief 从父节点的兄弟链中移除节点，不销毁其子树。
     * @param node 待解除父子关系的有效节点。
     * @return 无。
     */
    void detach(NodeHandle node);
    /**
     * @brief 将节点插入父节点的子链。
     * @param node 已解除旧父子关系的节点。
     * @param parent 新的有效父节点。
     * @return 无。
     */
    void attach(NodeHandle node, NodeHandle parent);
};
} // namespace renderer

#endif // RENDERER_SRC_INTERNAL_SCENE_HPP_INCLUDED
