#pragma once

#include <cstddef>
#include <vector>

namespace refl
{
class Type;
}

namespace klvk::events
{
class IEventListener
{
public:
    using CallbackFunction = void (*)(IEventListener* listener, const void* event_data);

    virtual ~IEventListener() = default;
    [[nodiscard]] virtual std::vector<const refl::Type*> GetEventTypes() const = 0;
    virtual CallbackFunction MakeCallbackFunction(const size_t index) = 0;
};
}  // namespace klvk::events
