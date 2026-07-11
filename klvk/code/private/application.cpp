#include "klvk/application.hpp"

#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#include <array>
#include <chrono>
#include <concepts>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "klvk/error_handling.hpp"
#include "klvk/events/event_manager.hpp"
#include "klvk/platform/os/os.hpp"
#include "klvk/reflection/register_types.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/swapchain.hpp"
#include "klvk/vulkan/vulkan_api.hpp"
#include "klvk/window.hpp"
#include "platform/glfw/glfw_state.hpp"

// Vulkan create-info structs are designed for partial designated initialization;
// unlisted fields must be zero.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

namespace klvk
{

struct Application::State
{
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;

    struct FrameInFlight
    {
        VkCommandPool command_pool = VK_NULL_HANDLE;
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        VkSemaphore image_available = VK_NULL_HANDLE;
        VkFence in_flight = VK_NULL_HANDLE;
    };

    GlfwState glfw_;
    std::unique_ptr<Window> window_;
    std::unique_ptr<DeviceContext> device_context_;
    std::unique_ptr<Swapchain> swapchain_;
    std::array<FrameInFlight, kFramesInFlight> frames_{};
    // One per swapchain image: signaled by the last submit that rendered to the image.
    std::vector<VkSemaphore> render_finished_;
    VkDescriptorPool imgui_descriptor_pool_ = VK_NULL_HANDLE;

    std::filesystem::path executable_dir_;
    std::string imgui_ini_filename_;

    edt::Vec4f clear_color_{};
    size_t frame_index_ = 0;
    uint32_t image_index_ = 0;
    bool frame_active_ = false;

    void InitTime()
    {
        app_start_time_ = GetTime();
        std::ranges::fill(frame_start_time_history_, app_start_time_);
    }

    void RegisterFrameStartTime()
    {
        const TimePoint previous_frame_start_time = frame_start_time_history_[current_frame_time_index_];
        current_frame_time_index_ = (current_frame_time_index_ + 1) % frame_start_time_history_.size();
        const TimePoint current_frame_start_time = Clock::now();
        const TimePoint oldest_frame_start_time =
            std::exchange(frame_start_time_history_[current_frame_time_index_], current_frame_start_time);

        framerate_ = static_cast<float>(
            static_cast<double>(frame_start_time_history_.size()) /
            DurationToSeconds<double>(current_frame_start_time - oldest_frame_start_time));

        last_frame_duration_seconds_ = DurationToSeconds<float>(current_frame_start_time - previous_frame_start_time);
    }

    static TimePoint GetTime() { return Clock::now(); }

    template <std::floating_point Result = float, typename Duration>
    static Result DurationToSeconds(Duration&& duration)
    {
        return std::chrono::duration_cast<std::chrono::duration<Result, std::chrono::seconds::period>>(
                   std::forward<Duration>(duration))
            .count();
    }

    float GetRelativeTimeSeconds() const { return State::DurationToSeconds(GetTime() - app_start_time_); }

    float GetCurrentFrameStartTime() const
    {
        return State::DurationToSeconds(frame_start_time_history_[current_frame_time_index_] - app_start_time_);
    }

    void AlignWithFramerate()
    {
        if (target_framerate_.has_value())
        {
            const float frame_start = GetCurrentFrameStartTime();
            constexpr float target_frame_duration = (1 / 60.f) * 0.9995f;
            while (GetRelativeTimeSeconds() - frame_start < target_frame_duration)
            {
            }
        }
    }

    FrameInFlight& CurrentFrame() { return frames_[frame_index_]; }

