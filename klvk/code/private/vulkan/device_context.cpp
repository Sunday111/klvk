#include "klvk/vulkan/device_context.hpp"

#include <GLFW/glfw3.h>
#include <fmt/core.h>
#include <vk_mem_alloc.h>

#include <vector>

#include "klvk/error_handling.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/shader/shader_cache_manager.hpp"
#include "klvk/vulkan/vulkan_api.hpp"

// Vulkan create-info structs are designed for partial designated initialization;
// unlisted fields must be zero.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

namespace klvk
{

namespace
{

VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessengerCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    [[maybe_unused]] void* user_data)
{
    const auto style = severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? fmt::fg(fmt::rgb(255, 0, 0))
                                                                                 : fmt::fg(fmt::rgb(255, 255, 0));
    fmt::print(style, "[vulkan] {}\n", callback_data->pMessage);
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT MakeDebugMessengerCreateInfo()
{
    return {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = DebugMessengerCallback,
    };
}

bool HasLayer(std::string_view name)
{
    const auto layers = Vulkan::EnumerateInstanceLayerProperties();
    for (const auto& layer : layers)
    {
        if (name == layer.layerName) return true;
    }
    return false;
}

bool HasDeviceExtension(VkPhysicalDevice device, std::string_view name)
{
    const auto extensions = Vulkan::EnumerateDeviceExtensionProperties(device);
    for (const auto& extension : extensions)
    {
        if (name == extension.extensionName) return true;
    }
    return false;
}

void InitializeVolkOnce()
{
    static const VkResult result = volkInitialize();
    CheckVkResult(result, "volkInitialize");
}

}  // namespace

DeviceContext::DeviceContext(GLFWwindow* window) : DeviceContext(window, Settings{}) {}

DeviceContext::DeviceContext(GLFWwindow* window, const Settings& settings)
{
    InitializeVolkOnce();
    CreateInstance(settings);
    CheckVkResult(glfwCreateWindowSurface(instance_, window, nullptr, &surface_), "glfwCreateWindowSurface");
    PickPhysicalDevice();
    CreateDevice();
    CreateAllocator();

    const VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = graphics_queue_family_,
    };
    one_time_pool_ = Vulkan::CreateCommandPool(device_, pool_info);
}

DeviceContext::~DeviceContext()
{
    shader_cache_.reset();
    if (device_) (void)Vulkan::DeviceWaitIdleNE(device_);
    if (one_time_pool_) Vulkan::DestroyCommandPool(device_, one_time_pool_);
    if (allocator_) vmaDestroyAllocator(allocator_);
    if (device_) Vulkan::DestroyDevice(device_);
    if (surface_) Vulkan::DestroySurfaceKHR(instance_, surface_);
    if (debug_messenger_) Vulkan::DestroyDebugUtilsMessengerEXT(instance_, debug_messenger_);
    if (instance_) Vulkan::DestroyInstance(instance_);
}

void DeviceContext::InitializeShaderCache(
    const std::filesystem::path& source_root,
    const std::filesystem::path& cache_root)
{
    ErrorHandling::Ensure(!shader_cache_, "Shader cache is already initialized");
    shader_cache_ = std::make_unique<ShaderCacheManager>(source_root, cache_root);
}

ShaderCacheManager& DeviceContext::GetShaderCacheManager() const
{
    ErrorHandling::Ensure(shader_cache_ != nullptr, "Shader cache is not initialized");
    return *shader_cache_;
}

VkShaderModule DeviceContext::CreateShaderModuleFromSource(const std::filesystem::path& source_path) const
{
    const auto spirv = GetShaderCacheManager().GetOrCompile(source_path);
    return CreateShaderModule(
        std::string_view(reinterpret_cast<const char*>(spirv->data()), spirv->size() * sizeof(u32)),
        source_path.filename().string());
}

void DeviceContext::CreateInstance(const Settings& settings)
{
    const VkApplicationInfo app_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = settings.app_name.c_str(),
        .pEngineName = "klvk",
        .apiVersion = VK_API_VERSION_1_3,
    };

    u32 glfw_extension_count = 0;
    const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
    ErrorHandling::Ensure(glfw_extensions != nullptr, "GLFW cannot provide Vulkan instance extensions");
    std::vector<const char*> extensions(glfw_extensions, glfw_extensions + glfw_extension_count);  // NOLINT

    std::vector<const char*> layers;
    const bool validation = settings.enable_validation && HasLayer("VK_LAYER_KHRONOS_validation");
    if (settings.enable_validation && !validation)
    {
        fmt::print("klvk: VK_LAYER_KHRONOS_validation requested but is not available\n");
    }

    VkInstanceCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
    };

    const auto messenger_info = MakeDebugMessengerCreateInfo();
    if (validation)
    {
        layers.push_back("VK_LAYER_KHRONOS_validation");
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        create_info.pNext = &messenger_info;
    }

    create_info.enabledLayerCount = static_cast<u32>(layers.size());
    create_info.ppEnabledLayerNames = layers.data();
    create_info.enabledExtensionCount = static_cast<u32>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();

    instance_ = Vulkan::CreateInstance(create_info);
    volkLoadInstance(instance_);

    if (validation)
    {
        debug_messenger_ = Vulkan::CreateDebugUtilsMessengerEXT(instance_, messenger_info);
    }
}

