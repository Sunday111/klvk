#include "klvk/application.hpp"

#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <concepts>
#include <limits>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "diagnostics/diagnostic_runner.hpp"
#include "diagnostics/input_recorder.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/events/application_events.hpp"
#include "klvk/events/event_listener_method.hpp"
#include "klvk/events/event_manager.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/platform/os/os.hpp"
#include "klvk/reflection/register_types.hpp"
#include "klvk/signed_integral_aliases.hpp"
#include "klvk/timing/timer_manager.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/offscreen_render_target.hpp"
#include "klvk/vulkan/render_target.hpp"
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

    static constexpr double kNanosecondsPerSecond = 1'000'000'000.0;

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
    std::unique_ptr<RenderTarget> render_target_;
    Swapchain* swapchain_ = nullptr;
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
    bool offscreen_ = false;
    bool exit_requested_ = false;
    u64 completed_frames_ = 0;
    events::EventManager event_manager_;
    std::optional<DiagnosticRunConfig> diagnostic_config_;
    std::unique_ptr<DiagnosticRunner> diagnostic_runner_;
    std::optional<std::filesystem::path> input_record_path_;
    std::unique_ptr<DiagnosticInputRecorder> input_recorder_;

    // The fixed step is exact nanoseconds, so logical time is an integer product
    // rather than an accumulated float and a replayed run lands on precisely the
    // deadlines its recording did.
    [[nodiscard]] std::optional<u64> GetFixedStepNanoseconds() const
    {
        if (!diagnostic_config_.has_value()) return std::nullopt;
        return diagnostic_config_->clock.fixed_step_ns;
    }

    [[nodiscard]] std::optional<double> GetFixedStep() const
    {
        const auto step_ns = GetFixedStepNanoseconds();
        if (!step_ns.has_value()) return std::nullopt;
        return static_cast<double>(*step_ns) / kNanosecondsPerSecond;
    }

    // Exact logical time for the diagnostic path. Under a fixed clock this is an
    // integer product; otherwise it is the monotonic clock truncated to
    // nanoseconds, which is already its native resolution.
    [[nodiscard]] TimerDuration GetElapsedTime() const
    {
        if (const auto step_ns = GetFixedStepNanoseconds())
        {
            ErrorHandling::Ensure(
                completed_frames_ == 0 || *step_ns <= std::numeric_limits<u64>::max() / completed_frames_,
                "Diagnostic logical time overflowed the nanosecond range");
            return TimerDuration{*step_ns * completed_frames_};
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(GetTime() - app_start_time_).count();
        return TimerDuration{elapsed > 0 ? static_cast<u64>(elapsed) : 0};
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

    [[nodiscard]] float GetCurrentFrameStartTime() const
    {
        if (GetFixedStep().has_value()) return GetRelativeTimeSeconds();
        return State::DurationToSeconds(frame_start_time_history_[current_frame_time_index_] - app_start_time_);
    }

    // A fixed clock normally means "render as fast as possible", which is what
    // an offscreen or hidden run wants. A visible one exists to be watched, so
    // hold each frame until the wall clock catches up with logical time.
    [[nodiscard]] bool ShouldPaceToRealTime() const
    {
        return diagnostic_config_.has_value() && diagnostic_config_->presentation == DiagnosticPresentation::Visible &&
               GetFixedStepNanoseconds().has_value();
    }

    void PaceToRealTime() const
    {
        const u64 step_ns = *GetFixedStepNanoseconds();
        if (completed_frames_ != 0 && step_ns > std::numeric_limits<u64>::max() / completed_frames_) return;
        const auto elapsed = std::chrono::nanoseconds{static_cast<i64>(step_ns * completed_frames_)};
        std::this_thread::sleep_until(app_start_time_ + elapsed);
    }

    void AlignWithFramerate()
    {
        if (ShouldPaceToRealTime())
        {
            PaceToRealTime();
            return;
        }
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
        render_finished_.clear();
        if (!swapchain_) return;
        render_finished_.assign(render_target_->GetImageCount(), VK_NULL_HANDLE);
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
            glfw_.WaitEvents();
            framebuffer_size = window_->GetFramebufferSize();
        }

        ErrorHandling::Ensure(swapchain_ != nullptr, "Cannot recreate an offscreen render target");
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
        state_->glfw_.ShutdownImGui();
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
        state_->swapchain_ = nullptr;
        state_->render_target_.reset();
        state_->device_context_.reset();
    }
}

