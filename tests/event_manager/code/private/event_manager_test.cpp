#include "klvk/events/event_manager.hpp"

#include <fmt/core.h>

#include <edt/guid.hpp>
#include <functional>
#include <memory>
#include <refl/reflection_provider.hpp>
#include <refl/static_type/class.hpp>
#include <stdexcept>
#include <string>
#include <string_view>

#include "klvk/events/event_listener.hpp"

namespace event_manager_test
{

struct EventA
{
};

struct EventB
{
};

}  // namespace event_manager_test

namespace refl
{

template <>
struct TypeReflectionProvider<event_manager_test::EventA>
{
    [[nodiscard]] inline constexpr static auto ReflectType()
    {
        return StaticClassTypeInfo<event_manager_test::EventA>(
            "EventA",
            edt::GUID::Create("36F7A0D9-44B9-4EF7-B655-D3838863FB58"));
    }
};

template <>
struct TypeReflectionProvider<event_manager_test::EventB>
{
    [[nodiscard]] inline constexpr static auto ReflectType()
    {
        return StaticClassTypeInfo<event_manager_test::EventB>(
            "EventB",
            edt::GUID::Create("F293AD1F-A6DF-42D1-B4AD-E58469925E02"));
    }
};

}  // namespace refl

namespace
{

using event_manager_test::EventA;
using event_manager_test::EventB;

void Ensure(bool condition, std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

void TestLifetimeAndMoves()
{
    klvk::events::EventManager manager;
    size_t calls = 0;
    auto listener = klvk::events::EventListener<EventA>::PtrFromFunctions([&](const EventA&) { ++calls; });
    {
        auto subscription = manager.AddEventListener(*listener);
        manager.Emit(EventA{});
        Ensure(calls == 1, "registered listener did not receive an event");

        klvk::events::EventSubscription moved = std::move(subscription);
        Ensure(!subscription && moved, "moving a subscription did not transfer ownership");
        manager.Emit(EventA{});
        Ensure(calls == 2, "moved subscription stopped receiving events");
    }
    manager.Emit(EventA{});
    Ensure(calls == 2, "destroyed subscription remained registered");
}

void TestRemovalDuringDispatch()
{
    klvk::events::EventManager manager;
    size_t first_calls = 0;
    size_t second_calls = 0;
    klvk::events::EventSubscription second_subscription;
    auto first = klvk::events::EventListener<EventA>::PtrFromFunctions(
        [&](const EventA&)
        {
            ++first_calls;
            second_subscription.Reset();
        });
    auto second = klvk::events::EventListener<EventA>::PtrFromFunctions([&](const EventA&) { ++second_calls; });
    auto first_subscription = manager.AddEventListener(*first);
    second_subscription = manager.AddEventListener(*second);

    manager.Emit(EventA{});
    Ensure(first_calls == 1, "first listener was not invoked");
    Ensure(second_calls == 0, "listener removed during dispatch was invoked later in the same dispatch");
    manager.Emit(EventA{});
    Ensure(first_calls == 2 && second_calls == 0, "removed listener was retained after dispatch");
}

void TestAdditionDuringDispatch()
{
    klvk::events::EventManager manager;
    size_t added_calls = 0;
    std::unique_ptr<klvk::events::IEventListener> added;
    klvk::events::EventSubscription added_subscription;
    auto creator = klvk::events::EventListener<EventA>::PtrFromFunctions(
        [&](const EventA&)
        {
            if (added) return;
            added = klvk::events::EventListener<EventA>::PtrFromFunctions([&](const EventA&) { ++added_calls; });
            added_subscription = manager.AddEventListener(*added);
        });
    auto creator_subscription = manager.AddEventListener(*creator);

    manager.Emit(EventA{});
    Ensure(added_calls == 0, "listener added during dispatch observed the active event");
    manager.Emit(EventA{});
    Ensure(added_calls == 1, "deferred listener addition was not activated after dispatch");
}

void TestSelfRemovalAndNestedDispatch()
{
    klvk::events::EventManager manager;
    size_t self_calls = 0;
    klvk::events::EventSubscription self_subscription;
    auto self = klvk::events::EventListener<EventA>::PtrFromFunctions(
        [&](const EventA&)
        {
            ++self_calls;
            self_subscription.Reset();
            manager.Emit(EventB{});
        });
    self_subscription = manager.AddEventListener(*self);

    size_t nested_calls = 0;
    auto nested = klvk::events::EventListener<EventB>::PtrFromFunctions([&](const EventB&) { ++nested_calls; });
    auto nested_subscription = manager.AddEventListener(*nested);
    manager.Emit(EventA{});
    Ensure(self_calls == 1 && nested_calls == 1, "self-removal corrupted nested dispatch");
    manager.Emit(EventA{});
    Ensure(self_calls == 1, "self-removed listener remained registered");
}

void TestRemovalBeforeNestedDispatch()
{
    klvk::events::EventManager manager;
    size_t nested_calls = 0;
    klvk::events::EventSubscription nested_subscription;
    auto remover = klvk::events::EventListener<EventA>::PtrFromFunctions(
        [&](const EventA&)
        {
            nested_subscription.Reset();
            manager.Emit(EventB{});
        });
    auto nested = klvk::events::EventListener<EventB>::PtrFromFunctions([&](const EventB&) { ++nested_calls; });
    auto remover_subscription = manager.AddEventListener(*remover);
    nested_subscription = manager.AddEventListener(*nested);

    manager.Emit(EventA{});
    Ensure(nested_calls == 0, "listener removed by an outer dispatch observed a nested event");
}

void TestExceptionFlushesMutations()
{
    klvk::events::EventManager manager;
    size_t added_calls = 0;
    std::unique_ptr<klvk::events::IEventListener> added;
    klvk::events::EventSubscription added_subscription;
    auto throwing = klvk::events::EventListener<EventA>::PtrFromFunctions(
        [&](const EventA&)
        {
            added = klvk::events::EventListener<EventA>::PtrFromFunctions([&](const EventA&) { ++added_calls; });
            added_subscription = manager.AddEventListener(*added);
            throw std::runtime_error("expected");
        });
    auto throwing_subscription = manager.AddEventListener(*throwing);

    try
    {
        manager.Emit(EventA{});
        Ensure(false, "listener exception was swallowed");
    }
    catch (const std::runtime_error&)
    {
    }
    throwing_subscription.Reset();
    manager.Emit(EventA{});
    Ensure(added_calls == 1, "mutation queued before an exception was not flushed");
}

class SwitchingListener final : public klvk::events::IEventListener
{
public:
    std::vector<const refl::Type*> GetEventTypes() const override
    {
        return {listen_to_b ? refl::GetTypeInfo<EventB>() : refl::GetTypeInfo<EventA>()};
    }

    CallbackFunction MakeCallbackFunction(size_t) override
    {
        return [](IEventListener* listener, const void*)
        {
            ++static_cast<SwitchingListener*>(listener)->calls;
        };
    }

    bool listen_to_b = false;
    size_t calls = 0;
};

void TestListenerTypeUpdateDuringDispatch()
{
    klvk::events::EventManager manager;
    SwitchingListener switching;
    auto switching_subscription = manager.AddEventListener(switching);
    auto updater = klvk::events::EventListener<EventA>::PtrFromFunctions(
        [&](const EventA&)
        {
            switching.listen_to_b = true;
            manager.UpdateListenTypes(switching);
            manager.Emit(EventB{});
        });
    auto updater_subscription = manager.AddEventListener(*updater);

    manager.Emit(EventA{});
    Ensure(switching.calls == 1, "listener type update affected the event already being dispatched");
    manager.Emit(EventA{});
    Ensure(switching.calls == 1, "listener retained an event type removed during dispatch");
    manager.Emit(EventB{});
    Ensure(switching.calls == 2, "listener type update was not applied after dispatch");
}

class OwnedListener final : public klvk::events::IEventListener
{
public:
    OwnedListener(std::function<void()> callback, bool& destroyed)
        : callback_(std::move(callback)),
          destroyed_(&destroyed)
    {
    }

    ~OwnedListener() override { *destroyed_ = true; }

    std::vector<const refl::Type*> GetEventTypes() const override { return {refl::GetTypeInfo<EventA>()}; }

    CallbackFunction MakeCallbackFunction(size_t) override
    {
        return [](IEventListener* listener, const void*)
        {
            static_cast<OwnedListener*>(listener)->callback_();
        };
    }

private:
    std::function<void()> callback_;
    bool* destroyed_ = nullptr;
};

void TestOwnedListenerDestructionIsDeferred()
{
    klvk::events::EventManager manager;
    bool destroyed = false;
    klvk::events::EventSubscription subscription;
    auto listener = std::make_unique<OwnedListener>(
        [&]
        {
            subscription.Reset();
            Ensure(!destroyed, "owned listener was destroyed inside its callback");
        },
        destroyed);
    subscription = manager.AddEventListener(std::move(listener));
    manager.Emit(EventA{});
    Ensure(destroyed, "owned listener removal was not completed after dispatch");
}

void Run()
{
    TestLifetimeAndMoves();
    TestRemovalDuringDispatch();
    TestAdditionDuringDispatch();
    TestSelfRemovalAndNestedDispatch();
    TestRemovalBeforeNestedDispatch();
    TestExceptionFlushesMutations();
    TestListenerTypeUpdateDuringDispatch();
    TestOwnedListenerDestructionIsDeferred();
}

}  // namespace

int main()
{
    try
    {
        Run();
        fmt::println("event manager tests passed");
        return 0;
    }
    catch (const std::exception& exception)
    {
        fmt::println(stderr, "{}", exception.what());
        return 1;
    }
}
