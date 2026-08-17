#include "klvk/vulkan/gpu_buffer.hpp"

#include <vk_mem_alloc.h>

#include <cstring>
#include <utility>

#include "klvk/error_handling.hpp"
#include "klvk/vulkan/device_context.hpp"

namespace klvk
{

GpuBuffer::GpuBuffer(DeviceContext& context, vk::BufferUsageFlags usage, vk::DeviceSize size, bool host_visible)
    : GpuBuffer(context, usage, size, host_visible ? GpuBufferHostAccess::SequentialWrite : GpuBufferHostAccess::None)
{
}

GpuBuffer::GpuBuffer(
    DeviceContext& context,
    vk::BufferUsageFlags usage,
    vk::DeviceSize size,
    GpuBufferHostAccess host_access)
    : allocator_(context.GetAllocator()),
      size_(size),
      host_access_(host_access)
{
    const auto buffer_info =
        vk::BufferCreateInfo{}.setSize(size).setUsage(usage).setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocation_info{.usage = VMA_MEMORY_USAGE_AUTO};
    if (host_access != GpuBufferHostAccess::None)
    {
        const VmaAllocationCreateFlags access_flag = host_access == GpuBufferHostAccess::SequentialWrite
                                                         ? VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                                                         : VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        allocation_info.flags = access_flag | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    const VkBufferCreateInfo& raw_buffer_info = buffer_info;
    VmaAllocationInfo result_info{};
    VkBuffer buffer = VK_NULL_HANDLE;
    VulkanCheck(
        static_cast<vk::Result>(vmaCreateBuffer(
            context.GetAllocator(),
            &raw_buffer_info,
            &allocation_info,
            &buffer,
            &allocation_,
            &result_info)));
    buffer_ = vk::Buffer{buffer};
    mapped_ = result_info.pMappedData;
}

GpuBuffer::GpuBuffer(GpuBuffer&& other) noexcept
    : allocator_(std::exchange(other.allocator_, nullptr)),
      buffer_(std::exchange(other.buffer_, nullptr)),
      allocation_(std::exchange(other.allocation_, nullptr)),
      size_(std::exchange(other.size_, 0)),
      mapped_(std::exchange(other.mapped_, nullptr)),
      host_access_(std::exchange(other.host_access_, GpuBufferHostAccess::None))
{
}

GpuBuffer& GpuBuffer::operator=(GpuBuffer&& other) noexcept
{
    if (this != &other)
    {
        Destroy();
        allocator_ = std::exchange(other.allocator_, nullptr);
        buffer_ = std::exchange(other.buffer_, nullptr);
        allocation_ = std::exchange(other.allocation_, nullptr);
        size_ = std::exchange(other.size_, 0);
        mapped_ = std::exchange(other.mapped_, nullptr);
        host_access_ = std::exchange(other.host_access_, GpuBufferHostAccess::None);
    }
    return *this;
}

GpuBuffer::~GpuBuffer()
{
    Destroy();
}

void GpuBuffer::Destroy()
{
    if (buffer_)
    {
        vmaDestroyBuffer(allocator_, static_cast<VkBuffer>(buffer_), allocation_);
        allocator_ = nullptr;
        buffer_ = nullptr;
        allocation_ = nullptr;
        mapped_ = nullptr;
        size_ = 0;
        host_access_ = GpuBufferHostAccess::None;
    }
}

void GpuBuffer::Write(std::span<const std::byte> bytes, vk::DeviceSize offset)
{
    ErrorHandling::Ensure(
        mapped_ != nullptr && host_access_ != GpuBufferHostAccess::None,
        "Writing to a buffer that is not host visible");
    ErrorHandling::Ensure(
        offset <= size_ && bytes.size() <= size_ - offset,
        "Buffer overflow: writing {} bytes at offset {} into a buffer of {} bytes",
        bytes.size(),
        offset,
        size_);
    if (bytes.empty()) return;
    std::memcpy(static_cast<std::byte*>(mapped_) + offset, bytes.data(), bytes.size());
    VulkanCheck(static_cast<vk::Result>(vmaFlushAllocation(allocator_, allocation_, offset, bytes.size())));
}

void GpuBuffer::Read(std::span<std::byte> bytes, vk::DeviceSize offset) const
{
    ErrorHandling::Ensure(
        mapped_ != nullptr && host_access_ == GpuBufferHostAccess::Random,
        "Reading from a buffer that was not created for random host access");
    ErrorHandling::Ensure(
        offset <= size_ && bytes.size() <= size_ - offset,
        "Buffer overflow: reading {} bytes at offset {} from a buffer of {} bytes",
        bytes.size(),
        offset,
        size_);
    if (bytes.empty()) return;
    VulkanCheck(static_cast<vk::Result>(vmaInvalidateAllocation(allocator_, allocation_, offset, bytes.size())));
    std::memcpy(bytes.data(), static_cast<const std::byte*>(mapped_) + offset, bytes.size());
}

}  // namespace klvk