void Application::Initialize()
{
    state_->executable_dir_ = os::GetExecutableDir();

    InitializeReflectionTypes();

    state_->offscreen_ = state_->diagnostic_config_.has_value() &&
                         state_->diagnostic_config_->presentation == DiagnosticPresentation::Offscreen;
    const bool hidden_diagnostic = state_->diagnostic_config_.has_value() &&
                                   state_->diagnostic_config_->presentation == DiagnosticPresentation::Hidden;
    bool realize_hidden_x11 = false;
    if (!state_->offscreen_)
    {
        state_->glfw_.Initialize();
        realize_hidden_x11 = state_->glfw_.ConfigureForVulkan(hidden_diagnostic);
        ErrorHandling::Ensure(state_->glfw_.IsVulkanSupported(), "GLFW reports no Vulkan support");
    }

    {
        u32 window_width = 900;
        u32 window_height = 900;
        if (state_->diagnostic_config_.has_value() && state_->diagnostic_config_->framebuffer_size.has_value())
        {
            window_width = state_->diagnostic_config_->framebuffer_size->x();
            window_height = state_->diagnostic_config_->framebuffer_size->y();
        }
        else if (!state_->offscreen_)
        {
            const edt::Vec2f scale = state_->glfw_.GetPrimaryMonitorContentScale();
            window_width = static_cast<u32>(static_cast<float>(window_width) * scale.x());
            window_height = static_cast<u32>(static_cast<float>(window_height) * scale.y());
        }

        if (state_->offscreen_)
        {
            state_->window_ = Window::CreateOffscreen(*this, window_width, window_height);
        }
        else
        {
            state_->window_ = std::make_unique<Window>(*this, window_width, window_height);
        }
    }
    if (state_->diagnostic_config_.has_value() && state_->diagnostic_config_->framebuffer_size.has_value())
    {
        state_->window_->SetFixedFramebufferSize(*state_->diagnostic_config_->framebuffer_size);
    }
    if (realize_hidden_x11)
    {
        state_->glfw_.ShowWindow(*state_->window_);
        state_->glfw_.PollEvents();
        if (state_->diagnostic_config_->framebuffer_size.has_value())
        {
            state_->window_->SetFramebufferSize(*state_->diagnostic_config_->framebuffer_size);
        }
    }

    state_->device_context_ = std::make_unique<DeviceContext>(state_->offscreen_ ? nullptr : state_->window_.get());
    state_->device_context_->InitializeShaderCache(GetShaderDir());
    if (state_->offscreen_)
    {
        state_->render_target_ = std::make_unique<OffscreenRenderTarget>(
            *state_->device_context_,
            state_->window_->GetFramebufferSize(),
            kFramesInFlight);
    }
    else
    {
        const VkImageUsageFlags diagnostic_usage =
            state_->diagnostic_config_.has_value() && !state_->diagnostic_config_->captures.empty()
                ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                : 0;
        auto swapchain = std::make_unique<Swapchain>(
            *state_->device_context_,
            state_->window_->GetFramebufferSize(),
            diagnostic_usage);
        state_->swapchain_ = swapchain.get();
        state_->render_target_ = std::move(swapchain);
        if (state_->diagnostic_config_.has_value() && state_->diagnostic_config_->framebuffer_size.has_value())
        {
            const auto requested = *state_->diagnostic_config_->framebuffer_size;
            const VkExtent2D actual = state_->render_target_->GetExtent();
            ErrorHandling::Ensure(
                actual.width == requested.x() && actual.height == requested.y(),
                "Vulkan surface extent is {}x{}, but diagnostic framebuffer_size requested {}x{}",
                actual.width,
                actual.height,
                requested.x(),
                requested.y());
        }
    }
    if (realize_hidden_x11) state_->glfw_.HideWindow(*state_->window_);
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
    if (!state_->offscreen_)
    {
        ErrorHandling::Ensure(
            state_->glfw_.InitializeImGui(*state_->window_),
            "Failed to initialize imgui GLFW backend");
        state_->imgui_glfw_initialized_ = true;
    }

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

        const std::array color_formats{state_->render_target_->GetFormat()};
        ImGui_ImplVulkan_InitInfo init_info{};
        init_info.Instance = state_->device_context_->GetInstance();
        init_info.PhysicalDevice = state_->device_context_->GetPhysicalDevice();
        init_info.Device = device;
        init_info.QueueFamily = state_->device_context_->GetGraphicsQueueFamily();
        init_info.Queue = state_->device_context_->GetGraphicsQueue();
        init_info.DescriptorPool = state_->imgui_descriptor_pool_;
        init_info.MinImageCount = 2;
        init_info.ImageCount = static_cast<u32>(state_->render_target_->GetImageCount());
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

    const edt::Vec2f content_scale =
        state_->offscreen_ ? edt::Vec2f{1.f, 1.f} : state_->glfw_.GetPrimaryMonitorContentScale();
    ImGui::GetStyle().ScaleAllSizes(2);
    ImGuiIO& io = ImGui::GetIO();

    ImFontConfig font_config{};
    font_config.SizePixels = 13 * content_scale.x();
    io.Fonts->AddFontDefault(&font_config);

    state_->InitTime();
}

