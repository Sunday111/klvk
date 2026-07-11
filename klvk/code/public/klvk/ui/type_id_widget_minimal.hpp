#pragma once

#include <string_view>

#include "EverydayTools/GUID.hpp"

namespace klvk
{

bool SimpleTypeWidget(edt::GUID type_guid, std::string_view name, void* value);
bool SimpleTypeWidget(edt::GUID type_guid, std::string_view name, const void* value);
void TypeIdWidget(edt::GUID type_guid, void* base, bool& value_changed);

}  // namespace klvk
