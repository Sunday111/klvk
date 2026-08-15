#include <fmt/core.h>

#include <expected>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "klvk/application.hpp"
#include "klvk/camera/camera_2d.hpp"
#include "klvk/camera/camera_3d.hpp"
#include "klvk/camera/viewport.hpp"
#include "klvk/diagnostics/diagnostic_run_config.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/events/application_events.hpp"
#include "klvk/events/detail.hpp"
#include "klvk/events/event_listener.hpp"
#include "klvk/events/event_listener_interface.hpp"
#include "klvk/events/event_listener_method.hpp"
#include "klvk/events/event_manager.hpp"
#include "klvk/events/keyboard_events.hpp"
#include "klvk/events/mouse_events.hpp"
#include "klvk/events/window_events.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/float_aliases.hpp"
#include "klvk/image/image_decoder.hpp"
#include "klvk/input.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/macro/ensure_enum_size.hpp"
#include "klvk/mesh/procedural_mesh_generator.hpp"
#include "klvk/platform/file_dialog.hpp"
#include "klvk/platform/os/os.hpp"
#include "klvk/reflection/matrix_reflect.hpp"
#include "klvk/reflection/register_types.hpp"
#include "klvk/rendering/curve_renderer_2d.hpp"
#include "klvk/rendering/instanced_sprite_renderer_2d.hpp"
#include "klvk/shader/define_handle.hpp"
#include "klvk/shader/shader.hpp"
#include "klvk/shader/shader_cache_manager.hpp"
#include "klvk/shader/shader_interface.hpp"
#include "klvk/shader/shader_module.hpp"
#include "klvk/shader/shader_stages.hpp"
#include "klvk/template/constexpr_string_hash.hpp"
#include "klvk/template/tagged_id_hash.hpp"
#include "klvk/text/font_face.hpp"
#include "klvk/text/glyph_atlas.hpp"
#include "klvk/texture/procedural_texture_generator.hpp"
#include "klvk/timing/timer_manager.hpp"
#include "klvk/ui/imgui_enum_combo.hpp"
#include "klvk/ui/imgui_helpers.hpp"
#include "klvk/ui/imgui_texture_viewer.hpp"
#include "klvk/ui/imgui_value_combo.hpp"
#include "klvk/ui/registered_imgui_texture.hpp"
#include "klvk/ui/simple_imgui_combo.hpp"
#include "klvk/ui/simple_type_widget.hpp"
#include "klvk/ui/transform_widget.hpp"
#include "klvk/ui/type_id_widget_minimal.hpp"
#include "klvk/vulkan/depth_stencil_format.hpp"
#include "klvk/vulkan/descriptor_sets.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/gpu_buffer.hpp"
#include "klvk/vulkan/graphics_pipeline_builder.hpp"
#include "klvk/vulkan/offscreen_render_target.hpp"
#include "klvk/vulkan/pipeline_layout.hpp"
#include "klvk/vulkan/render_target.hpp"
#include "klvk/vulkan/swapchain.hpp"
#include "klvk/vulkan/texture.hpp"
#include "klvk/vulkan/vulkan.hpp"
#include "klvk/vulkan/vulkan_common.hpp"
#include "klvk/window.hpp"

