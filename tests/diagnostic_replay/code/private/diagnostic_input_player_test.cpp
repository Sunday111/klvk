#include "diagnostics/diagnostic_input_player.hpp"

#include <imgui.h>
#include <imgui_internal.h>

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
#include "platform/input_mapping.hpp"

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
        tests::Ensure(window->GetCursorPos() == Vec2f{12.5f, 34.25f}, "replayed cursor position was not stored");
        tests::Ensure(
            mouse_move.has_value() && mouse_move->previous == Vec2f{-1'000'000.f, -1'000'000.f} &&
                mouse_move->current == Vec2f{12.5f, 34.25f},
            "replayed mouse movement did not emit the expected event");

        player.Apply(DiagnosticMouseButtonInput{.button = MouseButton::Right, .action = InputAction::Press});
        tests::Ensure(window->IsMouseButtonPressed(MouseButton::Right), "replayed mouse press was not stored");
        tests::Ensure(window->IsInInputMode(), "replayed right mouse press did not enter input mode");
        tests::Ensure(
            mouse_button.has_value() && mouse_button->button == MouseButton::Right &&
                mouse_button->action == InputAction::Press,
            "replayed mouse press did not emit the expected event");
        player.Apply(DiagnosticMouseButtonInput{.button = MouseButton::Right, .action = InputAction::Release});
        tests::Ensure(!window->IsMouseButtonPressed(MouseButton::Right), "replayed mouse release was not stored");
        tests::Ensure(!window->IsInInputMode(), "replayed right mouse release did not leave input mode");

        player.Apply(DiagnosticMouseScrollInput{.offset = {-1.5f, 2.f}});
        tests::Ensure(
            mouse_scroll.has_value() && mouse_scroll->value == Vec2f{-1.5f, 2.f},
            "replayed mouse scroll did not emit the expected event");

        player.Apply(DiagnosticKeyInput{.key = Key::W, .action = InputAction::Press});
        tests::Ensure(window->IsKeyPressed(Key::W), "replayed key press was not stored");
        tests::Ensure(
            key.has_value() && key->key == Key::W && key->action == InputAction::Press,
            "replayed key press did not emit the expected event");
        player.Apply(DiagnosticKeyInput{.key = Key::W, .action = InputAction::Release});
        tests::Ensure(!window->IsKeyPressed(Key::W), "replayed key release was not stored");

        player.Apply(DiagnosticKeyInput{.key = Key::LeftCtrl, .action = InputAction::Press});
        tests::Ensure(window->IsKeyPressed(Key::LeftCtrl), "replayed left modifier press was not stored");
        player.Apply(DiagnosticKeyInput{.key = Key::RightCtrl, .action = InputAction::Press});
        tests::Ensure(window->IsKeyPressed(Key::RightCtrl), "replayed right modifier press was not stored");
        player.Apply(DiagnosticKeyInput{.key = Key::LeftCtrl, .action = InputAction::Release});
        tests::Ensure(
            !window->IsKeyPressed(Key::LeftCtrl) && window->IsKeyPressed(Key::RightCtrl),
            "releasing one replayed modifier cleared both sides");
        player.Apply(DiagnosticKeyInput{.key = Key::RightCtrl, .action = InputAction::Release});
        tests::Ensure(!window->IsKeyPressed(Key::RightCtrl), "replayed right modifier release was not stored");
        TestImGuiQueue();
    }

private:
    static bool Near(float first, float second) { return std::abs(first - second) < 0.000'001f; }

    static void TestImGuiQueue()
    {
        const ImVector<ImGuiInputEvent>& queue = ImGui::GetCurrentContext()->InputEventsQueue;
        tests::Ensure(queue.Size == 12, "replayed input queued the wrong number of ImGui events");
        tests::Ensure(
            queue[0].Type == ImGuiInputEventType_MousePos && Near(queue[0].MousePos.PosX, 12.f) &&
                Near(queue[0].MousePos.PosY, 34.f),
            "replayed cursor position did not reach ImGui");
        tests::Ensure(
            queue[1].Type == ImGuiInputEventType_MouseButton &&
                queue[1].MouseButton.Button == MouseButtonToImGui(MouseButton::Right) && queue[1].MouseButton.Down,
            "replayed mouse press did not reach ImGui");
        tests::Ensure(
            queue[3].Type == ImGuiInputEventType_MouseWheel && Near(queue[3].MouseWheel.WheelX, -1.5f) &&
                Near(queue[3].MouseWheel.WheelY, 2.f),
            "replayed mouse scroll did not reach ImGui");
        tests::Ensure(
            queue[4].Type == ImGuiInputEventType_Key && queue[4].Key.Key == KeyToImGui(Key::W) && queue[4].Key.Down,
            "replayed key press did not reach ImGui");

        tests::Ensure(
            queue[7].Type == ImGuiInputEventType_Key && queue[7].Key.Key == ImGuiMod_Ctrl && queue[7].Key.Down,
            "replayed modifier press did not reach ImGui");
        tests::Ensure(
            queue[11].Type == ImGuiInputEventType_Key && queue[11].Key.Key == ImGuiMod_Ctrl && !queue[11].Key.Down,
            "replayed modifier release did not reach ImGui");
    }
};

namespace tests
{

void RunDiagnosticInputPlayerTests()
{
    DiagnosticInputPlayerTest::Run();
}

}  // namespace tests
}  // namespace klvk
