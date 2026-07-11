#pragma once

#include <fmt/core.h>

#include <cassert>
#include <concepts>
#include <limits>

#include "EverydayTools/Concepts/Callable.hpp"
#include "imgui.h"

namespace klvk
{
class ImGuiHelper
{
public:
    template <std::unsigned_integral T>
    static bool SliderUInt(const char* name, T* ptr, const T min, const T max)
    {
        static_assert(sizeof(T) <= sizeof(size_t));
        [[maybe_unused]] constexpr auto maxint = static_cast<size_t>(std::numeric_limits<int>::lowest());
        assert(min < maxint && max <= maxint);
        int value = static_cast<int>(*ptr);
        if (ImGui::SliderInt(name, &value, static_cast<int>(min), static_cast<int>(max)))
        {
            *ptr = static_cast<T>(value);
            return true;
        }

        return false;
    }

    template <typename T, edt::Callable<T> Getter, edt::Callable<void, T> Setter>
        requires(std::floating_point<T> || std::integral<T>)
    static bool SliderGetterSetter(const char* text, const T& min, const T& max, Getter&& getter, Setter&& setter)
    {
        using Signature = bool (*)(const char*, T*, T, T);

        auto slider_fn = []() -> Signature
        {
            if constexpr (std::unsigned_integral<T>)
            {
                return SliderUInt;
            }
            else if constexpr (std::floating_point<T>)
            {
                return [](const char* label, float* v, float v_min, float v_max)
                {
                    return ImGui::SliderFloat(label, v, v_min, v_max);
                };
            }
            else if constexpr (std::same_as<T, int>)
            {
                return ImGui::SliderInt;
            }
            else
            {
                assert(false);
                return nullptr;
            }
        }();

        auto value = getter();
        if (slider_fn(text, &value, min, max))
        {
            setter(value);
            return true;
        }

        return false;
    }

    template <typename... Args>
    static void FormattedText(std::string& buffer, fmt::format_string<Args...> format, Args&&... args)
    {
        buffer.clear();
        fmt::format_to(std::back_inserter(buffer), format, std::forward<Args>(args)...);
        ImGui::Text("%s", buffer.data());  // NOLINT
    }
};
}  // namespace klvk
