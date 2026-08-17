#include "diagnostics/diagnostic_framebuffer_readback.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <vector>

#include "diagnostic_test_support.hpp"
#include "edt/functional/on_scope_leave.hpp"

namespace klvk
{

class DiagnosticFramebufferReadbackTest
{
public:
    static void Run()
    {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path root =
            std::filesystem::temp_directory_path() / ("klvk_diagnostic_readback_test_" + std::to_string(nonce));
        std::filesystem::create_directories(root);
        auto cleanup = edt::OnScopeLeave([&] { std::filesystem::remove_all(root); });

        TestPpmChannels(root);
        TestCheckpoints();
    }

private:
    static std::string Read(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        tests::Ensure(stream.is_open(), "failed to open written diagnostic PPM");
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    static std::string Ppm(std::initializer_list<unsigned char> rgb)
    {
        std::string result = "P6\n2 1\n255\n";
        for (unsigned char channel : rgb) result.push_back(static_cast<char>(channel));
        return result;
    }

    static void TestPpmChannels(const std::filesystem::path& root)
    {
        const std::vector pixels{
            std::byte{1},
            std::byte{2},
            std::byte{3},
            std::byte{4},
            std::byte{250},
            std::byte{128},
            std::byte{0},
            std::byte{255},
        };
        const vk::Extent2D extent{2, 1};

        const std::filesystem::path rgba = root / "nested" / "rgba.ppm";
        DiagnosticFramebufferReadback::WritePpm(rgba, extent, pixels, false);
        tests::Ensure(Read(rgba) == Ppm({1, 2, 3, 250, 128, 0}), "RGBA PPM channels were reordered");

        const std::filesystem::path bgra = root / "bgra.ppm";
        DiagnosticFramebufferReadback::WritePpm(bgra, extent, pixels, true);
        tests::Ensure(Read(bgra) == Ppm({3, 2, 1, 0, 128, 250}), "BGRA PPM channels were not converted");
    }

    static void TestCheckpoints()
    {
        const std::vector first_pixels{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
        const std::vector changed_pixels{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{5}};
        const u64 expected_hash = DiagnosticFramebufferReadback::HashPixels(first_pixels);
        tests::Ensure(expected_hash == 13'725'386'680'924'731'485ULL, "checkpoint hash algorithm changed unexpectedly");

        const DiagnosticCheckpointConfig config{
            .every_frames = 2,
            .include_ui = false,
            .expected = {{.frame = 2, .hash = expected_hash}, {.frame = 4, .hash = expected_hash}}};
        DiagnosticFramebufferReadback readback(2, config);
        readback.RecordCheckpoint(2, first_pixels);
        readback.RecordCheckpoint(4, changed_pixels);
        readback.RecordCheckpoint(6, changed_pixels);

        tests::Ensure(
            readback.GetCheckpoints() ==
                std::vector<DiagnosticCheckpoint>{
                    {.frame = 2, .hash = expected_hash},
                    {.frame = 4, .hash = DiagnosticFramebufferReadback::HashPixels(changed_pixels)},
                    {.frame = 6, .hash = DiagnosticFramebufferReadback::HashPixels(changed_pixels)}},
            "recorded checkpoints were incomplete or reordered");
        tests::Ensure(
            readback.GetFirstDivergence() == readback.GetCheckpoints()[1],
            "the earliest checkpoint divergence was not retained");
        tests::EnsureThrows([&] { readback.EnsureComplete(); }, "a divergent framebuffer was accepted");

        DiagnosticFramebufferReadback matching(1, config);
        matching.RecordCheckpoint(2, first_pixels);
        matching.EnsureComplete();
    }
};

namespace tests
{

void RunDiagnosticFramebufferReadbackTests()
{
    DiagnosticFramebufferReadbackTest::Run();
}

}  // namespace tests
}  // namespace klvk
