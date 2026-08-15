#pragma once

#include <cpptrace/cpptrace.hpp>
#include <expected>
#include <string>
#include <string_view>

#include "klvk/vulkan/vulkan.hpp"

namespace klvk
{

class VulkanError : public cpptrace::runtime_error
{
public:
    VulkanError(vk::Result result, std::string context, cpptrace::raw_trace&& trace = cpptrace::generate_raw_trace());

    [[nodiscard]] vk::Result GetResult() const noexcept { return result_; }
    [[nodiscard]] std::string_view GetContext() const noexcept { return context_; }

private:
    vk::Result result_;
    std::string context_;
};

void VulkanCheck(vk::Result result, std::string_view context);

template <typename T>
[[nodiscard]] T VulkanValue(std::expected<T, vk::Result>&& result, std::string_view context)
{
    if (!result.has_value()) [[unlikely]]
    {
        throw VulkanError(result.error(), std::string{context}, cpptrace::generate_raw_trace(1));
    }
    return std::move(result).value();
}

inline void VulkanValue(std::expected<void, vk::Result>&& result, std::string_view context)
{
    if (!result.has_value()) [[unlikely]]
    {
        throw VulkanError(result.error(), std::string{context}, cpptrace::generate_raw_trace(1));
    }
}

void InitializeVulkanDispatcher();
void InitializeVulkanDispatcher(vk::Instance instance);
void InitializeVulkanDispatcher(vk::Device device);

}  // namespace klvk