    void CreateFrames()
    {
        VkDevice device = device_context_->GetDevice();
        for (FrameInFlight& frame : frames_)
        {
            const VkCommandPoolCreateInfo pool_info{
                .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .queueFamilyIndex = device_context_->GetGraphicsQueueFamily(),
            };
            frame.command_pool = Vulkan::CreateCommandPool(device, pool_info);

            const VkCommandBufferAllocateInfo allocate_info{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = frame.command_pool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1,
            };
            frame.command_buffer = Vulkan::AllocateCommandBuffers(device, allocate_info).front();

            const VkSemaphoreCreateInfo semaphore_info{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            frame.image_available = Vulkan::CreateSemaphore(device, semaphore_info);

            const VkFenceCreateInfo fence_info{
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                .flags = VK_FENCE_CREATE_SIGNALED_BIT,
            };
            frame.in_flight = Vulkan::CreateFence(device, fence_info);
        }

        CreateRenderFinishedSemaphores();
    }

    void CreateRenderFinishedSemaphores()
    {
        VkDevice device = device_context_->GetDevice();
        for (VkSemaphore semaphore : render_finished_)
        {
            Vulkan::DestroySemaphoreNE(device, semaphore);
        }
        render_finished_.assign(swapchain_->GetImageCount(), VK_NULL_HANDLE);
        for (VkSemaphore& semaphore : render_finished_)
        {
            const VkSemaphoreCreateInfo semaphore_info{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            semaphore = Vulkan::CreateSemaphore(device, semaphore_info);
        }
    }

    void DestroyFrames()
    {
        VkDevice device = device_context_->GetDevice();
        for (VkSemaphore semaphore : render_finished_)
        {
            Vulkan::DestroySemaphoreNE(device, semaphore);
        }
        render_finished_.clear();
        for (FrameInFlight& frame : frames_)
        {
            if (frame.in_flight) Vulkan::DestroyFenceNE(device, frame.in_flight);
            if (frame.image_available) Vulkan::DestroySemaphoreNE(device, frame.image_available);
            if (frame.command_pool) Vulkan::DestroyCommandPoolNE(device, frame.command_pool);
            frame = {};
        }
    }

    void RecreateSwapchain()
    {
        // Wait until the window is not minimized.
        auto framebuffer_size = window_->GetFramebufferSize();
        while (framebuffer_size.x() == 0 || framebuffer_size.y() == 0)
        {
            glfwWaitEvents();
            framebuffer_size = window_->GetFramebufferSize();
        }

        swapchain_->Recreate(framebuffer_size);
        CreateRenderFinishedSemaphores();
    }

    bool auto_clear_ = true;
    TimePoint app_start_time_{};
    static constexpr size_t kFrameTimeHistorySize = 128;
    std::array<TimePoint, kFrameTimeHistorySize> frame_start_time_history_{};
    float last_frame_duration_seconds_ = 0.f;
    float framerate_ = 0.0f;
    uint8_t current_frame_time_index_ = kFrameTimeHistorySize - 1;
    std::optional<float> target_framerate_;
    events::EventManager event_manager_;
};

Application::Application()
{
    state_ = std::make_unique<State>();
}

Application::~Application()
{
    if (state_->device_context_)
    {
        state_->device_context_->WaitIdle();
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        Vulkan::DestroyDescriptorPoolNE(state_->device_context_->GetDevice(), state_->imgui_descriptor_pool_);
        state_->DestroyFrames();
        state_->swapchain_.reset();
        state_->device_context_.reset();
    }
}

void Application::Initialize()
{
    state_->executable_dir_ = os::GetExecutableDir();

    InitializeReflectionTypes();

    state_->glfw_.Initialize();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    ErrorHandling::Ensure(glfwVulkanSupported() == GLFW_TRUE, "GLFW reports no Vulkan support");

    {
        uint32_t window_width = 900;
        uint32_t window_height = 900;
        if (GLFWmonitor* monitor = glfwGetPrimaryMonitor())
        {
            float x_scale = 0.f, y_scale = 0.f;
            glfwGetMonitorContentScale(monitor, &x_scale, &y_scale);
            window_width = static_cast<uint32_t>(static_cast<float>(window_width) * x_scale);
            window_height = static_cast<uint32_t>(static_cast<float>(window_height) * y_scale);
        }

        state_->window_ = std::make_unique<Window>(*this, window_width, window_height);
    }

    state_->device_context_ = std::make_unique<DeviceContext>(state_->window_->GetGlfwWindow());
    state_->swapchain_ = std::make_unique<Swapchain>(*state_->device_context_, state_->window_->GetFramebufferSize());
    state_->CreateFrames();

    ImGui::CreateContext();
    state_->imgui_ini_filename_ = (state_->executable_dir_ / "imgui.ini").string();
    ImGui::GetIO().IniFilename = state_->imgui_ini_filename_.c_str();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForVulkan(state_->window_->GetGlfwWindow(), true);

    {
        VkDevice device = state_->device_context_->GetDevice();
        const std::array<VkDescriptorPoolSize, 1> pool_sizes{
            VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 16},
        };
        const VkDescriptorPoolCreateInfo pool_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets = 16,
            .poolSizeCount = static_cast<uint32_t>(pool_sizes.size()),
            .pPoolSizes = pool_sizes.data(),
        };
        state_->imgui_descriptor_pool_ = Vulkan::CreateDescriptorPool(device, pool_info);

        const VkFormat color_format = state_->swapchain_->GetFormat();
        ImGui_ImplVulkan_InitInfo init_info{};
        init_info.Instance = state_->device_context_->GetInstance();
        init_info.PhysicalDevice = state_->device_context_->GetPhysicalDevice();
        init_info.Device = device;
        init_info.QueueFamily = state_->device_context_->GetGraphicsQueueFamily();
        init_info.Queue = state_->device_context_->GetGraphicsQueue();
        init_info.DescriptorPool = state_->imgui_descriptor_pool_;
        init_info.MinImageCount = 2;
        init_info.ImageCount = static_cast<uint32_t>(state_->swapchain_->GetImageCount());
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.UseDynamicRendering = true;
        init_info.PipelineRenderingCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &color_format,
        };
        ErrorHandling::Ensure(ImGui_ImplVulkan_Init(&init_info), "Failed to initialize imgui vulkan backend");
    }

