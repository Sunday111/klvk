#include "klvk/shader/shader.hpp"

#include <cstring>
#include <nlohmann/json.hpp>

#include "klvk/filesystem/filesystem.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/signed_integral_aliases.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/vulkan_api.hpp"

// Vulkan create-info structs are designed for partial designated initialization;
// unlisted fields must be zero.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

namespace klvk
{

namespace
{

constexpr std::pair<std::string_view, VkShaderStageFlagBits> kStageExtensions[] = {
    {".vert", VK_SHADER_STAGE_VERTEX_BIT},
    {".frag", VK_SHADER_STAGE_FRAGMENT_BIT},
    {".geom", VK_SHADER_STAGE_GEOMETRY_BIT},
    {".comp", VK_SHADER_STAGE_COMPUTE_BIT},
    {".tesc", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT},
    {".tese", VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT},
};

u32 DefaultValueBits(const nlohmann::json& definition)
{
    const std::string type = definition.at("type").get<std::string>();
    u32 raw = 0;
    if (type == "float")
    {
        const float value = definition.at("default").get<float>();
        std::memcpy(&raw, &value, sizeof(raw));
    }
    else if (type == "int")
    {
        const i32 value = definition.at("default").get<i32>();
        std::memcpy(&raw, &value, sizeof(raw));
    }
    else if (type == "uint")
    {
        raw = definition.at("default").get<u32>();
    }
    else if (type == "bool")
    {
        raw = definition.at("default").get<bool>() ? 1 : 0;
    }
    else
    {
        ErrorHandling::ThrowWithMessage("Unsupported shader define type '{}'", type);
    }
    return raw;
}

}  // namespace

Shader::Shader(DeviceContext& context, std::string_view name) : context_(&context), name_(name)
{
    const std::filesystem::path base = shaders_dir_ / name_;

    for (const auto& [extension, stage] : kStageExtensions)
    {
        std::filesystem::path path = base;
        path += extension;
        if (!std::filesystem::exists(path)) continue;

        stages_.emplace_back(stage, context.CreateShaderModuleFromSource(path));
    }
    ErrorHandling::Ensure(!stages_.empty(), "Shader '{}': no GLSL stages found at {}", name_, base.string());

    std::filesystem::path config_path = base;
    config_path += ".shader.json";
    if (std::filesystem::exists(config_path))
    {
        std::string config_text;
        Filesystem::ReadFile(config_path, config_text);
        const auto config = nlohmann::json::parse(config_text);
        for (const auto& definition : config.value("definitions", nlohmann::json::array()))
        {
            const auto constant_id = static_cast<u32>(define_values_.size());
            define_names_.push_back(definition.at("name").get<std::string>());
            define_values_.push_back(DefaultValueBits(definition));
            specialization_entries_.push_back({
                .constantID = constant_id,
                .offset = constant_id * static_cast<u32>(sizeof(u32)),
                .size = sizeof(u32),
            });
        }
    }
}

Shader::~Shader()
{
    for (const auto& [stage, module] : stages_)
    {
        Vulkan::DestroyShaderModuleNE(context_->GetDevice(), module);
    }
}

std::optional<DefineHandle> Shader::FindDefine(std::string_view name) const noexcept
{
    for (size_t index = 0; index != define_names_.size(); ++index)
    {
        if (define_names_[index] == name)
        {
            return DefineHandle{.name = std::string(name), .index = static_cast<u32>(index)};
        }
    }
    return std::nullopt;
}

DefineHandle Shader::GetDefine(std::string_view name) const
{
    auto handle = FindDefine(name);
    ErrorHandling::Ensure(handle.has_value(), "Shader '{}' has no define named '{}'", name_, name);
    return *handle;
}

std::vector<VkPipelineShaderStageCreateInfo> Shader::MakeShaderStages(VkShaderStageFlags stage_mask) const
{
    specialization_info_ = VkSpecializationInfo{
        .mapEntryCount = static_cast<u32>(specialization_entries_.size()),
        .pMapEntries = specialization_entries_.data(),
        .dataSize = define_values_.size() * sizeof(u32),
        .pData = define_values_.data(),
    };
    const VkSpecializationInfo* specialization = specialization_entries_.empty() ? nullptr : &specialization_info_;

    std::vector<VkPipelineShaderStageCreateInfo> result;
    for (const auto& [stage, module] : stages_)
    {
        if (!(stage & stage_mask)) continue;
        result.push_back({
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = stage,
            .module = module,
            .pName = "main",
            .pSpecializationInfo = specialization,
        });
    }
    return result;
}

}  // namespace klvk
