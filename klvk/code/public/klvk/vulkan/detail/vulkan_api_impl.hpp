#pragma once

#include <fmt/format.h>

#include <algorithm>
#include <exception>
#include <utility>

#include "klvk/vulkan/vulkan_api.hpp"

namespace klvk
{

struct Vulkan::Internal
{
    [[nodiscard]] static uint32_t Count(size_t value) noexcept
    {
        if (value > std::numeric_limits<uint32_t>::max()) [[unlikely]]
        {
            std::terminate();
        }
        return static_cast<uint32_t>(value);
    }

    [[nodiscard]] static VulkanError
    MakeError(VkResult result, std::string_view function_name, cpptrace::raw_trace&& trace) noexcept
    {
        return VulkanError(
            result,
            fmt::format("{} failed: {}", function_name, VkResultToString(result)),
            std::move(trace));
    }

    template <typename T>
    [[nodiscard]] static tl::expected<T, VulkanError> ValueOrError(
        VkCallResult<T>&& call_result,
        std::string_view function_name) noexcept
    {
        if (call_result.result == VK_SUCCESS)
        {
            return std::move(call_result.value);
        }

        return tl::unexpected{MakeError(call_result.result, function_name, cpptrace::generate_raw_trace(1))};
    }

    [[nodiscard]] static std::optional<VulkanError> ErrorOrNothing(
        VkResult result,
        std::string_view function_name) noexcept
    {
        if (result == VK_SUCCESS)
        {
            return std::nullopt;
        }

        return MakeError(result, function_name, cpptrace::generate_raw_trace(1));
    }

    [[nodiscard]] static tl::unexpected<VulkanError> Unexpected(
        VkResult result,
        std::string_view function_name) noexcept
    {
        return tl::unexpected{MakeError(result, function_name, cpptrace::generate_raw_trace(1))};
    }

    template <typename T>
    [[nodiscard]] static T TakeValue(tl::expected<T, VulkanError>&& expected)
    {
        if (expected.has_value())
        {
            return std::move(expected.value());
        }

        throw std::move(expected.error());
    }

    static void ThrowIfError(std::optional<VulkanError>&& error)
    {
        if (error.has_value())
        {
            throw std::move(error.value());
        }
    }

    template <typename T, typename Enumerator>
    [[nodiscard]] static VkCallResult<std::vector<T>> Enumerate(Enumerator&& enumerate) noexcept
    {
        for (;;)
        {
            uint32_t count = 0;
            VkResult result = enumerate(count, nullptr);
            if (result != VK_SUCCESS && result != VK_INCOMPLETE)
            {
                return {.result = result};
            }
            if (count == 0)
            {
                if (result == VK_INCOMPLETE)
                {
                    continue;
                }
                return {.result = result};
            }

            std::vector<T> values(count);
            result = enumerate(count, values.empty() ? nullptr : values.data());
            if (result == VK_INCOMPLETE)
            {
                continue;
            }
            if (result == VK_SUCCESS)
            {
                values.resize(count);
            }
            return {.result = result, .value = std::move(values)};
        }
    }
};

VkCallResult<uint32_t> Vulkan::AcquireNextImageKHRNE(
    VkDevice device,
    VkSwapchainKHR swapchain,
    uint64_t timeout,
    VkSemaphore semaphore,
    VkFence fence) noexcept
{
    uint32_t image_index = 0;
    const VkResult result = vkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, &image_index);
    return {.result = result, .value = image_index};
}

tl::expected<AcquireNextImageOutcome, VulkanError> Vulkan::AcquireNextImageKHRCE(
    VkDevice device,
    VkSwapchainKHR swapchain,
    uint64_t timeout,
    VkSemaphore semaphore,
    VkFence fence) noexcept
{
    const auto [result, image_index] = AcquireNextImageKHRNE(device, swapchain, timeout, semaphore, fence);
    switch (result)
    {
    case VK_SUCCESS:
        return AcquireNextImageOutcome{
            .status = AcquireNextImageStatus::Acquired,
            .image_index = image_index,
        };
    case VK_SUBOPTIMAL_KHR:
        return AcquireNextImageOutcome{
            .status = AcquireNextImageStatus::Suboptimal,
            .image_index = image_index,
        };
    case VK_ERROR_OUT_OF_DATE_KHR:
        return AcquireNextImageOutcome{.status = AcquireNextImageStatus::OutOfDate};
    case VK_NOT_READY:
        return AcquireNextImageOutcome{.status = AcquireNextImageStatus::NotReady};
    case VK_TIMEOUT:
        return AcquireNextImageOutcome{.status = AcquireNextImageStatus::Timeout};
    default:
        return Internal::Unexpected(result, "vkAcquireNextImageKHR");
    }
}

AcquireNextImageOutcome Vulkan::AcquireNextImageKHR(
    VkDevice device,
    VkSwapchainKHR swapchain,
    uint64_t timeout,
    VkSemaphore semaphore,
    VkFence fence)
{
    return Internal::TakeValue(AcquireNextImageKHRCE(device, swapchain, timeout, semaphore, fence));
}

VkCallResult<std::vector<VkCommandBuffer>> Vulkan::AllocateCommandBuffersNE(
    VkDevice device,
    const VkCommandBufferAllocateInfo& allocate_info) noexcept
{
    std::vector<VkCommandBuffer> command_buffers(allocate_info.commandBufferCount);
    const VkResult result = vkAllocateCommandBuffers(device, &allocate_info, command_buffers.data());
    return {.result = result, .value = std::move(command_buffers)};
}

tl::expected<std::vector<VkCommandBuffer>, VulkanError> Vulkan::AllocateCommandBuffersCE(
    VkDevice device,
    const VkCommandBufferAllocateInfo& allocate_info) noexcept
{
    return Internal::ValueOrError(AllocateCommandBuffersNE(device, allocate_info), "vkAllocateCommandBuffers");
}

