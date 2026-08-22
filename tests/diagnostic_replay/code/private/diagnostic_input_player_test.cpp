#include "diagnostics/diagnostic_input_player.hpp"

#include <imgui.h>

#include <cmath>
#include <memory>
#include <optional>

#include "diagnostic_test_support.hpp"
#include "edt/functional/on_scope_leave.hpp"
#include "klvk/application.hpp"
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

        player.Apply(DiagnosticMouseMoveInput{.position = {12.5f, 34.25f}});
        BeginImGuiFrame();
        tests::Ensure(window->GetCursorPos() == Vec2f{12.5f, 34.25f}, "replayed cursor position was not stored");
        tests::Ensure(
            mouse_move.has_value() && mouse_move->previous == Vec2f{-1'000'000.f, -1'000'000.f} &&
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
        ImGui::EndFrame();
    }

private:
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
