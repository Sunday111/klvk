#pragma once

#include <span>

#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

VK_DEFINE_HANDLE(VmaAllocation)
VK_DEFINE_HANDLE(VmaAllocator)

namespace klvk
{

class DeviceContext;

enum class GpuBufferHostAccess : u8
{
    None,
    SequentialWrite,
    Random,
};

// A buffer with its VMA allocation. Host-visible buffers stay persistently mapped.
class GpuBuffer
{
public:
    GpuBuffer() = default;
    GpuBuffer(DeviceContext& context, vk::BufferUsageFlags usage, vk::DeviceSize size, bool host_visible);
    GpuBuffer(DeviceContext& context, vk::BufferUsageFlags usage, vk::DeviceSize size, GpuBufferHostAccess host_access);
    GpuBuffer(const GpuBuffer&) = delete;
    GpuBuffer(GpuBuffer&& other) noexcept;
    GpuBuffer& operator=(GpuBuffer&& other) noexcept;
    ~GpuBuffer();

    [[nodiscard]] bool IsValid() const noexcept { return buffer_ != nullptr; }
    [[nodiscard]] vk::Buffer GetHandle() const noexcept { return buffer_; }
    [[nodiscard]] vk::DeviceSize GetSize() const noexcept { return size_; }

    void Write(std::span<const std::byte> bytes, vk::DeviceSize offset = 0);
    void Read(std::span<std::byte> bytes, vk::DeviceSize offset = 0) const;

private:
    void Destroy();

    VmaAllocator allocator_ = nullptr;
    vk::Buffer buffer_ = nullptr;
    VmaAllocation allocation_ = nullptr;
    vk::DeviceSize size_ = 0;
    void* mapped_ = nullptr;
    GpuBufferHostAccess host_access_ = GpuBufferHostAccess::None;
};

}  // namespace klvk