std::vector<VkCommandBuffer> Vulkan::AllocateCommandBuffers(
    VkDevice device,
    const VkCommandBufferAllocateInfo& allocate_info)
{
    return Internal::TakeValue(AllocateCommandBuffersCE(device, allocate_info));
}

VkCallResult<std::vector<VkDescriptorSet>> Vulkan::AllocateDescriptorSetsNE(
    VkDevice device,
    const VkDescriptorSetAllocateInfo& allocate_info) noexcept
{
    std::vector<VkDescriptorSet> descriptor_sets(allocate_info.descriptorSetCount);
    const VkResult result = vkAllocateDescriptorSets(device, &allocate_info, descriptor_sets.data());
    return {.result = result, .value = std::move(descriptor_sets)};
}

tl::expected<std::vector<VkDescriptorSet>, VulkanError> Vulkan::AllocateDescriptorSetsCE(
    VkDevice device,
    const VkDescriptorSetAllocateInfo& allocate_info) noexcept
{
    return Internal::ValueOrError(AllocateDescriptorSetsNE(device, allocate_info), "vkAllocateDescriptorSets");
}

std::vector<VkDescriptorSet> Vulkan::AllocateDescriptorSets(
    VkDevice device,
    const VkDescriptorSetAllocateInfo& allocate_info)
{
    return Internal::TakeValue(AllocateDescriptorSetsCE(device, allocate_info));
}

VkResult Vulkan::BeginCommandBufferNE(
    VkCommandBuffer command_buffer,
    const VkCommandBufferBeginInfo& begin_info) noexcept
{
    return vkBeginCommandBuffer(command_buffer, &begin_info);
}

std::optional<VulkanError> Vulkan::BeginCommandBufferCE(
    VkCommandBuffer command_buffer,
    const VkCommandBufferBeginInfo& begin_info) noexcept
{
    return Internal::ErrorOrNothing(BeginCommandBufferNE(command_buffer, begin_info), "vkBeginCommandBuffer");
}

void Vulkan::BeginCommandBuffer(VkCommandBuffer command_buffer, const VkCommandBufferBeginInfo& begin_info)
{
    Internal::ThrowIfError(BeginCommandBufferCE(command_buffer, begin_info));
}

#define KLVK_DEFINE_CREATE_WRAPPERS(Name, VkFunction, ParentType, parent_name, InfoType, HandleType) \
    VkCallResult<HandleType> Vulkan::Name##NE(                                                       \
        ParentType parent_name,                                                                      \
        const InfoType& create_info,                                                                 \
        const VkAllocationCallbacks* allocator) noexcept                                             \
    {                                                                                                \
        HandleType handle = VK_NULL_HANDLE;                                                          \
        const VkResult result = VkFunction(parent_name, &create_info, allocator, &handle);           \
        return {.result = result, .value = handle};                                                  \
    }                                                                                                \
                                                                                                     \
    tl::expected<HandleType, VulkanError> Vulkan::Name##CE(                                          \
        ParentType parent_name,                                                                      \
        const InfoType& create_info,                                                                 \
        const VkAllocationCallbacks* allocator) noexcept                                             \
    {                                                                                                \
        return Internal::ValueOrError(Name##NE(parent_name, create_info, allocator), #VkFunction);   \
    }                                                                                                \
                                                                                                     \
    HandleType Vulkan::Name(                                                                         \
        ParentType parent_name,                                                                      \
        const InfoType& create_info,                                                                 \
        const VkAllocationCallbacks* allocator)                                                      \
    {                                                                                                \
        return Internal::TakeValue(Name##CE(parent_name, create_info, allocator));                   \
    }

KLVK_DEFINE_CREATE_WRAPPERS(
    CreateCommandPool,
    vkCreateCommandPool,
    VkDevice,
    device,
    VkCommandPoolCreateInfo,
    VkCommandPool)
KLVK_DEFINE_CREATE_WRAPPERS(
    CreateDebugUtilsMessengerEXT,
    vkCreateDebugUtilsMessengerEXT,
    VkInstance,
    instance,
    VkDebugUtilsMessengerCreateInfoEXT,
    VkDebugUtilsMessengerEXT)
KLVK_DEFINE_CREATE_WRAPPERS(
    CreateDescriptorPool,
    vkCreateDescriptorPool,
    VkDevice,
    device,
    VkDescriptorPoolCreateInfo,
    VkDescriptorPool)
KLVK_DEFINE_CREATE_WRAPPERS(
    CreateDescriptorSetLayout,
    vkCreateDescriptorSetLayout,
    VkDevice,
    device,
    VkDescriptorSetLayoutCreateInfo,
    VkDescriptorSetLayout)
KLVK_DEFINE_CREATE_WRAPPERS(
    CreateDevice,
    vkCreateDevice,
    VkPhysicalDevice,
    physical_device,
    VkDeviceCreateInfo,
    VkDevice)
KLVK_DEFINE_CREATE_WRAPPERS(CreateFence, vkCreateFence, VkDevice, device, VkFenceCreateInfo, VkFence)
KLVK_DEFINE_CREATE_WRAPPERS(CreateImageView, vkCreateImageView, VkDevice, device, VkImageViewCreateInfo, VkImageView)
KLVK_DEFINE_CREATE_WRAPPERS(
    CreatePipelineLayout,
    vkCreatePipelineLayout,
    VkDevice,
    device,
    VkPipelineLayoutCreateInfo,
    VkPipelineLayout)
KLVK_DEFINE_CREATE_WRAPPERS(CreateSampler, vkCreateSampler, VkDevice, device, VkSamplerCreateInfo, VkSampler)
KLVK_DEFINE_CREATE_WRAPPERS(CreateSemaphore, vkCreateSemaphore, VkDevice, device, VkSemaphoreCreateInfo, VkSemaphore)
KLVK_DEFINE_CREATE_WRAPPERS(
    CreateShaderModule,
    vkCreateShaderModule,
    VkDevice,
    device,
    VkShaderModuleCreateInfo,
    VkShaderModule)
KLVK_DEFINE_CREATE_WRAPPERS(
    CreateSwapchainKHR,
    vkCreateSwapchainKHR,
    VkDevice,
    device,
    VkSwapchainCreateInfoKHR,
    VkSwapchainKHR)

#undef KLVK_DEFINE_CREATE_WRAPPERS

VkCallResult<VkInstance> Vulkan::CreateInstanceNE(
    const VkInstanceCreateInfo& create_info,
    const VkAllocationCallbacks* allocator) noexcept
{
    VkInstance instance = VK_NULL_HANDLE;
    const VkResult result = vkCreateInstance(&create_info, allocator, &instance);
    return {.result = result, .value = instance};
}

tl::expected<VkInstance, VulkanError> Vulkan::CreateInstanceCE(
    const VkInstanceCreateInfo& create_info,
    const VkAllocationCallbacks* allocator) noexcept
{
    return Internal::ValueOrError(CreateInstanceNE(create_info, allocator), "vkCreateInstance");
}

VkInstance Vulkan::CreateInstance(const VkInstanceCreateInfo& create_info, const VkAllocationCallbacks* allocator)
{
    return Internal::TakeValue(CreateInstanceCE(create_info, allocator));
}

VkCallResult<std::vector<VkPipeline>> Vulkan::CreateGraphicsPipelinesNE(
    VkDevice device,
    VkPipelineCache pipeline_cache,
    std::span<const VkGraphicsPipelineCreateInfo> create_infos,
    const VkAllocationCallbacks* allocator) noexcept
{
    std::vector<VkPipeline> pipelines(create_infos.size());
    const VkResult result = vkCreateGraphicsPipelines(
        device,
        pipeline_cache,
        Internal::Count(create_infos.size()),
        create_infos.data(),
        allocator,
        pipelines.data());
    return {.result = result, .value = std::move(pipelines)};
}

tl::expected<std::vector<VkPipeline>, VulkanError> Vulkan::CreateGraphicsPipelinesCE(
    VkDevice device,
    VkPipelineCache pipeline_cache,
    std::span<const VkGraphicsPipelineCreateInfo> create_infos,
    const VkAllocationCallbacks* allocator) noexcept
{
    auto call_result = CreateGraphicsPipelinesNE(device, pipeline_cache, create_infos, allocator);
    if (call_result.result == VK_SUCCESS)
    {
        return std::move(call_result.value);
    }

    for (VkPipeline pipeline : call_result.value)
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device, pipeline, allocator);
        }
    }
    return Internal::Unexpected(call_result.result, "vkCreateGraphicsPipelines");
}

