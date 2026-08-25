#include "application_imgui.hpp"

#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>

#include "klvk/error_handling.hpp"
#include "klvk/timing/timer_manager.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/render_target.hpp"
#include "klvk/window.hpp"
#include "platform/glfw/glfw_state.hpp"

namespace klvk
{
namespace
{

constexpr float PointsToPixels(float points, float logical_dots_per_inch)
{
    constexpr float points_per_inch = 72.f;
    return points * logical_dots_per_inch / points_per_inch;
}

VKAPI_ATTR void VKAPI_CALL UnusedPresentationFunction() {}

bool IsPresentationFunction(std::string_view name)
{
    constexpr std::array names{
        "vkCreateSwapchainKHR",
        "vkDestroySurfaceKHR",
        "vkDestroySwapchainKHR",
        "vkGetPhysicalDeviceSurfaceCapabilitiesKHR",
        "vkGetPhysicalDeviceSurfaceFormatsKHR",
        "vkGetPhysicalDeviceSurfacePresentModesKHR",
        "vkGetSwapchainImagesKHR",
    };
    return std::ranges::find(names, name) != names.end();
}

struct VulkanLoaderContext
{
    VkInstance instance;
    VkDevice device;
    bool presentation_enabled;
};

PFN_vkVoidFunction LoadVulkanFunction(const char* name, void* user_data)
{
    const auto& context = *static_cast<const VulkanLoaderContext*>(user_data);
    if (std::strcmp(name, "vkCmdBeginRenderingKHR") == 0)
    {
        return reinterpret_cast<PFN_vkVoidFunction>(VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdBeginRendering);
    }
    if (std::strcmp(name, "vkCmdEndRenderingKHR") == 0)
    {
        return reinterpret_cast<PFN_vkVoidFunction>(VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdEndRendering);
    }
    if (const PFN_vkVoidFunction function = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr(context.instance, name))
    {
        return function;
    }
    const PFN_vkVoidFunction function = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr(context.device, name);
    if (function == nullptr && !context.presentation_enabled && IsPresentationFunction(name))
    {
        return reinterpret_cast<PFN_vkVoidFunction>(UnusedPresentationFunction);
    }
    return function;
}

}  // namespace

void ApplicationImGui::Initialize(
    DeviceContext& device_context,
    const RenderTarget& render_target,
    GlfwState& glfw,
    Window& window,
    bool offscreen,
    edt::Vec2f content_scale,
    edt::Vec2f framebuffer_scale,
    const std::optional<std::filesystem::path>& ini_path,
    const std::filesystem::path& font_path)
{
    ImGui::CreateContext();
    context_created_ = true;
    if (ini_path.has_value())
    {
        ini_filename_ = ini_path->string();
        ImGui::GetIO().IniFilename = ini_filename_.c_str();
    }
    else
    {
        ImGui::GetIO().IniFilename = nullptr;
    }
    ImGui::StyleColorsDark();
    if (!offscreen)
    {
        ErrorHandling::Ensure(glfw.InitializeImGui(window), "Failed to initialize imgui GLFW backend");
        glfw_initialized_ = true;
    }

    const vk::Device device = device_context.GetDevice();
    const std::array pool_sizes{
        vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, 16},
        vk::DescriptorPoolSize{vk::DescriptorType::eSampler, 2},
    };
    const auto pool_info = vk::DescriptorPoolCreateInfo{}
                               .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
                               .setMaxSets(18)
                               .setPoolSizes(pool_sizes);
    descriptor_pool_ = device.createDescriptorPoolUnique(pool_info);

    const std::array<VkFormat, 1> color_formats{static_cast<VkFormat>(render_target.GetFormat())};
    VulkanLoaderContext loader_context{
        .instance = static_cast<VkInstance>(device_context.GetInstance()),
        .device = static_cast<VkDevice>(device),
        .presentation_enabled = device_context.GetSurface() != nullptr,
    };
    ErrorHandling::Ensure(
        ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3, LoadVulkanFunction, &loader_context),
        "Failed to load imgui Vulkan functions");
    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.ApiVersion = VK_API_VERSION_1_3;
    init_info.Instance = loader_context.instance;
    init_info.PhysicalDevice = static_cast<VkPhysicalDevice>(device_context.GetPhysicalDevice());
    init_info.Device = static_cast<VkDevice>(device);
    init_info.QueueFamily = device_context.GetGraphicsQueueFamily();
    init_info.Queue = static_cast<VkQueue>(device_context.GetGraphicsQueue());
    init_info.DescriptorPool = static_cast<VkDescriptorPool>(descriptor_pool_.get());
    init_info.MinImageCount = 2;
    init_info.ImageCount = static_cast<u32>(render_target.GetImageCount());
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.UseDynamicRendering = true;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = static_cast<u32>(color_formats.size()),
        .pColorAttachmentFormats = color_formats.data(),
    };
    ErrorHandling::Ensure(ImGui_ImplVulkan_Init(&init_info), "Failed to initialize imgui vulkan backend");
    vulkan_initialized_ = true;

    ImGui::GetStyle().ScaleAllSizes(content_scale.x() / framebuffer_scale.x());
    ImGuiIO& io = ImGui::GetIO();

    ImFontConfig font_config{};
    constexpr float font_size_points = 10.f;
    constexpr float base_logical_dots_per_inch = 96.f;
    font_config.SizePixels =
        PointsToPixels(font_size_points, base_logical_dots_per_inch * content_scale.y()) / framebuffer_scale.y();
    font_config.RasterizerDensity = framebuffer_scale.y();
    ImFont* font = io.Fonts->AddFontFromFileTTF(font_path.string().c_str(), font_config.SizePixels, &font_config);
    ErrorHandling::Ensure(font != nullptr, "Failed to load ImGui font from {}", font_path.string());
}

void ApplicationImGui::Shutdown(GlfwState& glfw)
{
    if (vulkan_initialized_)
    {
        ImGui_ImplVulkan_Shutdown();
        vulkan_initialized_ = false;
    }
    if (glfw_initialized_)
    {
        glfw.ShutdownImGui();
        glfw_initialized_ = false;
    }
    if (context_created_)
    {
        ImGui::DestroyContext();
        context_created_ = false;
    }
    descriptor_pool_.reset();
}

void ApplicationImGui::PrepareFrame(GlfwState& glfw, bool offscreen, vk::Extent2D extent) const
{
    ImGui_ImplVulkan_NewFrame();
    if (offscreen)
    {
        ImGui::GetIO().DisplaySize = {
            static_cast<float>(extent.width),
            static_cast<float>(extent.height),
        };
    }
    else
    {
        glfw.BeginImGuiFrame();
    }
}

void ApplicationImGui::BeginFrame(std::optional<u64> fixed_step_nanoseconds)
{
    if (fixed_step_nanoseconds.has_value())
    {
        ImGui::GetIO().DeltaTime = TimerDurationToSeconds(TimerDuration{*fixed_step_nanoseconds});
    }
    ImGui::NewFrame();
}

void ApplicationImGui::Render(vk::CommandBuffer command_buffer)
{
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(command_buffer));
}

}  // namespace klvk
