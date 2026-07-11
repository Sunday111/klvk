#pragma once

#include <CppReflection/GetTypeInfo.hpp>

#include "klvk/reflection/matrix_reflect.hpp"  // IWYU pragma: keep
#include "type_id_widget_minimal.hpp"

namespace klvk
{
template <typename T>
inline bool SimpleTypeWidget(std::string_view name, T& value)
{
    return klvk::SimpleTypeWidget(cppreflection::GetTypeInfo<std::remove_const_t<T>>()->GetGuid(), name, &value);
}

}  // namespace klvk
