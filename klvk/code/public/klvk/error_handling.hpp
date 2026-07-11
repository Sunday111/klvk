#pragma once

#include <fmt/color.h>
#include <fmt/core.h>

#include <cpptrace/cpptrace.hpp>

namespace klvk
{

class ErrorHandling
{
public:
    template <typename Exception = cpptrace::runtime_error, typename... Args>
    static constexpr void Ensure(const bool condition, fmt::format_string<Args...> format, Args&&... args)
    {
        if (!condition)
        {
            throw Exception(fmt::format(format, std::forward<Args>(args)...));
        }
    }

    template <typename... Args>
    static auto RuntimeErrorWithMessage(fmt::format_string<Args...> format, Args&&... args)
    {
        return cpptrace::runtime_error(fmt::format(format, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void ThrowWithMessage(fmt::format_string<Args...> format, Args&&... args)
    {
        throw RuntimeErrorWithMessage(format, std::forward<Args>(args)...);
    }

    template <typename F, typename... Args>
    static int InvokeAndCatchAll(F&& f, Args&&... args)  // NOLINT
    {
        constexpr auto red_fg = fmt::fg(fmt::rgb(255, 0, 0));
        try
        {
            f(std::forward<Args>(args)...);
            return 0;
        }
        catch (const cpptrace::exception& exception)
        {
            fmt::print(red_fg, "Unhandled exception: {}\n", exception.message());
            exception.trace().print();
        }
        catch (const std::exception& exception)
        {
            fmt::print(red_fg, "Unhandled exception: {}\n", exception.what());
        }
        catch (...)
        {
            fmt::print(red_fg, "Unhandled exception of unknown type\n");
        }

        return 1;
    }
};
}  // namespace klvk
