#pragma once

#include "klvk/diagnostics/diagnostic_run_config.hpp"

namespace klvk
{

class Window;

class DiagnosticInputPlayer
{
public:
    explicit DiagnosticInputPlayer(Window& window, std::optional<edt::Vec2f> initial_cursor_position = std::nullopt);

    void Apply(const DiagnosticInputEvent& input);

private:
    void ApplyModifier(Key key);

    Window& window_;
    bool cursor_initialized_;
};

}  // namespace klvk
