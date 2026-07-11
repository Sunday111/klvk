#pragma once

#include "CppReflection/ReflectionProvider.hpp"
#include "CppReflection/StaticType/class.hpp"
#include "EverydayTools/Math/Matrix.hpp"

namespace klvk::events
{
class OnWindowResize
{
public:
    edt::Vec2i previous{};
    edt::Vec2i current{};
};

}  // namespace klvk::events

namespace cppreflection
{

template <>
struct TypeReflectionProvider<klvk::events::OnWindowResize>
{
    [[nodiscard]] inline constexpr static auto ReflectType()
    {
        return cppreflection::StaticClassTypeInfo<klvk::events::OnWindowResize>(
            "OnWindowResize",
            edt::GUID::Create("24DC2E34-B85B-4772-A05B-09B4DD84497A"));
    }
};

}  // namespace cppreflection
