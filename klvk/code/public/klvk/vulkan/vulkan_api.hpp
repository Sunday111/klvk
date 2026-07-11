#pragma once

#include <volk.h>  // IWYU pragma: export

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <tl/expected.hpp>
#include <vector>

#include "klvk/vulkan/detail/settings.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{

template <typename T>
struct VkCallResult
{
    VkResult result = VK_SUCCESS;
    T value{};
};

enum class WaitStatus : uint8_t
{
    Complete,
    Timeout,
};

enum class AcquireNextImageStatus : uint8_t
{
    Acquired,
    Suboptimal,
    OutOfDate,
    NotReady,
    Timeout,
};

struct AcquireNextImageOutcome
{
    AcquireNextImageStatus status = AcquireNextImageStatus::Acquired;
    std::optional<uint32_t> image_index{};
};

enum class PresentStatus : uint8_t
{
    Presented,
    Suboptimal,
    OutOfDate,
};

// Vulkan calls with a VkResult have three forms:
// - NE invokes Vulkan and returns the raw VkResult together with typed output.
// - CE converts failure results to VulkanError without throwing.
// - no suffix throws the exact VulkanError produced by CE.
// Vulkan void calls have NE and no-suffix forms because there is no error to consume.
class Vulkan
{
    struct Internal;

public:
    /*********************************************** Result calls ****************************************************/

    [[nodiscard]] KLVK_VK_INLINE static VkCallResult<uint32_t> AcquireNextImageKHRNE(
        VkDevice device,
        VkSwapchainKHR swapchain,
        uint64_t timeout,
        VkSemaphore semaphore = VK_NULL_HANDLE,
        VkFence fence = VK_NULL_HANDLE) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static tl::expected<AcquireNextImageOutcome, VulkanError> AcquireNextImageKHRCE(
        VkDevice device,
        VkSwapchainKHR swapchain,
        uint64_t timeout,
        VkSemaphore semaphore = VK_NULL_HANDLE,
        VkFence fence = VK_NULL_HANDLE) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static AcquireNextImageOutcome AcquireNextImageKHR(
        VkDevice device,
        VkSwapchainKHR swapchain,
        uint64_t timeout,
        VkSemaphore semaphore = VK_NULL_HANDLE,
        VkFence fence = VK_NULL_HANDLE);

    [[nodiscard]] KLVK_VK_INLINE static VkCallResult<std::vector<VkCommandBuffer>> AllocateCommandBuffersNE(
        VkDevice device,
        const VkCommandBufferAllocateInfo& allocate_info) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static tl::expected<std::vector<VkCommandBuffer>, VulkanError>
    AllocateCommandBuffersCE(VkDevice device, const VkCommandBufferAllocateInfo& allocate_info) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static std::vector<VkCommandBuffer> AllocateCommandBuffers(
        VkDevice device,
        const VkCommandBufferAllocateInfo& allocate_info);

    [[nodiscard]] KLVK_VK_INLINE static VkCallResult<std::vector<VkDescriptorSet>> AllocateDescriptorSetsNE(
        VkDevice device,
        const VkDescriptorSetAllocateInfo& allocate_info) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static tl::expected<std::vector<VkDescriptorSet>, VulkanError>
    AllocateDescriptorSetsCE(VkDevice device, const VkDescriptorSetAllocateInfo& allocate_info) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static std::vector<VkDescriptorSet> AllocateDescriptorSets(
        VkDevice device,
        const VkDescriptorSetAllocateInfo& allocate_info);

    [[nodiscard]] KLVK_VK_INLINE static VkResult BeginCommandBufferNE(
        VkCommandBuffer command_buffer,
        const VkCommandBufferBeginInfo& begin_info) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static std::optional<VulkanError> BeginCommandBufferCE(
        VkCommandBuffer command_buffer,
        const VkCommandBufferBeginInfo& begin_info) noexcept;
    KLVK_VK_INLINE static void BeginCommandBuffer(
        VkCommandBuffer command_buffer,
        const VkCommandBufferBeginInfo& begin_info);

