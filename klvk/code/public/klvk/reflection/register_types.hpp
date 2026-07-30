#pragma once

#include "klvk/integral_aliases.hpp"
#include "klvk/reflection/matrix_reflect.hpp"  // IWYU pragma: keep (provides reflection for matrices)
#include "refl/get_type_info.hpp"
#include "refl/type_registry.hpp"

namespace klvk
{

using namespace edt::lazy_matrix_aliases;  // NOLINT

inline void RegisterReflectionTypes()
{
    [[maybe_unused]] const refl::Type* t{};
    t = refl::GetTypeInfo<float>();
    t = refl::GetTypeInfo<i8>();
    t = refl::GetTypeInfo<i16>();
    t = refl::GetTypeInfo<i32>();
    t = refl::GetTypeInfo<i64>();
    t = refl::GetTypeInfo<u8>();
    t = refl::GetTypeInfo<u16>();
    t = refl::GetTypeInfo<u32>();
    t = refl::GetTypeInfo<u64>();
    t = refl::GetTypeInfo<Vec3f>();
    t = refl::GetTypeInfo<Vec4f>();
    t = refl::GetTypeInfo<Mat3f>();
    t = refl::GetTypeInfo<Mat4f>();
    t = refl::GetTypeInfo<Vec2f>();
    t = refl::GetTypeInfo<Vec3f>();
    t = refl::GetTypeInfo<Vec4f>();
    t = refl::GetTypeInfo<Mat3f>();
    t = refl::GetTypeInfo<Mat4f>();
}

}  // namespace klvk
