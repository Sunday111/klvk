#include "klvk/ui/simple_imgui_combo.hpp"

#include "imgui.h"
#include "klvk/error_handling.hpp"


namespace klvk
{
bool ImGuiCombo::Draw() noexcept
{
    return ImGui::Combo(
        title_.data(),
        &selected_,
        names_ptrs_.data(),
        static_cast<int>(names_ptrs_.size()),
        max_height_);
}

size_t ImGuiCombo::AddItem(std::string_view name)
{
    size_t index = names_.size();
    const bool rebuild_ptrs = names_.size() == names_.capacity();
    names_.emplace_back(name);
    if (rebuild_ptrs)
    {
        names_ptrs_.clear();
        for (auto& v : names_) names_ptrs_.push_back(v.data());
    }
    else
    {
        names_ptrs_.push_back(names_.back().data());
    }
    return index;
}

bool ImGuiCombo::TryRemoveItem(size_t index) noexcept
{
    if (index >= names_ptrs_.size()) return false;

    auto off = static_cast<std::ptrdiff_t>(index);
    names_.erase(std::next(names_.begin(), off));
    names_ptrs_.erase(std::next(names_ptrs_.begin(), off));
    int size = static_cast<int>(names_.size());
    selected_ = size ? std::min(selected_, size - 1) : -1;
    return true;
}

std::optional<size_t> ImGuiCombo::GetSelected() const noexcept
{
    if (selected_ < 0) return std::nullopt;
    return std::optional<size_t>{std::in_place, static_cast<size_t>(selected_)};
}

void ImGuiCombo::RemoveItem(size_t index)
{
    klvk::ErrorHandling::Ensure(
        TryRemoveItem(index),
        "Failed to remove item from combo. Index: {}, title: {}, num entries: {}",
        index,
        title_,
        names_.size());
}
}  // namespace klvk
