#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

VK_DEFINE_HANDLE(VmaAllocator)

namespace klvk
{

class ShaderCacheManager;
class ShaderModule;
class Window;

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

    explicit DeviceContext(Window* presentation_window);
    DeviceContext(Window* presentation_window, const Settings& settings);
    DeviceContext(const DeviceContext&) = delete;
    DeviceContext(DeviceContext&&) = delete;
    ~DeviceContext();

    [[nodiscard]] vk::Instance GetInstance() const noexcept { return instance_; }
    [[nodiscard]] vk::SurfaceKHR GetSurface() const noexcept { return surface_; }
    [[nodiscard]] vk::PhysicalDevice GetPhysicalDevice() const noexcept { return physical_device_; }
    [[nodiscard]] vk::Device GetDevice() const noexcept { return device_; }
    [[nodiscard]] vk::Queue GetGraphicsQueue() const noexcept { return graphics_queue_; }
    [[nodiscard]] u32 GetGraphicsQueueFamily() const noexcept { return graphics_queue_family_; }
    [[nodiscard]] VmaAllocator GetAllocator() const noexcept { return allocator_; }

    // True when the geometryShader feature was available and enabled on the device.
    [[nodiscard]] bool IsGeometryShaderEnabled() const noexcept { return geometry_shader_enabled_; }

    // True when the tessellationShader feature was available and enabled on the device.
    [[nodiscard]] bool IsTessellationShaderEnabled() const noexcept { return tessellation_shader_enabled_; }

    // True when VK_KHR_external_memory_fd was available and enabled, which lets device
    // memory allocated here be exported as an opaque fd and imported by an external API.
    [[nodiscard]] bool IsExternalMemoryFdEnabled() const noexcept { return external_memory_fd_enabled_; }

    void WaitIdle() const;

    // Records commands into a temporary command buffer, submits it to the graphics queue and waits for completion.
    template <typename F>
    void SubmitOneTimeCommands(F&& record) const
    {
        vk::CommandBuffer command_buffer = BeginOneTimeCommands();
        std::forward<F>(record)(command_buffer);
        EndOneTimeCommands(command_buffer);
    }

    [[nodiscard]] vk::ShaderModule CreateShaderModule(std::string_view spirv_bytes, std::string_view debug_name) const;
    void InitializeShaderCache(const std::filesystem::path& source_root, const std::filesystem::path& cache_root = {});
    [[nodiscard]] ShaderModule LoadShaderModule(const std::filesystem::path& source_path) const;
    [[nodiscard]] vk::ShaderModule CreateShaderModuleFromSourceUnchecked(
        const std::filesystem::path& source_path) const;
    [[nodiscard]] ShaderCacheManager& GetShaderCacheManager() const;

private:
    [[nodiscard]] vk::CommandBuffer BeginOneTimeCommands() const;
    void EndOneTimeCommands(vk::CommandBuffer command_buffer) const;

    void CreateInstance(const Settings& settings, const Window* presentation_window);
    void CreateDebugMessenger();
    void PickPhysicalDevice();
    void CreateDevice();
    void CreateAllocator();

    vk::Instance instance_ = nullptr;
    vk::DebugUtilsMessengerEXT debug_messenger_ = nullptr;
    vk::SurfaceKHR surface_ = nullptr;
    vk::PhysicalDevice physical_device_ = nullptr;
    vk::Device device_ = nullptr;
    vk::Queue graphics_queue_ = nullptr;
    u32 graphics_queue_family_ = 0;
    VmaAllocator allocator_ = nullptr;
    vk::CommandPool one_time_pool_ = nullptr;
    bool geometry_shader_enabled_ = false;
    bool tessellation_shader_enabled_ = false;
    bool external_memory_fd_enabled_ = false;
    bool presentation_enabled_ = false;
    std::unique_ptr<ShaderCacheManager> shader_cache_;
};

}  // namespace klvk
