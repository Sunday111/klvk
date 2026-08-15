#include "klvk/events/event_manager.hpp"

#include <algorithm>
#include <utility>

#include "klvk/error_handling.hpp"

namespace klvk::events
{

EventSubscription::EventSubscription(EventManager& manager, IEventListener& listener) noexcept
    : manager_(&manager),
      listener_(&listener)
{
}

EventSubscription::EventSubscription(EventSubscription&& other) noexcept
    : manager_(std::exchange(other.manager_, nullptr)),
      listener_(std::exchange(other.listener_, nullptr))
{
}

EventSubscription::~EventSubscription()
{
    Reset();
}

EventSubscription& EventSubscription::operator=(EventSubscription&& other) noexcept
{
    if (this == &other) return *this;
    Reset();
    manager_ = std::exchange(other.manager_, nullptr);
    listener_ = std::exchange(other.listener_, nullptr);
    return *this;
}

void EventSubscription::Reset() noexcept
{
    if (manager_ != nullptr) manager_->Release(*listener_);
    manager_ = nullptr;
    listener_ = nullptr;
}

EventSubscription EventManager::AddEventListener(std::unique_ptr<IEventListener> listener)
{
    ErrorHandling::Ensure(listener != nullptr, "Attempt to register a null event listener");
    auto [owned, inserted] = owned_listeners_.insert(std::move(listener));
    ErrorHandling::Ensure(inserted, "Attempt to register the same listener twice");
    try
    {
        return AddEventListener(*owned->get());
    }
    catch (...)
    {
        owned_listeners_.erase(owned);
        throw;
    }
}

EventSubscription EventManager::AddEventListener(IEventListener& listener)
{
    auto [iterator, inserted] = all_listeners_.try_emplace(&listener);
    ErrorHandling::Ensure(inserted, "Attempt to register the same listener twice");
    iterator->second.pending_add = dispatch_depth_ != 0;
    if (dispatch_depth_ == 0)
    {
        try
        {
            Activate(listener);
        }
        catch (...)
        {
            all_listeners_.erase(iterator);
            throw;
        }
    }
    return EventSubscription{*this, listener};
}

void EventManager::Activate(IEventListener& listener)
{
    try
    {
        UpdateListenTypesNow(listener);
        all_listeners_.at(&listener).active = true;
    }
    catch (...)
    {
        ClearListenerTypes(listener);
        throw;
    }
}

void EventManager::UpdateListenTypes(IEventListener& listener)
{
    auto iterator = all_listeners_.find(&listener);
    ErrorHandling::Ensure(iterator != all_listeners_.end(), "Attempt to update an unregistered listener");
    ErrorHandling::Ensure(!iterator->second.pending_remove, "Attempt to update a removed listener");
    if (dispatch_depth_ == 0)
    {
        iterator->second.active = false;
        try
        {
            UpdateListenTypesNow(listener);
            iterator->second.active = true;
        }
        catch (...)
        {
            ClearListenerTypes(listener);
            throw;
        }
        return;
    }
    iterator->second.active = false;
    iterator->second.pending_update = true;
}

void EventManager::UpdateListenTypesNow(IEventListener& listener)
{
    auto iterator = all_listeners_.find(&listener);
    ErrorHandling::Ensure(iterator != all_listeners_.end(), "Attempt to update an unregistered listener");
    ListenerInfo& listener_info = iterator->second;

    const auto types = listener.GetEventTypes();
    ankerl::unordered_dense::set<const refl::Type*> requested_types;
    std::vector<std::pair<const refl::Type*, IEventListener::CallbackFunction>> requested_entries;
    requested_types.reserve(types.size());
    requested_entries.reserve(types.size());
    for (size_t index = 0; index != types.size(); ++index)
    {
        const refl::Type* type = types[index];
        ErrorHandling::Ensure(type != nullptr, "IEventListener::GetEventTypes returned null");
        ErrorHandling::Ensure(
            requested_types.insert(type).second,
            "IEventListener::GetEventTypes returned a duplicate");
        const auto callback = listener.MakeCallbackFunction(index);
        ErrorHandling::Ensure(callback != nullptr, "IEventListener::MakeCallbackFunction returned null");
        requested_entries.emplace_back(type, callback);
    }

    for (const refl::Type* type : listener_info.registered_types)
    {
        if (!requested_types.contains(type)) StopListeningEventType(listener, type);
    }
    for (const auto& [type, callback] : requested_entries)
    {
        if (listener_info.registered_types.contains(type))
        {
            auto& entries = type_lookup_.at(type);
            const auto entry = std::ranges::find(entries, &listener, &ListenerTypeEntry::listener);
            ErrorHandling::Ensure(entry != entries.end(), "Registered event listener entry is missing");
            entry->callback = callback;
        }
        else
        {
            type_lookup_[type].push_back({.listener = &listener, .callback = callback});
        }
    }
    listener_info.registered_types = std::move(requested_types);
}

void EventManager::Release(IEventListener& listener) noexcept
{
    const auto iterator = all_listeners_.find(&listener);
    if (iterator == all_listeners_.end()) return;
    iterator->second.active = false;
    if (dispatch_depth_ == 0)
    {
        RemoveNow(listener);
    }
    else
    {
        iterator->second.pending_add = false;
        iterator->second.pending_update = false;
        iterator->second.pending_remove = true;
    }
}

void EventManager::RemoveNow(IEventListener& listener) noexcept
{
    const auto iterator = all_listeners_.find(&listener);
    if (iterator == all_listeners_.end()) return;
    ClearListenerTypes(listener);
    all_listeners_.erase(iterator);
    owned_listeners_.erase(&listener);
}

void EventManager::ClearListenerTypes(IEventListener& listener) noexcept
{
    const auto iterator = all_listeners_.find(&listener);
    if (iterator == all_listeners_.end()) return;
    for (const refl::Type* type : iterator->second.registered_types)
    {
        StopListeningEventType(listener, type);
    }
    iterator->second.registered_types.clear();
    iterator->second.active = false;
}

void EventManager::FlushPendingMutations()
{
    for (;;)
    {
        const auto pending =
            std::ranges::find_if(all_listeners_, [](const auto& entry) { return entry.second.pending_remove; });
        if (pending == all_listeners_.end()) break;
        RemoveNow(*pending->first);
    }
    for (auto& [listener, info] : all_listeners_)
    {
        if (info.pending_add)
        {
            info.pending_add = false;
            Activate(*listener);
        }
        else if (info.pending_update)
        {
            info.pending_update = false;
            UpdateListenTypesNow(*listener);
            info.active = true;
        }
    }
}

void EventManager::Emit(const refl::Type* event_type, const void* event_data)
{
    ++dispatch_depth_;
    try
    {
        const auto type = type_lookup_.find(event_type);
        if (type != type_lookup_.end())
        {
            for (const ListenerTypeEntry& entry : type->second)
            {
                const auto listener = all_listeners_.find(entry.listener);
                if (listener != all_listeners_.end() && listener->second.active)
                {
                    entry.callback(entry.listener, event_data);
                }
            }
        }
    }
    catch (...)
    {
        --dispatch_depth_;
        if (dispatch_depth_ == 0)
        {
            try
            {
                FlushPendingMutations();
            }
            catch (...)
            {
            }
        }
        throw;
    }
    --dispatch_depth_;
    if (dispatch_depth_ == 0) FlushPendingMutations();
}

void EventManager::StopListeningEventType(IEventListener& listener, const refl::Type* type) noexcept
{
    if (auto iterator = type_lookup_.find(type); iterator != type_lookup_.end())
    {
        auto& entries = iterator->second;
        const auto [erase_begin, erase_end] = std::ranges::remove(entries, &listener, &ListenerTypeEntry::listener);
        entries.erase(erase_begin, erase_end);
        if (entries.empty()) type_lookup_.erase(iterator);
    }
}

}  // namespace klvk::events
