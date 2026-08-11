#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "edt/math/matrix.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/ui/registered_imgui_texture.hpp"

namespace klvk
{

class ImGuiTextureViewer
{
public:
    explicit ImGuiTextureViewer(
        std::string title,
        VkSamplerCreateInfo sampler_info = RegisteredImGuiTexture::DefaultSamplerCreateInfo())
        : title_{std::move(title)},
          sampler_info_{sampler_info}
    {
    }
    ~ImGuiTextureViewer();

    void Draw(
        DeviceContext& context,
        VkImageView image_view,
        edt::Vec2<u32> size,
        std::string_view description = {},
        bool* open = nullptr);

private:
    void RegisterTexture(DeviceContext& context, VkImageView image_view);
    [[nodiscard]] bool DrawSamplerControls();

    std::string title_;
    VkSamplerCreateInfo sampler_info_{};
    DeviceContext* registered_context_ = nullptr;
    VkImageView registered_view_ = VK_NULL_HANDLE;
    std::unique_ptr<RegisteredImGuiTexture> registered_texture_;
    float zoom_ = 1.f;
    bool fit_ = true;
};

}  // namespace klvk
