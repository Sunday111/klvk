#pragma once

#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "klvk/error_handling.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/shader/define_handle.hpp"
#include "klvk/shader/shader_module.hpp"
#include "klvk/shader/shader_stages.hpp"
#include "klvk/signed_integral_aliases.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{

class DeviceContext;

namespace detail
{

template <typename T>
constexpr ShaderScalarType ShaderScalarTypeOf()
{
    using Value = std::remove_cv_t<T>;
    if constexpr (std::is_same_v<Value, bool>) return ShaderScalarType::Bool;
    if constexpr (std::is_same_v<Value, i32>) return ShaderScalarType::Int32;
    if constexpr (std::is_same_v<Value, u32>) return ShaderScalarType::UInt32;
    if constexpr (std::is_same_v<Value, float>) return ShaderScalarType::Float32;
    return ShaderScalarType::Unknown;
}

}  // namespace detail

// The klvk counterpart of klgl's Shader: given a name it loads every present
// GLSL stage (<name>.vert, .frag, .geom, .comp, .tesc, .tese), compiling it
// through DeviceContext's memory/persistent cache, and the <name>.shader.json
// config whose "definitions" become
// specialization constants (constant_id = index in the array).
//
// klgl recompiles GLSL when a define changes; with specialization constants the new
// values take effect through a pipeline rebuild instead. Changing a define
// bumps GetVersion(): every pipeline remembers the version it was built from
// and rebuilds with fresh MakeStages() when the versions differ, so any
// number of pipelines can share one shader.
class Shader
{
public:
    inline static std::filesystem::path shaders_dir_{};

    Shader(DeviceContext& context, std::string_view name);
    Shader(const Shader&) = delete;
    Shader(Shader&&) = delete;
    ~Shader();

    [[nodiscard]] std::optional<DefineHandle> FindDefine(std::string_view name) const noexcept;
    [[nodiscard]] DefineHandle GetDefine(std::string_view name) const;

    template <typename T>
        requires(detail::ShaderScalarTypeOf<T>() != ShaderScalarType::Unknown)
    void SetDefineValue(const DefineHandle& handle, const T& value)
    {
        ErrorHandling::Ensure(handle.index < define_values_.size(), "Unknown define '{}'", handle.name);
        ErrorHandling::Ensure(
            define_types_[handle.index] == detail::ShaderScalarTypeOf<T>(),
            "Shader define '{}' has an incompatible host value type",
            handle.name);
        u32 raw = 0;
        if constexpr (std::is_same_v<std::remove_cv_t<T>, bool>)
        {
            raw = value ? 1u : 0u;
        }
        else
        {
            static_assert(sizeof(T) == sizeof(raw));
            std::memcpy(&raw, &value, sizeof(raw));
        }
        if (!define_overridden_[handle.index] || define_values_[handle.index] != raw)
        {
            define_values_[handle.index] = raw;
            define_overridden_[handle.index] = true;
            ++version_;
        }
    }

    template <typename T>
        requires(detail::ShaderScalarTypeOf<T>() != ShaderScalarType::Unknown)
    [[nodiscard]] T GetDefineValue(const DefineHandle& handle) const
    {
        ErrorHandling::Ensure(handle.index < define_values_.size(), "Unknown define '{}'", handle.name);
        ErrorHandling::Ensure(
            define_types_[handle.index] == detail::ShaderScalarTypeOf<T>(),
            "Shader define '{}' has an incompatible host value type",
            handle.name);
        ErrorHandling::Ensure(
            define_overridden_[handle.index],
            "Shader define '{}' uses its shader default and has no host override",
            handle.name);
        if constexpr (std::is_same_v<std::remove_cv_t<T>, bool>)
        {
            return define_values_[handle.index] != 0;
        }
        else
        {
            T value;
            static_assert(sizeof(T) == sizeof(u32));
            std::memcpy(&value, &define_values_[handle.index], sizeof(value));
            return value;
        }
    }

    // Incremented whenever a define value changes. Compare against the version
    // a pipeline was built from to decide whether it needs a rebuild.
    [[nodiscard]] size_t GetVersion() const noexcept { return version_; }

    // Stage create infos with the current specialization constants attached.
    // The returned value owns specialization storage but borrows this shader's
    // module handles, so this Shader must outlive pipeline creation.
    [[nodiscard]] ShaderStages MakeStages(VkShaderStageFlags stage_mask = VK_SHADER_STAGE_ALL) const;

private:
    std::string name_;
    std::vector<std::pair<VkShaderStageFlagBits, ShaderModule>> stages_;
    std::vector<std::string> define_names_;
    std::vector<std::vector<std::pair<VkShaderStageFlagBits, u32>>> define_stage_ids_;
    std::vector<ShaderScalarType> define_types_;
    std::vector<u32> define_values_;
    std::vector<bool> define_overridden_;
    size_t version_ = 0;
};

}  // namespace klvk
