#include "renderer/events.hpp"
#include "internal/events.hpp"

namespace renderer {
struct EventSubscription::Impl {
    std::weak_ptr<detail::EventListener> listener;
    explicit Impl(const std::shared_ptr<detail::EventListener> &value) : listener(value) {}
    void disconnect() noexcept {
        if (auto value = listener.lock())
            value->active = false;
    }
    ~Impl() {
        disconnect();
    } // 移动赋值替换旧 Impl 时也必须断开旧订阅。
};
EventSubscription::EventSubscription() = default;
EventSubscription::EventSubscription(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
EventSubscription::~EventSubscription() = default;
EventSubscription::EventSubscription(EventSubscription &&) noexcept = default;
EventSubscription &EventSubscription::operator=(EventSubscription &&) noexcept = default;
void EventSubscription::disconnect() noexcept {
    if (impl_)
        impl_->disconnect();
}
bool EventSubscription::connected() const noexcept {
    if (impl_)
        if (auto listener = impl_->listener.lock())
            return listener->active.load();
    return false;
}
EventSubscription detail::EventAccess::subscribe(EventDispatcher &dispatcher,
                                                 WindowEventHandler handler) {
    if (!handler)
        throw std::invalid_argument("Window event handler must not be empty");
    if (!dispatcher.open)
        throw std::logic_error("Window event dispatcher is closed");
    auto listener = std::make_shared<EventListener>(std::move(handler));
    EventSubscription subscription(std::make_unique<EventSubscription::Impl>(listener));
    dispatcher.collect();
    dispatcher.listeners.push_back(std::move(listener));
    return subscription;
}
} // namespace renderer
