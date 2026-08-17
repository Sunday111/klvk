#pragma once

#include "edt/guid.hpp"
#include "refl/reflection_provider.hpp"
#include "refl/static_type/class.hpp"

namespace klvk::events
{

struct OnApplicationQuitRequested
{
};

}  // namespace klvk::events

namespace refl
{

template <>
struct TypeReflectionProvider<klvk::events::OnApplicationQuitRequested>
{
    [[nodiscard]] inline constexpr static auto ReflectType()
    {
        return refl::StaticClassTypeInfo<klvk::events::OnApplicationQuitRequested>(
            "OnApplicationQuitRequested",
            edt::GUID::Create("3AECD1F0-C888-4F49-9B9A-16AC458CC010"));
    }
};

}  // namespace refl
