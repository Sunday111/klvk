#include "input_mapping.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <array>

#include "klvk/error_handling.hpp"

namespace klvk
{
namespace
{

struct KeyMapping
{
    constexpr KeyMapping(std::string_view name, Key key, int glfw_key, ImGuiKey imgui_key) noexcept
        : name(name),
          key(key),
          glfw_key(glfw_key),
          imgui_key(imgui_key)
    {
    }

    std::string_view name;
    Key key;
    int glfw_key;
    ImGuiKey imgui_key;
};

constexpr auto kKeyMappings = std::to_array<KeyMapping>({
    {"tab", Key::Tab, GLFW_KEY_TAB, ImGuiKey_Tab},
    {"left", Key::LeftArrow, GLFW_KEY_LEFT, ImGuiKey_LeftArrow},
    {"right", Key::RightArrow, GLFW_KEY_RIGHT, ImGuiKey_RightArrow},
    {"up", Key::UpArrow, GLFW_KEY_UP, ImGuiKey_UpArrow},
    {"down", Key::DownArrow, GLFW_KEY_DOWN, ImGuiKey_DownArrow},
    {"page_up", Key::PageUp, GLFW_KEY_PAGE_UP, ImGuiKey_PageUp},
    {"page_down", Key::PageDown, GLFW_KEY_PAGE_DOWN, ImGuiKey_PageDown},
    {"home", Key::Home, GLFW_KEY_HOME, ImGuiKey_Home},
    {"end", Key::End, GLFW_KEY_END, ImGuiKey_End},
    {"insert", Key::Insert, GLFW_KEY_INSERT, ImGuiKey_Insert},
    {"delete", Key::Delete, GLFW_KEY_DELETE, ImGuiKey_Delete},
    {"backspace", Key::Backspace, GLFW_KEY_BACKSPACE, ImGuiKey_Backspace},
    {"space", Key::Space, GLFW_KEY_SPACE, ImGuiKey_Space},
    {"enter", Key::Enter, GLFW_KEY_ENTER, ImGuiKey_Enter},
    {"escape", Key::Escape, GLFW_KEY_ESCAPE, ImGuiKey_Escape},
    {"apostrophe", Key::Apostrophe, GLFW_KEY_APOSTROPHE, ImGuiKey_Apostrophe},
    {"comma", Key::Comma, GLFW_KEY_COMMA, ImGuiKey_Comma},
    {"minus", Key::Minus, GLFW_KEY_MINUS, ImGuiKey_Minus},
    {"period", Key::Period, GLFW_KEY_PERIOD, ImGuiKey_Period},
    {"slash", Key::Slash, GLFW_KEY_SLASH, ImGuiKey_Slash},
    {"semicolon", Key::Semicolon, GLFW_KEY_SEMICOLON, ImGuiKey_Semicolon},
    {"equal", Key::Equal, GLFW_KEY_EQUAL, ImGuiKey_Equal},
    {"left_bracket", Key::LeftBracket, GLFW_KEY_LEFT_BRACKET, ImGuiKey_LeftBracket},
    {"backslash", Key::Backslash, GLFW_KEY_BACKSLASH, ImGuiKey_Backslash},
    {"right_bracket", Key::RightBracket, GLFW_KEY_RIGHT_BRACKET, ImGuiKey_RightBracket},
    {"grave_accent", Key::GraveAccent, GLFW_KEY_GRAVE_ACCENT, ImGuiKey_GraveAccent},
    {"caps_lock", Key::CapsLock, GLFW_KEY_CAPS_LOCK, ImGuiKey_CapsLock},
    {"scroll_lock", Key::ScrollLock, GLFW_KEY_SCROLL_LOCK, ImGuiKey_ScrollLock},
    {"num_lock", Key::NumLock, GLFW_KEY_NUM_LOCK, ImGuiKey_NumLock},
    {"print_screen", Key::PrintScreen, GLFW_KEY_PRINT_SCREEN, ImGuiKey_PrintScreen},
    {"pause", Key::Pause, GLFW_KEY_PAUSE, ImGuiKey_Pause},
    {"0", Key::Num0, GLFW_KEY_0, ImGuiKey_0},
    {"1", Key::Num1, GLFW_KEY_1, ImGuiKey_1},
    {"2", Key::Num2, GLFW_KEY_2, ImGuiKey_2},
    {"3", Key::Num3, GLFW_KEY_3, ImGuiKey_3},
    {"4", Key::Num4, GLFW_KEY_4, ImGuiKey_4},
    {"5", Key::Num5, GLFW_KEY_5, ImGuiKey_5},
    {"6", Key::Num6, GLFW_KEY_6, ImGuiKey_6},
    {"7", Key::Num7, GLFW_KEY_7, ImGuiKey_7},
    {"8", Key::Num8, GLFW_KEY_8, ImGuiKey_8},
    {"9", Key::Num9, GLFW_KEY_9, ImGuiKey_9},
    {"a", Key::A, GLFW_KEY_A, ImGuiKey_A},
    {"b", Key::B, GLFW_KEY_B, ImGuiKey_B},
    {"c", Key::C, GLFW_KEY_C, ImGuiKey_C},
    {"d", Key::D, GLFW_KEY_D, ImGuiKey_D},
    {"e", Key::E, GLFW_KEY_E, ImGuiKey_E},
    {"f", Key::F, GLFW_KEY_F, ImGuiKey_F},
    {"g", Key::G, GLFW_KEY_G, ImGuiKey_G},
    {"h", Key::H, GLFW_KEY_H, ImGuiKey_H},
    {"i", Key::I, GLFW_KEY_I, ImGuiKey_I},
    {"j", Key::J, GLFW_KEY_J, ImGuiKey_J},
    {"k", Key::K, GLFW_KEY_K, ImGuiKey_K},
    {"l", Key::L, GLFW_KEY_L, ImGuiKey_L},
    {"m", Key::M, GLFW_KEY_M, ImGuiKey_M},
    {"n", Key::N, GLFW_KEY_N, ImGuiKey_N},
    {"o", Key::O, GLFW_KEY_O, ImGuiKey_O},
    {"p", Key::P, GLFW_KEY_P, ImGuiKey_P},
    {"q", Key::Q, GLFW_KEY_Q, ImGuiKey_Q},
    {"r", Key::R, GLFW_KEY_R, ImGuiKey_R},
    {"s", Key::S, GLFW_KEY_S, ImGuiKey_S},
    {"t", Key::T, GLFW_KEY_T, ImGuiKey_T},
    {"u", Key::U, GLFW_KEY_U, ImGuiKey_U},
    {"v", Key::V, GLFW_KEY_V, ImGuiKey_V},
    {"w", Key::W, GLFW_KEY_W, ImGuiKey_W},
    {"x", Key::X, GLFW_KEY_X, ImGuiKey_X},
    {"y", Key::Y, GLFW_KEY_Y, ImGuiKey_Y},
    {"z", Key::Z, GLFW_KEY_Z, ImGuiKey_Z},
    {"f1", Key::F1, GLFW_KEY_F1, ImGuiKey_F1},
    {"f2", Key::F2, GLFW_KEY_F2, ImGuiKey_F2},
    {"f3", Key::F3, GLFW_KEY_F3, ImGuiKey_F3},
    {"f4", Key::F4, GLFW_KEY_F4, ImGuiKey_F4},
    {"f5", Key::F5, GLFW_KEY_F5, ImGuiKey_F5},
    {"f6", Key::F6, GLFW_KEY_F6, ImGuiKey_F6},
    {"f7", Key::F7, GLFW_KEY_F7, ImGuiKey_F7},
    {"f8", Key::F8, GLFW_KEY_F8, ImGuiKey_F8},
    {"f9", Key::F9, GLFW_KEY_F9, ImGuiKey_F9},
    {"f10", Key::F10, GLFW_KEY_F10, ImGuiKey_F10},
    {"f11", Key::F11, GLFW_KEY_F11, ImGuiKey_F11},
    {"f12", Key::F12, GLFW_KEY_F12, ImGuiKey_F12},
    {"keypad_0", Key::Keypad0, GLFW_KEY_KP_0, ImGuiKey_Keypad0},
    {"keypad_1", Key::Keypad1, GLFW_KEY_KP_1, ImGuiKey_Keypad1},
    {"keypad_2", Key::Keypad2, GLFW_KEY_KP_2, ImGuiKey_Keypad2},
    {"keypad_3", Key::Keypad3, GLFW_KEY_KP_3, ImGuiKey_Keypad3},
    {"keypad_4", Key::Keypad4, GLFW_KEY_KP_4, ImGuiKey_Keypad4},
    {"keypad_5", Key::Keypad5, GLFW_KEY_KP_5, ImGuiKey_Keypad5},
    {"keypad_6", Key::Keypad6, GLFW_KEY_KP_6, ImGuiKey_Keypad6},
    {"keypad_7", Key::Keypad7, GLFW_KEY_KP_7, ImGuiKey_Keypad7},
    {"keypad_8", Key::Keypad8, GLFW_KEY_KP_8, ImGuiKey_Keypad8},
    {"keypad_9", Key::Keypad9, GLFW_KEY_KP_9, ImGuiKey_Keypad9},
    {"keypad_decimal", Key::KeypadDecimal, GLFW_KEY_KP_DECIMAL, ImGuiKey_KeypadDecimal},
    {"keypad_divide", Key::KeypadDivide, GLFW_KEY_KP_DIVIDE, ImGuiKey_KeypadDivide},
    {"keypad_multiply", Key::KeypadMultiply, GLFW_KEY_KP_MULTIPLY, ImGuiKey_KeypadMultiply},
    {"keypad_subtract", Key::KeypadSubtract, GLFW_KEY_KP_SUBTRACT, ImGuiKey_KeypadSubtract},
    {"keypad_add", Key::KeypadAdd, GLFW_KEY_KP_ADD, ImGuiKey_KeypadAdd},
    {"keypad_enter", Key::KeypadEnter, GLFW_KEY_KP_ENTER, ImGuiKey_KeypadEnter},
    {"keypad_equal", Key::KeypadEqual, GLFW_KEY_KP_EQUAL, ImGuiKey_KeypadEqual},
    {"left_shift", Key::LeftShift, GLFW_KEY_LEFT_SHIFT, ImGuiKey_LeftShift},
    {"left_ctrl", Key::LeftCtrl, GLFW_KEY_LEFT_CONTROL, ImGuiKey_LeftCtrl},
    {"left_alt", Key::LeftAlt, GLFW_KEY_LEFT_ALT, ImGuiKey_LeftAlt},
    {"left_super", Key::LeftSuper, GLFW_KEY_LEFT_SUPER, ImGuiKey_LeftSuper},
    {"right_shift", Key::RightShift, GLFW_KEY_RIGHT_SHIFT, ImGuiKey_RightShift},
    {"right_ctrl", Key::RightCtrl, GLFW_KEY_RIGHT_CONTROL, ImGuiKey_RightCtrl},
    {"right_alt", Key::RightAlt, GLFW_KEY_RIGHT_ALT, ImGuiKey_RightAlt},
    {"right_super", Key::RightSuper, GLFW_KEY_RIGHT_SUPER, ImGuiKey_RightSuper},
    {"menu", Key::Menu, GLFW_KEY_MENU, ImGuiKey_Menu},
});

const KeyMapping* FindKey(Key key) noexcept
{
    const auto found = std::ranges::find(kKeyMappings, key, &KeyMapping::key);
    return found == std::ranges::end(kKeyMappings) ? nullptr : found;
}

}  // namespace

std::optional<Key> KeyFromName(std::string_view name) noexcept
{
    const auto found = std::ranges::find(kKeyMappings, name, &KeyMapping::name);
    if (found == std::ranges::end(kKeyMappings)) return std::nullopt;
    return found->key;
}

std::optional<Key> KeyFromGlfw(int glfw_key) noexcept
{
    const auto found = std::ranges::find(kKeyMappings, glfw_key, &KeyMapping::glfw_key);
    if (found == std::ranges::end(kKeyMappings)) return std::nullopt;
    return found->key;
}

int KeyToImGui(Key key)
{
    const KeyMapping* mapping = FindKey(key);
    ErrorHandling::Ensure(mapping != nullptr, "Invalid klvk key {}", static_cast<u16>(key));
    return mapping->imgui_key;
}

std::optional<MouseButton> MouseButtonFromGlfw(int glfw_button) noexcept
{
    switch (glfw_button)
    {
    case GLFW_MOUSE_BUTTON_LEFT:
        return MouseButton::Left;
    case GLFW_MOUSE_BUTTON_RIGHT:
        return MouseButton::Right;
    case GLFW_MOUSE_BUTTON_MIDDLE:
        return MouseButton::Middle;
    case GLFW_MOUSE_BUTTON_4:
        return MouseButton::Button4;
    case GLFW_MOUSE_BUTTON_5:
        return MouseButton::Button5;
    default:
        return std::nullopt;
    }
}

int MouseButtonToImGui(MouseButton button)
{
    const auto index = static_cast<u8>(button);
    ErrorHandling::Ensure(index < static_cast<u8>(MouseButton::Count), "Invalid klvk mouse button {}", index);
    return index;
}

}  // namespace klvk
