#include "klvk/application.hpp"

#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <concepts>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "diagnostics/diagnostic_runner.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/events/application_events.hpp"
#include "klvk/events/event_listener_method.hpp"
#include "klvk/events/event_manager.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/platform/os/os.hpp"
#include "klvk/reflection/register_types.hpp"
#include "klvk/timing/timer_manager.hpp"
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
    using Clock = std::chrono::steady_clock;
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
    u32 image_index_ = 0;
    bool frame_active_ = false;
    bool depth_buffer_enabled_ = false;
    bool imgui_context_created_ = false;
    bool imgui_glfw_initialized_ = false;
    bool imgui_vulkan_initialized_ = false;
    bool exit_requested_ = false;
    u64 completed_frames_ = 0;
    events::EventManager event_manager_;
    std::optional<DiagnosticRunConfig> diagnostic_config_;
    std::unique_ptr<DiagnosticRunner> diagnostic_runner_;

    [[nodiscard]] std::optional<double> GetFixedStep() const
    {
        if (!diagnostic_config_.has_value()) return std::nullopt;
        return diagnostic_config_->clock.fixed_step_seconds;
    }

    void InitTime()
    {
        app_start_time_ = GetTime();
        std::ranges::fill(frame_start_time_history_, app_start_time_);
    }

    void RegisterFrameStartTime()
    {
        if (const auto step = GetFixedStep())
        {
            last_frame_duration_seconds_ = static_cast<float>(*step);
            framerate_ = static_cast<float>(1.0 / *step);
            return;
        }
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

    [[nodiscard]] float GetRelativeTimeSeconds() const
    {
        if (const auto step = GetFixedStep()) return static_cast<float>(static_cast<double>(completed_frames_) * *step);
        return State::DurationToSeconds(GetTime() - app_start_time_);
    }

    [[nodiscard]] double GetElapsedTimeSeconds() const
    {
        if (const auto step = GetFixedStep()) return static_cast<double>(completed_frames_) * *step;
        return State::DurationToSeconds<double>(GetTime() - app_start_time_);
    }

    [[nodiscard]] float GetCurrentFrameStartTime() const
    {
        if (GetFixedStep().has_value()) return GetRelativeTimeSeconds();
        return State::DurationToSeconds(frame_start_time_history_[current_frame_time_index_] - app_start_time_);
    }

    void AlignWithFramerate()
    {
        if (GetFixedStep().has_value()) return;
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
    u8 current_frame_time_index_ = kFrameTimeHistorySize - 1;
    std::optional<float> target_framerate_;
    TimerManager timer_manager_;

    State()
    {
        [[maybe_unused]] const events::IEventListener* quit_listener = event_manager_.AddEventListener(
            events::EventListenerMethodCallbacks<&State::OnApplicationQuitRequested>::CreatePtr(this));
    }

    void OnApplicationQuitRequested(const events::OnApplicationQuitRequested&) { exit_requested_ = true; }
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
    }
    state_->diagnostic_runner_.reset();
    if (state_->imgui_vulkan_initialized_)
    {
        ImGui_ImplVulkan_Shutdown();
        state_->imgui_vulkan_initialized_ = false;
    }
    if (state_->imgui_glfw_initialized_)
    {
        ImGui_ImplGlfw_Shutdown();
        state_->imgui_glfw_initialized_ = false;
    }
    if (state_->imgui_context_created_)
    {
        ImGui::DestroyContext();
        state_->imgui_context_created_ = false;
    }
    if (state_->device_context_)
    {
        if (state_->imgui_descriptor_pool_)
        {
            Vulkan::DestroyDescriptorPoolNE(state_->device_context_->GetDevice(), state_->imgui_descriptor_pool_);
        }
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
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    const bool hidden_diagnostic = state_->diagnostic_config_.has_value() &&
                                   state_->diagnostic_config_->presentation == DiagnosticPresentation::Hidden;
    const bool realize_hidden_x11 = hidden_diagnostic && glfwGetPlatform() == GLFW_PLATFORM_X11;
    if (hidden_diagnostic)
    {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
        glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        if (realize_hidden_x11)
        {
            // Some Vulkan X11 drivers report a fixed fallback surface extent until
            // the native window has been mapped at least once. Realize it outside
            // the desktop before swapchain creation and hide it immediately after.
            glfwWindowHint(GLFW_POSITION_X, -32000);
            glfwWindowHint(GLFW_POSITION_Y, -32000);
        }
    }
    ErrorHandling::Ensure(glfwVulkanSupported() == GLFW_TRUE, "GLFW reports no Vulkan support");

    {
        u32 window_width = 900;
        u32 window_height = 900;
        if (state_->diagnostic_config_.has_value() && state_->diagnostic_config_->framebuffer_size.has_value())
        {
            window_width = state_->diagnostic_config_->framebuffer_size->x();
            window_height = state_->diagnostic_config_->framebuffer_size->y();
        }
        else if (GLFWmonitor* monitor = glfwGetPrimaryMonitor())
        {
            float x_scale = 0.f, y_scale = 0.f;
            glfwGetMonitorContentScale(monitor, &x_scale, &y_scale);
            window_width = static_cast<u32>(static_cast<float>(window_width) * x_scale);
            window_height = static_cast<u32>(static_cast<float>(window_height) * y_scale);
        }

        state_->window_ = std::make_unique<Window>(*this, window_width, window_height);
    }
    if (state_->diagnostic_config_.has_value() && state_->diagnostic_config_->framebuffer_size.has_value())
    {
        state_->window_->SetFixedFramebufferSize(*state_->diagnostic_config_->framebuffer_size);
    }
    if (realize_hidden_x11)
    {
        glfwShowWindow(state_->window_->GetGlfwWindow());
        glfwPollEvents();
        if (state_->diagnostic_config_->framebuffer_size.has_value())
        {
            state_->window_->SetFramebufferSize(*state_->diagnostic_config_->framebuffer_size);
        }
    }

    state_->device_context_ = std::make_unique<DeviceContext>(state_->window_->GetGlfwWindow());
    state_->device_context_->InitializeShaderCache(GetShaderDir());
    const VkImageUsageFlags diagnostic_usage =
        state_->diagnostic_config_.has_value() && !state_->diagnostic_config_->captures.empty()
            ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT
            : 0;
    state_->swapchain_ =
        std::make_unique<Swapchain>(*state_->device_context_, state_->window_->GetFramebufferSize(), diagnostic_usage);
    if (state_->diagnostic_config_.has_value() && state_->diagnostic_config_->framebuffer_size.has_value())
    {
        const auto requested = *state_->diagnostic_config_->framebuffer_size;
        const VkExtent2D actual = state_->swapchain_->GetExtent();
        ErrorHandling::Ensure(
            actual.width == requested.x() && actual.height == requested.y(),
            "Vulkan surface extent is {}x{}, but diagnostic framebuffer_size requested {}x{}",
            actual.width,
            actual.height,
            requested.x(),
            requested.y());
    }
    if (realize_hidden_x11) glfwHideWindow(state_->window_->GetGlfwWindow());
    state_->CreateFrames();

    ImGui::CreateContext();
    state_->imgui_context_created_ = true;
    if (state_->diagnostic_config_.has_value())
    {
        // Diagnostic output must not depend on UI state persisted by a previous run.
        ImGui::GetIO().IniFilename = nullptr;
    }
    else
    {
        state_->imgui_ini_filename_ = (state_->executable_dir_ / "imgui.ini").string();
        ImGui::GetIO().IniFilename = state_->imgui_ini_filename_.c_str();
    }
    ImGui::StyleColorsDark();
    ErrorHandling::Ensure(
        ImGui_ImplGlfw_InitForVulkan(state_->window_->GetGlfwWindow(), true),
        "Failed to initialize imgui GLFW backend");
    state_->imgui_glfw_initialized_ = true;

    {
        VkDevice device = state_->device_context_->GetDevice();
        const std::array<VkDescriptorPoolSize, 1> pool_sizes{
            VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 16},
        };
        const VkDescriptorPoolCreateInfo pool_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets = 16,
            .poolSizeCount = static_cast<u32>(pool_sizes.size()),
            .pPoolSizes = pool_sizes.data(),
        };
        state_->imgui_descriptor_pool_ = Vulkan::CreateDescriptorPool(device, pool_info);

        const std::array color_formats{state_->swapchain_->GetFormat()};
        ImGui_ImplVulkan_InitInfo init_info{};
        init_info.Instance = state_->device_context_->GetInstance();
        init_info.PhysicalDevice = state_->device_context_->GetPhysicalDevice();
        init_info.Device = device;
        init_info.QueueFamily = state_->device_context_->GetGraphicsQueueFamily();
        init_info.Queue = state_->device_context_->GetGraphicsQueue();
        init_info.DescriptorPool = state_->imgui_descriptor_pool_;
        init_info.MinImageCount = 2;
        init_info.ImageCount = static_cast<u32>(state_->swapchain_->GetImageCount());
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.UseDynamicRendering = true;
        init_info.PipelineRenderingCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
            .colorAttachmentCount = color_formats.size(),
            .pColorAttachmentFormats = color_formats.data(),
        };
        ErrorHandling::Ensure(ImGui_ImplVulkan_Init(&init_info), "Failed to initialize imgui vulkan backend");
        state_->imgui_vulkan_initialized_ = true;
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
    RunImpl();
}

