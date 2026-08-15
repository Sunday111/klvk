#include "klvk/application.hpp"

#include <fmt/core.h>

#include <array>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include "application_frame_clock.hpp"
#include "application_imgui.hpp"
#include "diagnostics/diagnostic_runner.hpp"
#include "diagnostics/input_recorder.hpp"
#include "edt/functional/on_scope_leave.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/events/application_events.hpp"
#include "klvk/events/event_listener_method.hpp"
#include "klvk/events/event_manager.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/platform/os/os.hpp"
#include "klvk/reflection/register_types.hpp"
#include "klvk/timing/timer_manager.hpp"
#include "klvk/vulkan/depth_stencil_format.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/offscreen_render_target.hpp"
#include "klvk/vulkan/render_target.hpp"
#include "klvk/vulkan/swapchain.hpp"
#include "klvk/vulkan/vulkan_common.hpp"
#include "klvk/window.hpp"
#include "platform/glfw/glfw_state.hpp"

namespace klvk
{

struct Application::State
{
    struct FrameInFlight
    {
        vk::UniqueCommandPool command_pool;
        vk::CommandBuffer command_buffer = nullptr;
        vk::UniqueSemaphore image_available;
        vk::UniqueFence in_flight;
    };

    GlfwState glfw_;
    std::unique_ptr<Window> window_;
    std::unique_ptr<DeviceContext> device_context_;
    std::unique_ptr<RenderTarget> render_target_;
    Swapchain* swapchain_ = nullptr;
    std::array<FrameInFlight, kFramesInFlight> frames_{};
    // One per swapchain image: signaled by the last submit that rendered to the image.
    std::vector<vk::UniqueSemaphore> render_finished_;

    std::filesystem::path executable_dir_;

    edt::Vec4f clear_color_{};
    size_t frame_index_ = 0;
    u32 image_index_ = 0;
    bool frame_active_ = false;
    bool depth_buffer_enabled_ = false;
    bool stencil_buffer_enabled_ = false;
    bool offscreen_ = false;
    bool exit_requested_ = false;
    SwapchainPresentMode present_mode_ = SwapchainPresentMode::PreferLowLatency;
    u64 completed_frames_ = 0;
    events::EventManager event_manager_;
    events::EventSubscription quit_subscription_;
    std::optional<DiagnosticRunConfig> diagnostic_config_;
    std::unique_ptr<DiagnosticRunner> diagnostic_runner_;
    std::optional<std::filesystem::path> input_record_path_;
    std::optional<std::filesystem::path> write_checkpoints_path_;
    std::unique_ptr<DiagnosticInputRecorder> input_recorder_;

    // Depth and stencil share one image, so either one being enabled binds it.
    [[nodiscard]] bool DepthStencilAttachmentEnabled() const
    {
        return depth_buffer_enabled_ || stencil_buffer_enabled_;
    }

    // The fixed step is exact nanoseconds, so logical time is an integer product
    // rather than an accumulated float and a replayed run lands on precisely the
    // deadlines its recording did.
    [[nodiscard]] std::optional<u64> GetFixedStepNanoseconds() const
    {
        if (!diagnostic_config_.has_value()) return std::nullopt;
        return diagnostic_config_->clock.fixed_step_ns;
    }

    [[nodiscard]] TimerDuration GetElapsedTime() const { return frame_clock_.GetElapsedTime(completed_frames_); }

    void InitTime() { frame_clock_.Initialize(GetFixedStepNanoseconds()); }

    void RegisterFrameStartTime() { frame_clock_.RegisterFrameStart(); }

    [[nodiscard]] float GetRelativeTimeSeconds() const
    {
        return frame_clock_.GetRelativeTimeSeconds(completed_frames_);
    }

    [[nodiscard]] float GetCurrentFrameStartTime() const
    {
        return frame_clock_.GetCurrentFrameStartTime(completed_frames_);
    }

    // A fixed clock normally means "render as fast as possible", which is what
    // an offscreen or hidden run wants. A visible one exists to be watched, so
    // hold each frame until the wall clock catches up with logical time.
    [[nodiscard]] bool ShouldPaceToRealTime() const
    {
        return diagnostic_config_.has_value() && diagnostic_config_->presentation == DiagnosticPresentation::Visible &&
               GetFixedStepNanoseconds().has_value();
    }

