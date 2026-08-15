#pragma once

#include <string>
#include <string_view>

#include "klvk/integral_aliases.hpp"
#include "klvk/shader/shader_interface.hpp"

namespace klvk
{

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
