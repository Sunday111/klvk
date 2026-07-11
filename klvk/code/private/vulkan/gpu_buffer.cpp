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
    : context_(&context),
      size_(size)
{
    const VkBufferCreateInfo buffer_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo allocation_info{.usage = VMA_MEMORY_USAGE_AUTO};
    if (host_visible)
    {
        allocation_info.flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    VmaAllocationInfo result_info{};
    CheckVkResult(
        vmaCreateBuffer(
            context.GetAllocator(),
            &buffer_info,
            &allocation_info,
            &buffer_,
            &allocation_,
            &result_info),
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
    }
}

void GpuBuffer::Write(std::span<const std::byte> bytes, VkDeviceSize offset)
{
    ErrorHandling::Ensure(mapped_ != nullptr, "Writing to a buffer that is not host visible");
    ErrorHandling::Ensure(
        offset + bytes.size() <= size_,
        "Buffer overflow: writing {} bytes at offset {} into a buffer of {} bytes",
        bytes.size(),
        offset,
        size_);
    std::memcpy(static_cast<std::byte*>(mapped_) + offset, bytes.data(), bytes.size());
}

}  // namespace klvk
