#include "klvk/vulkan/vulkan_common.hpp"

#include <fmt/core.h>

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

VulkanError::VulkanError(vk::Result result, std::string context, cpptrace::raw_trace&& trace)
    : cpptrace::runtime_error(fmt::format("{} failed: {}", context, vk::to_string(result)), std::move(trace)),
      result_(result),
      context_(std::move(context))
{
}

void VulkanCheck(vk::Result result, std::string_view context)
{
    if (result != vk::Result::eSuccess) [[unlikely]]
    {
        throw VulkanError(result, std::string{context}, cpptrace::generate_raw_trace(1));
    }
}

void InitializeVulkanDispatcher()
{
    const auto get_instance_proc_addr =
        GetVulkanLoader().getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    if (get_instance_proc_addr == nullptr) [[unlikely]]
    {
        throw VulkanError(vk::Result::eErrorInitializationFailed, "vkGetInstanceProcAddr");
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