std::vector<VkPipeline> Vulkan::CreateGraphicsPipelines(
    VkDevice device,
    VkPipelineCache pipeline_cache,
    std::span<const VkGraphicsPipelineCreateInfo> create_infos,
    const VkAllocationCallbacks* allocator)
{
    return Internal::TakeValue(CreateGraphicsPipelinesCE(device, pipeline_cache, create_infos, allocator));
}

VkCallResult<std::vector<VkPipeline>> Vulkan::CreateComputePipelinesNE(
    VkDevice device,
    VkPipelineCache pipeline_cache,
    std::span<const VkComputePipelineCreateInfo> create_infos,
    const VkAllocationCallbacks* allocator) noexcept
{
    std::vector<VkPipeline> pipelines(create_infos.size());
    const VkResult result = vkCreateComputePipelines(
        device,
        pipeline_cache,
        Internal::Count(create_infos.size()),
        create_infos.data(),
        allocator,
        pipelines.data());
    return {.result = result, .value = std::move(pipelines)};
}

tl::expected<std::vector<VkPipeline>, VulkanError> Vulkan::CreateComputePipelinesCE(
    VkDevice device,
    VkPipelineCache pipeline_cache,
    std::span<const VkComputePipelineCreateInfo> create_infos,
    const VkAllocationCallbacks* allocator) noexcept
{
    auto call_result = CreateComputePipelinesNE(device, pipeline_cache, create_infos, allocator);
    if (call_result.result == VK_SUCCESS) return std::move(call_result.value);
    return Internal::Unexpected(call_result.result, "vkCreateComputePipelines");
}

std::vector<VkPipeline> Vulkan::CreateComputePipelines(
    VkDevice device,
    VkPipelineCache pipeline_cache,
    std::span<const VkComputePipelineCreateInfo> create_infos,
    const VkAllocationCallbacks* allocator)
{
    return Internal::TakeValue(CreateComputePipelinesCE(device, pipeline_cache, create_infos, allocator));
}

VkResult Vulkan::DeviceWaitIdleNE(VkDevice device) noexcept
{
    return vkDeviceWaitIdle(device);
}

std::optional<VulkanError> Vulkan::DeviceWaitIdleCE(VkDevice device) noexcept
{
    return Internal::ErrorOrNothing(DeviceWaitIdleNE(device), "vkDeviceWaitIdle");
}

void Vulkan::DeviceWaitIdle(VkDevice device)
{
    Internal::ThrowIfError(DeviceWaitIdleCE(device));
}

VkResult Vulkan::EndCommandBufferNE(VkCommandBuffer command_buffer) noexcept
{
    return vkEndCommandBuffer(command_buffer);
}

std::optional<VulkanError> Vulkan::EndCommandBufferCE(VkCommandBuffer command_buffer) noexcept
{
    return Internal::ErrorOrNothing(EndCommandBufferNE(command_buffer), "vkEndCommandBuffer");
}

