#pragma once

#include <optional>
#include <string>
#include <vector>

namespace klvk
{
class ImGuiCombo
{
public:
    explicit ImGuiCombo(std::string_view title = "") noexcept : title_(title) {}

    bool Draw() noexcept;
    size_t AddItem(std::string_view name);
    [[nodiscard]] std::optional<size_t> GetSelected() const noexcept;
    [[nodiscard]] std::string_view GetTitle() const noexcept { return title_; }
    [[nodiscard]] size_t GetSize() const noexcept { return names_.size(); }

    bool TryRemoveItem(size_t index) noexcept;
    void RemoveItem(size_t index);

private:
    std::vector<std::string> names_;
    std::vector<const char*> names_ptrs_;
    int selected_ = 0;
    int max_height_ = -1;
    std::string title_;
};

}  // namespace klvk