    void AlignWithFramerate() { frame_clock_.AlignWithFramerate(ShouldPaceToRealTime(), completed_frames_); }

    FrameInFlight& CurrentFrame() { return frames_[frame_index_]; }

    void CreateFrames()
    {
        vk::Device device = device_context_->GetDevice();
        for (FrameInFlight& frame : frames_)
        {
            const auto pool_info =
                vk::CommandPoolCreateInfo{}.setQueueFamilyIndex(device_context_->GetGraphicsQueueFamily());
            frame.command_pool = device.createCommandPoolUnique(pool_info);

            const auto allocate_info = vk::CommandBufferAllocateInfo{}
                                           .setCommandPool(frame.command_pool.get())
                                           .setLevel(vk::CommandBufferLevel::ePrimary)
                                           .setCommandBufferCount(1);
            frame.command_buffer = device.allocateCommandBuffers(allocate_info).front();

            frame.image_available = device.createSemaphoreUnique(vk::SemaphoreCreateInfo{});

            const auto fence_info = vk::FenceCreateInfo{}.setFlags(vk::FenceCreateFlagBits::eSignaled);
            frame.in_flight = device.createFenceUnique(fence_info);
        }

        CreateRenderFinishedSemaphores();
    }

    void CreateRenderFinishedSemaphores()
    {
        render_finished_.clear();
        if (!swapchain_) return;
        render_finished_.reserve(render_target_->GetImageCount());
        vk::Device device = device_context_->GetDevice();
        for (size_t index = 0; index != render_target_->GetImageCount(); ++index)
        {
            render_finished_.push_back(device.createSemaphoreUnique(vk::SemaphoreCreateInfo{}));
        }
    }

