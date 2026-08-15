#pragma once

#include <utility>

#include "klvk/vulkan/vulkan.hpp"

namespace klvk
{

template <typename Handle>
class VulkanObject
{
public:
    VulkanObject() = default;
    VulkanObject(vk::Device device, Handle handle) noexcept : device_(device), handle_(handle) {}
    VulkanObject(const VulkanObject&) = delete;
    VulkanObject& operator=(const VulkanObject&) = delete;

    VulkanObject(VulkanObject&& other) noexcept
        : device_(std::exchange(other.device_, nullptr)),
          handle_(std::exchange(other.handle_, nullptr))
    {
    }

    VulkanObject& operator=(VulkanObject&& other) noexcept
    {
        if (this != &other)
        {
            Destroy();
            device_ = std::exchange(other.device_, nullptr);
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    ~VulkanObject() { Destroy(); }

    [[nodiscard]] Handle GetHandle() const noexcept { return handle_; }

    operator Handle() const noexcept { return handle_; }  // NOLINT(google-explicit-constructor)

    [[nodiscard]] bool IsValid() const noexcept { return handle_ != nullptr; }

    [[nodiscard]] Handle Release() noexcept
    {
        device_ = nullptr;
        return std::exchange(handle_, nullptr);
    }

    void Reset() noexcept { Destroy(); }

private:
    void Destroy() noexcept
    {
        if (handle_ != nullptr)
        {
            device_.destroy(handle_);
        }
        handle_ = nullptr;
        device_ = nullptr;
    }

    vk::Device device_;
    Handle handle_;
};

}  // namespace klvk
