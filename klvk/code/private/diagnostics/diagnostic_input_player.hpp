#pragma once

#include "klvk/diagnostics/diagnostic_run_config.hpp"

namespace klvk
{

class Window;

class DiagnosticInputPlayer
{
public:
    explicit DiagnosticInputPlayer(Window& window) noexcept;

    void Apply(const DiagnosticInputEvent& input);

private:
    void ApplyModifier(Key key);

    Window& window_;
};

}  // namespace klvk
