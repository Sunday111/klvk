#pragma once

#include <slang.h>

#include "klvk/shader/shader_interface.hpp"

namespace klvk
{

// Slang's reflection output, read into the interface klvk works with. The
// document belongs to Slang, so this reads it permissively: it takes the fields
// it knows and ignores the rest, because a Slang release may add more.
class ShaderReflectionJson
{
public:
    [[nodiscard]] static ShaderInterface Read(slang::ProgramLayout& layout);
};

}  // namespace klvk
