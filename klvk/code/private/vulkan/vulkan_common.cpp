#include "klvk/vulkan/vulkan_common.hpp"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace klvk
{
namespace
{

vk::detail::DynamicLoader& GetVulkanLoader()
{
    static vk::detail::DynamicLoader loader;
    return loader;
}

}  // namespace

void VulkanCheck(vk::Result result)
{
    if (result != vk::Result::eSuccess) [[unlikely]]
    {
        throw vk::SystemError(vk::make_error_code(result));
    }
}

void InitializeVulkanDispatcher()
{
    const auto get_instance_proc_addr =
        GetVulkanLoader().getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    if (get_instance_proc_addr == nullptr) [[unlikely]]
    {
        VulkanCheck(vk::Result::eErrorInitializationFailed);
    }
    VULKAN_HPP_DEFAULT_DISPATCHER.init(get_instance_proc_addr);
}

void InitializeVulkanDispatcher(vk::Instance instance)
{
    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);
}

void InitializeVulkanDispatcher(vk::Device device)
{
    VULKAN_HPP_DEFAULT_DISPATCHER.init(device);
}

}  // namespace klvk