void Vulkan::EndCommandBuffer(VkCommandBuffer command_buffer)
{
    Internal::ThrowIfError(EndCommandBufferCE(command_buffer));
}

VkCallResult<std::vector<VkExtensionProperties>> Vulkan::EnumerateDeviceExtensionPropertiesNE(
    VkPhysicalDevice physical_device,
    const char* layer_name) noexcept
{
    return Internal::Enumerate<VkExtensionProperties>(
        [physical_device, layer_name](uint32_t& count, VkExtensionProperties* properties)
        { return vkEnumerateDeviceExtensionProperties(physical_device, layer_name, &count, properties); });
}

tl::expected<std::vector<VkExtensionProperties>, VulkanError> Vulkan::EnumerateDeviceExtensionPropertiesCE(
    VkPhysicalDevice physical_device,
    const char* layer_name) noexcept
{
    return Internal::ValueOrError(
        EnumerateDeviceExtensionPropertiesNE(physical_device, layer_name),
        "vkEnumerateDeviceExtensionProperties");
}

std::vector<VkExtensionProperties> Vulkan::EnumerateDeviceExtensionProperties(
    VkPhysicalDevice physical_device,
    const char* layer_name)
{
    return Internal::TakeValue(EnumerateDeviceExtensionPropertiesCE(physical_device, layer_name));
}

VkCallResult<std::vector<VkLayerProperties>> Vulkan::EnumerateInstanceLayerPropertiesNE() noexcept
{
    return Internal::Enumerate<VkLayerProperties>([](uint32_t& count, VkLayerProperties* properties)
                                                  { return vkEnumerateInstanceLayerProperties(&count, properties); });
}

tl::expected<std::vector<VkLayerProperties>, VulkanError> Vulkan::EnumerateInstanceLayerPropertiesCE() noexcept
{
    return Internal::ValueOrError(EnumerateInstanceLayerPropertiesNE(), "vkEnumerateInstanceLayerProperties");
}

std::vector<VkLayerProperties> Vulkan::EnumerateInstanceLayerProperties()
{
    return Internal::TakeValue(EnumerateInstanceLayerPropertiesCE());
}

VkCallResult<std::vector<VkPhysicalDevice>> Vulkan::EnumeratePhysicalDevicesNE(VkInstance instance) noexcept
{
    return Internal::Enumerate<VkPhysicalDevice>([instance](uint32_t& count, VkPhysicalDevice* devices)
                                                 { return vkEnumeratePhysicalDevices(instance, &count, devices); });
}

tl::expected<std::vector<VkPhysicalDevice>, VulkanError> Vulkan::EnumeratePhysicalDevicesCE(
    VkInstance instance) noexcept
{
    return Internal::ValueOrError(EnumeratePhysicalDevicesNE(instance), "vkEnumeratePhysicalDevices");
}

std::vector<VkPhysicalDevice> Vulkan::EnumeratePhysicalDevices(VkInstance instance)
{
    return Internal::TakeValue(EnumeratePhysicalDevicesCE(instance));
}

VkCallResult<VkSurfaceCapabilitiesKHR> Vulkan::GetPhysicalDeviceSurfaceCapabilitiesKHRNE(
    VkPhysicalDevice physical_device,
    VkSurfaceKHR surface) noexcept
{
    VkSurfaceCapabilitiesKHR capabilities{};
    const VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &capabilities);
    return {.result = result, .value = capabilities};
}

tl::expected<VkSurfaceCapabilitiesKHR, VulkanError> Vulkan::GetPhysicalDeviceSurfaceCapabilitiesKHRCE(
    VkPhysicalDevice physical_device,
    VkSurfaceKHR surface) noexcept
{
    return Internal::ValueOrError(
        GetPhysicalDeviceSurfaceCapabilitiesKHRNE(physical_device, surface),
        "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
}

VkSurfaceCapabilitiesKHR Vulkan::GetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice physical_device,
    VkSurfaceKHR surface)
{
    return Internal::TakeValue(GetPhysicalDeviceSurfaceCapabilitiesKHRCE(physical_device, surface));
}

VkCallResult<std::vector<VkSurfaceFormatKHR>> Vulkan::GetPhysicalDeviceSurfaceFormatsKHRNE(
    VkPhysicalDevice physical_device,
    VkSurfaceKHR surface) noexcept
{
    return Internal::Enumerate<VkSurfaceFormatKHR>(
        [physical_device, surface](uint32_t& count, VkSurfaceFormatKHR* formats)
        { return vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, formats); });
}

tl::expected<std::vector<VkSurfaceFormatKHR>, VulkanError> Vulkan::GetPhysicalDeviceSurfaceFormatsKHRCE(
    VkPhysicalDevice physical_device,
    VkSurfaceKHR surface) noexcept
{
    return Internal::ValueOrError(
        GetPhysicalDeviceSurfaceFormatsKHRNE(physical_device, surface),
        "vkGetPhysicalDeviceSurfaceFormatsKHR");
}

std::vector<VkSurfaceFormatKHR> Vulkan::GetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice physical_device,
    VkSurfaceKHR surface)
{
    return Internal::TakeValue(GetPhysicalDeviceSurfaceFormatsKHRCE(physical_device, surface));
}

VkCallResult<std::vector<VkPresentModeKHR>> Vulkan::GetPhysicalDeviceSurfacePresentModesKHRNE(
    VkPhysicalDevice physical_device,
    VkSurfaceKHR surface) noexcept
{
    return Internal::Enumerate<VkPresentModeKHR>(
        [physical_device, surface](uint32_t& count, VkPresentModeKHR* modes)
        { return vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &count, modes); });
}

tl::expected<std::vector<VkPresentModeKHR>, VulkanError> Vulkan::GetPhysicalDeviceSurfacePresentModesKHRCE(
    VkPhysicalDevice physical_device,
    VkSurfaceKHR surface) noexcept
{
    return Internal::ValueOrError(
        GetPhysicalDeviceSurfacePresentModesKHRNE(physical_device, surface),
        "vkGetPhysicalDeviceSurfacePresentModesKHR");
}