void DeviceContext::PickPhysicalDevice()
{
    const auto devices = Vulkan::EnumeratePhysicalDevices(instance_);
    ErrorHandling::Ensure(!devices.empty(), "No Vulkan devices found");

    int best_score = -1;
    for (VkPhysicalDevice device : devices)
    {
        const VkPhysicalDeviceProperties properties = Vulkan::GetPhysicalDeviceProperties(device);
        if (properties.apiVersion < VK_API_VERSION_1_3) continue;
        if (!HasDeviceExtension(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;

        VkPhysicalDeviceVulkan13Features features13{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 features2{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &features13,
        };
        Vulkan::GetPhysicalDeviceFeatures2(device, features2);
        if (!features13.dynamicRendering || !features13.synchronization2) continue;

        // Single queue family that can do both graphics and present keeps ownership simple.
        const auto families = Vulkan::GetPhysicalDeviceQueueFamilyProperties(device);

        std::optional<u32> graphics_family;
        for (u32 family = 0; family != static_cast<u32>(families.size()); ++family)
        {
            if (!(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            if (Vulkan::GetPhysicalDeviceSurfaceSupportKHR(device, family, surface_))
            {
                graphics_family = family;
                break;
            }
        }
        if (!graphics_family) continue;

        int score = 1;
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 1000;
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 100;

        if (score > best_score)
        {
            best_score = score;
            physical_device_ = device;
            graphics_queue_family_ = *graphics_family;
        }
    }

    ErrorHandling::Ensure(
        physical_device_ != VK_NULL_HANDLE,
        "No Vulkan device with 1.3 dynamic rendering and a graphics+present queue found");

    const VkPhysicalDeviceProperties properties = Vulkan::GetPhysicalDeviceProperties(physical_device_);
    fmt::print("klvk: using device {}\n", properties.deviceName);
}

void DeviceContext::CreateDevice()
{
    const std::array priorities{1.f};
    const std::array queue_infos{VkDeviceQueueCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = graphics_queue_family_,
        .queueCount = priorities.size(),
        .pQueuePriorities = priorities.data(),
    }};

    VkPhysicalDeviceVulkan13Features features13{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };

    // Optional features are enabled when the hardware has them; users query the
    // corresponding accessors before creating pipelines that need them.
    VkPhysicalDeviceFeatures supported_features{};
    vkGetPhysicalDeviceFeatures(physical_device_, &supported_features);
    geometry_shader_enabled_ = supported_features.geometryShader == VK_TRUE;

    const VkPhysicalDeviceFeatures2 features2{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &features13,
        .features = {.geometryShader = supported_features.geometryShader},
    };

    std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    // Promoted to core in 1.3 but the imgui backend resolves the KHR-suffixed entry points,
    // which are only guaranteed to exist when the extension is enabled explicitly.
    if (HasDeviceExtension(physical_device_, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME))
    {
        extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    }

    // Lets device memory be exported as an opaque fd so another API (CUDA) can import
    // the same allocation. Optional: only interop code needs it.
    if (HasDeviceExtension(physical_device_, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME))
    {
        extensions.push_back(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
        external_memory_fd_enabled_ = true;
    }

    const VkDeviceCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features2,
        .queueCreateInfoCount = queue_infos.size(),
        .pQueueCreateInfos = queue_infos.data(),
        .enabledExtensionCount = static_cast<u32>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

    device_ = Vulkan::CreateDevice(physical_device_, create_info);
    volkLoadDevice(device_);
    graphics_queue_ = Vulkan::GetDeviceQueue(device_, graphics_queue_family_, 0);
}

void DeviceContext::CreateAllocator()
{
    const VmaVulkanFunctions functions{
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
    };
    const VmaAllocatorCreateInfo create_info{
        .physicalDevice = physical_device_,
        .device = device_,
        .pVulkanFunctions = &functions,
        .instance = instance_,
        .vulkanApiVersion = VK_API_VERSION_1_3,
    };
    CheckVkResult(vmaCreateAllocator(&create_info, &allocator_), "vmaCreateAllocator");
}

void DeviceContext::WaitIdle() const
{
    Vulkan::DeviceWaitIdle(device_);
}

VkCommandBuffer DeviceContext::BeginOneTimeCommands() const
{
    const VkCommandBufferAllocateInfo allocate_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = one_time_pool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    const auto command_buffers = Vulkan::AllocateCommandBuffers(device_, allocate_info);
    const VkCommandBuffer command_buffer = command_buffers.front();

    const VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    Vulkan::BeginCommandBuffer(command_buffer, begin_info);
    return command_buffer;
}

void DeviceContext::EndOneTimeCommands(VkCommandBuffer command_buffer) const
{
    Vulkan::EndCommandBuffer(command_buffer);

    const std::array command_buffer_infos{VkCommandBufferSubmitInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = command_buffer,
    }};
    const std::array submit_infos{VkSubmitInfo2{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = command_buffer_infos.size(),
        .pCommandBufferInfos = command_buffer_infos.data(),
    }};
    Vulkan::QueueSubmit2(graphics_queue_, submit_infos);
    Vulkan::QueueWaitIdle(graphics_queue_);
    const std::array command_buffers{command_buffer};
    Vulkan::FreeCommandBuffers(device_, one_time_pool_, command_buffers);
}

VkShaderModule DeviceContext::CreateShaderModule(std::string_view spirv_bytes, std::string_view debug_name) const
{
    ErrorHandling::Ensure(
        spirv_bytes.size() % sizeof(u32) == 0 && !spirv_bytes.empty(),
        "Shader '{}': SPIR-V size {} is not a multiple of 4",
        debug_name,
        spirv_bytes.size());

    const VkShaderModuleCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spirv_bytes.size(),
        .pCode = reinterpret_cast<const u32*>(spirv_bytes.data()),  // NOLINT
    };
    return Vulkan::CreateShaderModule(device_, create_info);
}

}  // namespace klvk
