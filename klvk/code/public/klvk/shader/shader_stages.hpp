#pragma once

#include <memory>
#include <span>
#include <vector>

#include "klvk/shader/shader_interface.hpp"

namespace klvk
{

class ShaderModule;

class ShaderStages
{
public:
    // ShaderStages owns all create-info and specialization backing storage, but
    // borrows VkShaderModule handles. Their ShaderModule owners must outlive
    // pipeline creation.
    ShaderStages() = default;
    ShaderStages(VkShaderStageFlagBits stage, const ShaderModule& module);

    ShaderStages(
        std::vector<VkPipelineShaderStageCreateInfo> stages,
        std::vector<std::shared_ptr<const ShaderInterface>> interfaces,
        std::vector<VkSpecializationMapEntry> specialization_entries = {},
        std::vector<u32> specialization_values = {});

    [[nodiscard]] std::span<const VkPipelineShaderStageCreateInfo> GetCreateInfos() const noexcept { return stages_; }

    [[nodiscard]] const std::vector<std::shared_ptr<const ShaderInterface>>& GetInterfaces() const noexcept
    {
        return interfaces_;
    }

    [[nodiscard]] ShaderProgramInterface MergeInterfaces() const;
    void Append(const ShaderStages& other);

private:
    struct SpecializationStorage
    {
        std::vector<VkSpecializationMapEntry> entries;
        std::vector<u32> values;
        VkSpecializationInfo info{};
    };

    std::vector<VkPipelineShaderStageCreateInfo> stages_;
    std::vector<std::shared_ptr<const ShaderInterface>> interfaces_;
    std::vector<std::shared_ptr<SpecializationStorage>> specialization_storage_;
};

}  // namespace klvk