std::vector<VkPresentModeKHR> Vulkan::GetPhysicalDeviceSurfacePresentModesKHR(
    VkPhysicalDevice physical_device,
    VkSurfaceKHR surface)
{
    return Internal::TakeValue(GetPhysicalDeviceSurfacePresentModesKHRCE(physical_device, surface));
}

VkCallResult<bool> Vulkan::GetPhysicalDeviceSurfaceSupportKHRNE(
    VkPhysicalDevice physical_device,
    uint32_t queue_family_index,
    VkSurfaceKHR surface) noexcept
{
    VkBool32 supported = VK_FALSE;
    const VkResult result =
        vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, queue_family_index, surface, &supported);
    return {.result = result, .value = supported == VK_TRUE};
}

tl::expected<bool, VulkanError> Vulkan::GetPhysicalDeviceSurfaceSupportKHRCE(
    VkPhysicalDevice physical_device,
    uint32_t queue_family_index,
    VkSurfaceKHR surface) noexcept
{
    return Internal::ValueOrError(
        GetPhysicalDeviceSurfaceSupportKHRNE(physical_device, queue_family_index, surface),
        "vkGetPhysicalDeviceSurfaceSupportKHR");
}

bool Vulkan::GetPhysicalDeviceSurfaceSupportKHR(
    VkPhysicalDevice physical_device,
    uint32_t queue_family_index,
    VkSurfaceKHR surface)
{
    return Internal::TakeValue(GetPhysicalDeviceSurfaceSupportKHRCE(physical_device, queue_family_index, surface));
}

VkCallResult<std::vector<VkImage>> Vulkan::GetSwapchainImagesKHRNE(VkDevice device, VkSwapchainKHR swapchain) noexcept
{
    return Internal::Enumerate<VkImage>([device, swapchain](uint32_t& count, VkImage* images)
                                        { return vkGetSwapchainImagesKHR(device, swapchain, &count, images); });
}

tl::expected<std::vector<VkImage>, VulkanError> Vulkan::GetSwapchainImagesKHRCE(
    VkDevice device,
    VkSwapchainKHR swapchain) noexcept
{
    return Internal::ValueOrError(GetSwapchainImagesKHRNE(device, swapchain), "vkGetSwapchainImagesKHR");
}

std::vector<VkImage> Vulkan::GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain)
{
    return Internal::TakeValue(GetSwapchainImagesKHRCE(device, swapchain));
}

VkResult Vulkan::QueuePresentKHRNE(VkQueue queue, const VkPresentInfoKHR& present_info) noexcept
{
    return vkQueuePresentKHR(queue, &present_info);
}

tl::expected<PresentStatus, VulkanError> Vulkan::QueuePresentKHRCE(
    VkQueue queue,
    const VkPresentInfoKHR& present_info) noexcept
{
    const VkResult result = QueuePresentKHRNE(queue, present_info);
    switch (result)
    {
    case VK_SUCCESS:
        return PresentStatus::Presented;
    case VK_SUBOPTIMAL_KHR:
        return PresentStatus::Suboptimal;
    case VK_ERROR_OUT_OF_DATE_KHR:
        return PresentStatus::OutOfDate;
    default:
        return Internal::Unexpected(result, "vkQueuePresentKHR");
    }
}

PresentStatus Vulkan::QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR& present_info)
{
    return Internal::TakeValue(QueuePresentKHRCE(queue, present_info));
}

VkResult Vulkan::QueueSubmit2NE(VkQueue queue, std::span<const VkSubmitInfo2> submit_infos, VkFence fence) noexcept
{
    return vkQueueSubmit2(queue, Internal::Count(submit_infos.size()), submit_infos.data(), fence);
}

std::optional<VulkanError>
Vulkan::QueueSubmit2CE(VkQueue queue, std::span<const VkSubmitInfo2> submit_infos, VkFence fence) noexcept
{
    return Internal::ErrorOrNothing(QueueSubmit2NE(queue, submit_infos, fence), "vkQueueSubmit2");
}

void Vulkan::QueueSubmit2(VkQueue queue, std::span<const VkSubmitInfo2> submit_infos, VkFence fence)
{
    Internal::ThrowIfError(QueueSubmit2CE(queue, submit_infos, fence));
}

VkResult Vulkan::QueueWaitIdleNE(VkQueue queue) noexcept
{
    return vkQueueWaitIdle(queue);
}

std::optional<VulkanError> Vulkan::QueueWaitIdleCE(VkQueue queue) noexcept
{
    return Internal::ErrorOrNothing(QueueWaitIdleNE(queue), "vkQueueWaitIdle");
}

void Vulkan::QueueWaitIdle(VkQueue queue)
{
    Internal::ThrowIfError(QueueWaitIdleCE(queue));
}

VkResult Vulkan::ResetCommandPoolNE(VkDevice device, VkCommandPool command_pool, VkCommandPoolResetFlags flags) noexcept
{
    return vkResetCommandPool(device, command_pool, flags);
}

std::optional<VulkanError>
Vulkan::ResetCommandPoolCE(VkDevice device, VkCommandPool command_pool, VkCommandPoolResetFlags flags) noexcept
{
    return Internal::ErrorOrNothing(ResetCommandPoolNE(device, command_pool, flags), "vkResetCommandPool");
}

void Vulkan::ResetCommandPool(VkDevice device, VkCommandPool command_pool, VkCommandPoolResetFlags flags)
{
    Internal::ThrowIfError(ResetCommandPoolCE(device, command_pool, flags));
}

VkResult Vulkan::ResetFencesNE(VkDevice device, std::span<const VkFence> fences) noexcept
{
    return vkResetFences(device, Internal::Count(fences.size()), fences.data());
}