    if (GLFWmonitor* monitor = glfwGetPrimaryMonitor())
    {
        float xscale = 0, yscale = 0;
        glfwGetMonitorContentScale(monitor, &xscale, &yscale);

        ImGui::GetStyle().ScaleAllSizes(2);
        ImGuiIO& io = ImGui::GetIO();

        ImFontConfig font_config{};
        font_config.SizePixels = 13 * xscale;
        io.Fonts->AddFontDefault(&font_config);
    }

    state_->InitTime();
}

void Application::Run()
{
    Initialize();
    MainLoop();
}

void Application::PreTick()
{
    // Recreate the swapchain when the window size changes. Cannot rely on
    // VK_ERROR_OUT_OF_DATE_KHR alone: on Wayland the compositor silently
    // stretches the presented image instead of invalidating the swapchain.
    {
        const auto framebuffer_size = state_->window_->GetFramebufferSize();
        const VkExtent2D extent = state_->swapchain_->GetExtent();
        if (framebuffer_size.x() != extent.width || framebuffer_size.y() != extent.height)
        {
            state_->RecreateSwapchain();
        }
    }

    auto& frame = state_->CurrentFrame();
    VkDevice device = state_->device_context_->GetDevice();

    const WaitStatus wait_status =
        Vulkan::WaitForFences(device, std::span{&frame.in_flight, 1}, true, std::numeric_limits<uint64_t>::max());
    ErrorHandling::Ensure(wait_status == WaitStatus::Complete, "Timed out waiting for the frame fence");

    // Acquire the next swapchain image, recreating the swapchain when it is out of date.
    for (;;)
    {
        const AcquireNextImageOutcome outcome = Vulkan::AcquireNextImageKHR(
            device,
            state_->swapchain_->GetHandle(),
            std::numeric_limits<uint64_t>::max(),
            frame.image_available);

        if (outcome.status == AcquireNextImageStatus::Acquired || outcome.status == AcquireNextImageStatus::Suboptimal)
        {
            state_->image_index_ = *outcome.image_index;
            break;
        }
        if (outcome.status == AcquireNextImageStatus::OutOfDate)
        {
            state_->RecreateSwapchain();
            continue;
        }
        ErrorHandling::Ensure(
            outcome.status != AcquireNextImageStatus::NotReady,
            "No swapchain image was ready despite an infinite acquisition timeout");
        ErrorHandling::Ensure(
            outcome.status != AcquireNextImageStatus::Timeout,
            "Timed out acquiring a swapchain image despite an infinite timeout");
    }

    Vulkan::ResetFences(device, std::span{&frame.in_flight, 1});
    Vulkan::ResetCommandPool(device, frame.command_pool);

    const VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    Vulkan::BeginCommandBuffer(frame.command_buffer, begin_info);
    state_->frame_active_ = true;

    // undefined -> color attachment
    {
        const VkImageMemoryBarrier2 barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = state_->swapchain_->GetImage(state_->image_index_),
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
        };
        const VkDependencyInfo dependency{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier,
        };
        Vulkan::CmdPipelineBarrier2(frame.command_buffer, dependency);
    }

    const auto extent = state_->swapchain_->GetExtent();
    const auto& c = state_->clear_color_;
    const VkRenderingAttachmentInfo color_attachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = state_->swapchain_->GetImageView(state_->image_index_),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = state_->auto_clear_ ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = {.float32 = {c.x(), c.y(), c.z(), c.w()}}},
    };
    const VkRenderingInfo rendering_info{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.offset = {0, 0}, .extent = extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment,
    };
    Vulkan::CmdBeginRendering(frame.command_buffer, rendering_info);

    // GL-style viewport (y up) so view matrices keep working unchanged after the klgl port.
    const VkViewport viewport{
        .x = 0.f,
        .y = static_cast<float>(extent.height),
        .width = static_cast<float>(extent.width),
        .height = -static_cast<float>(extent.height),
        .minDepth = 0.f,
        .maxDepth = 1.f,
    };
    Vulkan::CmdSetViewport(frame.command_buffer, 0, std::span{&viewport, 1});
    const VkRect2D scissor{.offset = {0, 0}, .extent = extent};
    Vulkan::CmdSetScissor(frame.command_buffer, 0, std::span{&scissor, 1});

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void Application::Tick() {}

