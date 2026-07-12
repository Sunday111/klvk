#pragma once

#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "klvk/error_handling.hpp"
#include "klvk/shader/define_handle.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{

class DeviceContext;

// The klvk counterpart of klgl's Shader: given a name it loads every present
// SPIR-V stage (<name>.vert.spv, .frag.spv, .geom.spv, .comp.spv, .tesc.spv,
// .tese.spv) and the <name>.shader.json config whose "definitions" become
// specialization constants (constant_id = index in the array).
//
// klgl recompiles GLSL when a define changes; with precompiled SPIR-V the new
// values take effect through a pipeline rebuild instead. Changing a define
// bumps GetVersion(): every pipeline remembers the version it was built from
// and rebuilds with fresh MakeShaderStages() when the versions differ, so any
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
        requires(sizeof(T) == sizeof(uint32_t))
    void SetDefineValue(const DefineHandle& handle, const T& value)
    {
        ErrorHandling::Ensure(handle.index < define_values_.size(), "Unknown define '{}'", handle.name);
        uint32_t raw = 0;
        std::memcpy(&raw, &value, sizeof(raw));
        if (define_values_[handle.index] != raw)
        {
            define_values_[handle.index] = raw;
            ++version_;
        }
    }

    template <typename T>
        requires(sizeof(T) == sizeof(uint32_t))
    [[nodiscard]] T GetDefineValue(const DefineHandle& handle) const
    {
        ErrorHandling::Ensure(handle.index < define_values_.size(), "Unknown define '{}'", handle.name);
        T value;
        std::memcpy(&value, &define_values_[handle.index], sizeof(value));
        return value;
    }

    // Incremented whenever a define value changes. Compare against the version
    // a pipeline was built from to decide whether it needs a rebuild.
    [[nodiscard]] size_t GetVersion() const noexcept { return version_; }

    // Stage create infos with the current specialization constants attached.
    // The returned structs reference memory owned by this shader: keep it
    // alive (and do not change defines) until the pipeline is created.
    [[nodiscard]] std::vector<VkPipelineShaderStageCreateInfo> MakeShaderStages(
        VkShaderStageFlags stage_mask = VK_SHADER_STAGE_ALL) const;

private:
    DeviceContext* context_ = nullptr;
    std::string name_;
    std::vector<std::pair<VkShaderStageFlagBits, VkShaderModule>> stages_;
    std::vector<std::string> define_names_;
    std::vector<uint32_t> define_values_;
    std::vector<VkSpecializationMapEntry> specialization_entries_;
    mutable VkSpecializationInfo specialization_info_{};
    size_t version_ = 0;
};

}  // namespace klvk
