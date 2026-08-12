#pragma once

#include <imgui.h>

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "magic_enum/magic_enum.hpp"

namespace klvk
{

[[nodiscard]] inline std::string ImGuiEnumDisplayName(std::string_view identifier)
{
    std::string result;
    result.reserve(identifier.size() + identifier.size() / 4);
    for (size_t index = 0; index != identifier.size(); ++index)
    {
        const char current = identifier[index];
        const bool uppercase = current >= 'A' && current <= 'Z';
        const bool previous_lowercase = index > 0 && identifier[index - 1] >= 'a' && identifier[index - 1] <= 'z';
        const bool next_lowercase =
            index + 1 < identifier.size() && identifier[index + 1] >= 'a' && identifier[index + 1] <= 'z';
        const bool starts_word = index > 0 && uppercase && (previous_lowercase || next_lowercase);
        if (starts_word) result.push_back(' ');
        result.push_back(starts_word ? static_cast<char>(current - 'A' + 'a') : current);
    }
    return result;
}

template <typename Enum>
    requires std::is_enum_v<Enum>
bool ImGuiEnumCombo(std::string_view label, std::optional<Enum>& value, std::string_view empty_preview = "None")
{
    const std::string preview =
        value ? ImGuiEnumDisplayName(magic_enum::enum_name(*value)) : std::string{empty_preview};
    bool changed = false;
    if (ImGui::BeginCombo(label.data(), preview.c_str()))
    {
        for (Enum choice : magic_enum::enum_values<Enum>())
        {
            const std::string name = ImGuiEnumDisplayName(magic_enum::enum_name(choice));
            const bool is_selected = value == choice;
            if (ImGui::Selectable(name.c_str(), is_selected))
            {
                value = choice;
                changed = true;
            }
            if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

template <typename Enum>
    requires std::is_enum_v<Enum>
bool ImGuiEnumCombo(std::string_view label, Enum& value)
{
    std::optional selected{value};
    if (!ImGuiEnumCombo(label, selected)) return false;
    value = *selected;
    return true;
}

}  // namespace klvk