    void DestroyFrames()
    {
        render_finished_.clear();
        for (FrameInFlight& frame : frames_)
        {
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
    ApplicationFrameClock frame_clock_;
    ApplicationImGui imgui_;
    TimerManager timer_manager_;

    State()
    {
        quit_subscription_ = event_manager_.AddEventListener(
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
    state_->imgui_.Shutdown(state_->glfw_);
    if (state_->device_context_)
    {
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
        const vk::ImageUsageFlags diagnostic_usage =
            state_->diagnostic_config_.has_value() && !state_->diagnostic_config_->captures.empty()
                ? vk::ImageUsageFlagBits::eTransferSrc
                : vk::ImageUsageFlags{};
        auto swapchain = std::make_unique<Swapchain>(
            *state_->device_context_,
            state_->window_->GetFramebufferSize(),
            diagnostic_usage,
            state_->present_mode_);
        state_->swapchain_ = swapchain.get();
        state_->render_target_ = std::move(swapchain);
        if (state_->diagnostic_config_.has_value() && state_->diagnostic_config_->framebuffer_size.has_value())
        {
            const auto requested = *state_->diagnostic_config_->framebuffer_size;
            const vk::Extent2D actual = state_->render_target_->GetExtent();
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

    const std::optional<std::filesystem::path> imgui_ini_path =
        state_->diagnostic_config_.has_value() ? std::nullopt : std::optional{state_->executable_dir_ / "imgui.ini"};
    const auto font_path = GetContentDir() / "fonts" / "DejaVuSansMono.ttf";
    const edt::Vec2f content_scale =
        state_->offscreen_ ? edt::Vec2f{1.f, 1.f} : state_->glfw_.GetPrimaryMonitorContentScale();
    const edt::Vec2f framebuffer_scale =
        state_->offscreen_ ? edt::Vec2f{1.f, 1.f} : state_->window_->GetFramebufferScale();
    state_->imgui_.Initialize(
        *state_->device_context_,
        *state_->render_target_,
        state_->glfw_,
        *state_->window_,
        state_->offscreen_,
        content_scale.x(),
        content_scale.x() / framebuffer_scale.x(),
        imgui_ini_path,
        font_path);

    state_->InitTime();
}

void Application::Run()
{
    RunImpl();
}

void Application::RunImpl()
{
    bool device_needs_emergency_wait = true;
    auto wait_for_device_on_failure = edt::OnScopeLeave(
        [this, &device_needs_emergency_wait]
        {
            if (device_needs_emergency_wait && state_->device_context_)
            {
                state_->device_context_->WaitIdleNoexcept();
            }
        });
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
            const vk::Extent2D target_extent = state_->render_target_->GetExtent();
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
            kFramesInFlight,
            state_->event_manager_,
            *state_->window_);
    }
    MainLoop();

    // Make the device idle before returning, while the application object - and
    // any Vulkan resources it owns as members - are still alive. This is what
    // lets applications keep pipelines, layouts and descriptor sets as owning members
    // and rely on their destructors instead of writing an
    // explicit teardown that waits and destroys each handle by hand.
    if (state_->device_context_)
    {
        state_->device_context_->WaitIdle();
        device_needs_emergency_wait = false;
        if (state_->diagnostic_runner_)
        {
            state_->diagnostic_runner_->ProcessAllCompleted();
            if (state_->write_checkpoints_path_.has_value())
            {
                DiagnosticRunConfig blessed = *state_->diagnostic_config_;
                blessed.checkpoints->expected = state_->diagnostic_runner_->GetCheckpoints();
                Filesystem::WriteFile(
                    *state_->write_checkpoints_path_,
                    DiagnosticRunConfigToJson(blessed).dump(2) + "\n");
                fmt::println(
                    "klvk: wrote {} checkpoint hash(es) to {}",
                    blessed.checkpoints->expected.size(),
                    state_->write_checkpoints_path_->string());
            }
            state_->diagnostic_runner_->EnsureComplete();
        }
    }
    else
    {
        device_needs_emergency_wait = false;
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
        state_->diagnostic_config_ = LoadDiagnosticRunConfig(*command_line.config_path, os::GetExecutableDir());
    }
    if (command_line.presentation.has_value())
    {
        ErrorHandling::Ensure(
            state_->diagnostic_config_.has_value(),
            "--klvk-presentation overrides a diagnostic configuration and requires --klvk-diagnostics");
        state_->diagnostic_config_->presentation = *command_line.presentation;
    }
    state_->input_record_path_ = command_line.input_record_path;
    if (command_line.write_checkpoints_path.has_value())
    {
        ErrorHandling::Ensure(
            state_->diagnostic_config_.has_value() && state_->diagnostic_config_->checkpoints.has_value(),
            "--klvk-write-checkpoints requires a diagnostic configuration with a 'checkpoints' section");
        state_->write_checkpoints_path_ = command_line.write_checkpoints_path;
    }
    Run();
}

void Application::PreTick()
{
    // Recreate the swapchain when the window size changes. Cannot rely on
    // vk::Result::eErrorOutOfDateKHR alone: on Wayland the compositor silently
    // stretches the presented image instead of invalidating the swapchain.
    {
        if (state_->diagnostic_config_.has_value() && state_->diagnostic_config_->framebuffer_size.has_value())
        {
            state_->window_->SetFramebufferSize(*state_->diagnostic_config_->framebuffer_size);
        }
        const auto framebuffer_size = state_->window_->GetFramebufferSize();
        const vk::Extent2D extent = state_->render_target_->GetExtent();
        if (state_->swapchain_ && (framebuffer_size.x() != extent.width || framebuffer_size.y() != extent.height))
        {
            state_->RecreateSwapchain();
        }
    }

    auto& frame = state_->CurrentFrame();
    vk::Device device = state_->device_context_->GetDevice();

    const std::array fences{frame.in_flight.get()};
    const vk::Result wait_result = device.waitForFences(fences, true, std::numeric_limits<u64>::max());
    if (wait_result == vk::Result::eTimeout)
    {
        ErrorHandling::Ensure(false, "Timed out waiting for the frame fence");
    }
    VulkanCheck(wait_result);
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
            const auto outcome = device.acquireNextImageKHR(
                state_->swapchain_->GetHandle(),
                std::numeric_limits<u64>::max(),
                frame.image_available.get());
            if (outcome.result == vk::Result::eSuccess || outcome.result == vk::Result::eSuboptimalKHR)
            {
                state_->image_index_ = outcome.value;
                break;
            }
            if (outcome.result == vk::Result::eErrorOutOfDateKHR)
            {
                state_->RecreateSwapchain();
                continue;
            }
            ErrorHandling::Ensure(
                outcome.result != vk::Result::eNotReady,
                "No swapchain image was ready despite an infinite acquisition timeout");
            ErrorHandling::Ensure(
                outcome.result != vk::Result::eTimeout,
                "Timed out acquiring a swapchain image despite an infinite timeout");
            VulkanCheck(outcome.result);
        }
    }

    device.resetFences(fences);
    device.resetCommandPool(frame.command_pool.get());

    const auto begin_info = vk::CommandBufferBeginInfo{}.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    frame.command_buffer.begin(begin_info);
    state_->frame_active_ = true;

    BeforeSwapchainRender(frame.command_buffer);

    // undefined -> color attachment
    {
        const auto range = vk::ImageSubresourceRange{}
                               .setAspectMask(vk::ImageAspectFlagBits::eColor)
                               .setLevelCount(1)
                               .setLayerCount(1);
        const std::array barriers{vk::ImageMemoryBarrier2{}
                                      .setSrcStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
                                      .setSrcAccessMask(vk::AccessFlagBits2::eNone)
                                      .setDstStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
                                      .setDstAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
                                      .setOldLayout(vk::ImageLayout::eUndefined)
                                      .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)
                                      .setSrcQueueFamilyIndex(vk::QueueFamilyIgnored)
                                      .setDstQueueFamilyIndex(vk::QueueFamilyIgnored)
                                      .setImage(state_->render_target_->GetImage(state_->image_index_))
                                      .setSubresourceRange(range)};
        frame.command_buffer.pipelineBarrier2(vk::DependencyInfo{}.setImageMemoryBarriers(barriers));
    }

    const vk::Format depth_stencil_format = state_->render_target_->GetDepthStencilFormat();
    if (state_->DepthStencilAttachmentEnabled())
    {
        const auto range = vk::ImageSubresourceRange{}
                               .setAspectMask(DepthStencilAspectMask(depth_stencil_format))
                               .setLevelCount(1)
                               .setLayerCount(1);
        const std::array barriers{
            vk::ImageMemoryBarrier2{}
                .setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
                .setSrcAccessMask(vk::AccessFlagBits2::eNone)
                .setDstStageMask(
                    vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests)
                .setDstAccessMask(
                    vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                    vk::AccessFlagBits2::eDepthStencilAttachmentWrite)
                .setOldLayout(vk::ImageLayout::eUndefined)
                .setNewLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
                .setSrcQueueFamilyIndex(vk::QueueFamilyIgnored)
                .setDstQueueFamilyIndex(vk::QueueFamilyIgnored)
                .setImage(state_->render_target_->GetDepthImage(state_->image_index_))
                .setSubresourceRange(range)};
        frame.command_buffer.pipelineBarrier2(vk::DependencyInfo{}.setImageMemoryBarriers(barriers));
    }

    const auto extent = state_->render_target_->GetExtent();
    const auto& c = state_->clear_color_;
    const vk::ClearValue color_clear{vk::ClearColorValue{std::array{c.x(), c.y(), c.z(), c.w()}}};
    const std::array color_attachments{
        vk::RenderingAttachmentInfo{}
            .setImageView(state_->render_target_->GetImageView(state_->image_index_))
            .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
            .setLoadOp(state_->auto_clear_ ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setClearValue(color_clear)};
    // Single objects behind pDepthAttachment / pStencilAttachment - no count field,
    // so no arrays. Both name the same view: the planes live in one image.
    const vk::ImageView depth_stencil_view = state_->DepthStencilAttachmentEnabled()
                                                 ? state_->render_target_->GetDepthImageView(state_->image_index_)
                                                 : nullptr;
    const vk::ClearValue depth_clear{vk::ClearDepthStencilValue{1.f, 0}};
    const vk::ClearValue stencil_clear{vk::ClearDepthStencilValue{0.f, 0}};
    const auto depth_attachment = vk::RenderingAttachmentInfo{}
                                      .setImageView(depth_stencil_view)
                                      .setImageLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
                                      .setLoadOp(vk::AttachmentLoadOp::eClear)
                                      .setStoreOp(vk::AttachmentStoreOp::eDontCare)
                                      .setClearValue(depth_clear);
    const auto stencil_attachment = vk::RenderingAttachmentInfo{}
                                        .setImageView(depth_stencil_view)
                                        .setImageLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
                                        .setLoadOp(vk::AttachmentLoadOp::eClear)
                                        .setStoreOp(vk::AttachmentStoreOp::eDontCare)
                                        .setClearValue(stencil_clear);
    const auto rendering_info =
        vk::RenderingInfo{}
            .setRenderArea(vk::Rect2D{{0, 0}, extent})
            .setLayerCount(1)
            .setColorAttachments(color_attachments)
            .setPDepthAttachment(state_->depth_buffer_enabled_ ? &depth_attachment : nullptr)
            .setPStencilAttachment(state_->stencil_buffer_enabled_ ? &stencil_attachment : nullptr);
    frame.command_buffer.beginRendering(rendering_info);

    // GL-style viewport (y up) so view matrices keep working unchanged after the klgl port.
    const std::array viewports{vk::Viewport{
        0.f,
        static_cast<float>(extent.height),
        static_cast<float>(extent.width),
        -static_cast<float>(extent.height),
        0.f,
        1.f}};
    frame.command_buffer.setViewport(0, viewports);
    const std::array scissors{vk::Rect2D{{0, 0}, extent}};
    frame.command_buffer.setScissor(0, scissors);

    state_->imgui_.PrepareFrame(state_->glfw_, state_->offscreen_, extent);
    if (state_->diagnostic_runner_)
    {
        state_->diagnostic_runner_->AdvanceInput(state_->completed_frames_ + 1, state_->GetElapsedTime());
    }
    ApplicationImGui::BeginFrame(state_->GetFixedStepNanoseconds());
}

void Application::Tick() {}

void Application::BeforeSwapchainRender([[maybe_unused]] vk::CommandBuffer command_buffer) {}

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
    if (state_->DepthStencilAttachmentEnabled() || capture_without_ui)
    {
        frame.command_buffer.endRendering();
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
                vk::ImageLayout::eColorAttachmentOptimal);
            ErrorHandling::Ensure(recorded, "A due pre-UI diagnostic capture was not recorded");
        }
        const std::array color_attachments{vk::RenderingAttachmentInfo{}
                                               .setImageView(state_->render_target_->GetImageView(state_->image_index_))
                                               .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
                                               .setLoadOp(vk::AttachmentLoadOp::eLoad)
                                               .setStoreOp(vk::AttachmentStoreOp::eStore)};
        const auto rendering_info = vk::RenderingInfo{}
                                        .setRenderArea(vk::Rect2D{{0, 0}, state_->render_target_->GetExtent()})
                                        .setLayerCount(1)
                                        .setColorAttachments(color_attachments);
        frame.command_buffer.beginRendering(rendering_info);
    }

