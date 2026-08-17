#pragma once

#include "edt/math/matrix.hpp"
#include "klvk/input.hpp"
#include "refl/reflection_provider.hpp"
#include "refl/static_type/class.hpp"

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

namespace refl
{

template <>
struct TypeReflectionProvider<klvk::events::OnMouseMove>
{
    [[nodiscard]] inline constexpr static auto ReflectType()
    {
        return refl::StaticClassTypeInfo<klvk::events::OnMouseMove>(
            "OnMouseMove",
            edt::GUID::Create("92FDFAB7-0D48-44A0-B0A3-9C2FA3EE9E68"));
    }
};

template <>
struct TypeReflectionProvider<klvk::events::OnMouseScroll>
{
    [[nodiscard]] inline constexpr static auto ReflectType()
    {
        return refl::StaticClassTypeInfo<klvk::events::OnMouseScroll>(
            "OnMouseScroll",
            edt::GUID::Create("14FD5774-D251-49E4-92CC-8134242E266A"));
    }
};

template <>
struct TypeReflectionProvider<klvk::events::OnMouseButton>
{
    [[nodiscard]] inline constexpr static auto ReflectType()
    {
        return refl::StaticClassTypeInfo<klvk::events::OnMouseButton>(
            "OnMouseButton",
            edt::GUID::Create("651B35BC-7D83-4F22-9F0D-946929A66892"));
    }
};

}  // namespace refl
