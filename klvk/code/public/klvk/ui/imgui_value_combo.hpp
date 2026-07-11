#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "klvk/error_handling.hpp"
#include "simple_imgui_combo.hpp"

namespace klvk
{

template <typename T>
class ImGuiValueCombo
{
public:
    explicit ImGuiValueCombo(std::string_view title = "") : combo_(title) {}

    bool Draw() noexcept { return combo_.Draw(); }

    template <typename... Args>
    T& EmplaceItem(std::string_view name, Args&&... args)
    {
        size_t idx = combo_.AddItem(name);
        items_.emplace(items_.begin() + idx, std::forward<Args>(args)...);
        return items_.back();
    }

    std::optional<T> TryRemoveItem(size_t index) noexcept
    {
        std::optional<T> removed;
        if (combo_.TryRemoveItem(index))
        {
            removed = std::move(items_[index]);
            items_.erase(items_.begin() + index);
        }
        return removed;
    }

    T RemoveItem(size_t index)
    {
        combo_.RemoveItem(index);
        T removed = std::move(items_[index]);
        items_.erase(items_.begin() + index);
        return removed;
    }

    [[nodiscard]] std::optional<size_t> GetSelectedIndex() const noexcept { return combo_.GetSelected(); }

    [[nodiscard]] T* TryGetSelectedItem() noexcept
    {
        if (auto opt_idx = GetSelectedIndex())
        {
            return &items_[*opt_idx];
        }

        return nullptr;
    }

    [[nodiscard]] T& GetSelectedItem()
    {
        T* selected = TryGetSelectedItem();
        klvk::ErrorHandling::Ensure(selected, "Trying to get selected item but the there is nothing selected");
        return *selected;
    }

private:
    ImGuiCombo combo_;
    std::vector<T> items_;
};

}  // namespace klvk