    ApplicationImGui::Render(frame.command_buffer);

    frame.command_buffer.endRendering();

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
            state_->swapchain_ ? vk::ImageLayout::ePresentSrcKHR : vk::ImageLayout::eColorAttachmentOptimal);

    // color attachment -> present when capture did not already perform that transition.
    if (state_->swapchain_ && !captured_with_ui)
    {
        const auto range = vk::ImageSubresourceRange{}
                               .setAspectMask(vk::ImageAspectFlagBits::eColor)
                               .setLevelCount(1)
                               .setLayerCount(1);
        const std::array barriers{vk::ImageMemoryBarrier2{}
                                      .setSrcStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
                                      .setSrcAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
                                      .setDstStageMask(vk::PipelineStageFlagBits2::eNone)
                                      .setDstAccessMask(vk::AccessFlagBits2::eNone)
                                      .setOldLayout(vk::ImageLayout::eColorAttachmentOptimal)
                                      .setNewLayout(vk::ImageLayout::ePresentSrcKHR)
                                      .setSrcQueueFamilyIndex(vk::QueueFamilyIgnored)
                                      .setDstQueueFamilyIndex(vk::QueueFamilyIgnored)
                                      .setImage(state_->render_target_->GetImage(state_->image_index_))
                                      .setSubresourceRange(range)};
        frame.command_buffer.pipelineBarrier2(vk::DependencyInfo{}.setImageMemoryBarriers(barriers));
    }

    frame.command_buffer.end();
    state_->frame_active_ = false;

    const std::array command_buffer_infos{vk::CommandBufferSubmitInfo{}.setCommandBuffer(frame.command_buffer)};
    vk::Semaphore render_finished = nullptr;
    if (state_->swapchain_)
    {
        render_finished = state_->render_finished_[state_->image_index_].get();
        const std::array wait_infos{vk::SemaphoreSubmitInfo{}
                                        .setSemaphore(frame.image_available.get())
                                        .setStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)};
        const std::array signal_infos{vk::SemaphoreSubmitInfo{}
                                          .setSemaphore(render_finished)
                                          .setStageMask(vk::PipelineStageFlagBits2::eAllCommands)};
        const std::array submit_infos{vk::SubmitInfo2{}
                                          .setWaitSemaphoreInfos(wait_infos)
                                          .setCommandBufferInfos(command_buffer_infos)
                                          .setSignalSemaphoreInfos(signal_infos)};
        state_->device_context_->GetGraphicsQueue().submit2(submit_infos, frame.in_flight.get());
    }
    else
    {
        const std::array submit_infos{vk::SubmitInfo2{}.setCommandBufferInfos(command_buffer_infos)};
        state_->device_context_->GetGraphicsQueue().submit2(submit_infos, frame.in_flight.get());
    }

    if (state_->swapchain_)
    {
        const std::array wait_semaphores{render_finished};
        const std::array swapchains{state_->swapchain_->GetHandle()};
        // pImageIndices has no count of its own - it is parallel to pSwapchains.
        const std::array image_indices{state_->image_index_};
        const auto present_info = vk::PresentInfoKHR{}
                                      .setWaitSemaphores(wait_semaphores)
                                      .setSwapchains(swapchains)
                                      .setPImageIndices(image_indices.data());
        const vk::Result result = state_->device_context_->GetGraphicsQueue().presentKHR(present_info);
        if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR)
        {
            state_->RecreateSwapchain();
        }
        else
        {
            VulkanCheck(result);
        }
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

