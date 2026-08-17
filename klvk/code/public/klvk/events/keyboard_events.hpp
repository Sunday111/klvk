#pragma once

#include "edt/guid.hpp"
#include "klvk/input.hpp"
#include "refl/reflection_provider.hpp"
#include "refl/static_type/class.hpp"

namespace klvk::events
{

struct OnKey
{
    Key key = Key::Tab;
    InputAction action = InputAction::Release;
};

}  // namespace klvk::events

namespace refl
{

template <>
struct TypeReflectionProvider<klvk::events::OnKey>
{
    [[nodiscard]] inline constexpr static auto ReflectType()
    {
        return refl::StaticClassTypeInfo<klvk::events::OnKey>(
            "OnKey",
            edt::GUID::Create("487D83CC-82EA-4436-8E19-3A93337D7DB4"));
    }
};

}  // namespace refl
