#pragma once

#include "klvk/vulkan/vulkan.hpp"

namespace klvk
{

void VulkanCheck(vk::Result result);

void InitializeVulkanDispatcher();
void InitializeVulkanDispatcher(vk::Instance instance);
void InitializeVulkanDispatcher(vk::Device device);

}  // namespace klvk
