#include <fmt/core.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "klvk/diagnostics/diagnostic_run_config.hpp"
#include "klvk/integral_aliases.hpp"

namespace
{

void Ensure(bool condition, std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

void Write(const std::filesystem::path& path, std::string_view contents)
{
    std::ofstream stream(path, std::ios::trunc);
    Ensure(stream.is_open(), "failed to open diagnostic test file");
    stream << contents;
    Ensure(stream.good(), "failed to write diagnostic test file");
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

void Run()
{
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("klvk_diagnostic_config_test_" + std::to_string(nonce));
    std::filesystem::create_directories(root);
    const std::filesystem::path valid_path = root / "valid.json";
    Write(
        valid_path,
        R"({
            "version": 1,
            "presentation": "hidden",
            "framebuffer_size": [320, 240],
            "clock": {"mode": "fixed", "step_seconds": 0.0166666667},
            "captures": [
                {"frame": 2, "path": "captures/test.ppm", "include_ui": false},
                {"time_seconds": 0.25, "path": "captures/at-time-1.ppm"},
                {"time_seconds": 0.5, "path": "captures/at-time-2.ppm"}
            ],
            "exit": {"after_last_capture": true},
            "application": {"seed": 7}
        })");
    const klvk::DiagnosticRunConfig config = klvk::LoadDiagnosticRunConfig(valid_path);
    Ensure(config.presentation == klvk::DiagnosticPresentation::Hidden, "presentation was not parsed");
    Ensure(config.framebuffer_size == edt::Vec2<u32>{320, 240}, "framebuffer size was not parsed");
    Ensure(config.clock.fixed_step_seconds.has_value(), "fixed clock was not parsed");
    Ensure(config.captures.size() == 3 && config.captures.front().frame == 2, "captures were not parsed");
    Ensure(!config.captures.front().include_ui, "include_ui was not parsed");
    Ensure(config.captures[1].time_seconds == 0.25, "first time-point capture was not parsed");
    Ensure(config.captures[2].time_seconds == 0.5, "second time-point capture was not parsed");
    Ensure(config.exit.after_last_capture, "exit condition was not parsed");
    Ensure(config.application.at("seed") == 7, "application configuration was not preserved");

    const std::filesystem::path offscreen_path = root / "offscreen.json";
    Write(offscreen_path, R"({"version":1,"presentation":"offscreen","framebuffer_size":[64,48],"exit":{"frame":1}})");
    const klvk::DiagnosticRunConfig offscreen = klvk::LoadDiagnosticRunConfig(offscreen_path);
    Ensure(offscreen.presentation == klvk::DiagnosticPresentation::Offscreen, "offscreen presentation was not parsed");

    const std::array arguments{
        std::string_view{"--application-option"},
        std::string_view{"value"},
        std::string_view{"--klvk-diagnostics"},
        std::string_view{valid_path.native()}};
    Ensure(
        klvk::LoadDiagnosticRunConfigFromArguments(arguments).has_value(),
        "diagnostic command-line option was not found");
    Ensure(
        !klvk::LoadDiagnosticRunConfigFromArguments(std::span{arguments}.first(2)).has_value(),
        "application arguments were incorrectly treated as diagnostic arguments");

    const std::array invalid_documents{
        R"({"version":1,"framebuffer_size":[1,1],"captures":[{"frame":1,"time_seconds":0,"path":"a.ppm"}],"exit":{"after_last_capture":true}})",
        R"({"version":1,"captures":[{"frame":1,"path":"a.ppm"}],"exit":{"after_last_capture":true}})",
        R"({"version":1,"framebuffer_size":[1,1],"captures":[{"frame":1,"path":"a.ppm"},{"frame":2,"path":"a.ppm"}],"exit":{"after_last_capture":true}})",
        R"({"version":1,"unknown":true,"exit":{"frame":1}})",
        R"({"version":1,"presentation":"offscreen","exit":{"frame":1}})",
        R"({"version":1,"presentation":"invalid","exit":{"frame":1}})",
        R"({"version":1,"version":1,"exit":{"frame":1}})",
        R"({"version":18446744073709551615,"exit":{"frame":1}})",
        R"({"version":1,"clock":{"mode":"fixed","step_seconds":1e-300},"exit":{"frame":1}})",
        R"({"version":1,"clock":{"mode":"fixed","step_seconds":1e300},"exit":{"frame":1}})",
        R"({"version":1,"framebuffer_size":[2147483647,2147483647],"captures":[{"frame":1,"path":"a.ppm"}],"exit":{"after_last_capture":true}})",
        R"({"version":1,"framebuffer_size":[1,1],"captures":[{"frame":0,"path":"a.ppm"}],"exit":{"after_last_capture":true}})",
        R"({"version":1,"exit":{}})"};
    for (size_t index = 0; index != invalid_documents.size(); ++index)
    {
        const std::filesystem::path path = root / ("invalid_" + std::to_string(index) + ".json");
        Write(path, invalid_documents[index]);
        EnsureThrows([&] { (void)klvk::LoadDiagnosticRunConfig(path); }, "invalid document was accepted");
    }
    std::filesystem::remove_all(root);
}

}  // namespace

int main()
{
    try
    {
        Run();
        fmt::println("diagnostic configuration tests passed");
        return 0;
    }
    catch (const std::exception& exception)
    {
        fmt::println(stderr, "{}", exception.what());
        return 1;
    }
}
