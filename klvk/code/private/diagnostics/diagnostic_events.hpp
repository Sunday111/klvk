#pragma once

#include <cstddef>

#include "edt/guid.hpp"
#include "refl/reflection_provider.hpp"
#include "refl/static_type/class.hpp"

namespace klvk::events
{

struct DiagnosticCaptureDue
{
    size_t capture_index = 0;
};

}  // namespace klvk::events

namespace refl
{

template <>
struct TypeReflectionProvider<klvk::events::DiagnosticCaptureDue>
{
    [[nodiscard]] inline constexpr static auto ReflectType()
    {
        return refl::StaticClassTypeInfo<klvk::events::DiagnosticCaptureDue>(
            "DiagnosticCaptureDue",
            edt::GUID::Create("FF2447D1-27C4-40C3-8230-666031DD28D0"));
    }
};

}  // namespace refl
