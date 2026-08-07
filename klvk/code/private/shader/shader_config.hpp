#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "klvk/integral_aliases.hpp"

namespace klvk
{

// The '.shader.json' that may sit beside a shader. Parsed whole, so the loader
// works from this rather than from a document.
struct ShaderConfig
{
    // A value exactly as the document spells it. Whether it suits the constant
    // it names depends on what the shader reflected, which is the loader's
    // business and not the document's.
    using Value = std::variant<bool, i64, u64, double>;

    struct SpecializationConstant
    {
        std::string name;
        Value value;
    };

    std::vector<SpecializationConstant> specialization_constants;
};

class ShaderConfigJson
{
public:
    // Nothing when no configuration sits beside the shader, which is the usual
    // case; a present but unreadable one is an error.
    [[nodiscard]] static std::optional<ShaderConfig> Load(const std::filesystem::path& path);
};

}  // namespace klvk
