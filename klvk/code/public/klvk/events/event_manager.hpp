#pragma once

#include <refl/get_type_info.hpp>

#include "refl/type.hpp"
#include "ankerl/unordered_dense.h"
#include "klvk/events/event_listener_interface.hpp"
#include "klvk/integral_aliases.hpp"

namespace klvk::events
{

class EventManager
{
public:
    struct ListenerInfo
    {
        ankerl::unordered_dense::set<const refl::Type*> registered_types;
    };

    struct ListenerTypeEntry
    {
        IEventListener* listener;
        IEventListener::CallbackFunction callback;
    };

    void Emit(const refl::Type* event_type, const void* event_data);

    // Registers event listeners and takes ownership on the object
    [[nodiscard("Use return value to remove event listener")]] IEventListener* AddEventListener(
        std::unique_ptr<IEventListener> listener);

    // Registers listener by raw pointer but it is caller's responsibility
    // to guarantee object lifetime until RemoveListener is called
    IEventListener* AddEventListener(IEventListener& listener);

    void RemoveListener(IEventListener* listener);
    void UpdateListenTypes(IEventListener* listener);

    template <typename EventType>
    void Emit(const EventType& event)
    {
        Emit(refl::GetTypeInfo<EventType>(), &event);
    }

private:
    void StopListeningEventType(IEventListener* listener, const refl::Type* type);

private:
    ankerl::unordered_dense::map<const refl::Type*, std::vector<ListenerTypeEntry>> type_lookup_;
    ankerl::unordered_dense::map<IEventListener*, ListenerInfo> all_listeners_;

    struct PtrHasher
    {
        using is_transparent = void;  // enable heterogeneous overloads

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
        using is_transparent = void;  // enable heterogeneous overloads

        static bool operator()(const std::unique_ptr<IEventListener>& a, const std::unique_ptr<IEventListener>& b) noexcept
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
};

}  // namespace klvk::events
