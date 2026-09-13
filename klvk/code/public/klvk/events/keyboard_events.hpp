#pragma once

#include "edt/guid.hpp"
#include "klvk/input.hpp"
#include "klvk/integral_aliases.hpp"
#include "refl/reflection_provider.hpp"
#include "refl/static_type/class.hpp"

namespace klvk::events
{

struct OnKey
{
    Key key = Key::Tab;
    InputAction action = InputAction::Release;
};

struct OnTextInput
{
    u32 codepoint = 0;
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

template <>
struct TypeReflectionProvider<klvk::events::OnTextInput>
{
    [[nodiscard]] inline constexpr static auto ReflectType()
    {
        return refl::StaticClassTypeInfo<klvk::events::OnTextInput>(
            "OnTextInput",
            edt::GUID::Create("1B5AE4F4-63BB-4324-B5AA-D1F8F7B21F69"));
    }
};

}  // namespace refl
