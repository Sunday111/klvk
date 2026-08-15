#include "klvk/shader/shader_stages.hpp"

#include "klvk/error_handling.hpp"
#include "klvk/shader/shader_module.hpp"

namespace klvk
{

ShaderStages::ShaderStages(vk::ShaderStageFlagBits stage, const ShaderModule& module)
    : ShaderStages(
          {vk::PipelineShaderStageCreateInfo{}.setStage(stage).setModule(module.GetHandle()).setPName("main")},
          {module.GetInterface()})
{
    ErrorHandling::Ensure(
        module.GetInterface() != nullptr && module.GetInterface()->stage == stage,
        "Shader module reflection does not match the requested stage");
}

ShaderStages::ShaderStages(
    std::vector<vk::PipelineShaderStageCreateInfo> stages,
    std::vector<std::shared_ptr<const ShaderInterface>> interfaces,
    std::vector<vk::SpecializationMapEntry> specialization_entries,
    std::vector<u32> specialization_values)
    : stages_(std::move(stages)),
      interfaces_(std::move(interfaces))
{
    ErrorHandling::Ensure(
        stages_.size() == interfaces_.size(),
        "ShaderStages requires one reflected interface per stage");
    if (!specialization_entries.empty())
    {
        ErrorHandling::Ensure(
            stages_.size() == 1,
            "ShaderStages specialization storage may describe exactly one shader stage");
        auto specialization = std::make_shared<SpecializationStorage>();
        specialization->entries = std::move(specialization_entries);
        specialization->values = std::move(specialization_values);
        specialization->info.setMapEntries(specialization->entries)
            .setDataSize(specialization->values.size() * sizeof(u32))
            .setPData(specialization->values.data());
        for (auto& stage : stages_) stage.pSpecializationInfo = &specialization->info;
        specialization_storage_.push_back(std::move(specialization));
    }
}

ShaderProgramInterface ShaderStages::MergeInterfaces() const
{
    return MergeShaderInterfaces(interfaces_);
}

void ShaderStages::Append(const ShaderStages& other)
{
    stages_.insert(stages_.end(), other.stages_.begin(), other.stages_.end());
    interfaces_.insert(interfaces_.end(), other.interfaces_.begin(), other.interfaces_.end());
    specialization_storage_.insert(
        specialization_storage_.end(),
        other.specialization_storage_.begin(),
        other.specialization_storage_.end());
}

}  // namespace klvk
