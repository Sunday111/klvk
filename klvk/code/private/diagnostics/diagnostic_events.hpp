#pragma once

#include <cstddef>

#include "CppReflection/ReflectionProvider.hpp"
#include "CppReflection/StaticType/class.hpp"
#include "EverydayTools/GUID.hpp"

namespace klvk::events
{

struct DiagnosticCaptureDue
{
    size_t capture_index = 0;
};

}  // namespace klvk::events

namespace cppreflection
{

template <>
struct TypeReflectionProvider<klvk::events::DiagnosticCaptureDue>
{
    [[nodiscard]] inline constexpr static auto ReflectType()
    {
        return cppreflection::StaticClassTypeInfo<klvk::events::DiagnosticCaptureDue>(
            "DiagnosticCaptureDue",
            edt::GUID::Create("FF2447D1-27C4-40C3-8230-666031DD28D0"));
    }
};

}  // namespace cppreflection
