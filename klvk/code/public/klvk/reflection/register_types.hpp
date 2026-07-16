#pragma once

#include "CppReflection/GetTypeInfo.hpp"
#include "CppReflection/TypeRegistry.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/reflection/matrix_reflect.hpp"  // IWYU pragma: keep (provides reflection for matrices)
#include "klvk/signed_integral_aliases.hpp"

namespace klvk
{

using namespace edt::lazy_matrix_aliases;  // NOLINT

inline void RegisterReflectionTypes()
{
    [[maybe_unused]] const cppreflection::Type* t{};
    t = cppreflection::GetTypeInfo<float>();
    t = cppreflection::GetTypeInfo<i8>();
    t = cppreflection::GetTypeInfo<i16>();
    t = cppreflection::GetTypeInfo<i32>();
    t = cppreflection::GetTypeInfo<i64>();
    t = cppreflection::GetTypeInfo<u8>();
    t = cppreflection::GetTypeInfo<u16>();
    t = cppreflection::GetTypeInfo<u32>();
    t = cppreflection::GetTypeInfo<u64>();
    t = cppreflection::GetTypeInfo<Vec3f>();
    t = cppreflection::GetTypeInfo<Vec4f>();
    t = cppreflection::GetTypeInfo<Mat3f>();
    t = cppreflection::GetTypeInfo<Mat4f>();
    t = cppreflection::GetTypeInfo<Vec2f>();
    t = cppreflection::GetTypeInfo<Vec3f>();
    t = cppreflection::GetTypeInfo<Vec4f>();
    t = cppreflection::GetTypeInfo<Mat3f>();
    t = cppreflection::GetTypeInfo<Mat4f>();
}

}  // namespace klvk
