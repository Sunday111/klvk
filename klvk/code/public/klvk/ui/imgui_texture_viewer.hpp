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
        vk::SamplerCreateInfo sampler_info = RegisteredImGuiTexture::DefaultSamplerCreateInfo())
        : title_{std::move(title)},
          sampler_info_{sampler_info}
    {
    }
    ~ImGuiTextureViewer();

    void Draw(
        DeviceContext& context,
        vk::ImageView image_view,
        edt::Vec2<u32> size,
        std::string_view description = {},
        bool* open = nullptr);

private:
    void RegisterTexture(DeviceContext& context, vk::ImageView image_view);
    [[nodiscard]] bool DrawSamplerControls();

    std::string title_;
    vk::SamplerCreateInfo sampler_info_{};
    DeviceContext* registered_context_ = nullptr;
    vk::ImageView registered_view_ = nullptr;
    std::unique_ptr<RegisteredImGuiTexture> registered_texture_;
    float zoom_ = 1.f;
    bool fit_ = true;
};

}  // namespace klvk
