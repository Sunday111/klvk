#pragma once

#include <string>

#include "klvk/vulkan/vulkan_common.hpp"

struct GLFWwindow;

VK_DEFINE_HANDLE(VmaAllocator)

namespace klvk
{

#ifdef NDEBUG
inline constexpr bool kDebugBuild = false;
#else
inline constexpr bool kDebugBuild = true;
#endif

// Owns the Vulkan objects that live for the whole application lifetime:
// instance, surface, physical + logical device, queue and the VMA allocator.
class DeviceContext
{
public:
    struct Settings
    {
        std::string app_name = "klvk";
        bool enable_validation = kDebugBuild;
    };

    explicit DeviceContext(GLFWwindow* window);
    DeviceContext(GLFWwindow* window, const Settings& settings);
    DeviceContext(const DeviceContext&) = delete;
    DeviceContext(DeviceContext&&) = delete;
    ~DeviceContext();

    [[nodiscard]] VkInstance GetInstance() const noexcept { return instance_; }
    [[nodiscard]] VkSurfaceKHR GetSurface() const noexcept { return surface_; }
    [[nodiscard]] VkPhysicalDevice GetPhysicalDevice() const noexcept { return physical_device_; }
    [[nodiscard]] VkDevice GetDevice() const noexcept { return device_; }
    [[nodiscard]] VkQueue GetGraphicsQueue() const noexcept { return graphics_queue_; }
    [[nodiscard]] uint32_t GetGraphicsQueueFamily() const noexcept { return graphics_queue_family_; }
    [[nodiscard]] VmaAllocator GetAllocator() const noexcept { return allocator_; }

    void WaitIdle() const;

    // Records commands into a temporary command buffer, submits it to the graphics queue and waits for completion.
    template <typename F>
    void SubmitOneTimeCommands(F&& record) const
    {
        VkCommandBuffer command_buffer = BeginOneTimeCommands();
        std::forward<F>(record)(command_buffer);
        EndOneTimeCommands(command_buffer);
    }

    [[nodiscard]] VkShaderModule CreateShaderModule(std::string_view spirv_bytes, std::string_view debug_name) const;

private:
    [[nodiscard]] VkCommandBuffer BeginOneTimeCommands() const;
    void EndOneTimeCommands(VkCommandBuffer command_buffer) const;

    void CreateInstance(const Settings& settings);
    void CreateDebugMessenger();
    void PickPhysicalDevice();
    void CreateDevice();
    void CreateAllocator();

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    uint32_t graphics_queue_family_ = 0;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkCommandPool one_time_pool_ = VK_NULL_HANDLE;
};

}  // namespace klvk
