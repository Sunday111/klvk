#include "klvk/vulkan/gpu_buffer.hpp"

#include <vk_mem_alloc.h>

#include <cstring>
#include <utility>

#include "klvk/error_handling.hpp"
#include "klvk/vulkan/device_context.hpp"

// Vulkan create-info structs are designed for partial designated initialization;
// unlisted fields must be zero.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

namespace klvk
{

GpuBuffer::GpuBuffer(DeviceContext& context, VkBufferUsageFlags usage, VkDeviceSize size, bool host_visible)
    : GpuBuffer(context, usage, size, host_visible ? GpuBufferHostAccess::SequentialWrite : GpuBufferHostAccess::None)
{
}

GpuBuffer::GpuBuffer(
    DeviceContext& context,
    VkBufferUsageFlags usage,
    VkDeviceSize size,
    GpuBufferHostAccess host_access)
    : context_(&context),
      size_(size),
      host_access_(host_access)
{
    const VkBufferCreateInfo buffer_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo allocation_info{.usage = VMA_MEMORY_USAGE_AUTO};
    if (host_access != GpuBufferHostAccess::None)
    {
        const VmaAllocationCreateFlags access_flag = host_access == GpuBufferHostAccess::SequentialWrite
                                                         ? VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                                                         : VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        allocation_info.flags = access_flag | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    VmaAllocationInfo result_info{};
    CheckVkResult(
        vmaCreateBuffer(context.GetAllocator(), &buffer_info, &allocation_info, &buffer_, &allocation_, &result_info),
        "vmaCreateBuffer");
    mapped_ = result_info.pMappedData;
}

GpuBuffer::GpuBuffer(GpuBuffer&& other) noexcept
{
    *this = std::move(other);
}

GpuBuffer& GpuBuffer::operator=(GpuBuffer&& other) noexcept
{
    if (this != &other)
    {
        Destroy();
        context_ = std::exchange(other.context_, nullptr);
        buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
        allocation_ = std::exchange(other.allocation_, VK_NULL_HANDLE);
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
        vmaDestroyBuffer(context_->GetAllocator(), buffer_, allocation_);
        buffer_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
        mapped_ = nullptr;
        size_ = 0;
        host_access_ = GpuBufferHostAccess::None;
    }
}

void GpuBuffer::Write(std::span<const std::byte> bytes, VkDeviceSize offset)
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
    CheckVkResult(
        vmaFlushAllocation(context_->GetAllocator(), allocation_, offset, bytes.size()),
        "vmaFlushAllocation");
}

void GpuBuffer::Read(std::span<std::byte> bytes, VkDeviceSize offset) const
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
    CheckVkResult(
        vmaInvalidateAllocation(context_->GetAllocator(), allocation_, offset, bytes.size()),
        "vmaInvalidateAllocation");
    std::memcpy(bytes.data(), static_cast<const std::byte*>(mapped_) + offset, bytes.size());
}

}  // namespace klvk
