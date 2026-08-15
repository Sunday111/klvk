#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{

enum class ShaderSourceLanguage : u8
{
    Glsl,
    Slang
};

enum class ShaderScalarType : u8
{
    Unknown,
    Bool,
    Int32,
    UInt32,
    Float16,
    Float32,
    Float64
};

enum class ShaderMatrixLayout : u8
{
    NotApplicable,
    RowMajor,
    ColumnMajor
};

enum class ShaderResourceAccess : u8
{
    ReadOnly,
    ReadWrite
};

struct ShaderValueType
{
    ShaderScalarType scalar = ShaderScalarType::Unknown;
    u32 rows = 1;
    u32 columns = 1;
    u32 array_count = 1;
    bool unbounded = false;
    ShaderMatrixLayout matrix_layout = ShaderMatrixLayout::NotApplicable;

    bool operator==(const ShaderValueType&) const = default;
};

struct ShaderMemoryMember
{
    std::string name;
    ShaderValueType type;
    u64 offset = 0;
    u64 size = 0;
    // Zero means the compiler did not expose this value.
    u64 alignment = 0;
    u64 array_stride = 0;
    u64 matrix_stride = 0;
    std::vector<ShaderMemoryMember> members;

    bool operator==(const ShaderMemoryMember&) const = default;
};

struct ShaderMemoryLayout
{
    std::string name;
    u64 offset = 0;
    u64 size = 0;
    // Zero means the compiler did not expose this value.
    u64 alignment = 0;
    vk::ShaderStageFlags stages{};
    std::vector<ShaderMemoryMember> members;

    bool operator==(const ShaderMemoryLayout&) const = default;
};

struct ShaderDescriptorBinding
{
    std::string name;
    u32 set = 0;
    u32 binding = 0;
    vk::DescriptorType type{};
    u32 count = 1;
    bool unbounded = false;
    ShaderResourceAccess access = ShaderResourceAccess::ReadOnly;
    vk::ShaderStageFlags stages{};
    std::optional<ShaderMemoryLayout> memory_layout;

    bool operator==(const ShaderDescriptorBinding&) const = default;
};

struct ShaderInterfaceVariable
{
    std::string name;
    std::string semantic;
    u32 location = 0;
    u32 location_count = 1;
    ShaderValueType type;
    bool built_in = false;

    bool operator==(const ShaderInterfaceVariable&) const = default;
};

struct ShaderSpecializationConstant
{
    std::string name;
    u32 id = 0;
    ShaderScalarType type = ShaderScalarType::Unknown;
    u32 byte_size = 0;
    std::vector<u8> default_value;

    bool operator==(const ShaderSpecializationConstant&) const = default;
};

struct ShaderInterface
{
    ShaderSourceLanguage language = ShaderSourceLanguage::Slang;
    vk::ShaderStageFlagBits stage{};
    std::string entry_point = "main";
    std::vector<ShaderDescriptorBinding> descriptors;
    std::vector<ShaderMemoryLayout> push_constants;
    std::vector<ShaderInterfaceVariable> inputs;
    std::vector<ShaderInterfaceVariable> outputs;
    std::vector<ShaderSpecializationConstant> specialization_constants;
    std::array<u32, 3> workgroup_size{0, 0, 0};

    bool operator==(const ShaderInterface&) const = default;
};

struct CompiledShader
{
    std::shared_ptr<const std::vector<u32>> spirv;
    std::shared_ptr<const ShaderInterface> interface;
};

struct ShaderProgramInterface
{
    std::vector<ShaderDescriptorBinding> descriptors;
    std::vector<ShaderMemoryLayout> push_constants;
    vk::ShaderStageFlags stages{};

    bool operator==(const ShaderProgramInterface&) const = default;
};

[[nodiscard]] ShaderProgramInterface MergeShaderInterfaces(
    const std::vector<std::shared_ptr<const ShaderInterface>>& interfaces);

[[nodiscard]] u32 ShaderScalarByteSize(ShaderScalarType type);
[[nodiscard]] u32 ShaderValueLocationCount(const ShaderValueType& type);

}  // namespace klvk
