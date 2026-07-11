#pragma once

#include <volk.h>  // IWYU pragma: export

#include <cpptrace/cpptrace.hpp>
#include <string>
#include <string_view>

namespace klvk
{

// Returns the name of a VkResult value ("VK_ERROR_DEVICE_LOST") or "VkResult(unknown)" for unknown values.
[[nodiscard]] std::string_view VkResultToString(VkResult result);

class VulkanError : public cpptrace::runtime_error
{
public:
    VulkanError(VkResult result, std::string message, cpptrace::raw_trace&& trace = cpptrace::generate_raw_trace());

    [[nodiscard]] VkResult GetResult() const noexcept { return result_; }

private:
    VkResult result_;
};

// Throws VulkanError if result is not VK_SUCCESS. Context tells which operation failed.
void CheckVkResult(VkResult result, std::string_view context);

}  // namespace klvk
