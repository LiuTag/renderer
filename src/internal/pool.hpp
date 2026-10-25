/**
 * @file
 * @brief 实现以代际句柄索引的资源槽位池与内部身份分配，不作为 SDK 导出。
 */
#ifndef RENDERER_SRC_INTERNAL_POOL_HPP_INCLUDED
#define RENDERER_SRC_INTERNAL_POOL_HPP_INCLUDED

#include "renderer/handle.hpp"
#include <atomic>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace renderer::detail {
/**
 * @brief 分配进程内共享使用的递增身份编号，不承担内存发布同步。
 * @return 新的非零身份编号。
 */
inline uint64_t uniqueId() {
    static std::atomic<uint64_t> next{1};
    // 只要求编号唯一，不承担对象发布或数据同步职责。
    return next.fetch_add(1, std::memory_order_relaxed);
}

/**
 * @brief 通过 owner、代际和槽位存活性校验资源句柄的内部资源池。
 * @tparam T 池内资源值类型。
 * @tparam Tag 句柄种类标签类型。
 */
template <class T, class Tag> class Pool {
    /**
     * @brief 资源池中带代际编号与可选存活值的槽位。
     */
    struct Slot {
        uint64_t generation = 1;
        std::optional<T> value;
    };
    uint64_t owner_ = uniqueId();
    std::vector<Slot> slots_;
    std::vector<uint32_t> free_;

  public:
    /**
     * @brief 创建具有独立 owner 身份的空资源池。
     * @return 无（构造函数）。
     */
    Pool() = default;
    /**
     * @brief 禁止复制，避免重复的资源身份。
     * @param other 禁止复制的源资源池。
     * @return 无；此接口已删除。
     */
    Pool(const Pool &other) = delete;
    /**
     * @brief 禁止复制赋值，避免重复的资源身份。
     * @param other 禁止复制的源资源池。
     * @return 无；此接口已删除。
     */
    Pool &operator=(const Pool &other) = delete;
    uint64_t owner() const {
        return owner_;
    }
    template <class F> void forEach(F &&f) const {
        for (const auto &slot : slots_)
            if (slot.value)
                f(*slot.value);
    }
    /**
     * @brief 在空闲或新增槽位构造资源。
     * @tparam Args T 构造参数的类型列表。
     * @param args 转发给 T 构造函数的参数。
     * @return 包含池 owner、槽位索引和当前代际的新句柄。
     */
    template <class... Args> Handle<Tag> create(Args &&...args) {
        T value(std::forward<Args>(args)...);
        if (free_.empty()) {
            if (slots_.size() >= std::numeric_limits<uint32_t>::max())
                throw std::length_error("Handle pool exhausted");
            const auto index = static_cast<uint32_t>(slots_.size());
            slots_.push_back(Slot{1, std::move(value)});
            return {index, 1, owner_};
        }
        const auto index = free_.back();
        slots_[index].value.emplace(std::move(value));
        free_.pop_back();
        return {index, slots_[index].generation, owner_};
    }
    /**
     * @brief 校验所有者、索引、代际与存活标记。
     * @param h 待检查句柄。
     * @return 句柄指向当前池中的存活值时为 true。
     */
    bool valid(Handle<Tag> h) const {
        return h.owner == owner_ && h.index < slots_.size() &&
               slots_[h.index].generation == h.generation && slots_[h.index].value.has_value();
    }
    /**
     * @brief 取得可修改资源；无效或跨池句柄抛出异常。
     * @param h 有效资源句柄。
     * @return 槽位内资源的引用。
     */
    T &get(Handle<Tag> h) {
        if (!valid(h))
            throw std::invalid_argument("Stale handle or handle belongs to another owner");
        return *slots_[h.index].value;
    }
    /**
     * @brief 取得只读资源；无效或跨池句柄抛出异常。
     * @param h 有效资源句柄。
     * @return 槽位内资源的只读引用。
     */
    const T &get(Handle<Tag> h) const {
        if (!valid(h))
            throw std::invalid_argument("Stale handle or handle belongs to another owner");
        return *slots_[h.index].value;
    }
    /**
     * @brief 销毁资源并推进代际；代际耗尽的槽位不再复用。
     * @param h 当前池的有效资源句柄。
     * @return 无。
     */
    void erase(Handle<Tag> h) {
        get(h);
        // 代际耗尽的槽位不再复用，防止回绕后旧句柄重新命中。
        if (slots_[h.index].generation != std::numeric_limits<uint64_t>::max())
            free_.push_back(h.index);
        slots_[h.index].value.reset();
        ++slots_[h.index].generation;
    }
};
} // namespace renderer::detail

#endif // RENDERER_SRC_INTERNAL_POOL_HPP_INCLUDED
