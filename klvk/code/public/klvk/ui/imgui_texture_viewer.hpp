#pragma once

#include <imgui.h>

#include <string>
#include <string_view>
#include <utility>

#include "edt/math/matrix.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{

class DeviceContext;
class Texture;

class RegisteredImGuiTexture
{
public:
    RegisteredImGuiTexture(DeviceContext& context, const Texture& texture);
    RegisteredImGuiTexture(const RegisteredImGuiTexture&) = delete;
    RegisteredImGuiTexture(RegisteredImGuiTexture&&) = delete;
    ~RegisteredImGuiTexture();

    [[nodiscard]] ImTextureID GetId() const noexcept { return reinterpret_cast<ImTextureID>(descriptor_); }

private:
    DeviceContext* context_ = nullptr;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_ = VK_NULL_HANDLE;
};

class ImGuiTextureViewer
{
public:
    explicit ImGuiTextureViewer(std::string title) : title_{std::move(title)} {}

    void Draw(ImTextureID texture, edt::Vec2<u32> size, std::string_view description = {}, bool* open = nullptr);

private:
    std::string title_;
    float zoom_ = 1.f;
    bool fit_ = true;
};

}  // namespace klvk
