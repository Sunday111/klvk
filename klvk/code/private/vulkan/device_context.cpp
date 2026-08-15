#include "klvk/vulkan/device_context.hpp"

#include <fmt/core.h>
#include <vk_mem_alloc.h>

#include <vector>

#include "klvk/error_handling.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/shader/shader_cache_manager.hpp"
#include "klvk/shader/shader_module.hpp"
#include "klvk/vulkan/vulkan_common.hpp"
#include "klvk/window.hpp"

namespace klvk
{
namespace
{

VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugMessengerCallback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
    [[maybe_unused]] vk::DebugUtilsMessageTypeFlagsEXT type,
    const vk::DebugUtilsMessengerCallbackDataEXT* callback_data,
    [[maybe_unused]] void* user_data)
{
    const auto style = severity >= vk::DebugUtilsMessageSeverityFlagBitsEXT::eError ? fmt::fg(fmt::rgb(255, 0, 0))
                                                                                    : fmt::fg(fmt::rgb(255, 255, 0));
    fmt::print(style, "[vulkan] {}\n", callback_data->pMessage);
    return vk::False;
}

vk::DebugUtilsMessengerCreateInfoEXT MakeDebugMessengerCreateInfo()
{
    return vk::DebugUtilsMessengerCreateInfoEXT{}
        .setMessageSeverity(
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)
        .setMessageType(
            vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
            vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance)
        .setPfnUserCallback(DebugMessengerCallback);
}

bool HasLayer(std::string_view name)
{
    const auto layers = vk::enumerateInstanceLayerProperties();
    for (const auto& layer : layers)
    {
        if (name == layer.layerName) return true;
    }
    return false;
}

bool HasDeviceExtension(vk::PhysicalDevice device, std::string_view name)
{
    const auto extensions = device.enumerateDeviceExtensionProperties();
    for (const auto& extension : extensions)
    {
        if (name == extension.extensionName) return true;
    }
    return false;
}

}  // namespace

DeviceContext::DeviceContext(Window* presentation_window) : DeviceContext(presentation_window, Settings{}) {}

DeviceContext::DeviceContext(Window* presentation_window, const Settings& settings)
{
    InitializeVulkanDispatcher();
    presentation_enabled_ = presentation_window != nullptr;
    CreateInstance(settings, presentation_window);
    if (presentation_window) surface_ = presentation_window->CreateVulkanSurface(instance_.get());
    PickPhysicalDevice();
    CreateDevice();
    CreateAllocator();

    const auto pool_info = vk::CommandPoolCreateInfo{}
                               .setFlags(vk::CommandPoolCreateFlagBits::eTransient)
                               .setQueueFamilyIndex(graphics_queue_family_);
    one_time_pool_ = GetDevice().createCommandPoolUnique(pool_info);
}