std::optional<VulkanError> Vulkan::ResetFencesCE(VkDevice device, std::span<const VkFence> fences) noexcept
{
    return Internal::ErrorOrNothing(ResetFencesNE(device, fences), "vkResetFences");
}

void Vulkan::ResetFences(VkDevice device, std::span<const VkFence> fences)
{
    Internal::ThrowIfError(ResetFencesCE(device, fences));
}

VkResult
Vulkan::WaitForFencesNE(VkDevice device, std::span<const VkFence> fences, bool wait_all, uint64_t timeout) noexcept
{
    return vkWaitForFences(
        device,
        Internal::Count(fences.size()),
        fences.data(),
        wait_all ? VK_TRUE : VK_FALSE,
        timeout);
}

tl::expected<WaitStatus, VulkanError>
Vulkan::WaitForFencesCE(VkDevice device, std::span<const VkFence> fences, bool wait_all, uint64_t timeout) noexcept
{
    const VkResult result = WaitForFencesNE(device, fences, wait_all, timeout);
    switch (result)
    {
    case VK_SUCCESS:
        return WaitStatus::Complete;
    case VK_TIMEOUT:
        return WaitStatus::Timeout;
    default:
        return Internal::Unexpected(result, "vkWaitForFences");
    }
}

WaitStatus Vulkan::WaitForFences(VkDevice device, std::span<const VkFence> fences, bool wait_all, uint64_t timeout)
{
    return Internal::TakeValue(WaitForFencesCE(device, fences, wait_all, timeout));
}

void Vulkan::CmdBeginRenderingNE(VkCommandBuffer command_buffer, const VkRenderingInfo& rendering_info) noexcept
{
    vkCmdBeginRendering(command_buffer, &rendering_info);
}

void Vulkan::CmdBeginRendering(VkCommandBuffer command_buffer, const VkRenderingInfo& rendering_info) noexcept
{
    CmdBeginRenderingNE(command_buffer, rendering_info);
}

void Vulkan::CmdBindDescriptorSetsNE(
    VkCommandBuffer command_buffer,
    VkPipelineBindPoint pipeline_bind_point,
    VkPipelineLayout layout,
    uint32_t first_set,
    std::span<const VkDescriptorSet> descriptor_sets,
    std::span<const uint32_t> dynamic_offsets) noexcept
{
    vkCmdBindDescriptorSets(
        command_buffer,
        pipeline_bind_point,
        layout,
        first_set,
        Internal::Count(descriptor_sets.size()),
        descriptor_sets.data(),
        Internal::Count(dynamic_offsets.size()),
        dynamic_offsets.data());
}

void Vulkan::CmdBindDescriptorSets(
    VkCommandBuffer command_buffer,
    VkPipelineBindPoint pipeline_bind_point,
    VkPipelineLayout layout,
    uint32_t first_set,
    std::span<const VkDescriptorSet> descriptor_sets,
    std::span<const uint32_t> dynamic_offsets) noexcept
{
    CmdBindDescriptorSetsNE(command_buffer, pipeline_bind_point, layout, first_set, descriptor_sets, dynamic_offsets);
}

void Vulkan::CmdBindPipelineNE(
    VkCommandBuffer command_buffer,
    VkPipelineBindPoint pipeline_bind_point,
    VkPipeline pipeline) noexcept
{
    vkCmdBindPipeline(command_buffer, pipeline_bind_point, pipeline);
}

void Vulkan::CmdBindPipeline(
    VkCommandBuffer command_buffer,
    VkPipelineBindPoint pipeline_bind_point,
    VkPipeline pipeline) noexcept
{
    CmdBindPipelineNE(command_buffer, pipeline_bind_point, pipeline);
}

void Vulkan::CmdCopyBufferToImageNE(
    VkCommandBuffer command_buffer,
    VkBuffer source,
    VkImage destination,
    VkImageLayout destination_layout,
    std::span<const VkBufferImageCopy> regions) noexcept
{
    vkCmdCopyBufferToImage(
        command_buffer,
        source,
        destination,
        destination_layout,
        Internal::Count(regions.size()),
        regions.data());
}

void Vulkan::CmdCopyBufferToImage(
    VkCommandBuffer command_buffer,
    VkBuffer source,
    VkImage destination,
    VkImageLayout destination_layout,
    std::span<const VkBufferImageCopy> regions) noexcept
{
    CmdCopyBufferToImageNE(command_buffer, source, destination, destination_layout, regions);
}

void Vulkan::CmdBindIndexBufferNE(
    VkCommandBuffer command_buffer,
    VkBuffer buffer,
    VkDeviceSize offset,
    VkIndexType index_type) noexcept
{
    vkCmdBindIndexBuffer(command_buffer, buffer, offset, index_type);
}

void Vulkan::CmdBindIndexBuffer(
    VkCommandBuffer command_buffer,
    VkBuffer buffer,
    VkDeviceSize offset,
    VkIndexType index_type) noexcept
{
    CmdBindIndexBufferNE(command_buffer, buffer, offset, index_type);
}

void Vulkan::CmdBindVertexBuffersNE(
    VkCommandBuffer command_buffer,
    uint32_t first_binding,
    std::span<const VkBuffer> buffers,
    std::span<const VkDeviceSize> offsets) noexcept
{
    assert(buffers.size() == offsets.size());
    vkCmdBindVertexBuffers(
        command_buffer,
        first_binding,
        Internal::Count(buffers.size()),
        buffers.data(),
        offsets.data());
}

void Vulkan::CmdBindVertexBuffers(
    VkCommandBuffer command_buffer,
    uint32_t first_binding,
    std::span<const VkBuffer> buffers,
    std::span<const VkDeviceSize> offsets) noexcept
{
    CmdBindVertexBuffersNE(command_buffer, first_binding, buffers, offsets);
}

