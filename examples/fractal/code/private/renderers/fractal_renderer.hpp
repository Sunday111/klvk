#pragma once

#include "klvk/vulkan/vulkan_common.hpp"

class FractalSettings;

class FractalRenderer
{
public:
    virtual ~FractalRenderer() noexcept = default;

    // Runs before the swapchain render pass: uploads, clears and compute dispatches.
    virtual void PrepareFrame(VkCommandBuffer, const FractalSettings&) {}

    // Runs inside the swapchain render pass.
    virtual void Render(VkCommandBuffer, const FractalSettings&) = 0;

    virtual void ApplySettings(const FractalSettings&) = 0;
};
