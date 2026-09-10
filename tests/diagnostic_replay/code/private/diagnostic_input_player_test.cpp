#include "diagnostics/diagnostic_input_player.hpp"

#include <imgui.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "diagnostic_test_support.hpp"
#include "diagnostics/input_recorder.hpp"
#include "edt/functional/on_scope_leave.hpp"
#include "klvk/application.hpp"
#include "klvk/camera/camera_3d.hpp"
#include "klvk/events/event_listener.hpp"
#include "klvk/events/event_manager.hpp"
#include "klvk/events/keyboard_events.hpp"
#include "klvk/events/mouse_events.hpp"
#include "klvk/window.hpp"

namespace klvk
{

class DiagnosticInputPlayerTest
{
public:
    static void Run()
    {
        Application application;
        std::unique_ptr<Window> window = Window::CreateOffscreen(application, 320, 240);
        ImGui::CreateContext();
        auto destroy_imgui = edt::OnScopeLeave([] { ImGui::DestroyContext(); });
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {320.f, 240.f};
        io.DeltaTime = 1.f / 60.f;
        io.Fonts->AddFontDefault();
        tests::Ensure(io.Fonts->Build(), "failed to build the diagnostic input test font atlas");

        std::optional<events::OnMouseMove> mouse_move;
        std::optional<events::OnMouseButton> mouse_button;
        std::optional<events::OnMouseScroll> mouse_scroll;
        std::optional<events::OnKey> key;
        auto listener =
            events::EventListener<events::OnMouseMove, events::OnMouseButton, events::OnMouseScroll, events::OnKey>::
                PtrFromFunctions(
                    [&](const events::OnMouseMove& event) { mouse_move = event; },
                    [&](const events::OnMouseButton& event) { mouse_button = event; },
                    [&](const events::OnMouseScroll& event) { mouse_scroll = event; },
                    [&](const events::OnKey& event) { key = event; });
        auto subscription = application.GetEventManager().AddEventListener(*listener);
        DiagnosticInputPlayer player(*window);
        tests::Ensure(!window->HasInputFocus(), "an unfocused window accepted native input");
        window->SetPlatformInputEnabled(false);
        tests::Ensure(!window->IsFocused(), "replay changed native window focus");
        Camera3d camera;
        camera.SetRotation({.yaw = 0.f, .pitch = 0.f, .roll = 0.f});
        auto camera_listener = events::EventListener<events::OnMouseMove>::PtrFromFunctions(
            [&](const events::OnMouseMove& event)
            {
                if (!window->HasInputFocus() || !window->IsInInputMode() || io.WantCaptureMouse) return;
                const Vec2f delta = (event.current - event.previous) * 0.01f;
                const auto rotation = camera.GetRotation();
                camera.SetRotation(
                    {.yaw = rotation.yaw + delta.x(), .pitch = rotation.pitch + delta.y(), .roll = rotation.roll});
            });
        auto camera_subscription = application.GetEventManager().AddEventListener(*camera_listener);

        player.Apply(DiagnosticMouseMoveInput{.position = {12.5f, 34.25f}});
        BeginImGuiFrame();
        tests::Ensure(window->GetCursorPos() == Vec2f{12.5f, 34.25f}, "replayed cursor position was not stored");
        tests::Ensure(
            mouse_move.has_value() && mouse_move->previous == Vec2f{12.5f, 34.25f} &&
                mouse_move->current == Vec2f{12.5f, 34.25f},
            "replayed mouse movement did not emit the expected event");
        tests::Ensure(Near(io.MousePos.x, 12.f) && Near(io.MousePos.y, 34.f), "replayed cursor did not reach ImGui");
        ImGui::EndFrame();

        player.Apply(DiagnosticMouseButtonInput{.button = MouseButton::Right, .action = InputAction::Press});
        BeginImGuiFrame();
        tests::Ensure(window->IsMouseButtonPressed(MouseButton::Right), "replayed mouse press was not stored");
        tests::Ensure(window->IsInInputMode(), "replayed right mouse press did not enter input mode");
        tests::Ensure(
            mouse_button.has_value() && mouse_button->button == MouseButton::Right &&
                mouse_button->action == InputAction::Press,
            "replayed mouse press did not emit the expected event");
        tests::Ensure(ImGui::IsMouseDown(ImGuiMouseButton_Right), "replayed mouse press did not reach ImGui");
        ImGui::EndFrame();
        player.Apply(DiagnosticMouseMoveInput{.position = {22.5f, 54.25f}});
        tests::Ensure(
            Near(camera.GetRotation().yaw, 0.1f) && Near(camera.GetRotation().pitch, 0.2f),
            "offscreen replay did not rotate the camera");
        player.Apply(DiagnosticMouseButtonInput{.button = MouseButton::Right, .action = InputAction::Release});
        BeginImGuiFrame();
        tests::Ensure(!window->IsMouseButtonPressed(MouseButton::Right), "replayed mouse release was not stored");
        tests::Ensure(!window->IsInInputMode(), "replayed right mouse release did not leave input mode");
        tests::Ensure(!ImGui::IsMouseDown(ImGuiMouseButton_Right), "replayed mouse release did not reach ImGui");
        ImGui::EndFrame();

        player.Apply(DiagnosticMouseScrollInput{.offset = {-1.5f, 2.f}});
        BeginImGuiFrame();
        tests::Ensure(
            mouse_scroll.has_value() && mouse_scroll->value == Vec2f{-1.5f, 2.f},
            "replayed mouse scroll did not emit the expected event");
        tests::Ensure(
            Near(io.MouseWheelH, -1.5f) && Near(io.MouseWheel, 2.f),
            "replayed mouse scroll did not reach ImGui");
        ImGui::EndFrame();

        player.Apply(DiagnosticKeyInput{.key = Key::W, .action = InputAction::Press});
        BeginImGuiFrame();
        tests::Ensure(window->IsKeyPressed(Key::W), "replayed key press was not stored");
        tests::Ensure(
            key.has_value() && key->key == Key::W && key->action == InputAction::Press,
            "replayed key press did not emit the expected event");
        tests::Ensure(ImGui::IsKeyDown(ImGuiKey_W), "replayed key press did not reach ImGui");
        ImGui::EndFrame();
        player.Apply(DiagnosticKeyInput{.key = Key::W, .action = InputAction::Release});
        BeginImGuiFrame();
        tests::Ensure(!window->IsKeyPressed(Key::W), "replayed key release was not stored");
        tests::Ensure(!ImGui::IsKeyDown(ImGuiKey_W), "replayed key release did not reach ImGui");
        ImGui::EndFrame();

        player.Apply(DiagnosticKeyInput{.key = Key::LeftCtrl, .action = InputAction::Press});
        BeginImGuiFrame();
        tests::Ensure(window->IsKeyPressed(Key::LeftCtrl), "replayed left modifier press was not stored");
        tests::Ensure(io.KeyCtrl, "replayed left modifier press did not reach ImGui");
        ImGui::EndFrame();
        player.Apply(DiagnosticKeyInput{.key = Key::RightCtrl, .action = InputAction::Press});
        BeginImGuiFrame();
        tests::Ensure(window->IsKeyPressed(Key::RightCtrl), "replayed right modifier press was not stored");
        tests::Ensure(io.KeyCtrl, "replayed right modifier press did not reach ImGui");
        ImGui::EndFrame();
        player.Apply(DiagnosticKeyInput{.key = Key::LeftCtrl, .action = InputAction::Release});
        BeginImGuiFrame();
        tests::Ensure(
            !window->IsKeyPressed(Key::LeftCtrl) && window->IsKeyPressed(Key::RightCtrl),
            "releasing one replayed modifier cleared both sides");
        tests::Ensure(io.KeyCtrl, "releasing one replayed modifier cleared ImGui's aggregate modifier");
        ImGui::EndFrame();
        player.Apply(DiagnosticKeyInput{.key = Key::RightCtrl, .action = InputAction::Release});
        BeginImGuiFrame();
        tests::Ensure(!window->IsKeyPressed(Key::RightCtrl), "replayed right modifier release was not stored");
        tests::Ensure(!io.KeyCtrl, "replayed final modifier release did not reach ImGui");
        window->SetPlatformInputEnabled(true);
        tests::Ensure(!window->HasInputFocus(), "ending replay did not restore native focus gating");
        ImGui::EndFrame();
        TestRecordedCursor(false);
        TestRecordedCursor(true);
    }

private:
    static void TestRecordedCursor(bool move_before_press)
    {
        ImGuiContext* previous_context = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(ImGui::CreateContext());
        auto restore_context = edt::OnScopeLeave(
            [&]
            {
                ImGui::DestroyContext();
                ImGui::SetCurrentContext(previous_context);
            });
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {320.f, 240.f};
        io.DeltaTime = 1.f / 60.f;
        io.Fonts->AddFontDefault();
        tests::Ensure(io.Fonts->Build(), "failed to build the recorded cursor test font atlas");
        Application application;
        auto window = Window::CreateOffscreen(application, 320, 240);
        window->cursor_ = {160.f, 120.f};
        window->SetPlatformInputEnabled(false);
        Camera3d camera;
        camera.SetRotation({.yaw = 0.f, .pitch = 0.f, .roll = 0.f});
        size_t mouse_moves = 0;
        auto listener = events::EventListener<events::OnMouseMove>::PtrFromFunctions(
            [&](const events::OnMouseMove& event)
            {
                ++mouse_moves;
                if (!window->HasInputFocus() || !window->IsInInputMode()) return;
                const Vec2f delta = (event.current - event.previous) * 0.01f;
                const auto rotation = camera.GetRotation();
                camera.SetRotation(
                    {.yaw = rotation.yaw + delta.x(), .pitch = rotation.pitch + delta.y(), .roll = rotation.roll});
            });
        auto subscription = application.GetEventManager().AddEventListener(*listener);
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto path =
            std::filesystem::temp_directory_path() / ("klvk_cursor_recording_" + std::to_string(nonce) + ".json");
        auto remove_recording = edt::OnScopeLeave([&] { std::filesystem::remove(path); });
        {
            DiagnosticInputRecorder recorder(path, application.GetEventManager(), window->GetCursorPos());
            recorder.BeginFrame(1);
            window->OnMouseMove({160.f, 120.f});
            if (move_before_press) window->OnMouseMove({165.f, 125.f});
            window->OnMouseButton(MouseButton::Right, InputAction::Press);
            window->OnMouseMove({170.f, 130.f});
            window->OnMouseMove({180.f, 140.f});
            window->OnMouseMove({180.f, 140.f});
            window->OnMouseButton(MouseButton::Right, InputAction::Release);
            tests::Ensure(
                recorder.GetRecordedEventCount() == (move_before_press ? 4u : 3u),
                "initial position changed motion collapsing or duplicate suppression");
            recorder.Write({320, 240}, 16'666'667, nlohmann::json::object(), path.parent_path());
        }
        const auto expected_rotation = camera.GetRotation();
        const auto recording = LoadDiagnosticRunConfig(path, path.parent_path());
        tests::Ensure(
            recording.initial_cursor_position == Vec2f{160.f, 120.f},
            "recording did not preserve the initial cursor position");
        window = Window::CreateOffscreen(application, 320, 240);
        window->SetPlatformInputEnabled(false);
        camera.SetRotation({.yaw = 0.f, .pitch = 0.f, .roll = 0.f});
        mouse_moves = 0;
        DiagnosticInputPlayer player(*window, recording.initial_cursor_position);
        tests::Ensure(mouse_moves == 0, "initial cursor position emitted a synthetic movement");
        tests::Ensure(window->GetCursorPos() == Vec2f{160.f, 120.f}, "replay did not seed the window cursor");
        BeginImGuiFrame();
        tests::Ensure(
            Near(ImGui::GetIO().MousePos.x, 160.f) && Near(ImGui::GetIO().MousePos.y, 120.f),
            "replay did not seed the ImGui cursor");
        ImGui::EndFrame();
        for (const DiagnosticInputConfig& input : recording.input) player.Apply(input.event);
        tests::Ensure(
            Near(camera.GetRotation().yaw, expected_rotation.yaw) &&
                Near(camera.GetRotation().pitch, expected_rotation.pitch),
            "recorded first movement or collapsed mouse-look changed camera rotation");
        window = Window::CreateOffscreen(application, 320, 240);
        window->SetPlatformInputEnabled(false);
        camera.SetRotation({.yaw = 0.f, .pitch = 0.f, .roll = 0.f});
        DiagnosticInputPlayer legacy_player(*window);
        legacy_player.Apply(DiagnosticMouseButtonInput{.button = MouseButton::Right, .action = InputAction::Press});
        legacy_player.Apply(DiagnosticMouseMoveInput{.position = {180.f, 140.f}});
        tests::Ensure(
            Near(camera.GetRotation().yaw, 0.f) && Near(camera.GetRotation().pitch, 0.f),
            "an unseeded replay rotated from the offscreen cursor sentinel");
        legacy_player.Apply(DiagnosticMouseMoveInput{.position = {190.f, 150.f}});
        tests::Ensure(
            Near(camera.GetRotation().yaw, 0.1f) && Near(camera.GetRotation().pitch, 0.1f),
            "an unseeded replay dropped movement after establishing its cursor position");
        for (const auto initial_position : {recording.initial_cursor_position, std::optional<Vec2f>{}})
        {
            window = Window::CreateOffscreen(application, 320, 240);
            window->SetPlatformInputEnabled(false);
            camera.SetRotation({.yaw = 0.f, .pitch = 0.f, .roll = 0.f});
            DiagnosticInputPlayer source(*window, initial_position);
            {
                DiagnosticInputRecorder recorder(path, application.GetEventManager(), initial_position);
                recorder.BeginFrame(1);
                source.Apply(DiagnosticMouseButtonInput{.button = MouseButton::Right, .action = InputAction::Press});
                recorder.BeginFrame(5);
                source.Apply(DiagnosticMouseMoveInput{.position = {180.f, 140.f}});
                source.Apply(DiagnosticMouseMoveInput{.position = {190.f, 150.f}});
                recorder.Write({320, 240}, 16'666'667, nlohmann::json::object(), path.parent_path());
            }
            const auto source_rotation = camera.GetRotation();
            const auto rerecorded = LoadDiagnosticRunConfig(path, path.parent_path());
            tests::Ensure(
                rerecorded.initial_cursor_position == initial_position,
                "re-recording a replay lost its known initial cursor position");
            window = Window::CreateOffscreen(application, 320, 240);
            window->SetPlatformInputEnabled(false);
            camera.SetRotation({.yaw = 0.f, .pitch = 0.f, .roll = 0.f});
            const Vec2f unknown_position = window->GetCursorPos();
            DiagnosticInputPlayer destination(*window, rerecorded.initial_cursor_position);
            for (const DiagnosticInputConfig& input : rerecorded.input)
            {
                destination.Apply(input.event);
                if (!initial_position && input.frame < 5)
                {
                    tests::Ensure(
                        window->GetCursorPos() == unknown_position,
                        "an unknown cursor acquired a future position before its first movement");
                }
            }
            if (!initial_position)
            {
                tests::Ensure(
                    rerecorded.input.size() == 3 && rerecorded.input[1].frame == 5 && rerecorded.input[2].frame == 5,
                    "re-recording collapsed the initial baseline into a later movement");
            }
            tests::Ensure(
                Near(camera.GetRotation().yaw, source_rotation.yaw) &&
                    Near(camera.GetRotation().pitch, source_rotation.pitch),
                "re-recording a replay changed its first mouse movement");
        }
    }

    static bool Near(float first, float second) { return std::abs(first - second) < 0.000'001f; }

    static void BeginImGuiFrame() { ImGui::NewFrame(); }
};

namespace tests
{

void RunDiagnosticInputPlayerTests()
{
    DiagnosticInputPlayerTest::Run();
}

}  // namespace tests
}  // namespace klvk
