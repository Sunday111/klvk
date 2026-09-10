#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "edt/functional/on_scope_leave.hpp"
#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/rendering/instanced_sprite_renderer_2d.hpp"
#include "klvk/vulkan/texture.hpp"

namespace
{

class SpriteBatchesApp : public klvk::Application
{
    void Initialize() override
    {
        Application::Initialize();
        SetClearColor({0.f, 0.f, 0.f, 1.f});
        const std::array<u8, 1> pixels{255};
        texture_ = klvk::Texture::CreateR8(GetDeviceContext(), {1, 1}, pixels);
        renderer_ = std::make_unique<klvk::InstancedSpriteRenderer2d>(*this, *texture_);
    }

    void Tick() override
    {
        Application::Tick();
        renderer_->Clear();
        renderer_->Render(edt::Mat3f::Identity());
        if (GetFrameNumber() == 3) return;

        renderer_->Add({-0.5f, 0.f}, {255, 0, 0, 255}, {0.2f, 0.2f});
        renderer_->Render(edt::Mat3f::Identity());
        renderer_->Clear();
        const size_t count = GetFrameNumber() < 4 ? 1 : 100;
        for (size_t index = 0; index != count; ++index)
        {
            renderer_->Add({0.5f, 0.f}, {0, 255, 0, 255}, {0.2f, 0.2f});
        }
        renderer_->Render(edt::Mat3f::Identity());
    }

    std::unique_ptr<klvk::Texture> texture_;
    std::unique_ptr<klvk::InstancedSpriteRenderer2d> renderer_;
};

void CheckCapture(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::string magic;
    edt::Vec2<size_t> size{};
    u32 maximum = 0;
    stream >> magic >> size.x() >> size.y() >> maximum;
    stream.get();
    klvk::ErrorHandling::Ensure(
        magic == "P6" && size == edt::Vec2<size_t>{320, 240} && maximum == 255,
        "invalid sprite batch capture");
    std::vector<u8> pixels(size.x() * size.y() * 3);
    stream.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    klvk::ErrorHandling::Ensure(static_cast<bool>(stream), "incomplete sprite batch capture");
    const auto check = [&](edt::Vec2<size_t> position, std::array<u8, 3> expected)
    {
        const size_t offset = (position.y() * size.x() + position.x()) * 3;
        for (size_t channel = 0; channel != expected.size(); ++channel)
        {
            klvk::ErrorHandling::Ensure(
                pixels[offset + channel] == expected[channel],
                "a recorded sprite batch was overwritten");
        }
    };
    check({80, 120}, {255, 0, 0});
    check({240, 120}, {0, 255, 0});
    check({160, 120}, {0, 0, 0});
}

}  // namespace

int main()
{
    return klvk::ErrorHandling::InvokeAndCatchAll(
        []
        {
            const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
            const auto root = std::filesystem::temp_directory_path() / ("klvk_sprite_batches_" + std::to_string(nonce));
            std::filesystem::create_directories(root);
            auto cleanup = edt::OnScopeLeave([&] { std::filesystem::remove_all(root); });
            nlohmann::json config = {
                {"version", 1},
                {"presentation", "offscreen"},
                {"framebuffer_size", {320, 240}},
                {"clock", {{"mode", "fixed"}, {"step_seconds", 1. / 60.}}},
                {"exit", {{"after_last_capture", true}}},
                {"captures", nlohmann::json::array()},
            };
            const std::array<u64, 5> frames{1, 2, 4, 5, 6};
            for (u64 frame : frames)
            {
                config["captures"].push_back({
                    {"frame", frame},
                    {"path", (root / (std::to_string(frame) + ".ppm")).string()},
                    {"include_ui", false},
                });
            }
            const auto config_path = root / "config.json";
            std::ofstream(config_path) << config;
            std::array<std::string, 3> arguments{"sprite_batches_test", "--klvk-diagnostics", config_path.string()};
            std::array<char*, 3> argv{arguments[0].data(), arguments[1].data(), arguments[2].data()};
            SpriteBatchesApp app;
            app.RunWithArguments(static_cast<int>(argv.size()), argv.data());
            for (u64 frame : frames) CheckCapture(root / (std::to_string(frame) + ".ppm"));
        });
}
