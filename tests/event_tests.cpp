#include "internal/events.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace renderer;
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void throws(F &&function) {
    bool caught = false;
    try {
        function();
    } catch (const std::exception &) {
        caught = true;
    }
    require(caught, "Expected an event error");
}
} // namespace
int main() {
    try {
        {
            detail::EventDispatcher events;
            std::vector<int> order;
            std::vector<WindowEvent> captured;
            auto first = detail::EventAccess::subscribe(events, [&](const auto &event) {
                order.push_back(1);
                captured.push_back(event);
            });
            auto second =
                detail::EventAccess::subscribe(events, [&](const auto &) { order.push_back(2); });
            events.post(
                KeyEvent{Key::W, 17, InputAction::Press, Modifiers::Control | Modifiers::Shift});
            events.post(TextInputEvent{U'中'});
            FileDropEvent files{{"first.obj"}};
            events.post(files);
            files.paths.front() = "changed.obj";
            require(order.empty(), "Events dispatched before polling");
            events.dispatch();
            require(order == std::vector<int>({1, 2, 1, 2, 1, 2}), "Listener/event order changed");
            const auto &key = std::get<KeyEvent>(captured[0]);
            require(key.key == Key::W && key.scanCode == 17 &&
                        hasModifier(key.modifiers, Modifiers::Control) &&
                        hasModifier(key.modifiers, Modifiers::Shift) &&
                        std::get<TextInputEvent>(captured[1]).codepoint == U'中' &&
                        std::get<FileDropEvent>(captured[2]).paths.front() == "first.obj",
                    "Event payloads/modifiers were not retained");
        }
        {
            detail::EventDispatcher events;
            int selfCalls = 0, removedCalls = 0, remainingCalls = 0;
            EventSubscription self, removed;
            self = detail::EventAccess::subscribe(events, [&](const auto &) {
                ++selfCalls;
                self.disconnect();
                removed.disconnect();
            });
            removed = detail::EventAccess::subscribe(events, [&](const auto &) { ++removedCalls; });
            auto remaining =
                detail::EventAccess::subscribe(events, [&](const auto &) { ++remainingCalls; });
            events.post(KeyEvent{});
            events.post(KeyEvent{});
            events.dispatch();
            require(selfCalls == 1 && removedCalls == 0 && remainingCalls == 2,
                    "Disconnect during a callback was not applied immediately");
            {
                auto temporary =
                    detail::EventAccess::subscribe(events, [&](const auto &) { ++removedCalls; });
                require(temporary.connected(), "New subscription is disconnected");
            }
            events.post(KeyEvent{});
            events.dispatch();
            require(removedCalls == 0, "RAII subscription did not disconnect");
        }
        {
            detail::EventDispatcher events;
            EventSubscription late;
            int earlyCalls = 0, lateCalls = 0;
            auto early = detail::EventAccess::subscribe(events, [&](const auto &) {
                ++earlyCalls;
                if (!late.connected()) {
                    late =
                        detail::EventAccess::subscribe(events, [&](const auto &) { ++lateCalls; });
                    events.post(KeyEvent{Key::B});
                }
            });
            events.post(KeyEvent{Key::A});
            events.post(KeyEvent{Key::C});
            events.dispatch();
            require(earlyCalls == 2 && lateCalls == 0, "New listener received the current batch");
            events.dispatch();
            require(earlyCalls == 3 && lateCalls == 1,
                    "Callback-generated event was lost or reentered");
        }
        {
            detail::EventDispatcher events;
            int delivered = 0;
            bool fail = true;
            auto first = detail::EventAccess::subscribe(events, [&](const auto &) {
                if (fail)
                    throw std::runtime_error("first");
            });
            auto second = detail::EventAccess::subscribe(events, [&](const auto &) {
                ++delivered;
                if (fail)
                    throw std::runtime_error("second");
            });
            events.post(KeyEvent{});
            events.post(KeyEvent{});
            try {
                events.dispatch();
                require(false, "Handler exception was swallowed");
            } catch (const std::runtime_error &error) {
                require(std::string(error.what()) == "first",
                        "First handler error was not preserved");
            }
            require(delivered == 2, "Handler error prevented delivery to other listeners");
            fail = false;
            events.post(KeyEvent{});
            events.dispatch();
            require(delivered == 3, "Dispatcher did not recover after handler failure");
        }
        {
            detail::EventDispatcher events;
            int a = 0, b = 0;
            auto first = detail::EventAccess::subscribe(events, [&](const auto &) { ++a; });
            auto second = detail::EventAccess::subscribe(events, [&](const auto &) { ++b; });
            auto moved = std::move(first);
            require(!first.connected() && moved.connected(), "Subscription move lost ownership");
            second = std::move(moved);
            events.post(KeyEvent{});
            events.dispatch();
            require(a == 1 && b == 0, "Move assignment kept the replaced subscription active");
            second.disconnect();
            second.disconnect();
            require(!second.connected(), "Disconnect is not idempotent");
        }
        {
            detail::EventDispatcher events;
            auto subscription = detail::EventAccess::subscribe(events, [](const auto &) {});
            events.close();
            require(!subscription.connected(), "Closed dispatcher kept subscriptions connected");
            events.post(KeyEvent{});
            require(events.pending.empty(), "Closed dispatcher accepted an event");
            throws([&] { detail::EventAccess::subscribe(events, [](const auto &) {}); });
            detail::EventDispatcher other;
            throws([&] { detail::EventAccess::subscribe(other, {}); });
        }
        {
            detail::EventDispatcher events;
            int delivered = 0;
            auto reentrant =
                detail::EventAccess::subscribe(events, [&](const auto &) { events.dispatch(); });
            auto observer =
                detail::EventAccess::subscribe(events, [&](const auto &) { ++delivered; });
            events.post(KeyEvent{});
            throws([&] { events.dispatch(); });
            require(delivered == 1, "Reentrancy failure stopped the batch");
            reentrant.disconnect();
            events.post(KeyEvent{});
            events.dispatch();
            require(delivered == 2, "Reentrancy guard was not released");
        }
        {
            detail::EventDispatcher events;
            int delivered = 0;
            auto subscription =
                detail::EventAccess::subscribe(events, [&](const auto &) { ++delivered; });
            std::thread worker([&] { subscription.disconnect(); });
            worker.join();
            events.post(KeyEvent{});
            events.dispatch();
            require(delivered == 0 && !subscription.connected(),
                    "Cross-thread cancellation failed");
        }
        std::cout << "All 8 event subscription tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "EVENT TEST FAILED: " << error.what() << '\n';
        return 1;
    }
}
