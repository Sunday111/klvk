#pragma once

#include "CppReflection/ReflectionProvider.hpp"
#include "CppReflection/StaticType/class.hpp"
#include "EverydayTools/Math/Matrix.hpp"
#include "klvk/input.hpp"

namespace klvk::events
{
class OnMouseMove
{
public:
    edt::Vec2f previous{};
    edt::Vec2f current{};
};

class OnMouseScroll
{
public:
    edt::Vec2f value{};
};

class OnMouseButton
{
public:
    MouseButton button = MouseButton::Left;
    InputAction action = InputAction::Release;
};
}  // namespace klvk::events

namespace cppreflection
{

template <>
struct TypeReflectionProvider<klvk::events::OnMouseMove>
{
    [[nodiscard]] inline constexpr static auto ReflectType()
    {
        return cppreflection::StaticClassTypeInfo<klvk::events::OnMouseMove>(
            "OnMouseMove",
            edt::GUID::Create("92FDFAB7-0D48-44A0-B0A3-9C2FA3EE9E68"));
    }
};

template <>
struct TypeReflectionProvider<klvk::events::OnMouseScroll>
{
    [[nodiscard]] inline constexpr static auto ReflectType()
    {
        return cppreflection::StaticClassTypeInfo<klvk::events::OnMouseScroll>(
            "OnMouseScroll",
            edt::GUID::Create("14FD5774-D251-49E4-92CC-8134242E266A"));
    }
};

template <>
struct TypeReflectionProvider<klvk::events::OnMouseButton>
{
    [[nodiscard]] inline constexpr static auto ReflectType()
    {
        return cppreflection::StaticClassTypeInfo<klvk::events::OnMouseButton>(
            "OnMouseButton",
            edt::GUID::Create("651B35BC-7D83-4F22-9F0D-946929A66892"));
    }
};

}  // namespace cppreflection
