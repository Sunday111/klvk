#include "klvk/ui/imgui_texture_viewer.hpp"

#include <backends/imgui_impl_vulkan.h>

#include <algorithm>
#include <cmath>
#include <optional>

#include "klvk/ui/imgui_helpers.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/texture.hpp"
#include "klvk/vulkan/vulkan_api.hpp"

namespace klvk
{
namespace
{
constexpr float kMinimumZoom = 0.01f;
constexpr float kMaximumZoom = 16.f;
constexpr float kWheelZoomStep = 1.2f;
}  // namespace

RegisteredImGuiTexture::RegisteredImGuiTexture(DeviceContext& context, const Texture& texture) : context_{&context}
{
    const VkSamplerCreateInfo sampler_info{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
    };
    sampler_ = Vulkan::CreateSampler(context.GetDevice(), sampler_info);
    descriptor_ = ImGui_ImplVulkan_AddTexture(sampler_, texture.GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

RegisteredImGuiTexture::~RegisteredImGuiTexture()
{
    if (descriptor_ != VK_NULL_HANDLE) ImGui_ImplVulkan_RemoveTexture(descriptor_);
    if (sampler_ != VK_NULL_HANDLE) Vulkan::DestroySamplerNE(context_->GetDevice(), sampler_);
}

void ImGuiTextureViewer::Draw(ImTextureID texture, edt::Vec2<u32> size, std::string_view description, bool* open)
{
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
        ImGui::Image(texture, display_size, {}, {1.f, 1.f});

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

}  // namespace klvk
