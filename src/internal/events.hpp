/**
 * @file
 * @brief 实现窗口事件排队、批量分派与订阅内部桥接，不作为 SDK 导出。
 */
#ifndef RENDERER_SRC_INTERNAL_EVENTS_HPP_INCLUDED
#define RENDERER_SRC_INTERNAL_EVENTS_HPP_INCLUDED
#include "renderer/events.hpp"
#include <algorithm>
#include <atomic>
#include <deque>
#include <exception>
#include <stdexcept>
#include <utility>

namespace renderer::detail {
/**
 * @brief 持有处理函数并通过原子 active 标记控制取消的订阅节点。
 */
struct EventListener {
    WindowEventHandler handler;
    std::atomic<bool> active{true};
    /**
     * @brief 保存事件处理函数并初始化为有效订阅。
     * @param callback 需要接收事件的处理函数。
     * @return 无（构造函数）。
     */
    explicit EventListener(WindowEventHandler callback) : handler(std::move(callback)) {}
};

// 队列及订阅列表只由窗口主线程操作；令牌仅通过原子标记取消订阅。
/**
 * @brief 仅在窗口主线程操作事件和订阅列表的批量分派器。
 */
struct EventDispatcher {
    std::vector<std::shared_ptr<EventListener>> listeners;
    std::deque<WindowEvent> pending;
    std::exception_ptr error;
    bool open = true, dispatching = false;

    /**
     * @brief 将事件移入待分派队列；分配错误延迟交给 dispatch 处理。
     * @param event 拥有自身数据的窗口事件。
     * @return 无。
     */
    void post(WindowEvent event) noexcept {
        if (!open)
            return;
        try {
            pending.push_back(std::move(event));
        } catch (...) {
            recordError(std::current_exception());
        }
    }
    /**
     * @brief 保留本批次最早的错误。
     * @param exception 需要延迟抛出的异常。
     * @return 无。
     */
    void recordError(std::exception_ptr exception) noexcept {
        if (!error)
            error = std::move(exception);
    }
    /**
     * @brief 从订阅列表移除已断开的监听器。
     * @return 无。
     */
    void collect() {
        std::erase_if(listeners, [](const auto &listener) { return !listener->active.load(); });
    }
    /**
     * @brief 关闭分派器、断开全部订阅并清空待处理事件。
     * @return 无。
     */
    void close() noexcept {
        open = false;
        for (const auto &listener : listeners)
            listener->active = false;
        listeners.clear();
        pending.clear();
        error = {};
    }
    /**
     * @brief 关闭事件分派器并取消全部订阅。
     * @return 无（析构函数）。
     */
    ~EventDispatcher() {
        close();
    }

    /**
     * @brief 向本批订阅者分派本批事件，结束后重抛首个错误；不允许重入。
     * @return 无。
     */
    void dispatch() {
        if (dispatching)
            throw std::logic_error("Window event dispatch cannot be reentered");
        collect();
        // 固定本批订阅者；在回调中新订阅或产生的事件从下一次 poll 开始处理。
        auto current = listeners;
        std::deque<WindowEvent> batch;
        batch.swap(pending);
        auto firstError = std::exchange(error, {});
        dispatching = true;
        /**
         * @brief 退出分派作用域时自动复位重入标记的局部守卫。
         */
        struct Guard {
            bool &value;
            /**
             * @brief 离开分派作用域时恢复重入标记。
             * @return 无（析构函数）。
             */
            ~Guard() {
                value = false;
            }
        } guard{dispatching};
        for (const auto &event : batch) {
            for (const auto &listener : current) {
                if (!open)
                    break;
                if (!listener->active.load())
                    continue;
                try {
                    listener->handler(event);
                } catch (...) {
                    if (!firstError)
                        firstError = std::current_exception();
                }
            }
        }
        collect();
        // 一个订阅者抛出异常不阻止其他订阅者；完成当前批次后再抛给应用。
        if (firstError)
            std::rethrow_exception(firstError);
    }
};
/**
 * @brief 构建公开订阅令牌与内部监听器关系的友元桥。
 */
struct EventAccess {
    /**
     * @brief 创建监听器并返回其 RAII 订阅令牌。
     * @param dispatcher 存活的事件分派器。
     * @param handler 非空事件处理函数。
     * @return 连接到新监听器的独占令牌。
     */
    static EventSubscription subscribe(EventDispatcher &dispatcher, WindowEventHandler handler);
};
} // namespace renderer::detail

#endif // RENDERER_SRC_INTERNAL_EVENTS_HPP_INCLUDED
