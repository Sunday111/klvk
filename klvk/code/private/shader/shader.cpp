#include "klvk/shader/shader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include "klvk/error_handling.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/vulkan_common.hpp"
#include "shader/shader_config.hpp"

namespace klvk
{

namespace
{

constexpr std::array<std::pair<std::string_view, vk::ShaderStageFlagBits>, 6> kStageExtensions{{
    {".vert", vk::ShaderStageFlagBits::eVertex},
    {".frag", vk::ShaderStageFlagBits::eFragment},
    {".geom", vk::ShaderStageFlagBits::eGeometry},
    {".comp", vk::ShaderStageFlagBits::eCompute},
    {".tesc", vk::ShaderStageFlagBits::eTessellationControl},
    {".tese", vk::ShaderStageFlagBits::eTessellationEvaluation},
}};

// A document may spell an integer as signed or unsigned; a constant only cares
// that it is one.
std::optional<i64> AsInteger(const ShaderConfig::Value& value)
{
    if (const auto* signed_value = std::get_if<i64>(&value)) return *signed_value;
    if (const auto* unsigned_value = std::get_if<u64>(&value))
    {
        return std::in_range<i64>(*unsigned_value) ? std::optional{static_cast<i64>(*unsigned_value)} : std::nullopt;
    }
    return std::nullopt;
}

std::optional<double> AsNumber(const ShaderConfig::Value& value)
{
    if (const auto* real = std::get_if<double>(&value)) return *real;
    if (const auto integer = AsInteger(value)) return static_cast<double>(*integer);
    return std::nullopt;
}

u32 OverrideValueBits(const ShaderConfig::Value& value, ShaderScalarType type)
{
    u32 raw = 0;
    if (type == ShaderScalarType::Float32)
    {
        const auto parsed = AsNumber(value);
        ErrorHandling::Ensure(parsed.has_value(), "A float specialization constant requires a number");
        ErrorHandling::Ensure(
            std::isfinite(*parsed) && std::abs(*parsed) <= static_cast<double>(std::numeric_limits<float>::max()),
            "Float specialization constant value is out of range");
        const auto typed = static_cast<float>(*parsed);
        std::memcpy(&raw, &typed, sizeof(raw));
    }
    else if (type == ShaderScalarType::Int32)
    {
        const auto parsed = AsInteger(value);
        ErrorHandling::Ensure(parsed.has_value(), "An int specialization constant requires an integer");
        ErrorHandling::Ensure(std::in_range<i32>(*parsed), "Int specialization constant value is out of range");
        const auto typed = static_cast<i32>(*parsed);
        std::memcpy(&raw, &typed, sizeof(raw));
    }
    else if (type == ShaderScalarType::UInt32)
    {
        const auto parsed = AsInteger(value);
        ErrorHandling::Ensure(parsed.has_value(), "A uint specialization constant requires an integer");
        ErrorHandling::Ensure(std::in_range<u32>(*parsed), "Uint specialization constant value is out of range");
        raw = static_cast<u32>(*parsed);
    }
    else if (type == ShaderScalarType::Bool)
    {
        const auto* parsed = std::get_if<bool>(&value);
        ErrorHandling::Ensure(parsed != nullptr, "A bool specialization constant requires a boolean");
        raw = *parsed ? 1 : 0;
    }
    else
    {
        ErrorHandling::ThrowWithMessage("Unsupported shader specialization constant type {}", static_cast<u32>(type));
    }
    return raw;
}

}  // namespace

Shader::Shader(DeviceContext& context, std::string_view name) : name_(name)
{
    const std::filesystem::path base = shaders_dir_ / name_;

    for (const auto& [extension, stage] : kStageExtensions)
    {
        std::filesystem::path path = base;
        path += extension;
        // Prefer a Slang stage (<name>.frag.slang) over the GLSL one, so an
        // example can be migrated stage by stage; the cache routes by extension.
        std::filesystem::path slang_path = path;
        slang_path += ".slang";
        if (std::filesystem::exists(slang_path))
        {
            ShaderModule module = context.LoadShaderModule(slang_path);
            ErrorHandling::Ensure(
                module.GetInterface()->stage == stage,
                "Shader '{}' stage declaration does not match file suffix",
                slang_path.string());
            stages_.emplace_back(stage, std::move(module));
        }
        else if (std::filesystem::exists(path))
        {
            ErrorHandling::ThrowWithMessage(
                "Shader '{}' is legacy GLSL; klvk::Shader requires reflected Slang stages",
                path.string());
        }
    }
    ErrorHandling::Ensure(!stages_.empty(), "Shader '{}': no shader stages found at {}", name_, base.string());

    for (const auto& [stage, module] : stages_)
    {
        for (const ShaderSpecializationConstant& constant : module.GetInterface()->specialization_constants)
        {
            const auto existing = std::ranges::find(define_names_, constant.name);
            if (existing != define_names_.end())
            {
                const size_t index = static_cast<size_t>(existing - define_names_.begin());
                ErrorHandling::Ensure(
                    define_types_[index] == constant.type,
                    "Shader '{}' has conflicting specialization constant '{}'",
                    name_,
                    constant.name);
                define_stage_ids_[index].emplace_back(stage, constant.id);
                continue;
            }
            ErrorHandling::Ensure(
                constant.byte_size == sizeof(u32),
                "Shader '{}' specialization constant '{}' is not 32 bits",
                name_,
                constant.name);
            u32 raw_default = 0;
            if (constant.default_value.size() == sizeof(raw_default))
            {
                std::memcpy(&raw_default, constant.default_value.data(), sizeof(raw_default));
            }
            define_names_.push_back(constant.name);
            define_stage_ids_.push_back({{stage, constant.id}});
            define_types_.push_back(constant.type);
            define_values_.push_back(raw_default);
            define_overridden_.push_back(false);
        }
    }

    std::filesystem::path config_path = base;
    config_path += ".shader.json";
    if (const auto config = ShaderConfigJson::Load(config_path))
    {
        for (const ShaderConfig::SpecializationConstant& constant : config->specialization_constants)
        {
            const auto existing = std::ranges::find(define_names_, constant.name);
            ErrorHandling::Ensure(
                existing != define_names_.end(),
                "Shader '{}' config overrides unknown specialization constant '{}'",
                name_,
                constant.name);
            const size_t index = static_cast<size_t>(existing - define_names_.begin());
            define_values_[index] = OverrideValueBits(constant.value, define_types_[index]);
            define_overridden_[index] = true;
        }
    }
}

Shader::~Shader() = default;

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

ShaderStages Shader::MakeStages(vk::ShaderStageFlags stage_mask) const
{
    ShaderStages result;
    for (const auto& [stage, module] : stages_)
    {
        if (!(stage & stage_mask)) continue;
        std::vector<vk::PipelineShaderStageCreateInfo> create_infos{
            vk::PipelineShaderStageCreateInfo{}.setStage(stage).setModule(module.GetHandle()).setPName("main")};
        std::vector<vk::SpecializationMapEntry> entries;
        std::vector<u32> values;
        for (size_t index = 0; index != define_names_.size(); ++index)
        {
            if (!define_overridden_[index]) continue;
            const auto occurrence =
                std::ranges::find(define_stage_ids_[index], stage, &std::pair<vk::ShaderStageFlagBits, u32>::first);
            if (occurrence == define_stage_ids_[index].end()) continue;
            entries.push_back(
                vk::SpecializationMapEntry{}
                    .setConstantID(occurrence->second)
                    .setOffset(static_cast<u32>(values.size() * sizeof(u32)))
                    .setSize(sizeof(u32)));
            values.push_back(define_values_[index]);
        }
        result.Append(
            ShaderStages{
                std::move(create_infos),
                {module.GetInterface()},
                std::move(entries),
                std::move(values),
            });
    }
    return result;
}

}  // namespace klvk
