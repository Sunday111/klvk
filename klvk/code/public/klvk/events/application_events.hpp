#pragma once

#include "CppReflection/ReflectionProvider.hpp"
#include "CppReflection/StaticType/class.hpp"
#include "EverydayTools/GUID.hpp"

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
