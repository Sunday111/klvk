#include "shader_config.hpp"

#include "json/json_reader.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/filesystem/filesystem.hpp"

namespace klvk
{
namespace
{

ShaderConfig::Value ReadValue(const JsonReader& value)
{
    if (value.Value().is_boolean()) return value.Bool();
    if (value.Value().is_number_unsigned()) return value.Value().get<u64>();
    if (value.Value().is_number_integer()) return value.Value().get<i64>();
    ErrorHandling::Ensure(value.Value().is_number(), "Field '{}' must be a boolean or a number", value.Path());
    return value.Value().get<double>();
}

}  // namespace

std::optional<ShaderConfig> ShaderConfigJson::Load(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path)) return std::nullopt;

    std::string text;
    Filesystem::ReadFile(path, text);

    ShaderConfig config;
    try
    {
        const nlohmann::json document = nlohmann::json::parse(text);
        const JsonReader root{document, "root"};
        root.EnsureKnownKeys({"specialization_constants"});

        if (const auto constants = root.OptionalField("specialization_constants"))
        {
            for (const auto& [name, value] : constants->Fields())
            {
                config.specialization_constants.push_back({.name = name, .value = ReadValue(value)});
            }
        }
    }
    catch (const nlohmann::json::exception& exception)
    {
        ErrorHandling::ThrowWithMessage("Failed to parse shader config '{}': {}", path.string(), exception.what());
    }

    return config;
}

}  // namespace klvk