void Application::RunImpl()
{
    Initialize();
    if (state_->diagnostic_config_.has_value())
    {
        if (state_->diagnostic_config_->framebuffer_size.has_value())
        {
            state_->window_->SetFramebufferSize(*state_->diagnostic_config_->framebuffer_size);
            const auto framebuffer_size = state_->window_->GetFramebufferSize();
            const VkExtent2D swapchain_extent = state_->swapchain_->GetExtent();
            if (swapchain_extent.width != framebuffer_size.x() || swapchain_extent.height != framebuffer_size.y())
            {
                state_->RecreateSwapchain();
            }
        }
        state_->InitTime();
        state_->completed_frames_ = 0;
        state_->diagnostic_runner_ = std::make_unique<DiagnosticRunner>(
            *state_->diagnostic_config_,
            state_->executable_dir_,
            kFramesInFlight,
            state_->event_manager_);
    }
    MainLoop();

    // Make the device idle before returning, while the application object - and
    // any Vulkan resources it owns as members - are still alive. This is what
    // lets applications keep pipelines, layouts and descriptor sets as VkObject /
    // DescriptorSets members and rely on their destructors instead of writing an
    // explicit teardown that waits and destroys each handle by hand.
    if (state_->device_context_)
    {
        state_->device_context_->WaitIdle();
        if (state_->diagnostic_runner_)
        {
            state_->diagnostic_runner_->ProcessAllCompleted();
            state_->diagnostic_runner_->EnsureComplete();
        }
    }
}

