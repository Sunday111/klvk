#include "klvk/vulkan/device_context.hpp"

#include <GLFW/glfw3.h>
#include <fmt/core.h>
#include <vk_mem_alloc.h>

#include <vector>

#include "klvk/error_handling.hpp"

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
    const auto style = severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
                           ? fmt::fg(fmt::rgb(255, 0, 0))
                           : fmt::fg(fmt::rgb(255, 255, 0));
    fmt::print(style, "[vulkan] {}\n", callback_data->pMessage);
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT MakeDebugMessengerCreateInfo()
{
    return {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = DebugMessengerCallback,
    };
}

bool HasLayer(std::string_view name)
{
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const auto& layer : layers)
    {
        if (name == layer.layerName) return true;
    }
    return false;
}

bool HasDeviceExtension(VkPhysicalDevice device, std::string_view name)
{
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
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
    CheckVkResult(vkCreateCommandPool(device_, &pool_info, nullptr, &one_time_pool_), "vkCreateCommandPool");
}

DeviceContext::~DeviceContext()
{
    if (device_) vkDeviceWaitIdle(device_);
    if (one_time_pool_) vkDestroyCommandPool(device_, one_time_pool_, nullptr);
    if (allocator_) vmaDestroyAllocator(allocator_);
    if (device_) vkDestroyDevice(device_, nullptr);
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (debug_messenger_) vkDestroyDebugUtilsMessengerEXT(instance_, debug_messenger_, nullptr);
    if (instance_) vkDestroyInstance(instance_, nullptr);
}

void DeviceContext::CreateInstance(const Settings& settings)
{
    const VkApplicationInfo app_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = settings.app_name.c_str(),
        .pEngineName = "klvk",
        .apiVersion = VK_API_VERSION_1_3,
    };

    uint32_t glfw_extension_count = 0;
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

    create_info.enabledLayerCount = static_cast<uint32_t>(layers.size());
    create_info.ppEnabledLayerNames = layers.data();
    create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();

    CheckVkResult(vkCreateInstance(&create_info, nullptr, &instance_), "vkCreateInstance");
    volkLoadInstance(instance_);

    if (validation)
    {
        CheckVkResult(
            vkCreateDebugUtilsMessengerEXT(instance_, &messenger_info, nullptr, &debug_messenger_),
            "vkCreateDebugUtilsMessengerEXT");
    }
}

void DeviceContext::PickPhysicalDevice()
{
    uint32_t count = 0;
    CheckVkResult(vkEnumeratePhysicalDevices(instance_, &count, nullptr), "vkEnumeratePhysicalDevices");
    ErrorHandling::Ensure(count != 0, "No Vulkan devices found");
    std::vector<VkPhysicalDevice> devices(count);
    CheckVkResult(vkEnumeratePhysicalDevices(instance_, &count, devices.data()), "vkEnumeratePhysicalDevices");

    int best_score = -1;
    for (VkPhysicalDevice device : devices)
    {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(device, &properties);
        if (properties.apiVersion < VK_API_VERSION_1_3) continue;
        if (!HasDeviceExtension(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;

        VkPhysicalDeviceVulkan13Features features13{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 features2{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &features13,
        };
        vkGetPhysicalDeviceFeatures2(device, &features2);
        if (!features13.dynamicRendering || !features13.synchronization2) continue;

        // Single queue family that can do both graphics and present keeps ownership simple.
        uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, families.data());

        std::optional<uint32_t> graphics_family;
        for (uint32_t family = 0; family != family_count; ++family)
        {
            if (!(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            VkBool32 present_supported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, family, surface_, &present_supported);
            if (present_supported)
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

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical_device_, &properties);
    fmt::print("klvk: using device {}\n", properties.deviceName);
}

void DeviceContext::CreateDevice()
{
    const float priority = 1.f;
    const VkDeviceQueueCreateInfo queue_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = graphics_queue_family_,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };

    VkPhysicalDeviceVulkan13Features features13{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };
    const VkPhysicalDeviceFeatures2 features2{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &features13,
    };

    std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    // Promoted to core in 1.3 but the imgui backend resolves the KHR-suffixed entry points,
    // which are only guaranteed to exist when the extension is enabled explicitly.
    if (HasDeviceExtension(physical_device_, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME))
    {
        extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    }

    const VkDeviceCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features2,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

    CheckVkResult(vkCreateDevice(physical_device_, &create_info, nullptr, &device_), "vkCreateDevice");
    volkLoadDevice(device_);
    vkGetDeviceQueue(device_, graphics_queue_family_, 0, &graphics_queue_);
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
    CheckVkResult(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
}

VkCommandBuffer DeviceContext::BeginOneTimeCommands() const
{
    const VkCommandBufferAllocateInfo allocate_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = one_time_pool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    CheckVkResult(vkAllocateCommandBuffers(device_, &allocate_info, &command_buffer), "vkAllocateCommandBuffers");

    const VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    CheckVkResult(vkBeginCommandBuffer(command_buffer, &begin_info), "vkBeginCommandBuffer");
    return command_buffer;
}

void DeviceContext::EndOneTimeCommands(VkCommandBuffer command_buffer) const
{
    CheckVkResult(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");

    const VkCommandBufferSubmitInfo command_buffer_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = command_buffer,
    };
    const VkSubmitInfo2 submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &command_buffer_info,
    };
    CheckVkResult(vkQueueSubmit2(graphics_queue_, 1, &submit_info, VK_NULL_HANDLE), "vkQueueSubmit2");
    CheckVkResult(vkQueueWaitIdle(graphics_queue_), "vkQueueWaitIdle");
    vkFreeCommandBuffers(device_, one_time_pool_, 1, &command_buffer);
}

VkShaderModule DeviceContext::CreateShaderModule(std::string_view spirv_bytes, std::string_view debug_name) const
{
    ErrorHandling::Ensure(
        spirv_bytes.size() % sizeof(uint32_t) == 0 && !spirv_bytes.empty(),
        "Shader '{}': SPIR-V size {} is not a multiple of 4",
        debug_name,
        spirv_bytes.size());

    const VkShaderModuleCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spirv_bytes.size(),
        .pCode = reinterpret_cast<const uint32_t*>(spirv_bytes.data()),  // NOLINT
    };
    VkShaderModule shader_module = VK_NULL_HANDLE;
    CheckVkResult(
        vkCreateShaderModule(device_, &create_info, nullptr, &shader_module),
        "vkCreateShaderModule");
    return shader_module;
}

}  // namespace klvk
