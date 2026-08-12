#pragma once

#include <array>
#include <cassert>
#include <optional>
#include <utility>

#include "ass/enum/enum_as_index_magic_enum.hpp"
#include "ass/enum_map.hpp"
#include "magic_enum/magic_enum.hpp"

namespace klvk::detail
{

template <typename KlvkValue, typename VulkanValue>
class VulkanEnumConverter
{
public:
    using Enum = KlvkValue;
    using VkEnum = VulkanValue;

    consteval void Add(KlvkValue key, VulkanValue value)
    {
        if (to_vulkan_.Contains(key)) throw "Duplicate klvk enum value";
        for (size_t index = 0; index != size_; ++index)
        {
            if (from_vulkan_[index].first == value) throw "Duplicate Vulkan enum value";
        }

        to_vulkan_.GetOrAdd(key) = value;
        from_vulkan_[size_++] = {value, key};
    }

    [[nodiscard]] consteval VulkanEnumConverter Complete() &&
    {
        if (size_ != kSize) throw "Incomplete Vulkan enum mapping";
        return std::move(*this);
    }

    [[nodiscard]] constexpr VulkanValue ToVulkan(KlvkValue value) const noexcept
    {
        assert(to_vulkan_.Contains(value));
        return to_vulkan_.Get(value);
    }

    [[nodiscard]] constexpr std::optional<KlvkValue> FromVulkan(VulkanValue value) const noexcept
    {
        for (size_t index = 0; index != size_; ++index)
        {
            if (from_vulkan_[index].first == value) return from_vulkan_[index].second;
        }
        return std::nullopt;
    }

private:
    using IndexConverter = ass::EnumIndexConverter_MagicEnum<KlvkValue>;
    static constexpr size_t kSize = magic_enum::enum_count<KlvkValue>();

    ass::EnumMap<KlvkValue, VulkanValue, IndexConverter> to_vulkan_;
    std::array<std::pair<VulkanValue, KlvkValue>, kSize> from_vulkan_{};
    size_t size_ = 0;
};

}  // namespace klvk::detail
