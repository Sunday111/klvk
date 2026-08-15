#include "klvk/ui/imgui_texture_viewer.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "klvk/ui/imgui_enum_combo.hpp"
#include "klvk/ui/imgui_helpers.hpp"
#include "klvk/vulkan/device_context.hpp"

namespace klvk
{
namespace
{
constexpr float kMinimumZoom = 0.01f;
constexpr float kMaximumZoom = 16.f;
constexpr float kWheelZoomStep = 1.2f;

template <typename Enum, size_t Size>
bool DrawEnumCombo(const char* label, Enum& value, const std::array<std::pair<Enum, std::string_view>, Size>& choices)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1.f);

    const auto selected = std::ranges::find(choices, value, &std::pair<Enum, std::string_view>::first);
    const std::string_view preview = selected != choices.end() ? selected->second : "Custom";
    ImGui::PushID(label);
    bool changed = false;
    if (ImGui::BeginCombo("##Value", preview.data()))
    {
        for (const auto& [choice, name] : choices)
        {
            const bool is_selected = value == choice;
            if (ImGui::Selectable(name.data(), is_selected))
            {
                value = choice;
                changed = true;
            }
            if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
}

constexpr std::array kFilterChoices{
    std::pair{vk::Filter::eNearest, std::string_view{"Nearest"}},
    std::pair{vk::Filter::eLinear, std::string_view{"Linear"}},
};
constexpr std::array kAddressChoices{
    std::pair{vk::SamplerAddressMode::eRepeat, std::string_view{"Repeat"}},
    std::pair{vk::SamplerAddressMode::eMirroredRepeat, std::string_view{"Mirrored repeat"}},
    std::pair{vk::SamplerAddressMode::eClampToEdge, std::string_view{"Clamp to edge"}},
    std::pair{vk::SamplerAddressMode::eClampToBorder, std::string_view{"Clamp to border"}},
};
constexpr std::array kBorderColorChoices{
    std::pair{vk::BorderColor::eFloatTransparentBlack, std::string_view{"Transparent black (float)"}},
    std::pair{vk::BorderColor::eIntTransparentBlack, std::string_view{"Transparent black (integer)"}},
    std::pair{vk::BorderColor::eFloatOpaqueBlack, std::string_view{"Opaque black (float)"}},
    std::pair{vk::BorderColor::eIntOpaqueBlack, std::string_view{"Opaque black (integer)"}},
    std::pair{vk::BorderColor::eFloatOpaqueWhite, std::string_view{"Opaque white (float)"}},
    std::pair{vk::BorderColor::eIntOpaqueWhite, std::string_view{"Opaque white (integer)"}},
};
}  // namespace

ImGuiTextureViewer::~ImGuiTextureViewer() = default;

void ImGuiTextureViewer::Draw(
    DeviceContext& context,
    vk::ImageView image_view,
    edt::Vec2<u32> size,
    std::string_view description,
    bool* open)
{
    if (registered_context_ != &context || registered_view_ != image_view) RegisterTexture(context, image_view);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 default_size{
        std::min(420.f, viewport->WorkSize.x),
        std::min(460.f, viewport->WorkSize.y),
    };
    const ImVec2 default_position{
        viewport->WorkPos.x + viewport->WorkSize.x - default_size.x,
        viewport->WorkPos.y,
    };
    ImGui::SetNextWindowPos(default_position, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(default_size, ImGuiCond_FirstUseEver);

    if (!ImGui::Begin(title_.c_str(), open))
    {
        ImGui::End();
        return;
    }

    if (!description.empty())
    {
        ImGui::TextUnformatted(description.begin(), description.end());
        ImGui::SameLine();
        ImGui::TextUnformatted("|");
        ImGui::SameLine();
    }
    ImGui::Text("%u x %u texture", size.x(), size.y());

    if (DrawSamplerControls()) RegisterTexture(context, image_view);

    ImGui::Checkbox("Fit", &fit_);
    ImGui::SameLine();
    if (ImGui::Button("1:1"))
    {
        fit_ = false;
        zoom_ = 1.f;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.f);
    float zoom_percent = zoom_ * 100.f;
    if (ImGuiHelper::FiniteSliderFloat(
            "##Texture zoom",
            zoom_percent,
            kMinimumZoom * 100.f,
            kMaximumZoom * 100.f,
            "%.0f%%",
            ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp))
    {
        fit_ = false;
        zoom_ = zoom_percent / 100.f;
    }
    ImGui::TextDisabled("Ctrl+wheel to zoom, middle-drag to pan");

    constexpr auto canvas_flags = ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::BeginChild("Texture canvas", {}, ImGuiChildFlags_Border, canvas_flags))
    {
        const ImVec2 canvas_origin = ImGui::GetCursorScreenPos();
        const ImVec2 canvas_size = ImGui::GetContentRegionAvail();
        if (fit_)
        {
            zoom_ = std::clamp(
                std::min(canvas_size.x / static_cast<float>(size.x()), canvas_size.y / static_cast<float>(size.y())),
                kMinimumZoom,
                kMaximumZoom);
        }

        const ImGuiIO& io = ImGui::GetIO();
        const bool hovered = ImGui::IsWindowHovered();
        std::optional<ImVec2> zoomed_scroll;
        if (hovered && !io.KeyCtrl && io.MouseWheel != 0.f)
        {
            const float delta = io.MouseWheel * ImGui::GetTextLineHeightWithSpacing() * 5.f;
            if (io.KeyShift)
            {
                ImGui::SetScrollX(ImGui::GetScrollX() - delta);
            }
            else
            {
                ImGui::SetScrollY(ImGui::GetScrollY() - delta);
            }
        }
        if (hovered && io.KeyCtrl && io.MouseWheel != 0.f)
        {
            const ImVec2 mouse = ImGui::GetMousePos();
            const ImVec2 mouse_in_canvas{mouse.x - canvas_origin.x, mouse.y - canvas_origin.y};
            const ImVec2 texture_pixel_under_mouse{
                (ImGui::GetScrollX() + mouse_in_canvas.x) / zoom_,
                (ImGui::GetScrollY() + mouse_in_canvas.y) / zoom_,
            };
            fit_ = false;
            zoom_ = std::clamp(zoom_ * std::pow(kWheelZoomStep, io.MouseWheel), kMinimumZoom, kMaximumZoom);
            zoomed_scroll = ImVec2{
                texture_pixel_under_mouse.x * zoom_ - mouse_in_canvas.x,
                texture_pixel_under_mouse.y * zoom_ - mouse_in_canvas.y,
            };
        }

        const ImVec2 display_size{
            static_cast<float>(size.x()) * zoom_,
            static_cast<float>(size.y()) * zoom_,
        };
        ImGui::Image(registered_texture_->GetId(), display_size, {}, {1.f, 1.f});

        if (zoomed_scroll)
        {
            ImGui::SetScrollX(zoomed_scroll->x);
            ImGui::SetScrollY(zoomed_scroll->y);
        }
        if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.f))
        {
            ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseDelta.x);
            ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

void ImGuiTextureViewer::RegisterTexture(DeviceContext& context, vk::ImageView image_view)
{
    if (registered_texture_)
    {
        registered_context_->WaitIdle();
        registered_texture_.reset();
    }
    registered_texture_ = std::make_unique<RegisteredImGuiTexture>(context, image_view, sampler_info_);
    registered_context_ = &context;
    registered_view_ = image_view;
}

bool ImGuiTextureViewer::DrawSamplerControls()
{
    if (!ImGui::CollapsingHeader("Sampler", ImGuiTreeNodeFlags_DefaultOpen)) return false;

    bool changed = false;
    if (ImGui::BeginTable("Sampler parameters", 2, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn(
            "Parameter",
            ImGuiTableColumnFlags_WidthFixed,
            ImGui::CalcTextSize("Horizontal address").x);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.f);
        changed |= DrawEnumCombo("Magnification", sampler_info_.magFilter, kFilterChoices);
        changed |= DrawEnumCombo("Minification", sampler_info_.minFilter, kFilterChoices);
        changed |= DrawEnumCombo("Horizontal address", sampler_info_.addressModeU, kAddressChoices);
        changed |= DrawEnumCombo("Vertical address", sampler_info_.addressModeV, kAddressChoices);
        if (sampler_info_.addressModeU == vk::SamplerAddressMode::eClampToBorder ||
            sampler_info_.addressModeV == vk::SamplerAddressMode::eClampToBorder)
        {
            changed |= DrawEnumCombo("Border color", sampler_info_.borderColor, kBorderColorChoices);
        }
        ImGui::EndTable();
    }
    return changed;
}

}  // namespace klvk