std::optional<u64> Application::GetDiagnosticExitFrame() const noexcept
{
    if (!state_->diagnostic_config_.has_value()) return std::nullopt;
    return state_->diagnostic_config_->exit.frame;
}

std::optional<std::filesystem::path> Application::OpenFileDialog(
    std::string_view title,
    std::span<const FileDialog::Filter> filters,
    const std::filesystem::path& default_path)
{
    return AnswerFileDialog([&] { return FileDialog::Open(title, filters, default_path); });
}

std::optional<std::filesystem::path> Application::SaveFileDialog(
    std::string_view title,
    std::span<const FileDialog::Filter> filters,
    const std::filesystem::path& default_path)
{
    return AnswerFileDialog([&] { return FileDialog::Save(title, filters, default_path); });
}

std::optional<std::filesystem::path> Application::AnswerFileDialog(
    const std::function<std::optional<std::filesystem::path>()>& ask)
{
    // A replay must not put a dialog on screen: it may have no display at all,
    // and an answer that came from a person is not reproducible anyway.
    if (state_->diagnostic_runner_ && state_->diagnostic_runner_->AnswersDialogs())
    {
        return state_->diagnostic_runner_->TakeDialogAnswer();
    }

    auto answer = ask();
    if (state_->input_recorder_) state_->input_recorder_->RecordDialog(answer, GetExecutableDir());

    return answer;
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
    return state_->frame_clock_.GetFramerate();
}