void Application::Run()
{
    RunImpl();
}

void Application::RunImpl()
{
    Initialize();
    // Recording is independent of replaying: the point is to capture an ordinary
    // interactive session, which has no diagnostic configuration at all.
    if (state_->input_record_path_.has_value())
    {
        state_->input_recorder_ =
            std::make_unique<DiagnosticInputRecorder>(*state_->input_record_path_, state_->event_manager_);
    }
    if (state_->diagnostic_config_.has_value())
    {
        if (state_->diagnostic_config_->framebuffer_size.has_value())
        {
            state_->window_->SetFramebufferSize(*state_->diagnostic_config_->framebuffer_size);
            const auto framebuffer_size = state_->window_->GetFramebufferSize();
            const VkExtent2D target_extent = state_->render_target_->GetExtent();
            if (state_->swapchain_ &&
                (target_extent.width != framebuffer_size.x() || target_extent.height != framebuffer_size.y()))
            {
                state_->RecreateSwapchain();
            }
        }
        // A replay that carries its own input must not also receive the real
        // thing: with a visible window, moving the cursor across it would alter
        // the run being reproduced.
        if (!state_->diagnostic_config_->input.empty())
        {
            state_->window_->SetPlatformInputEnabled(false);
        }
        state_->InitTime();
        state_->completed_frames_ = 0;
        state_->diagnostic_runner_ = std::make_unique<DiagnosticRunner>(
            *state_->diagnostic_config_,
            state_->executable_dir_,
            kFramesInFlight,
            state_->event_manager_,
            *state_->window_);
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
    if (state_->input_recorder_)
    {
        constexpr u64 kDefaultRecordedStepNs = 16'666'667;
        const u64 step_ns = state_->GetFixedStepNanoseconds().value_or(kDefaultRecordedStepNs);
        state_->input_recorder_->Write(
            state_->window_->GetFramebufferSize(),
            step_ns,
            state_->diagnostic_config_.has_value() ? state_->diagnostic_config_->application : nlohmann::json::object(),
            state_->executable_dir_);
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
    const DiagnosticCommandLine command_line = ParseDiagnosticCommandLine(arguments);
    if (command_line.config_path.has_value())
    {
        state_->diagnostic_config_ = LoadDiagnosticRunConfig(*command_line.config_path);
    }
    if (command_line.presentation.has_value())
    {
        ErrorHandling::Ensure(
            state_->diagnostic_config_.has_value(),
            "--klvk-presentation overrides a diagnostic configuration and requires --klvk-diagnostics");
        state_->diagnostic_config_->presentation = *command_line.presentation;
    }
    state_->input_record_path_ = command_line.input_record_path;
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
        const VkExtent2D extent = state_->render_target_->GetExtent();
        if (state_->swapchain_ && (framebuffer_size.x() != extent.width || framebuffer_size.y() != extent.height))
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

    // Acquire the next swapchain image. Offscreen images map one-to-one to
    // frame-in-flight slots and therefore need no presentation semaphore.
    if (!state_->swapchain_)
    {
        state_->image_index_ = static_cast<u32>(state_->frame_index_);
    }
    else
    {
        for (;;)
        {
            const AcquireNextImageOutcome outcome = Vulkan::AcquireNextImageKHR(
                device,
                state_->swapchain_->GetHandle(),
                std::numeric_limits<u64>::max(),
                frame.image_available);

            if (outcome.status == AcquireNextImageStatus::Acquired ||
                outcome.status == AcquireNextImageStatus::Suboptimal)
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
            .image = state_->render_target_->GetImage(state_->image_index_),
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
            .image = state_->render_target_->GetDepthImage(state_->image_index_),
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1},
        }};
        const VkDependencyInfo dependency{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = barriers.size(),
            .pImageMemoryBarriers = barriers.data(),
        };
        Vulkan::CmdPipelineBarrier2(frame.command_buffer, dependency);
    }

    const auto extent = state_->render_target_->GetExtent();
    const auto& c = state_->clear_color_;
    const std::array color_attachments{VkRenderingAttachmentInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = state_->render_target_->GetImageView(state_->image_index_),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = state_->auto_clear_ ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = {.float32 = {c.x(), c.y(), c.z(), c.w()}}},
    }};
    // Single object behind pDepthAttachment - no count field, so no array.
    const VkRenderingAttachmentInfo depth_attachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = state_->depth_buffer_enabled_ ? state_->render_target_->GetDepthImageView(state_->image_index_)
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
    if (state_->offscreen_)
    {
        ImGui::GetIO().DisplaySize = {
            static_cast<float>(extent.width),
            static_cast<float>(extent.height),
        };
    }
    else
    {
        state_->glfw_.BeginImGuiFrame();
    }
    if (state_->diagnostic_runner_)
    {
        state_->diagnostic_runner_->AdvanceInput(state_->completed_frames_ + 1, state_->GetElapsedTime());
    }
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
        state_->diagnostic_runner_->Advance(state_->completed_frames_ + 1, state_->GetElapsedTime());
    }
    const bool capture_without_ui = state_->diagnostic_runner_ && state_->diagnostic_runner_->NeedsReadback(false);

    // ImGui's pipeline is color-only. End an application's depth-enabled pass and
    // resume rendering the same color image without a depth attachment for the UI.
    // A capture that excludes UI uses the same split point.
    if (state_->depth_buffer_enabled_ || capture_without_ui)
    {
        Vulkan::CmdEndRendering(frame.command_buffer);
        if (capture_without_ui)
        {
            const bool recorded = state_->diagnostic_runner_->RecordReadback(
                *state_->device_context_,
                frame.command_buffer,
                state_->frame_index_,
                false,
                state_->render_target_->GetImage(state_->image_index_),
                state_->render_target_->GetFormat(),
                state_->render_target_->GetExtent(),
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            ErrorHandling::Ensure(recorded, "A due pre-UI diagnostic capture was not recorded");
        }
        const std::array color_attachments{VkRenderingAttachmentInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = state_->render_target_->GetImageView(state_->image_index_),
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        }};
        const VkRenderingInfo rendering_info{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {.offset = {.x = 0, .y = 0}, .extent = state_->render_target_->GetExtent()},
            .layerCount = 1,
            .colorAttachmentCount = color_attachments.size(),
            .pColorAttachments = color_attachments.data(),
        };
        Vulkan::CmdBeginRendering(frame.command_buffer, rendering_info);
    }

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frame.command_buffer);

    Vulkan::CmdEndRendering(frame.command_buffer);

    const bool captured_with_ui =
        state_->diagnostic_runner_ &&
        state_->diagnostic_runner_->RecordReadback(
            *state_->device_context_,
            frame.command_buffer,
            state_->frame_index_,
            true,
            state_->render_target_->GetImage(state_->image_index_),
            state_->render_target_->GetFormat(),
            state_->render_target_->GetExtent(),
            state_->swapchain_ ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // color attachment -> present when capture did not already perform that transition.
    if (state_->swapchain_ && !captured_with_ui)
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
            .image = state_->render_target_->GetImage(state_->image_index_),
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

    const std::array command_buffer_infos{VkCommandBufferSubmitInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = frame.command_buffer,
    }};
    VkSemaphore render_finished = VK_NULL_HANDLE;
    if (state_->swapchain_)
    {
        render_finished = state_->render_finished_[state_->image_index_];
        const std::array wait_infos{VkSemaphoreSubmitInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = frame.image_available,
            .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
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
    else
    {
        const std::array submit_infos{VkSubmitInfo2{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .commandBufferInfoCount = command_buffer_infos.size(),
            .pCommandBufferInfos = command_buffer_infos.data(),
        }};
        Vulkan::QueueSubmit2(state_->device_context_->GetGraphicsQueue(), submit_infos, frame.in_flight);
    }

    if (state_->swapchain_)
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
        if (status == PresentStatus::OutOfDate || status == PresentStatus::Suboptimal) state_->RecreateSwapchain();
    }

    ++state_->completed_frames_;
    state_->frame_index_ = (state_->frame_index_ + 1) % kFramesInFlight;
    // Events arrive during PollEvents and are first observable by the tick that
    // follows, so they belong to the next frame.
    if (state_->input_recorder_) state_->input_recorder_->BeginFrame(state_->completed_frames_ + 1);
    if (!state_->offscreen_) state_->glfw_.PollEvents();
}

void Application::MainLoop()
{
    while (!WantsToClose())
    {
        state_->RegisterFrameStartTime();

        PreTick();
        [[maybe_unused]] const u64 timer_callback_count =
            state_->timer_manager_.Advance(state_->GetElapsedTime(), state_->completed_frames_ + 1);
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
    return state_->render_target_->GetFormat();
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
