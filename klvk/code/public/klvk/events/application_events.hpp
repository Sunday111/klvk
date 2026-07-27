#pragma once

#include "cpp_reflection/reflection_provider.hpp"
#include "cpp_reflection/static_type/class.hpp"
#include "edt/guid.hpp"

namespace klvk::events
{

struct OnApplicationQuitRequested
{
};

}  // namespace klvk::events

namespace cppreflection
{

template <>
struct TypeReflectionProvider<klvk::events::OnApplicationQuitRequested>
{
    [[nodiscard]] inline constexpr static auto ReflectType()
    {
        return cppreflection::StaticClassTypeInfo<klvk::events::OnApplicationQuitRequested>(
            "OnApplicationQuitRequested",
            edt::GUID::Create("3AECD1F0-C888-4F49-9B9A-16AC458CC010"));
    }
};

}  // namespace cppreflection