DeviceContext::~DeviceContext()
{
    shader_cache_.reset();
    WaitIdleNoexcept();
    one_time_pool_.reset();
    if (allocator_) vmaDestroyAllocator(allocator_);
    allocator_ = nullptr;
    device_.reset();
    surface_.reset();
    debug_messenger_.reset();
    instance_.reset();
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

ShaderModule DeviceContext::LoadShaderModule(const std::filesystem::path& source_path) const
{
    const auto shader = GetShaderCacheManager().GetOrCompile(source_path);
    ErrorHandling::Ensure(
        shader->interface != nullptr,
        "Shader '{}' has no reflection; use CreateShaderModuleFromSourceUnchecked for legacy GLSL",
        source_path.string());
    vk::UniqueShaderModule module = CreateShaderModule(
        std::string_view(reinterpret_cast<const char*>(shader->spirv->data()), shader->spirv->size() * sizeof(u32)),
        source_path.filename().string());
    return ShaderModule{std::move(module), shader->interface};
}

vk::UniqueShaderModule DeviceContext::CreateShaderModuleFromSourceUnchecked(
    const std::filesystem::path& source_path) const
{
    const auto shader = GetShaderCacheManager().GetOrCompile(source_path);
    return CreateShaderModule(
        std::string_view(reinterpret_cast<const char*>(shader->spirv->data()), shader->spirv->size() * sizeof(u32)),
        source_path.filename().string());
}

void DeviceContext::CreateInstance(const Settings& settings, const Window* presentation_window)
{
    const auto app_info = vk::ApplicationInfo{}
                              .setPApplicationName(settings.app_name.c_str())
                              .setPEngineName("klvk")
                              .setApiVersion(vk::ApiVersion13);

    std::vector<const char*> extensions;
    if (presentation_window)
    {
        extensions = presentation_window->GetRequiredVulkanInstanceExtensions();
    }

    std::vector<const char*> layers;
    const bool validation = settings.enable_validation && HasLayer("VK_LAYER_KHRONOS_validation");
    if (settings.enable_validation && !validation)
    {
        fmt::print("klvk: VK_LAYER_KHRONOS_validation requested but is not available\n");
    }

    auto create_info = vk::InstanceCreateInfo{}.setPApplicationInfo(&app_info);
    const auto messenger_info = MakeDebugMessengerCreateInfo();
    if (validation)
    {
        layers.push_back("VK_LAYER_KHRONOS_validation");
        extensions.push_back(vk::EXTDebugUtilsExtensionName);
        const std::array synchronization_validation{vk::ValidationFeatureEnableEXT::eSynchronizationValidation};
        auto validation_features = vk::ValidationFeaturesEXT{};
        if (settings.enable_synchronization_validation)
        {
            validation_features.setEnabledValidationFeatures(synchronization_validation);
        }
        vk::StructureChain<vk::InstanceCreateInfo, vk::DebugUtilsMessengerCreateInfoEXT, vk::ValidationFeaturesEXT>
            chain{create_info, messenger_info, validation_features};
        chain.get<vk::InstanceCreateInfo>().setPEnabledLayerNames(layers).setPEnabledExtensionNames(extensions);
        instance_ = vk::createInstanceUnique(chain.get<vk::InstanceCreateInfo>());
    }
    else
    {
        create_info.setPEnabledLayerNames(layers).setPEnabledExtensionNames(extensions);
        instance_ = vk::createInstanceUnique(create_info);
    }
    InitializeVulkanDispatcher(instance_.get());

    if (validation)
    {
        debug_messenger_ = instance_->createDebugUtilsMessengerEXTUnique(messenger_info);
    }
}

void DeviceContext::PickPhysicalDevice()
{
    const auto devices = instance_->enumeratePhysicalDevices();
    ErrorHandling::Ensure(!devices.empty(), "No Vulkan devices found");

    int best_score = -1;
    for (vk::PhysicalDevice device : devices)
    {
        const vk::PhysicalDeviceProperties properties = device.getProperties();
        if (properties.apiVersion < vk::ApiVersion13) continue;
        if (presentation_enabled_ && !HasDeviceExtension(device, vk::KHRSwapchainExtensionName)) continue;

        auto features = device.getFeatures2<
            vk::PhysicalDeviceFeatures2,
            vk::PhysicalDeviceVulkan11Features,
            vk::PhysicalDeviceVulkan13Features>();
        if (!features.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters ||
            !features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering ||
            !features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2)
        {
            continue;
        }

        const auto families = device.getQueueFamilyProperties();
        std::optional<u32> graphics_family;
        for (u32 family = 0; family != static_cast<u32>(families.size()); ++family)
        {
            if (!(families[family].queueFlags & vk::QueueFlagBits::eGraphics)) continue;
            if (!presentation_enabled_ || device.getSurfaceSupportKHR(family, surface_.get()))
            {
                graphics_family = family;
                break;
            }
        }
        if (!graphics_family) continue;

        int score = 1;
        if (properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) score += 1000;
        if (properties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu) score += 100;

        if (score > best_score)
        {
            best_score = score;
            physical_device_ = device;
            graphics_queue_family_ = *graphics_family;
        }
    }

    if (presentation_enabled_)
    {
        ErrorHandling::Ensure(
            physical_device_ != nullptr,
            "No Vulkan device with shader draw parameters, 1.3 dynamic rendering, synchronization2, and a "
            "graphics+present queue found");
    }
    else
    {
        ErrorHandling::Ensure(
            physical_device_ != nullptr,
            "No Vulkan device with shader draw parameters, 1.3 dynamic rendering, synchronization2, and a graphics "
            "queue found");
    }

    fmt::print("klvk: using device {}\n", physical_device_.getProperties().deviceName.data());
}

void DeviceContext::CreateDevice()
{
    const std::array priorities{1.f};
    const std::array queue_infos{
        vk::DeviceQueueCreateInfo{}.setQueueFamilyIndex(graphics_queue_family_).setQueuePriorities(priorities)};

    const vk::PhysicalDeviceFeatures supported_features = physical_device_.getFeatures();
    geometry_shader_enabled_ = supported_features.geometryShader == vk::True;
    tessellation_shader_enabled_ = supported_features.tessellationShader == vk::True;

    auto features2 = vk::PhysicalDeviceFeatures2{}.setFeatures(
        vk::PhysicalDeviceFeatures{}
            .setGeometryShader(supported_features.geometryShader)
            .setTessellationShader(supported_features.tessellationShader));
    auto features11 = vk::PhysicalDeviceVulkan11Features{}.setShaderDrawParameters(true);
    auto features13 = vk::PhysicalDeviceVulkan13Features{}.setSynchronization2(true).setDynamicRendering(true);

    std::vector<const char*> extensions;
    if (presentation_enabled_) extensions.push_back(vk::KHRSwapchainExtensionName);
    if (HasDeviceExtension(physical_device_, vk::KHRDynamicRenderingExtensionName))
    {
        extensions.push_back(vk::KHRDynamicRenderingExtensionName);
    }
    if (HasDeviceExtension(physical_device_, vk::KHRExternalMemoryFdExtensionName))
    {
        extensions.push_back(vk::KHRExternalMemoryFdExtensionName);
        external_memory_fd_enabled_ = true;
    }

    auto create_info = vk::DeviceCreateInfo{}.setQueueCreateInfos(queue_infos).setPEnabledExtensionNames(extensions);
    vk::StructureChain<
        vk::DeviceCreateInfo,
        vk::PhysicalDeviceFeatures2,
        vk::PhysicalDeviceVulkan11Features,
        vk::PhysicalDeviceVulkan13Features>
        chain{create_info, features2, features11, features13};

    device_ = physical_device_.createDeviceUnique(chain.get<vk::DeviceCreateInfo>());
    InitializeVulkanDispatcher(device_.get());
    graphics_queue_ = device_->getQueue(graphics_queue_family_, 0);
}

void DeviceContext::CreateAllocator()
{
    const VmaVulkanFunctions functions{
        .vkGetInstanceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr,
    };
    const VmaAllocatorCreateInfo create_info{
        .physicalDevice = static_cast<VkPhysicalDevice>(physical_device_),
        .device = static_cast<VkDevice>(device_.get()),
        .pVulkanFunctions = &functions,
        .instance = static_cast<VkInstance>(instance_.get()),
        .vulkanApiVersion = vk::ApiVersion13,
    };
    VulkanCheck(static_cast<vk::Result>(vmaCreateAllocator(&create_info, &allocator_)));
}

void DeviceContext::WaitIdle() const
{
    GetDevice().waitIdle();
}

void DeviceContext::WaitIdleNoexcept() const noexcept
{
    if (device_) (void)VULKAN_HPP_DEFAULT_DISPATCHER.vkDeviceWaitIdle(static_cast<VkDevice>(GetDevice()));
}

vk::CommandBuffer DeviceContext::BeginOneTimeCommands() const
{
    const auto allocate_info = vk::CommandBufferAllocateInfo{}
                                   .setCommandPool(one_time_pool_.get())
                                   .setLevel(vk::CommandBufferLevel::ePrimary)
                                   .setCommandBufferCount(1);
    const auto command_buffers = GetDevice().allocateCommandBuffers(allocate_info);
    const vk::CommandBuffer command_buffer = command_buffers.front();

    const auto begin_info = vk::CommandBufferBeginInfo{}.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    command_buffer.begin(begin_info);
    return command_buffer;
}

void DeviceContext::EndOneTimeCommands(vk::CommandBuffer command_buffer) const
{
    command_buffer.end();

    const std::array command_buffer_infos{vk::CommandBufferSubmitInfo{}.setCommandBuffer(command_buffer)};
    const std::array submit_infos{vk::SubmitInfo2{}.setCommandBufferInfos(command_buffer_infos)};
    graphics_queue_.submit2(submit_infos);
    graphics_queue_.waitIdle();
    const std::array command_buffers{command_buffer};
    GetDevice().freeCommandBuffers(one_time_pool_.get(), command_buffers);
}

vk::UniqueShaderModule DeviceContext::CreateShaderModule(std::string_view spirv_bytes, std::string_view debug_name)
    const
{
    ErrorHandling::Ensure(
        spirv_bytes.size() % sizeof(u32) == 0 && !spirv_bytes.empty(),
        "Shader '{}': SPIR-V size {} is not a multiple of 4",
        debug_name,
        spirv_bytes.size());

    const auto create_info = vk::ShaderModuleCreateInfo{}
                                 .setCodeSize(spirv_bytes.size())
                                 .setPCode(reinterpret_cast<const u32*>(spirv_bytes.data()));  // NOLINT
    return GetDevice().createShaderModuleUnique(create_info);
}

}  // namespace klvk
