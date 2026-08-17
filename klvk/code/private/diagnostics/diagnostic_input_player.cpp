#include "diagnostic_input_player.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <type_traits>

#include "klvk/window.hpp"
#include "platform/input_mapping.hpp"

namespace klvk
{

DiagnosticInputPlayer::DiagnosticInputPlayer(Window& window) noexcept : window_(window) {}

void DiagnosticInputPlayer::ApplyModifier(Key key)
{
    struct Modifier
    {
        Key left;
        Key right;
        ImGuiKey flag;
    };
    static constexpr auto kModifiers = std::to_array<Modifier>({
        {.left = Key::LeftCtrl, .right = Key::RightCtrl, .flag = ImGuiMod_Ctrl},
        {.left = Key::LeftShift, .right = Key::RightShift, .flag = ImGuiMod_Shift},
        {.left = Key::LeftAlt, .right = Key::RightAlt, .flag = ImGuiMod_Alt},
        {.left = Key::LeftSuper, .right = Key::RightSuper, .flag = ImGuiMod_Super},
    });

    const auto found = std::ranges::find_if(
        kModifiers,
        [key](const Modifier& modifier) { return modifier.left == key || modifier.right == key; });
    if (found == std::ranges::end(kModifiers)) return;
    ImGui::GetIO().AddKeyEvent(found->flag, window_.IsKeyPressed(found->left) || window_.IsKeyPressed(found->right));
}

void DiagnosticInputPlayer::Apply(const DiagnosticInputEvent& input)
{
    ImGuiIO& io = ImGui::GetIO();
    std::visit(
        [&](const auto& event)
        {
            using Event = std::decay_t<decltype(event)>;
            if constexpr (std::is_same_v<Event, DiagnosticMouseMoveInput>)
            {
                window_.OnMouseMove(event.position);
                io.AddMousePosEvent(event.position.x(), event.position.y());
            }
            else if constexpr (std::is_same_v<Event, DiagnosticMouseButtonInput>)
            {
                const bool pressed = event.action == InputAction::Press;
                window_.OnMouseButton(event.button, event.action);
                io.AddMouseButtonEvent(MouseButtonToImGui(event.button), pressed);
            }
            else if constexpr (std::is_same_v<Event, DiagnosticMouseScrollInput>)
            {
                window_.OnMouseScroll(event.offset.x(), event.offset.y());
                io.AddMouseWheelEvent(event.offset.x(), event.offset.y());
            }
            else if constexpr (std::is_same_v<Event, DiagnosticKeyInput>)
            {
                const bool pressed = event.action == InputAction::Press;
                window_.OnKey(event.key, event.action);
                io.AddKeyEvent(static_cast<ImGuiKey>(KeyToImGui(event.key)), pressed);
                ApplyModifier(event.key);
            }
        },
        input);
}

}  // namespace klvk
