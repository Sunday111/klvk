#pragma once

#include <memory>
#include <utility>

#include "klvk/shader/shader_interface.hpp"
#include "klvk/vulkan/vk_object.hpp"

namespace klvk
{

class ShaderModule
{
public:
    ShaderModule() = default;
    ShaderModule(const ShaderModule&) = delete;
    ShaderModule(ShaderModule&&) noexcept = default;
    ShaderModule& operator=(const ShaderModule&) = delete;
    ShaderModule& operator=(ShaderModule&&) noexcept = default;

    ShaderModule(VkObject<VkShaderModule> module, std::shared_ptr<const ShaderInterface> interface)
        : module_(std::move(module)),
          interface_(std::move(interface))
    {
    }

    [[nodiscard]] VkShaderModule GetHandle() const noexcept { return module_.GetHandle(); }
    [[nodiscard]] const std::shared_ptr<const ShaderInterface>& GetInterface() const noexcept { return interface_; }
    [[nodiscard]] bool IsReflected() const noexcept { return interface_ != nullptr; }

private:
    VkObject<VkShaderModule> module_;
    std::shared_ptr<const ShaderInterface> interface_;
};

}  // namespace klvk
