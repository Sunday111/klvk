#pragma once

#include <slang.h>

#include <string>
#include <string_view>

#include "klvk/integral_aliases.hpp"
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

// klvk's own record of an interface, as stored beside a compiled shader. Both
// directions live together so the cache cannot be written in a form it will not
// read back.
class ShaderInterfaceJson
{
public:
    // Bumped whenever the record changes shape, so a cache written by an older
    // klvk is discarded rather than misread.
    static constexpr u32 kVersion = 4;

    [[nodiscard]] static std::string Write(const ShaderInterface& interface);
    [[nodiscard]] static ShaderInterface Read(std::string_view text);
};

}  // namespace klvk
