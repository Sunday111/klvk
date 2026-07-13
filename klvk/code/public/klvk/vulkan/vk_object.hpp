#pragma once

#include <utility>

#include "klvk/vulkan/vulkan_api.hpp"

namespace klvk
{

// Maps a Vulkan handle type to the wrapper call that destroys it. Specialized
// per supported type below; instantiating VkObject with an unsupported handle
// is a compile error (the primary template is left undefined on purpose).
template <typename Handle>
struct VkObjectTraits;

#define KLVK_DEFINE_VK_OBJECT_TRAITS(HandleType, DestroyCall)             \
    template <>                                                           \
    struct VkObjectTraits<HandleType>                                     \
    {                                                                     \
        static void Destroy(VkDevice device, HandleType handle) noexcept  \
        {                                                                 \
            Vulkan::DestroyCall(device, handle);                          \
        }                                                                 \
    }

KLVK_DEFINE_VK_OBJECT_TRAITS(VkPipeline, DestroyPipelineNE);
KLVK_DEFINE_VK_OBJECT_TRAITS(VkPipelineLayout, DestroyPipelineLayoutNE);
KLVK_DEFINE_VK_OBJECT_TRAITS(VkDescriptorSetLayout, DestroyDescriptorSetLayoutNE);
KLVK_DEFINE_VK_OBJECT_TRAITS(VkDescriptorPool, DestroyDescriptorPoolNE);
KLVK_DEFINE_VK_OBJECT_TRAITS(VkSampler, DestroySamplerNE);
KLVK_DEFINE_VK_OBJECT_TRAITS(VkShaderModule, DestroyShaderModuleNE);
KLVK_DEFINE_VK_OBJECT_TRAITS(VkImageView, DestroyImageViewNE);

#undef KLVK_DEFINE_VK_OBJECT_TRAITS

// Move-only RAII owner of a single Vulkan handle, in the spirit of klgl's
// GlObject. Holds the device needed to destroy the handle and calls the matching
// wrapper on destruction, so an owner no longer needs a hand-written destructor
// that repeats DestroyX for each member.
//
// It deliberately does NOT wait for the device to go idle: destroying a resource
// still in use by an in-flight frame is undefined, so callers must ensure the
// device is idle before the owners are torn down. Application::Run() issues one
// WaitIdle after the main loop for exactly this reason, which lets applications
// keep their Vulkan objects as VkObject members and drop the teardown entirely.
template <typename Handle>
class VkObject
{
public:
    VkObject() = default;
    VkObject(VkDevice device, Handle handle) noexcept : device_(device), handle_(handle) {}
    VkObject(const VkObject&) = delete;
    VkObject& operator=(const VkObject&) = delete;

    VkObject(VkObject&& other) noexcept
        : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
          handle_(std::exchange(other.handle_, VK_NULL_HANDLE))
    {
    }

    VkObject& operator=(VkObject&& other) noexcept
    {
        if (this != &other)
        {
            Destroy();
            device_ = std::exchange(other.device_, VK_NULL_HANDLE);
            handle_ = std::exchange(other.handle_, VK_NULL_HANDLE);
        }
        return *this;
    }

    ~VkObject() { Destroy(); }

    [[nodiscard]] Handle GetHandle() const noexcept { return handle_; }

    // Implicit conversion so a VkObject drops straight into the Vulkan calls that
    // expect the raw handle (CmdBindPipeline, builder setters, and so on).
    operator Handle() const noexcept { return handle_; }  // NOLINT(google-explicit-constructor)

    [[nodiscard]] bool IsValid() const noexcept { return handle_ != VK_NULL_HANDLE; }

    // Gives up ownership without destroying and returns the raw handle.
    [[nodiscard]] Handle Release() noexcept
    {
        device_ = VK_NULL_HANDLE;
        return std::exchange(handle_, VK_NULL_HANDLE);
    }

    void Reset() noexcept { Destroy(); }

private:
    void Destroy() noexcept
    {
        if (handle_ != VK_NULL_HANDLE) VkObjectTraits<Handle>::Destroy(device_, handle_);
        handle_ = VK_NULL_HANDLE;
        device_ = VK_NULL_HANDLE;
    }

    VkDevice device_ = VK_NULL_HANDLE;
    Handle handle_ = VK_NULL_HANDLE;
};

}  // namespace klvk