float Application::GetLastFrameDurationSeconds() const
{
    return state_->frame_clock_.GetLastFrameDurationSeconds();
}

void Application::SetTargetFramerate(std::optional<float> framerate)
{
    state_->frame_clock_.SetTargetFramerate(framerate);
}

void Application::SetSwapchainPresentMode(SwapchainPresentMode present_mode)
{
    ErrorHandling::Ensure(
        state_->swapchain_ == nullptr,
        "Swapchain present mode must be selected before initialization");
    state_->present_mode_ = present_mode;
}

void Application::SetClearColor(const edt::Vec4f& color)
{
    state_->clear_color_ = color;
}

void Application::SetDepthBufferEnabled(bool enabled)
{
    state_->depth_buffer_enabled_ = enabled;
}

void Application::SetStencilBufferEnabled(bool enabled)
{
    ErrorHandling::Ensure(
        !enabled || FormatHasStencil(state_->render_target_->GetDepthStencilFormat()),
        "The selected depth-stencil format has no stencil plane");
    state_->stencil_buffer_enabled_ = enabled;
}

DeviceContext& Application::GetDeviceContext()
{
    return *state_->device_context_;
}

vk::Format Application::GetSwapchainFormat() const
{
    return state_->render_target_->GetFormat();
}

vk::Format Application::GetDepthFormat() const
{
    return state_->render_target_->GetDepthStencilFormat();
}

vk::CommandBuffer Application::GetCurrentCommandBuffer() const
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