void Application::RunWithArguments(int argc, char** argv)
{
    ErrorHandling::Ensure(argc >= 0, "Invalid negative argument count");
    std::vector<std::string_view> arguments;
    arguments.reserve(static_cast<size_t>(argc > 0 ? argc - 1 : 0));
    for (int index = 1; index < argc; ++index)
    {
        ErrorHandling::Ensure(argv[index] != nullptr, "Null command-line argument at index {}", index);
        arguments.emplace_back(argv[index]);
    }
    state_->diagnostic_config_ = LoadDiagnosticRunConfigFromArguments(arguments);
    Run();
}

void Application::PreTick()
{
    // Recreate the swapchain when the window size changes. Cannot rely on
    // VK_ERROR_OUT_OF_DATE_KHR alone: on Wayland the compositor silently
    // stretches the presented image instead of invalidating the swapchain.
    {
        if (state_->diagnostic_config_.has_value() && state_->diagnostic_config_->framebuffer_size.has_value())
        {
            state_->window_->SetFramebufferSize(*state_->diagnostic_config_->framebuffer_size);
        }
        const auto framebuffer_size = state_->window_->GetFramebufferSize();
        const VkExtent2D extent = state_->swapchain_->GetExtent();
        if (framebuffer_size.x() != extent.width || framebuffer_size.y() != extent.height)
        {
            state_->RecreateSwapchain();
        }
    }

    auto& frame = state_->CurrentFrame();
    VkDevice device = state_->device_context_->GetDevice();

    const std::array fences{frame.in_flight};
    const WaitStatus wait_status = Vulkan::WaitForFences(device, fences, true, std::numeric_limits<u64>::max());
    ErrorHandling::Ensure(wait_status == WaitStatus::Complete, "Timed out waiting for the frame fence");
    if (state_->diagnostic_runner_) state_->diagnostic_runner_->ProcessCompletedFrame(state_->frame_index_);

    // Acquire the next swapchain image, recreating the swapchain when it is out of date.
    for (;;)
    {
        const AcquireNextImageOutcome outcome = Vulkan::AcquireNextImageKHR(
            device,
            state_->swapchain_->GetHandle(),
            std::numeric_limits<u64>::max(),
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

    Vulkan::ResetFences(device, fences);
    Vulkan::ResetCommandPool(device, frame.command_pool);

    const VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    Vulkan::BeginCommandBuffer(frame.command_buffer, begin_info);
    state_->frame_active_ = true;

    BeforeSwapchainRender(frame.command_buffer);

    // undefined -> color attachment
    {
        const std::array barriers{VkImageMemoryBarrier2{
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
        }};
        const VkDependencyInfo dependency{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = barriers.size(),
            .pImageMemoryBarriers = barriers.data(),
        };
        Vulkan::CmdPipelineBarrier2(frame.command_buffer, dependency);
    }

    if (state_->depth_buffer_enabled_)
    {
        const std::array barriers{VkImageMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            .dstAccessMask =
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = state_->swapchain_->GetDepthImage(state_->image_index_),
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1},
        }};
        const VkDependencyInfo dependency{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = barriers.size(),
            .pImageMemoryBarriers = barriers.data(),
        };
        Vulkan::CmdPipelineBarrier2(frame.command_buffer, dependency);
    }

    const auto extent = state_->swapchain_->GetExtent();
    const auto& c = state_->clear_color_;
    const std::array color_attachments{VkRenderingAttachmentInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = state_->swapchain_->GetImageView(state_->image_index_),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = state_->auto_clear_ ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = {.float32 = {c.x(), c.y(), c.z(), c.w()}}},
    }};
    // Single object behind pDepthAttachment - no count field, so no array.
    const VkRenderingAttachmentInfo depth_attachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = state_->depth_buffer_enabled_ ? state_->swapchain_->GetDepthImageView(state_->image_index_)
                                                   : VK_NULL_HANDLE,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = {.depthStencil = {.depth = 1.f}},
    };
    const VkRenderingInfo rendering_info{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.offset = {.x = 0, .y = 0}, .extent = extent},
        .layerCount = 1,
        .colorAttachmentCount = color_attachments.size(),
        .pColorAttachments = color_attachments.data(),
        .pDepthAttachment = state_->depth_buffer_enabled_ ? &depth_attachment : nullptr,
    };
    Vulkan::CmdBeginRendering(frame.command_buffer, rendering_info);

    // GL-style viewport (y up) so view matrices keep working unchanged after the klgl port.
    const std::array viewports{VkViewport{
        .x = 0.f,
        .y = static_cast<float>(extent.height),
        .width = static_cast<float>(extent.width),
        .height = -static_cast<float>(extent.height),
        .minDepth = 0.f,
        .maxDepth = 1.f,
    }};
    Vulkan::CmdSetViewport(frame.command_buffer, 0, viewports);
    const std::array scissors{VkRect2D{.offset = {.x = 0, .y = 0}, .extent = extent}};
    Vulkan::CmdSetScissor(frame.command_buffer, 0, scissors);

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    if (const auto step = state_->GetFixedStep()) ImGui::GetIO().DeltaTime = static_cast<float>(*step);
    ImGui::NewFrame();
}

