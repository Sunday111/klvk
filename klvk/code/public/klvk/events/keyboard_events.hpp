#pragma once

#include "CppReflection/ReflectionProvider.hpp"
#include "CppReflection/StaticType/class.hpp"
#include "EverydayTools/GUID.hpp"
#include "klvk/input.hpp"

namespace klvk::events
{

struct OnKey
{
    Key key = Key::Tab;
    InputAction action = InputAction::Release;
};

}  // namespace klvk::events

namespace cppreflection
{

template <>
struct TypeReflectionProvider<klvk::events::OnKey>
{
    [[nodiscard]] inline constexpr static auto ReflectType()
    {
        return cppreflection::StaticClassTypeInfo<klvk::events::OnKey>(
            "OnKey",
            edt::GUID::Create("487D83CC-82EA-4436-8E19-3A93337D7DB4"));
    }
};

}  // namespace cppreflection
