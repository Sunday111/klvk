#pragma once

#include <bit>
#include <cstddef>
#include <memory>
#include <refl/get_type_info.hpp>
#include <vector>

#include "ankerl/unordered_dense.h"
#include "klvk/events/event_listener_interface.hpp"
#include "klvk/integral_aliases.hpp"
#include "refl/type.hpp"

namespace klvk::events
{

class EventManager;

class EventSubscription
{
public:
    EventSubscription() = default;
    EventSubscription(const EventSubscription&) = delete;
    EventSubscription(EventSubscription&& other) noexcept;
    ~EventSubscription();

    EventSubscription& operator=(const EventSubscription&) = delete;
    EventSubscription& operator=(EventSubscription&& other) noexcept;

    void Reset() noexcept;
    [[nodiscard]] explicit operator bool() const noexcept { return manager_ != nullptr; }

private:
    friend EventManager;
    EventSubscription(EventManager& manager, IEventListener& listener) noexcept;

    EventManager* manager_ = nullptr;
    IEventListener* listener_ = nullptr;
};

class EventManager
{
public:
    EventManager() = default;
    EventManager(const EventManager&) = delete;
    EventManager(EventManager&&) = delete;
    ~EventManager() = default;

    EventManager& operator=(const EventManager&) = delete;
    EventManager& operator=(EventManager&&) = delete;

    void Emit(const refl::Type* event_type, const void* event_data);

    [[nodiscard]] EventSubscription AddEventListener(std::unique_ptr<IEventListener> listener);
    [[nodiscard]] EventSubscription AddEventListener(IEventListener& listener);
    void UpdateListenTypes(IEventListener& listener);

    template <typename EventType>
    void Emit(const EventType& event)
    {
        Emit(refl::GetTypeInfo<EventType>(), &event);
    }

private:
    friend EventSubscription;

    struct ListenerInfo
    {
        ankerl::unordered_dense::set<const refl::Type*> registered_types;
        bool active = false;
        bool pending_add = false;
        bool pending_update = false;
        bool pending_remove = false;
    };

    struct ListenerTypeEntry
    {
        IEventListener* listener;
        IEventListener::CallbackFunction callback;
    };

    void Activate(IEventListener& listener);
    void UpdateListenTypesNow(IEventListener& listener);
    void Release(IEventListener& listener) noexcept;
    void RemoveNow(IEventListener& listener) noexcept;
    void ClearListenerTypes(IEventListener& listener) noexcept;
    void FlushPendingMutations();
    void StopListeningEventType(IEventListener& listener, const refl::Type* type) noexcept;

    ankerl::unordered_dense::map<const refl::Type*, std::vector<ListenerTypeEntry>> type_lookup_;
    ankerl::unordered_dense::map<IEventListener*, ListenerInfo> all_listeners_;

    struct PtrHasher
    {
        using is_transparent = void;

        [[nodiscard]] static auto operator()(const std::unique_ptr<IEventListener>& ptr) noexcept -> u64
        {
            return operator()(ptr.get());
        }

        [[nodiscard]] static auto operator()(const IEventListener* ptr) noexcept -> u64
        {
            return ankerl::unordered_dense::hash<size_t>{}(std::bit_cast<size_t>(ptr));
        }
    };

    struct PtrComparator
    {
        using is_transparent = void;

        static bool operator()(
            const std::unique_ptr<IEventListener>& a,
            const std::unique_ptr<IEventListener>& b) noexcept
        {
            return a == b;
        }

        static bool operator()(const IEventListener* a, const IEventListener* b) noexcept { return a == b; }

        static bool operator()(const std::unique_ptr<IEventListener>& a, const IEventListener* b) noexcept
        {
            return a.get() == b;
        }

        static bool operator()(const IEventListener* a, const std::unique_ptr<IEventListener>& b) noexcept
        {
            return a == b.get();
        }
    };

    ankerl::unordered_dense::set<std::unique_ptr<IEventListener>, PtrHasher, PtrComparator> owned_listeners_;
    size_t dispatch_depth_ = 0;
};

}  // namespace klvk::events