void Application::Tick() {}

void Application::BeforeSwapchainRender([[maybe_unused]] VkCommandBuffer command_buffer) {}

void Application::PostTick()
{
    auto& frame = state_->CurrentFrame();
    if (state_->diagnostic_runner_)
    {
        state_->diagnostic_runner_->Advance(state_->completed_frames_ + 1, state_->GetElapsedTimeSeconds());
    }
    const bool capture_without_ui = state_->diagnostic_runner_ && state_->diagnostic_runner_->HasQueuedCaptures(false);

    // ImGui's pipeline is color-only. End an application's depth-enabled pass and
    // resume rendering the same color image without a depth attachment for the UI.
    // A capture that excludes UI uses the same split point.
    if (state_->depth_buffer_enabled_ || capture_without_ui)
    {
        Vulkan::CmdEndRendering(frame.command_buffer);
        if (capture_without_ui)
        {
            const bool recorded = state_->diagnostic_runner_->RecordQueuedCaptures(
                *state_->device_context_,
                frame.command_buffer,
                state_->frame_index_,
                false,
                state_->swapchain_->GetImage(state_->image_index_),
                state_->swapchain_->GetFormat(),
                state_->swapchain_->GetExtent(),
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            ErrorHandling::Ensure(recorded, "A due pre-UI diagnostic capture was not recorded");
        }
        const std::array color_attachments{VkRenderingAttachmentInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = state_->swapchain_->GetImageView(state_->image_index_),
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        }};
        const VkRenderingInfo rendering_info{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {.offset = {.x = 0, .y = 0}, .extent = state_->swapchain_->GetExtent()},
            .layerCount = 1,
            .colorAttachmentCount = color_attachments.size(),
            .pColorAttachments = color_attachments.data(),
        };
        Vulkan::CmdBeginRendering(frame.command_buffer, rendering_info);
    }

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frame.command_buffer);

    Vulkan::CmdEndRendering(frame.command_buffer);

    const bool captured_with_ui = state_->diagnostic_runner_ && state_->diagnostic_runner_->RecordQueuedCaptures(
                                                                    *state_->device_context_,
                                                                    frame.command_buffer,
                                                                    state_->frame_index_,
                                                                    true,
                                                                    state_->swapchain_->GetImage(state_->image_index_),
                                                                    state_->swapchain_->GetFormat(),
                                                                    state_->swapchain_->GetExtent(),
                                                                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    // color attachment -> present when capture did not already perform that transition.
    if (!captured_with_ui)
    {
        const std::array barriers{VkImageMemoryBarrier2{
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
        }};
        const VkDependencyInfo dependency{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = barriers.size(),
            .pImageMemoryBarriers = barriers.data(),
        };
        Vulkan::CmdPipelineBarrier2(frame.command_buffer, dependency);
    }

    Vulkan::EndCommandBuffer(frame.command_buffer);
    state_->frame_active_ = false;

    VkSemaphore render_finished = state_->render_finished_[state_->image_index_];
    {
        const std::array wait_infos{VkSemaphoreSubmitInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = frame.image_available,
            .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        }};
        const std::array command_buffer_infos{VkCommandBufferSubmitInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = frame.command_buffer,
        }};
        const std::array signal_infos{VkSemaphoreSubmitInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = render_finished,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        }};
        const std::array submit_infos{VkSubmitInfo2{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .waitSemaphoreInfoCount = wait_infos.size(),
            .pWaitSemaphoreInfos = wait_infos.data(),
            .commandBufferInfoCount = command_buffer_infos.size(),
            .pCommandBufferInfos = command_buffer_infos.data(),
            .signalSemaphoreInfoCount = signal_infos.size(),
            .pSignalSemaphoreInfos = signal_infos.data(),
        }};
        Vulkan::QueueSubmit2(state_->device_context_->GetGraphicsQueue(), submit_infos, frame.in_flight);
    }

    {
        const std::array wait_semaphores{render_finished};
        const std::array swapchains{state_->swapchain_->GetHandle()};
        // pImageIndices has no count of its own - it is parallel to pSwapchains.
        const std::array image_indices{state_->image_index_};
        const VkPresentInfoKHR present_info{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = wait_semaphores.size(),
            .pWaitSemaphores = wait_semaphores.data(),
            .swapchainCount = swapchains.size(),
            .pSwapchains = swapchains.data(),
            .pImageIndices = image_indices.data(),
        };
        const PresentStatus status = Vulkan::QueuePresentKHR(state_->device_context_->GetGraphicsQueue(), present_info);
        if (status == PresentStatus::OutOfDate || status == PresentStatus::Suboptimal)
        {
            state_->RecreateSwapchain();
        }
    }

    ++state_->completed_frames_;
    state_->frame_index_ = (state_->frame_index_ + 1) % kFramesInFlight;
    glfwPollEvents();
}

void Application::MainLoop()
{
    while (!WantsToClose())
    {
        state_->RegisterFrameStartTime();

        PreTick();
        [[maybe_unused]] const u64 timer_callback_count = state_->timer_manager_.Advance(
            TimerDuration{state_->GetElapsedTimeSeconds()},
            state_->completed_frames_ + 1);
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
    return state_->exit_requested_ || state_->window_->ShouldClose();
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

const nlohmann::json* Application::GetDiagnosticApplicationConfig() const noexcept
{
    return state_->diagnostic_config_.has_value() ? &state_->diagnostic_config_->application : nullptr;
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

void Application::SetDepthBufferEnabled(bool enabled)
{
    state_->depth_buffer_enabled_ = enabled;
}

DeviceContext& Application::GetDeviceContext()
{
    return *state_->device_context_;
}

VkFormat Application::GetSwapchainFormat() const
{
    return state_->swapchain_->GetFormat();
}

VkFormat Application::GetDepthFormat() const
{
    return Swapchain::kDepthFormat;
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

TimerManager& Application::GetTimerManager()
{
    return state_->timer_manager_;
}

}  // namespace klvk
