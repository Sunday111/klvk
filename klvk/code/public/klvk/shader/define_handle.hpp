#pragma once

#include <string>

#include "klvk/integral_aliases.hpp"

namespace klvk
{

class DefineHandle
{
public:
    std::string name;
    u32 index = 0;
};

}  // namespace klvk