namespace
{

using SemaphoreResult = decltype(std::declval<vk::Device>().createSemaphore(vk::SemaphoreCreateInfo{}));
using UniqueSemaphoreResult = decltype(std::declval<vk::Device>().createSemaphoreUnique(vk::SemaphoreCreateInfo{}));
using UniquePipelineResult = decltype(std::declval<vk::Device>().createGraphicsPipelineUnique(
    vk::PipelineCache{},
    vk::GraphicsPipelineCreateInfo{}));
using WaitIdleResult = decltype(std::declval<vk::Device>().waitIdle());

static_assert(std::same_as<SemaphoreResult, std::expected<vk::Semaphore, vk::Result>>);
static_assert(std::same_as<UniqueSemaphoreResult, std::expected<vk::UniqueSemaphore, vk::Result>>);
static_assert(std::same_as<UniquePipelineResult, vk::ResultValue<vk::UniquePipeline>>);
static_assert(std::same_as<WaitIdleResult, std::expected<void, vk::Result>>);
static_assert(std::same_as<decltype(std::declval<klvk::GraphicsPipelineBuilder&>().Build()), vk::UniquePipeline>);
static_assert(!std::copy_constructible<vk::UniqueSemaphore>);
static_assert(!std::is_copy_assignable_v<vk::UniqueSemaphore>);
static_assert(std::move_constructible<vk::UniqueSemaphore>);
static_assert(std::is_move_assignable_v<vk::UniqueSemaphore>);
static_assert(!std::convertible_to<vk::UniqueSemaphore, vk::Semaphore>);
static_assert(std::same_as<std::remove_cvref_t<decltype(std::declval<vk::UniqueSemaphore>().get())>, vk::Semaphore>);

void Ensure(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template <typename Function>
klvk::VulkanError CaptureVulkanError(Function&& function)
{
    try
    {
        function();
    }
    catch (const klvk::VulkanError& error)
    {
        return error;
    }
    throw std::runtime_error("Expected VulkanError");
}

void TestResults()
{
    klvk::VulkanCheck(vk::Result::eSuccess, "vkSuccess");
    Ensure(klvk::VulkanValue(std::expected<int, vk::Result>{42}, "vkValue") == 42, "Value was not returned");
    klvk::VulkanValue(std::expected<void, vk::Result>{}, "vkVoid");

    const klvk::VulkanError check_error =
        CaptureVulkanError([] { klvk::VulkanCheck(vk::Result::eErrorDeviceLost, "vkCheckFailure"); });
    Ensure(check_error.GetResult() == vk::Result::eErrorDeviceLost, "VulkanCheck lost the result");
    Ensure(check_error.GetContext() == "vkCheckFailure", "VulkanCheck lost the operation context");
    Ensure(!check_error.trace().frames.empty(), "VulkanCheck did not capture a stack trace");
    Ensure(std::string{check_error.what()}.contains("ErrorDeviceLost"), "VulkanCheck did not format the result");

    const klvk::VulkanError value_error = CaptureVulkanError(
        []
        {
            (void)klvk::VulkanValue(
                std::expected<int, vk::Result>{std::unexpected(vk::Result::eErrorOutOfHostMemory)},
                "vkValueFailure");
        });
    Ensure(value_error.GetResult() == vk::Result::eErrorOutOfHostMemory, "VulkanValue lost the result");
    Ensure(value_error.GetContext() == "vkValueFailure", "VulkanValue lost the operation context");

    const klvk::VulkanError void_error = CaptureVulkanError(
        []
        {
            klvk::VulkanValue(std::expected<void, vk::Result>{std::unexpected(vk::Result::eTimeout)}, "vkVoidFailure");
        });
    Ensure(void_error.GetResult() == vk::Result::eTimeout, "Void VulkanValue lost the result");
}

void TestTypedVulkan()
{
    constexpr vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
    static_assert(static_cast<bool>(usage & vk::ImageUsageFlagBits::eSampled));

    const vk::ImageCreateInfo image_info = vk::ImageCreateInfo{}
                                               .setImageType(vk::ImageType::e2D)
                                               .setFormat(vk::Format::eR8G8B8A8Unorm)
                                               .setExtent(vk::Extent3D{64, 32, 1})
                                               .setMipLevels(1)
                                               .setArrayLayers(1)
                                               .setSamples(vk::SampleCountFlagBits::e1)
                                               .setUsage(usage);
    Ensure(image_info.extent == vk::Extent3D{64, 32, 1}, "Typed structure construction failed");

    vk::StructureChain<vk::DeviceCreateInfo, vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features> chain;
    chain.get<vk::PhysicalDeviceVulkan13Features>().setDynamicRendering(true).setSynchronization2(true);
    Ensure(chain.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering, "StructureChain feature was not set");
    Ensure(chain.get<vk::DeviceCreateInfo>().pNext != nullptr, "StructureChain was not linked");
}

}  // namespace

int main()
{
    try
    {
        TestResults();
        TestTypedVulkan();
        fmt::println("Vulkan-Hpp integration tests passed");
        return 0;
    }
    catch (const std::exception& exception)
    {
        fmt::println(stderr, "{}", exception.what());
        return 1;
    }
}
