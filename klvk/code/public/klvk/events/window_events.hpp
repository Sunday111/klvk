#pragma once

#include "refl/reflection_provider.hpp"
#include "refl/static_type/class.hpp"
#include "edt/math/matrix.hpp"

namespace klvk::events
{
class OnWindowResize
{
public:
    edt::Vec2i previous{};
    edt::Vec2i current{};
};

}  // namespace klvk::events

namespace refl
{

template <>
struct TypeReflectionProvider<klvk::events::OnWindowResize>
{
    [[nodiscard]] inline constexpr static auto ReflectType()
    {
        return refl::StaticClassTypeInfo<klvk::events::OnWindowResize>(
            "OnWindowResize",
            edt::GUID::Create("24DC2E34-B85B-4772-A05B-09B4DD84497A"));
    }
};

}  // namespace refl
