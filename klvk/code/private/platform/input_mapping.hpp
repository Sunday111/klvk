#pragma once

#include <optional>
#include <string_view>

#include "klvk/input.hpp"

namespace klvk
{

[[nodiscard]] std::optional<Key> KeyFromName(std::string_view name) noexcept;

// Inverse of KeyFromName, so a recorded key round-trips through the diagnostic
// configuration vocabulary unchanged.
[[nodiscard]] std::optional<std::string_view> KeyToName(Key key) noexcept;
[[nodiscard]] std::optional<Key> KeyFromGlfw(int glfw_key) noexcept;
[[nodiscard]] int KeyToImGui(Key key);
[[nodiscard]] std::optional<MouseButton> MouseButtonFromGlfw(int glfw_button) noexcept;
[[nodiscard]] int MouseButtonToImGui(MouseButton button);

}  // namespace klvk
