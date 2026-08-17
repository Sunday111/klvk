#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace klvk::tests
{

inline void Ensure(bool condition, std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

template <typename Function>
void EnsureThrows(Function&& function, std::string_view message)
{
    try
    {
        function();
    }
    catch (const std::exception&)
    {
        return;
    }
    throw std::runtime_error(std::string(message));
}

void RunDiagnosticFramebufferReadbackTests();
void RunDiagnosticInputPlayerTests();

}  // namespace klvk::tests