    [[nodiscard]] KLVK_VK_INLINE static VkCallResult<VkCommandPool> CreateCommandPoolNE(
        VkDevice device,
        const VkCommandPoolCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static tl::expected<VkCommandPool, VulkanError> CreateCommandPoolCE(
        VkDevice device,
        const VkCommandPoolCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static VkCommandPool CreateCommandPool(
        VkDevice device,
        const VkCommandPoolCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr);

    [[nodiscard]] KLVK_VK_INLINE static VkCallResult<VkDebugUtilsMessengerEXT> CreateDebugUtilsMessengerEXTNE(
        VkInstance instance,
        const VkDebugUtilsMessengerCreateInfoEXT& create_info,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static tl::expected<VkDebugUtilsMessengerEXT, VulkanError>
    CreateDebugUtilsMessengerEXTCE(
        VkInstance instance,
        const VkDebugUtilsMessengerCreateInfoEXT& create_info,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static VkDebugUtilsMessengerEXT CreateDebugUtilsMessengerEXT(
        VkInstance instance,
        const VkDebugUtilsMessengerCreateInfoEXT& create_info,
        const VkAllocationCallbacks* allocator = nullptr);

    [[nodiscard]] KLVK_VK_INLINE static VkCallResult<VkDescriptorPool> CreateDescriptorPoolNE(
        VkDevice device,
        const VkDescriptorPoolCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static tl::expected<VkDescriptorPool, VulkanError> CreateDescriptorPoolCE(
        VkDevice device,
        const VkDescriptorPoolCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static VkDescriptorPool CreateDescriptorPool(
        VkDevice device,
        const VkDescriptorPoolCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr);

    [[nodiscard]] KLVK_VK_INLINE static VkCallResult<VkDescriptorSetLayout> CreateDescriptorSetLayoutNE(
        VkDevice device,
        const VkDescriptorSetLayoutCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static tl::expected<VkDescriptorSetLayout, VulkanError> CreateDescriptorSetLayoutCE(
        VkDevice device,
        const VkDescriptorSetLayoutCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static VkDescriptorSetLayout CreateDescriptorSetLayout(
        VkDevice device,
        const VkDescriptorSetLayoutCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr);

    [[nodiscard]] KLVK_VK_INLINE static VkCallResult<VkDevice> CreateDeviceNE(
        VkPhysicalDevice physical_device,
        const VkDeviceCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static tl::expected<VkDevice, VulkanError> CreateDeviceCE(
        VkPhysicalDevice physical_device,
        const VkDeviceCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static VkDevice CreateDevice(
        VkPhysicalDevice physical_device,
        const VkDeviceCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr);

    [[nodiscard]] KLVK_VK_INLINE static VkCallResult<VkFence> CreateFenceNE(
        VkDevice device,
        const VkFenceCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static tl::expected<VkFence, VulkanError> CreateFenceCE(
        VkDevice device,
        const VkFenceCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static VkFence CreateFence(
        VkDevice device,
        const VkFenceCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr);

    [[nodiscard]] KLVK_VK_INLINE static VkCallResult<std::vector<VkPipeline>> CreateGraphicsPipelinesNE(
        VkDevice device,
        VkPipelineCache pipeline_cache,
        std::span<const VkGraphicsPipelineCreateInfo> create_infos,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static tl::expected<std::vector<VkPipeline>, VulkanError> CreateGraphicsPipelinesCE(
        VkDevice device,
        VkPipelineCache pipeline_cache,
        std::span<const VkGraphicsPipelineCreateInfo> create_infos,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static std::vector<VkPipeline> CreateGraphicsPipelines(
        VkDevice device,
        VkPipelineCache pipeline_cache,
        std::span<const VkGraphicsPipelineCreateInfo> create_infos,
        const VkAllocationCallbacks* allocator = nullptr);

    [[nodiscard]] KLVK_VK_INLINE static VkCallResult<VkImageView> CreateImageViewNE(
        VkDevice device,
        const VkImageViewCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static tl::expected<VkImageView, VulkanError> CreateImageViewCE(
        VkDevice device,
        const VkImageViewCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static VkImageView CreateImageView(
        VkDevice device,
        const VkImageViewCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr);

    [[nodiscard]] KLVK_VK_INLINE static VkCallResult<VkInstance> CreateInstanceNE(
        const VkInstanceCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static tl::expected<VkInstance, VulkanError> CreateInstanceCE(
        const VkInstanceCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static VkInstance CreateInstance(
        const VkInstanceCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr);

    [[nodiscard]] KLVK_VK_INLINE static VkCallResult<VkPipelineLayout> CreatePipelineLayoutNE(
        VkDevice device,
        const VkPipelineLayoutCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static tl::expected<VkPipelineLayout, VulkanError> CreatePipelineLayoutCE(
        VkDevice device,
        const VkPipelineLayoutCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static VkPipelineLayout CreatePipelineLayout(
        VkDevice device,
        const VkPipelineLayoutCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr);

    [[nodiscard]] KLVK_VK_INLINE static VkCallResult<VkSampler> CreateSamplerNE(
        VkDevice device,
        const VkSamplerCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static tl::expected<VkSampler, VulkanError> CreateSamplerCE(
        VkDevice device,
        const VkSamplerCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static VkSampler CreateSampler(
        VkDevice device,
        const VkSamplerCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr);

    [[nodiscard]] KLVK_VK_INLINE static VkCallResult<VkSemaphore> CreateSemaphoreNE(
        VkDevice device,
        const VkSemaphoreCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static tl::expected<VkSemaphore, VulkanError> CreateSemaphoreCE(
        VkDevice device,
        const VkSemaphoreCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static VkSemaphore CreateSemaphore(
        VkDevice device,
        const VkSemaphoreCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr);

    [[nodiscard]] KLVK_VK_INLINE static VkCallResult<VkShaderModule> CreateShaderModuleNE(
        VkDevice device,
        const VkShaderModuleCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static tl::expected<VkShaderModule, VulkanError> CreateShaderModuleCE(
        VkDevice device,
        const VkShaderModuleCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static VkShaderModule CreateShaderModule(
        VkDevice device,
        const VkShaderModuleCreateInfo& create_info,
        const VkAllocationCallbacks* allocator = nullptr);

    [[nodiscard]] KLVK_VK_INLINE static VkCallResult<VkSwapchainKHR> CreateSwapchainKHRNE(
        VkDevice device,
        const VkSwapchainCreateInfoKHR& create_info,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static tl::expected<VkSwapchainKHR, VulkanError> CreateSwapchainKHRCE(
        VkDevice device,
        const VkSwapchainCreateInfoKHR& create_info,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static VkSwapchainKHR CreateSwapchainKHR(
        VkDevice device,
        const VkSwapchainCreateInfoKHR& create_info,
        const VkAllocationCallbacks* allocator = nullptr);

    [[nodiscard]] KLVK_VK_INLINE static VkResult DeviceWaitIdleNE(VkDevice device) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static std::optional<VulkanError> DeviceWaitIdleCE(VkDevice device) noexcept;
    KLVK_VK_INLINE static void DeviceWaitIdle(VkDevice device);

    [[nodiscard]] KLVK_VK_INLINE static VkResult EndCommandBufferNE(VkCommandBuffer command_buffer) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static std::optional<VulkanError> EndCommandBufferCE(
        VkCommandBuffer command_buffer) noexcept;
    KLVK_VK_INLINE static void EndCommandBuffer(VkCommandBuffer command_buffer);

    [[nodiscard]] KLVK_VK_INLINE static VkCallResult<std::vector<VkExtensionProperties>>
    EnumerateDeviceExtensionPropertiesNE(VkPhysicalDevice physical_device, const char* layer_name = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static tl::expected<std::vector<VkExtensionProperties>, VulkanError>
    EnumerateDeviceExtensionPropertiesCE(VkPhysicalDevice physical_device, const char* layer_name = nullptr) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static std::vector<VkExtensionProperties> EnumerateDeviceExtensionProperties(
        VkPhysicalDevice physical_device,
        const char* layer_name = nullptr);

    [[nodiscard]] KLVK_VK_INLINE static VkCallResult<std::vector<VkLayerProperties>>
    EnumerateInstanceLayerPropertiesNE() noexcept;
    [[nodiscard]] KLVK_VK_INLINE static tl::expected<std::vector<VkLayerProperties>, VulkanError>
    EnumerateInstanceLayerPropertiesCE() noexcept;
    [[nodiscard]] KLVK_VK_INLINE static std::vector<VkLayerProperties> EnumerateInstanceLayerProperties();

    [[nodiscard]] KLVK_VK_INLINE static VkCallResult<std::vector<VkPhysicalDevice>> EnumeratePhysicalDevicesNE(
        VkInstance instance) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static tl::expected<std::vector<VkPhysicalDevice>, VulkanError>
    EnumeratePhysicalDevicesCE(VkInstance instance) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static std::vector<VkPhysicalDevice> EnumeratePhysicalDevices(VkInstance instance);

    [[nodiscard]] KLVK_VK_INLINE static VkCallResult<VkSurfaceCapabilitiesKHR>
    GetPhysicalDeviceSurfaceCapabilitiesKHRNE(VkPhysicalDevice physical_device, VkSurfaceKHR surface) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static tl::expected<VkSurfaceCapabilitiesKHR, VulkanError>
    GetPhysicalDeviceSurfaceCapabilitiesKHRCE(VkPhysicalDevice physical_device, VkSurfaceKHR surface) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static VkSurfaceCapabilitiesKHR GetPhysicalDeviceSurfaceCapabilitiesKHR(
        VkPhysicalDevice physical_device,
        VkSurfaceKHR surface);

    [[nodiscard]] KLVK_VK_INLINE static VkCallResult<std::vector<VkSurfaceFormatKHR>>
    GetPhysicalDeviceSurfaceFormatsKHRNE(VkPhysicalDevice physical_device, VkSurfaceKHR surface) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static tl::expected<std::vector<VkSurfaceFormatKHR>, VulkanError>
    GetPhysicalDeviceSurfaceFormatsKHRCE(VkPhysicalDevice physical_device, VkSurfaceKHR surface) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static std::vector<VkSurfaceFormatKHR> GetPhysicalDeviceSurfaceFormatsKHR(
        VkPhysicalDevice physical_device,
        VkSurfaceKHR surface);

    [[nodiscard]] KLVK_VK_INLINE static VkCallResult<std::vector<VkPresentModeKHR>>
    GetPhysicalDeviceSurfacePresentModesKHRNE(VkPhysicalDevice physical_device, VkSurfaceKHR surface) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static tl::expected<std::vector<VkPresentModeKHR>, VulkanError>
    GetPhysicalDeviceSurfacePresentModesKHRCE(VkPhysicalDevice physical_device, VkSurfaceKHR surface) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static std::vector<VkPresentModeKHR> GetPhysicalDeviceSurfacePresentModesKHR(
        VkPhysicalDevice physical_device,
        VkSurfaceKHR surface);

    [[nodiscard]] KLVK_VK_INLINE static VkCallResult<bool> GetPhysicalDeviceSurfaceSupportKHRNE(
        VkPhysicalDevice physical_device,
        uint32_t queue_family_index,
        VkSurfaceKHR surface) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static tl::expected<bool, VulkanError> GetPhysicalDeviceSurfaceSupportKHRCE(
        VkPhysicalDevice physical_device,
        uint32_t queue_family_index,
        VkSurfaceKHR surface) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static bool GetPhysicalDeviceSurfaceSupportKHR(
        VkPhysicalDevice physical_device,
        uint32_t queue_family_index,
        VkSurfaceKHR surface);

    [[nodiscard]] KLVK_VK_INLINE static VkCallResult<std::vector<VkImage>> GetSwapchainImagesKHRNE(
        VkDevice device,
        VkSwapchainKHR swapchain) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static tl::expected<std::vector<VkImage>, VulkanError> GetSwapchainImagesKHRCE(
        VkDevice device,
        VkSwapchainKHR swapchain) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static std::vector<VkImage> GetSwapchainImagesKHR(
        VkDevice device,
        VkSwapchainKHR swapchain);

    [[nodiscard]] KLVK_VK_INLINE static VkResult QueuePresentKHRNE(
        VkQueue queue,
        const VkPresentInfoKHR& present_info) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static tl::expected<PresentStatus, VulkanError> QueuePresentKHRCE(
        VkQueue queue,
        const VkPresentInfoKHR& present_info) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static PresentStatus QueuePresentKHR(
        VkQueue queue,
        const VkPresentInfoKHR& present_info);

    [[nodiscard]] KLVK_VK_INLINE static VkResult
    QueueSubmit2NE(VkQueue queue, std::span<const VkSubmitInfo2> submit_infos, VkFence fence = VK_NULL_HANDLE) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static std::optional<VulkanError>
    QueueSubmit2CE(VkQueue queue, std::span<const VkSubmitInfo2> submit_infos, VkFence fence = VK_NULL_HANDLE) noexcept;
    KLVK_VK_INLINE static void
    QueueSubmit2(VkQueue queue, std::span<const VkSubmitInfo2> submit_infos, VkFence fence = VK_NULL_HANDLE);

    [[nodiscard]] KLVK_VK_INLINE static VkResult QueueWaitIdleNE(VkQueue queue) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static std::optional<VulkanError> QueueWaitIdleCE(VkQueue queue) noexcept;
    KLVK_VK_INLINE static void QueueWaitIdle(VkQueue queue);

    [[nodiscard]] KLVK_VK_INLINE static VkResult
    ResetCommandPoolNE(VkDevice device, VkCommandPool command_pool, VkCommandPoolResetFlags flags = 0) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static std::optional<VulkanError>
    ResetCommandPoolCE(VkDevice device, VkCommandPool command_pool, VkCommandPoolResetFlags flags = 0) noexcept;
    KLVK_VK_INLINE static void
    ResetCommandPool(VkDevice device, VkCommandPool command_pool, VkCommandPoolResetFlags flags = 0);

    [[nodiscard]] KLVK_VK_INLINE static VkResult ResetFencesNE(
        VkDevice device,
        std::span<const VkFence> fences) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static std::optional<VulkanError> ResetFencesCE(
        VkDevice device,
        std::span<const VkFence> fences) noexcept;
    KLVK_VK_INLINE static void ResetFences(VkDevice device, std::span<const VkFence> fences);

    [[nodiscard]] KLVK_VK_INLINE static VkResult WaitForFencesNE(
        VkDevice device,
        std::span<const VkFence> fences,
        bool wait_all,
        uint64_t timeout = std::numeric_limits<uint64_t>::max()) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static tl::expected<WaitStatus, VulkanError> WaitForFencesCE(
        VkDevice device,
        std::span<const VkFence> fences,
        bool wait_all,
        uint64_t timeout = std::numeric_limits<uint64_t>::max()) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static WaitStatus WaitForFences(
        VkDevice device,
        std::span<const VkFence> fences,
        bool wait_all,
        uint64_t timeout = std::numeric_limits<uint64_t>::max());

    /************************************************ Void calls *****************************************************/

    KLVK_VK_INLINE static void CmdBeginRenderingNE(
        VkCommandBuffer command_buffer,
        const VkRenderingInfo& rendering_info) noexcept;
    KLVK_VK_INLINE static void CmdBeginRendering(
        VkCommandBuffer command_buffer,
        const VkRenderingInfo& rendering_info) noexcept;

    KLVK_VK_INLINE static void CmdBindDescriptorSetsNE(
        VkCommandBuffer command_buffer,
        VkPipelineBindPoint pipeline_bind_point,
        VkPipelineLayout layout,
        uint32_t first_set,
        std::span<const VkDescriptorSet> descriptor_sets,
        std::span<const uint32_t> dynamic_offsets = {}) noexcept;
    KLVK_VK_INLINE static void CmdBindDescriptorSets(
        VkCommandBuffer command_buffer,
        VkPipelineBindPoint pipeline_bind_point,
        VkPipelineLayout layout,
        uint32_t first_set,
        std::span<const VkDescriptorSet> descriptor_sets,
        std::span<const uint32_t> dynamic_offsets = {}) noexcept;

    KLVK_VK_INLINE static void CmdBindIndexBufferNE(
        VkCommandBuffer command_buffer,
        VkBuffer buffer,
        VkDeviceSize offset,
        VkIndexType index_type) noexcept;
    KLVK_VK_INLINE static void CmdBindIndexBuffer(
        VkCommandBuffer command_buffer,
        VkBuffer buffer,
        VkDeviceSize offset,
        VkIndexType index_type) noexcept;

    KLVK_VK_INLINE static void CmdBindPipelineNE(
        VkCommandBuffer command_buffer,
        VkPipelineBindPoint pipeline_bind_point,
        VkPipeline pipeline) noexcept;
    KLVK_VK_INLINE static void CmdBindPipeline(
        VkCommandBuffer command_buffer,
        VkPipelineBindPoint pipeline_bind_point,
        VkPipeline pipeline) noexcept;

    // buffers and offsets must have the same size.
    KLVK_VK_INLINE static void CmdBindVertexBuffersNE(
        VkCommandBuffer command_buffer,
        uint32_t first_binding,
        std::span<const VkBuffer> buffers,
        std::span<const VkDeviceSize> offsets) noexcept;
    KLVK_VK_INLINE static void CmdBindVertexBuffers(
        VkCommandBuffer command_buffer,
        uint32_t first_binding,
        std::span<const VkBuffer> buffers,
        std::span<const VkDeviceSize> offsets) noexcept;

    KLVK_VK_INLINE static void CmdCopyBufferToImageNE(
        VkCommandBuffer command_buffer,
        VkBuffer source,
        VkImage destination,
        VkImageLayout destination_layout,
        std::span<const VkBufferImageCopy> regions) noexcept;
    KLVK_VK_INLINE static void CmdCopyBufferToImage(
        VkCommandBuffer command_buffer,
        VkBuffer source,
        VkImage destination,
        VkImageLayout destination_layout,
        std::span<const VkBufferImageCopy> regions) noexcept;

    KLVK_VK_INLINE static void CmdDrawNE(
        VkCommandBuffer command_buffer,
        uint32_t vertex_count,
        uint32_t instance_count,
        uint32_t first_vertex,
        uint32_t first_instance) noexcept;
    KLVK_VK_INLINE static void CmdDraw(
        VkCommandBuffer command_buffer,
        uint32_t vertex_count,
        uint32_t instance_count,
        uint32_t first_vertex,
        uint32_t first_instance) noexcept;

    KLVK_VK_INLINE static void CmdDrawIndexedNE(
        VkCommandBuffer command_buffer,
        uint32_t index_count,
        uint32_t instance_count,
        uint32_t first_index,
        int32_t vertex_offset,
        uint32_t first_instance) noexcept;
    KLVK_VK_INLINE static void CmdDrawIndexed(
        VkCommandBuffer command_buffer,
        uint32_t index_count,
        uint32_t instance_count,
        uint32_t first_index,
        int32_t vertex_offset,
        uint32_t first_instance) noexcept;

    KLVK_VK_INLINE static void CmdEndRenderingNE(VkCommandBuffer command_buffer) noexcept;
    KLVK_VK_INLINE static void CmdEndRendering(VkCommandBuffer command_buffer) noexcept;

    KLVK_VK_INLINE static void CmdPipelineBarrier2NE(
        VkCommandBuffer command_buffer,
        const VkDependencyInfo& dependency_info) noexcept;
    KLVK_VK_INLINE static void CmdPipelineBarrier2(
        VkCommandBuffer command_buffer,
        const VkDependencyInfo& dependency_info) noexcept;

    KLVK_VK_INLINE static void CmdPushConstantsNE(
        VkCommandBuffer command_buffer,
        VkPipelineLayout layout,
        VkShaderStageFlags stage_flags,
        uint32_t offset,
        std::span<const std::byte> values) noexcept;
    KLVK_VK_INLINE static void CmdPushConstants(
        VkCommandBuffer command_buffer,
        VkPipelineLayout layout,
        VkShaderStageFlags stage_flags,
        uint32_t offset,
        std::span<const std::byte> values) noexcept;

    template <typename T>
    static void CmdPushConstantsNE(
        VkCommandBuffer command_buffer,
        VkPipelineLayout layout,
        VkShaderStageFlags stage_flags,
        uint32_t offset,
        const T& value) noexcept
    {
        CmdPushConstantsNE(command_buffer, layout, stage_flags, offset, std::as_bytes(std::span{&value, 1}));
    }

    template <typename T>
    static void CmdPushConstants(
        VkCommandBuffer command_buffer,
        VkPipelineLayout layout,
        VkShaderStageFlags stage_flags,
        uint32_t offset,
        const T& value) noexcept
    {
        CmdPushConstants(command_buffer, layout, stage_flags, offset, std::as_bytes(std::span{&value, 1}));
    }

    KLVK_VK_INLINE static void CmdSetScissorNE(
        VkCommandBuffer command_buffer,
        uint32_t first_scissor,
        std::span<const VkRect2D> scissors) noexcept;
    KLVK_VK_INLINE static void
    CmdSetScissor(VkCommandBuffer command_buffer, uint32_t first_scissor, std::span<const VkRect2D> scissors) noexcept;

    KLVK_VK_INLINE static void CmdSetViewportNE(
        VkCommandBuffer command_buffer,
        uint32_t first_viewport,
        std::span<const VkViewport> viewports) noexcept;
    KLVK_VK_INLINE static void CmdSetViewport(
        VkCommandBuffer command_buffer,
        uint32_t first_viewport,
        std::span<const VkViewport> viewports) noexcept;

    KLVK_VK_INLINE static void DestroyCommandPoolNE(
        VkDevice device,
        VkCommandPool command_pool,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    KLVK_VK_INLINE static void DestroyCommandPool(
        VkDevice device,
        VkCommandPool command_pool,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;

    KLVK_VK_INLINE static void DestroyDebugUtilsMessengerEXTNE(
        VkInstance instance,
        VkDebugUtilsMessengerEXT messenger,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    KLVK_VK_INLINE static void DestroyDebugUtilsMessengerEXT(
        VkInstance instance,
        VkDebugUtilsMessengerEXT messenger,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;

    KLVK_VK_INLINE static void DestroyDescriptorPoolNE(
        VkDevice device,
        VkDescriptorPool descriptor_pool,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    KLVK_VK_INLINE static void DestroyDescriptorPool(
        VkDevice device,
        VkDescriptorPool descriptor_pool,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;

    KLVK_VK_INLINE static void DestroyDescriptorSetLayoutNE(
        VkDevice device,
        VkDescriptorSetLayout descriptor_set_layout,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    KLVK_VK_INLINE static void DestroyDescriptorSetLayout(
        VkDevice device,
        VkDescriptorSetLayout descriptor_set_layout,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;

    KLVK_VK_INLINE static void DestroyDeviceNE(
        VkDevice device,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    KLVK_VK_INLINE static void DestroyDevice(
        VkDevice device,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;

    KLVK_VK_INLINE static void
    DestroyFenceNE(VkDevice device, VkFence fence, const VkAllocationCallbacks* allocator = nullptr) noexcept;
    KLVK_VK_INLINE static void
    DestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks* allocator = nullptr) noexcept;

    KLVK_VK_INLINE static void DestroyImageViewNE(
        VkDevice device,
        VkImageView image_view,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    KLVK_VK_INLINE static void DestroyImageView(
        VkDevice device,
        VkImageView image_view,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;

    KLVK_VK_INLINE static void DestroyInstanceNE(
        VkInstance instance,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    KLVK_VK_INLINE static void DestroyInstance(
        VkInstance instance,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;

    KLVK_VK_INLINE static void
    DestroyPipelineNE(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* allocator = nullptr) noexcept;
    KLVK_VK_INLINE static void
    DestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* allocator = nullptr) noexcept;

    KLVK_VK_INLINE static void DestroyPipelineLayoutNE(
        VkDevice device,
        VkPipelineLayout pipeline_layout,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    KLVK_VK_INLINE static void DestroyPipelineLayout(
        VkDevice device,
        VkPipelineLayout pipeline_layout,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;

    KLVK_VK_INLINE static void
    DestroySamplerNE(VkDevice device, VkSampler sampler, const VkAllocationCallbacks* allocator = nullptr) noexcept;
    KLVK_VK_INLINE static void
    DestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks* allocator = nullptr) noexcept;

    KLVK_VK_INLINE static void DestroySemaphoreNE(
        VkDevice device,
        VkSemaphore semaphore,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    KLVK_VK_INLINE static void
    DestroySemaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks* allocator = nullptr) noexcept;

    KLVK_VK_INLINE static void DestroyShaderModuleNE(
        VkDevice device,
        VkShaderModule shader_module,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    KLVK_VK_INLINE static void DestroyShaderModule(
        VkDevice device,
        VkShaderModule shader_module,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;

    KLVK_VK_INLINE static void DestroySurfaceKHRNE(
        VkInstance instance,
        VkSurfaceKHR surface,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    KLVK_VK_INLINE static void DestroySurfaceKHR(
        VkInstance instance,
        VkSurfaceKHR surface,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;

    KLVK_VK_INLINE static void DestroySwapchainKHRNE(
        VkDevice device,
        VkSwapchainKHR swapchain,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;
    KLVK_VK_INLINE static void DestroySwapchainKHR(
        VkDevice device,
        VkSwapchainKHR swapchain,
        const VkAllocationCallbacks* allocator = nullptr) noexcept;

    KLVK_VK_INLINE static void FreeCommandBuffersNE(
        VkDevice device,
        VkCommandPool command_pool,
        std::span<const VkCommandBuffer> command_buffers) noexcept;
    KLVK_VK_INLINE static void FreeCommandBuffers(
        VkDevice device,
        VkCommandPool command_pool,
        std::span<const VkCommandBuffer> command_buffers) noexcept;

    [[nodiscard]] KLVK_VK_INLINE static VkQueue
    GetDeviceQueueNE(VkDevice device, uint32_t queue_family_index, uint32_t queue_index) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static VkQueue
    GetDeviceQueue(VkDevice device, uint32_t queue_family_index, uint32_t queue_index) noexcept;

    KLVK_VK_INLINE static void GetPhysicalDeviceFeatures2NE(
        VkPhysicalDevice physical_device,
        VkPhysicalDeviceFeatures2& features) noexcept;
    KLVK_VK_INLINE static void GetPhysicalDeviceFeatures2(
        VkPhysicalDevice physical_device,
        VkPhysicalDeviceFeatures2& features) noexcept;

    [[nodiscard]] KLVK_VK_INLINE static VkPhysicalDeviceProperties GetPhysicalDevicePropertiesNE(
        VkPhysicalDevice physical_device) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static VkPhysicalDeviceProperties GetPhysicalDeviceProperties(
        VkPhysicalDevice physical_device) noexcept;

    [[nodiscard]] KLVK_VK_INLINE static std::vector<VkQueueFamilyProperties> GetPhysicalDeviceQueueFamilyPropertiesNE(
        VkPhysicalDevice physical_device) noexcept;
    [[nodiscard]] KLVK_VK_INLINE static std::vector<VkQueueFamilyProperties> GetPhysicalDeviceQueueFamilyProperties(
        VkPhysicalDevice physical_device);

    KLVK_VK_INLINE static void UpdateDescriptorSetsNE(
        VkDevice device,
        std::span<const VkWriteDescriptorSet> descriptor_writes,
        std::span<const VkCopyDescriptorSet> descriptor_copies = {}) noexcept;
    KLVK_VK_INLINE static void UpdateDescriptorSets(
        VkDevice device,
        std::span<const VkWriteDescriptorSet> descriptor_writes,
        std::span<const VkCopyDescriptorSet> descriptor_copies = {}) noexcept;
};

}  // namespace klvk

#if KLVK_INLINE_VULKAN_WRAPPERS
#include "klvk/vulkan/detail/vulkan_api_impl.hpp"
#endif