void Vulkan::CmdDrawIndexedNE(
    VkCommandBuffer command_buffer,
    uint32_t index_count,
    uint32_t instance_count,
    uint32_t first_index,
    int32_t vertex_offset,
    uint32_t first_instance) noexcept
{
    vkCmdDrawIndexed(command_buffer, index_count, instance_count, first_index, vertex_offset, first_instance);
}

void Vulkan::CmdDrawIndexed(
    VkCommandBuffer command_buffer,
    uint32_t index_count,
    uint32_t instance_count,
    uint32_t first_index,
    int32_t vertex_offset,
    uint32_t first_instance) noexcept
{
    CmdDrawIndexedNE(command_buffer, index_count, instance_count, first_index, vertex_offset, first_instance);
}

void Vulkan::CmdDrawNE(
    VkCommandBuffer command_buffer,
    uint32_t vertex_count,
    uint32_t instance_count,
    uint32_t first_vertex,
    uint32_t first_instance) noexcept
{
    vkCmdDraw(command_buffer, vertex_count, instance_count, first_vertex, first_instance);
}

void Vulkan::CmdDraw(
    VkCommandBuffer command_buffer,
    uint32_t vertex_count,
    uint32_t instance_count,
    uint32_t first_vertex,
    uint32_t first_instance) noexcept
{
    CmdDrawNE(command_buffer, vertex_count, instance_count, first_vertex, first_instance);
}

void Vulkan::CmdDispatchNE(
    VkCommandBuffer command_buffer,
    uint32_t group_count_x,
    uint32_t group_count_y,
    uint32_t group_count_z) noexcept
{
    vkCmdDispatch(command_buffer, group_count_x, group_count_y, group_count_z);
}

void Vulkan::CmdDispatch(
    VkCommandBuffer command_buffer,
    uint32_t group_count_x,
    uint32_t group_count_y,
    uint32_t group_count_z) noexcept
{
    CmdDispatchNE(command_buffer, group_count_x, group_count_y, group_count_z);
}

void Vulkan::CmdEndRenderingNE(VkCommandBuffer command_buffer) noexcept
{
    vkCmdEndRendering(command_buffer);
}

void Vulkan::CmdEndRendering(VkCommandBuffer command_buffer) noexcept
{
    CmdEndRenderingNE(command_buffer);
}

void Vulkan::CmdPipelineBarrier2NE(VkCommandBuffer command_buffer, const VkDependencyInfo& dependency_info) noexcept
{
    vkCmdPipelineBarrier2(command_buffer, &dependency_info);
}

void Vulkan::CmdPipelineBarrier2(VkCommandBuffer command_buffer, const VkDependencyInfo& dependency_info) noexcept
{
    CmdPipelineBarrier2NE(command_buffer, dependency_info);
}

void Vulkan::CmdPushConstantsNE(
    VkCommandBuffer command_buffer,
    VkPipelineLayout layout,
    VkShaderStageFlags stage_flags,
    uint32_t offset,
    std::span<const std::byte> values) noexcept
{
    vkCmdPushConstants(command_buffer, layout, stage_flags, offset, Internal::Count(values.size()), values.data());
}

void Vulkan::CmdPushConstants(
    VkCommandBuffer command_buffer,
    VkPipelineLayout layout,
    VkShaderStageFlags stage_flags,
    uint32_t offset,
    std::span<const std::byte> values) noexcept
{
    CmdPushConstantsNE(command_buffer, layout, stage_flags, offset, values);
}

void Vulkan::CmdSetScissorNE(
    VkCommandBuffer command_buffer,
    uint32_t first_scissor,
    std::span<const VkRect2D> scissors) noexcept
{
    vkCmdSetScissor(command_buffer, first_scissor, Internal::Count(scissors.size()), scissors.data());
}

void Vulkan::CmdSetScissor(
    VkCommandBuffer command_buffer,
    uint32_t first_scissor,
    std::span<const VkRect2D> scissors) noexcept
{
    CmdSetScissorNE(command_buffer, first_scissor, scissors);
}

void Vulkan::CmdSetViewportNE(
    VkCommandBuffer command_buffer,
    uint32_t first_viewport,
    std::span<const VkViewport> viewports) noexcept
{
    vkCmdSetViewport(command_buffer, first_viewport, Internal::Count(viewports.size()), viewports.data());
}

void Vulkan::CmdSetViewport(
    VkCommandBuffer command_buffer,
    uint32_t first_viewport,
    std::span<const VkViewport> viewports) noexcept
{
    CmdSetViewportNE(command_buffer, first_viewport, viewports);
}

#define KLVK_DEFINE_DESTROY_WRAPPERS(Name, VkFunction, ParentType, parent_name, HandleType, handle_name)               \
    void Vulkan::Name##NE(                                                                                             \
        ParentType parent_name,                                                                                        \
        HandleType handle_name,                                                                                        \
        const VkAllocationCallbacks* allocator) noexcept                                                               \
    {                                                                                                                  \
        VkFunction(parent_name, handle_name, allocator);                                                               \
    }                                                                                                                  \
                                                                                                                       \
    void Vulkan::Name(ParentType parent_name, HandleType handle_name, const VkAllocationCallbacks* allocator) noexcept \
    {                                                                                                                  \
        Name##NE(parent_name, handle_name, allocator);                                                                 \
    }

KLVK_DEFINE_DESTROY_WRAPPERS(DestroyCommandPool, vkDestroyCommandPool, VkDevice, device, VkCommandPool, command_pool)
KLVK_DEFINE_DESTROY_WRAPPERS(
    DestroyDebugUtilsMessengerEXT,
    vkDestroyDebugUtilsMessengerEXT,
    VkInstance,
    instance,
    VkDebugUtilsMessengerEXT,
    messenger)
