#pragma once

#include <filesystem>

#include "klvk/integral_aliases.hpp"

namespace klvk::os
{
std::filesystem::path GetExecutableDir();
u64 GetProcessId();
}  // namespace klvk::os