void Application::PostTick()
{
    auto& frame = state_->CurrentFrame();

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frame.command_buffer);

    Vulkan::CmdEndRendering(frame.command_buffer);

    // color attachment -> present
    {
        const VkImageMemoryBarrier2 barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
            .dstAccessMask = VK_ACCESS_2_NONE,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = state_->swapchain_->GetImage(state_->image_index_),
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
        };
        const VkDependencyInfo dependency{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier,
        };
        Vulkan::CmdPipelineBarrier2(frame.command_buffer, dependency);
    }

    Vulkan::EndCommandBuffer(frame.command_buffer);
    state_->frame_active_ = false;

    VkSemaphore render_finished = state_->render_finished_[state_->image_index_];
    {
        const VkSemaphoreSubmitInfo wait_info{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = frame.image_available,
            .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        };
        const VkCommandBufferSubmitInfo command_buffer_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = frame.command_buffer,
        };
        const VkSemaphoreSubmitInfo signal_info{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = render_finished,
            .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        };
        const VkSubmitInfo2 submit_info{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .waitSemaphoreInfoCount = 1,
            .pWaitSemaphoreInfos = &wait_info,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &command_buffer_info,
            .signalSemaphoreInfoCount = 1,
            .pSignalSemaphoreInfos = &signal_info,
        };
        Vulkan::QueueSubmit2(state_->device_context_->GetGraphicsQueue(), std::span{&submit_info, 1}, frame.in_flight);
    }

    {
        VkSwapchainKHR swapchain = state_->swapchain_->GetHandle();
        const VkPresentInfoKHR present_info{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &render_finished,
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &state_->image_index_,
        };
        const PresentStatus status = Vulkan::QueuePresentKHR(state_->device_context_->GetGraphicsQueue(), present_info);
        if (status == PresentStatus::OutOfDate || status == PresentStatus::Suboptimal)
        {
            state_->RecreateSwapchain();
        }
    }

    state_->frame_index_ = (state_->frame_index_ + 1) % kFramesInFlight;
    glfwPollEvents();
}

void Application::MainLoop()
{
    while (!WantsToClose())
    {
        state_->RegisterFrameStartTime();

        PreTick();
        Tick();
        PostTick();
        state_->AlignWithFramerate();
    }
}

void Application::InitializeReflectionTypes()
{
    RegisterReflectionTypes();
}

bool Application::WantsToClose() const
{
    return state_->window_->ShouldClose();
}

Window& Application::GetWindow()
{
    return *state_->window_;
}

const Window& Application::GetWindow() const
{
    return *state_->window_;
}

const std::filesystem::path& Application::GetExecutableDir() const
{
    return state_->executable_dir_;
}

std::filesystem::path Application::GetContentDir() const
{
    return GetExecutableDir() / "content";
}

std::filesystem::path Application::GetShaderDir() const
{
    return GetContentDir() / "shaders";
}

float Application::GetTimeSeconds() const
{
    return state_->GetRelativeTimeSeconds();
}

float Application::GetCurrentFrameStartTime() const
{
    return state_->GetCurrentFrameStartTime();
}

float Application::GetFramerate() const
{
    return state_->framerate_;
}

float Application::GetLastFrameDurationSeconds() const
{
    return state_->last_frame_duration_seconds_;
}

void Application::SetTargetFramerate(std::optional<float> framerate)
{
    state_->target_framerate_ = framerate;
}

void Application::SetClearColor(const edt::Vec4f& color)
{
    state_->clear_color_ = color;
}

DeviceContext& Application::GetDeviceContext()
{
    return *state_->device_context_;
}

VkFormat Application::GetSwapchainFormat() const
{
    return state_->swapchain_->GetFormat();
}

VkCommandBuffer Application::GetCurrentCommandBuffer() const
{
    ErrorHandling::Ensure(state_->frame_active_, "No frame is being recorded (must be between PreTick and PostTick)");
    return state_->frames_[state_->frame_index_].command_buffer;
}

size_t Application::GetFrameInFlightIndex() const
{
    return state_->frame_index_;
}

events::EventManager& Application::GetEventManager()
{
    return state_->event_manager_;
}

}  // namespace klvk
