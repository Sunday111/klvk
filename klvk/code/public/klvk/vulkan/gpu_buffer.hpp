#pragma once

#include <span>

#include "klvk/vulkan/vulkan_common.hpp"

VK_DEFINE_HANDLE(VmaAllocation)

namespace klvk
{

class DeviceContext;

// A buffer with its VMA allocation. Host-visible buffers stay persistently mapped.
class GpuBuffer
{
public:
    GpuBuffer() = default;
    GpuBuffer(DeviceContext& context, VkBufferUsageFlags usage, VkDeviceSize size, bool host_visible);
    GpuBuffer(const GpuBuffer&) = delete;
    GpuBuffer(GpuBuffer&& other) noexcept;
    GpuBuffer& operator=(GpuBuffer&& other) noexcept;
    ~GpuBuffer();

    [[nodiscard]] bool IsValid() const noexcept { return buffer_ != VK_NULL_HANDLE; }
    [[nodiscard]] VkBuffer GetHandle() const noexcept { return buffer_; }
    [[nodiscard]] VkDeviceSize GetSize() const noexcept { return size_; }

    void Write(std::span<const std::byte> bytes, VkDeviceSize offset = 0);

private:
    void Destroy();

    DeviceContext* context_ = nullptr;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
    void* mapped_ = nullptr;
};

}  // namespace klvk
