#include "shader_interface_json.hpp"

#include <array>
#include <nlohmann/json.hpp>
#include <ranges>

#include "klvk/error_handling.hpp"

namespace klvk
{
namespace
{

nlohmann::json ValueTypeToJson(const ShaderValueType& type)
{
    return {
        {"scalar", static_cast<u32>(type.scalar)},
        {"rows", type.rows},
        {"columns", type.columns},
        {"array_count", type.array_count},
        {"unbounded", type.unbounded},
        {"matrix_layout", static_cast<u32>(type.matrix_layout)},
    };
}

ShaderValueType ValueTypeFromJson(const nlohmann::json& json)
{
    return {
        .scalar = static_cast<ShaderScalarType>(json.at("scalar").get<u32>()),
        .rows = json.at("rows").get<u32>(),
        .columns = json.at("columns").get<u32>(),
        .array_count = json.at("array_count").get<u32>(),
        .unbounded = json.at("unbounded").get<bool>(),
        .matrix_layout = static_cast<ShaderMatrixLayout>(json.at("matrix_layout").get<u32>()),
    };
}

nlohmann::json MemoryMemberToJson(const ShaderMemoryMember& member)
{
    nlohmann::json nested = nlohmann::json::array();
    for (const auto& value : member.members) nested.push_back(MemoryMemberToJson(value));
    return {
        {"name", member.name},
        {"type", ValueTypeToJson(member.type)},
        {"offset", member.offset},
        {"size", member.size},
        {"alignment", member.alignment},
        {"array_stride", member.array_stride},
        {"matrix_stride", member.matrix_stride},
        {"members", std::move(nested)},
    };
}

ShaderMemoryMember MemoryMemberFromJson(const nlohmann::json& json)
{
    ShaderMemoryMember result{
        .name = json.at("name").get<std::string>(),
        .type = ValueTypeFromJson(json.at("type")),
        .offset = json.at("offset").get<u64>(),
        .size = json.at("size").get<u64>(),
        .alignment = json.at("alignment").get<u64>(),
        .array_stride = json.at("array_stride").get<u64>(),
        .matrix_stride = json.at("matrix_stride").get<u64>(),
        .members = {},
    };
    for (const auto& nested : json.at("members")) result.members.push_back(MemoryMemberFromJson(nested));
    return result;
}

nlohmann::json MemoryLayoutToJson(const ShaderMemoryLayout& layout)
{
    nlohmann::json members = nlohmann::json::array();
    for (const auto& value : layout.members) members.push_back(MemoryMemberToJson(value));
    return {
        {"name", layout.name},
        {"offset", layout.offset},
        {"size", layout.size},
        {"alignment", layout.alignment},
        {"stages", static_cast<vk::ShaderStageFlags::MaskType>(layout.stages)},
        {"members", std::move(members)},
    };
}

ShaderMemoryLayout MemoryLayoutFromJson(const nlohmann::json& json)
{
    ShaderMemoryLayout result{
        .name = json.at("name").get<std::string>(),
        .offset = json.at("offset").get<u64>(),
        .size = json.at("size").get<u64>(),
        .alignment = json.at("alignment").get<u64>(),
        .stages = vk::ShaderStageFlags{json.at("stages").get<vk::ShaderStageFlags::MaskType>()},
        .members = {},
    };
    for (const auto& member : json.at("members")) result.members.push_back(MemoryMemberFromJson(member));
    return result;
}

}  // namespace

std::string ShaderInterfaceJson::Write(const ShaderInterface& interface)
{
    nlohmann::json descriptors = nlohmann::json::array();
    for (const auto& descriptor : interface.descriptors)
    {
        descriptors.push_back({
            {"name", descriptor.name},
            {"set", descriptor.set},
            {"binding", descriptor.binding},
            {"type", static_cast<u32>(descriptor.type)},
            {"count", descriptor.count},
            {"unbounded", descriptor.unbounded},
            {"access", static_cast<u32>(descriptor.access)},
            {"stages", static_cast<vk::ShaderStageFlags::MaskType>(descriptor.stages)},
            {"memory_layout",
             descriptor.memory_layout ? MemoryLayoutToJson(*descriptor.memory_layout) : nlohmann::json(nullptr)},
        });
    }
    nlohmann::json push_constants = nlohmann::json::array();
    for (const auto& value : interface.push_constants) push_constants.push_back(MemoryLayoutToJson(value));
    auto variables = [](const std::vector<ShaderInterfaceVariable>& values)
    {
        nlohmann::json json = nlohmann::json::array();
        for (const auto& value : values)
        {
            json.push_back({
                {"name", value.name},
                {"semantic", value.semantic},
                {"location", value.location},
                {"location_count", value.location_count},
                {"type", ValueTypeToJson(value.type)},
                {"built_in", value.built_in},
            });
        }
        return json;
    };
    nlohmann::json constants = nlohmann::json::array();
    for (const auto& value : interface.specialization_constants)
    {
        constants.push_back({
            {"name", value.name},
            {"id", value.id},
            {"type", static_cast<u32>(value.type)},
            {"byte_size", value.byte_size},
            {"default_value", value.default_value},
        });
    }
    return nlohmann::json{
        {"version", ShaderInterfaceJson::kVersion},
        {"language", static_cast<u32>(interface.language)},
        {"stage", static_cast<u32>(interface.stage)},
        {"entry_point", interface.entry_point},
        {"descriptors", std::move(descriptors)},
        {"push_constants", std::move(push_constants)},
        {"inputs", variables(interface.inputs)},
        {"outputs", variables(interface.outputs)},
        {"specialization_constants", std::move(constants)},
        {"workgroup_size", interface.workgroup_size},
    }
        .dump();
}

ShaderInterface ShaderInterfaceJson::Read(std::string_view text)
{
    const nlohmann::json json = nlohmann::json::parse(text);
    ErrorHandling::Ensure(
        json.at("version") == ShaderInterfaceJson::kVersion,
        "Unsupported shader interface metadata version");
    ShaderInterface result{};
    result.language = static_cast<ShaderSourceLanguage>(json.at("language").get<u32>());
    result.stage = static_cast<vk::ShaderStageFlagBits>(json.at("stage").get<u32>());
    result.entry_point = json.at("entry_point").get<std::string>();
    result.workgroup_size = json.at("workgroup_size").get<std::array<u32, 3>>();
    for (const auto& descriptor : json.at("descriptors"))
    {
        ShaderDescriptorBinding value{
            .name = descriptor.at("name").get<std::string>(),
            .set = descriptor.at("set").get<u32>(),
            .binding = descriptor.at("binding").get<u32>(),
            .type = static_cast<vk::DescriptorType>(descriptor.at("type").get<u32>()),
            .count = descriptor.at("count").get<u32>(),
            .unbounded = descriptor.at("unbounded").get<bool>(),
            .access = static_cast<ShaderResourceAccess>(descriptor.at("access").get<u32>()),
            .stages = vk::ShaderStageFlags{descriptor.at("stages").get<vk::ShaderStageFlags::MaskType>()},
            .memory_layout = std::nullopt,
        };
        if (!descriptor.at("memory_layout").is_null())
        {
            value.memory_layout = MemoryLayoutFromJson(descriptor.at("memory_layout"));
        }
        result.descriptors.push_back(std::move(value));
    }
    for (const auto& value : json.at("push_constants"))
    {
        result.push_constants.push_back(MemoryLayoutFromJson(value));
    }
    auto parse_variables = [](const nlohmann::json& values)
    {
        return values |
               std::views::transform(
                   [](const nlohmann::json& value)
                   {
                       return ShaderInterfaceVariable{
                           .name = value.at("name").get<std::string>(),
                           .semantic = value.at("semantic").get<std::string>(),
                           .location = value.at("location").get<u32>(),
                           .location_count = value.at("location_count").get<u32>(),
                           .type = ValueTypeFromJson(value.at("type")),
                           .built_in = value.at("built_in").get<bool>(),
                       };
                   }) |
               std::ranges::to<std::vector>();
    };
    result.inputs = parse_variables(json.at("inputs"));
    result.outputs = parse_variables(json.at("outputs"));
    for (const auto& value : json.at("specialization_constants"))
    {
        result.specialization_constants.push_back({
            .name = value.at("name").get<std::string>(),
            .id = value.at("id").get<u32>(),
            .type = static_cast<ShaderScalarType>(value.at("type").get<u32>()),
            .byte_size = value.at("byte_size").get<u32>(),
            .default_value = value.at("default_value").get<std::vector<u8>>(),
        });
    }
    return result;
}

}  // namespace klvk