KLVK_DEFINE_DESTROY_WRAPPERS(
    DestroyDescriptorPool,
    vkDestroyDescriptorPool,
    VkDevice,
    device,
    VkDescriptorPool,
    descriptor_pool)
KLVK_DEFINE_DESTROY_WRAPPERS(
    DestroyDescriptorSetLayout,
    vkDestroyDescriptorSetLayout,
    VkDevice,
    device,
    VkDescriptorSetLayout,
    descriptor_set_layout)
KLVK_DEFINE_DESTROY_WRAPPERS(DestroyFence, vkDestroyFence, VkDevice, device, VkFence, fence)
KLVK_DEFINE_DESTROY_WRAPPERS(DestroyImageView, vkDestroyImageView, VkDevice, device, VkImageView, image_view)
KLVK_DEFINE_DESTROY_WRAPPERS(DestroyPipeline, vkDestroyPipeline, VkDevice, device, VkPipeline, pipeline)
KLVK_DEFINE_DESTROY_WRAPPERS(
    DestroyPipelineLayout,
    vkDestroyPipelineLayout,
    VkDevice,
    device,
    VkPipelineLayout,
    pipeline_layout)
KLVK_DEFINE_DESTROY_WRAPPERS(DestroySampler, vkDestroySampler, VkDevice, device, VkSampler, sampler)
KLVK_DEFINE_DESTROY_WRAPPERS(DestroySemaphore, vkDestroySemaphore, VkDevice, device, VkSemaphore, semaphore)
KLVK_DEFINE_DESTROY_WRAPPERS(
    DestroyShaderModule,
    vkDestroyShaderModule,
    VkDevice,
    device,
    VkShaderModule,
    shader_module)
KLVK_DEFINE_DESTROY_WRAPPERS(DestroySurfaceKHR, vkDestroySurfaceKHR, VkInstance, instance, VkSurfaceKHR, surface)
KLVK_DEFINE_DESTROY_WRAPPERS(DestroySwapchainKHR, vkDestroySwapchainKHR, VkDevice, device, VkSwapchainKHR, swapchain)

#undef KLVK_DEFINE_DESTROY_WRAPPERS

void Vulkan::DestroyDeviceNE(VkDevice device, const VkAllocationCallbacks* allocator) noexcept
{
    vkDestroyDevice(device, allocator);
}

void Vulkan::DestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator) noexcept
{
    DestroyDeviceNE(device, allocator);
}

void Vulkan::DestroyInstanceNE(VkInstance instance, const VkAllocationCallbacks* allocator) noexcept
{
    vkDestroyInstance(instance, allocator);
}

void Vulkan::DestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator) noexcept
{
    DestroyInstanceNE(instance, allocator);
}

void Vulkan::FreeCommandBuffersNE(
    VkDevice device,
    VkCommandPool command_pool,
    std::span<const VkCommandBuffer> command_buffers) noexcept
{
    vkFreeCommandBuffers(device, command_pool, Internal::Count(command_buffers.size()), command_buffers.data());
}

void Vulkan::FreeCommandBuffers(
    VkDevice device,
    VkCommandPool command_pool,
    std::span<const VkCommandBuffer> command_buffers) noexcept
{
    FreeCommandBuffersNE(device, command_pool, command_buffers);
}

VkQueue Vulkan::GetDeviceQueueNE(VkDevice device, uint32_t queue_family_index, uint32_t queue_index) noexcept
{
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, queue_family_index, queue_index, &queue);
    return queue;
}

VkQueue Vulkan::GetDeviceQueue(VkDevice device, uint32_t queue_family_index, uint32_t queue_index) noexcept
{
    return GetDeviceQueueNE(device, queue_family_index, queue_index);
}

void Vulkan::GetPhysicalDeviceFeatures2NE(
    VkPhysicalDevice physical_device,
    VkPhysicalDeviceFeatures2& features) noexcept
{
    vkGetPhysicalDeviceFeatures2(physical_device, &features);
}

void Vulkan::GetPhysicalDeviceFeatures2(VkPhysicalDevice physical_device, VkPhysicalDeviceFeatures2& features) noexcept
{
    GetPhysicalDeviceFeatures2NE(physical_device, features);
}

VkPhysicalDeviceProperties Vulkan::GetPhysicalDevicePropertiesNE(VkPhysicalDevice physical_device) noexcept
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_device, &properties);
    return properties;
}

VkPhysicalDeviceProperties Vulkan::GetPhysicalDeviceProperties(VkPhysicalDevice physical_device) noexcept
{
    return GetPhysicalDevicePropertiesNE(physical_device);
}

std::vector<VkQueueFamilyProperties> Vulkan::GetPhysicalDeviceQueueFamilyPropertiesNE(
    VkPhysicalDevice physical_device) noexcept
{
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, properties.empty() ? nullptr : properties.data());
    properties.resize(count);
    return properties;
}

std::vector<VkQueueFamilyProperties> Vulkan::GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physical_device)
{
    return GetPhysicalDeviceQueueFamilyPropertiesNE(physical_device);
}

void Vulkan::UpdateDescriptorSetsNE(
    VkDevice device,
    std::span<const VkWriteDescriptorSet> descriptor_writes,
    std::span<const VkCopyDescriptorSet> descriptor_copies) noexcept
{
    vkUpdateDescriptorSets(
        device,
        Internal::Count(descriptor_writes.size()),
        descriptor_writes.data(),
        Internal::Count(descriptor_copies.size()),
        descriptor_copies.data());
}

void Vulkan::UpdateDescriptorSets(
    VkDevice device,
    std::span<const VkWriteDescriptorSet> descriptor_writes,
    std::span<const VkCopyDescriptorSet> descriptor_copies) noexcept
{
    UpdateDescriptorSetsNE(device, descriptor_writes, descriptor_copies);
}

}  // namespace klvk
