#include <imgui.h>

#include <EverydayTools/Math/Math.hpp>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/rendering/instanced_sprite_renderer_2d.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/texture.hpp"
#include "klvk/window.hpp"

namespace
{

using namespace edt::lazy_matrix_aliases;  // NOLINT

class Painter2dApp : public klvk::Application
{
    void Initialize() override
    {
        klvk::Application::Initialize();
        SetClearColor({});
        GetWindow().SetSize(1000, 1000);
        GetWindow().SetTitle("Painter 2d");

        constexpr std::array<uint8_t, 1> white{255};
        white_texture_ = klvk::Texture::CreateR8(GetDeviceContext(), {1, 1}, white);

        constexpr uint32_t texture_size = 64;
        std::array<uint8_t, texture_size * texture_size> circle{};
        for (uint32_t y = 0; y != texture_size; ++y)
        {
            for (uint32_t x = 0; x != texture_size; ++x)
            {
                const float dx = (static_cast<float>(x) + 0.5f) / static_cast<float>(texture_size) * 2.f - 1.f;
                const float dy = (static_cast<float>(y) + 0.5f) / static_cast<float>(texture_size) * 2.f - 1.f;
                const float distance = std::sqrt(dx * dx + dy * dy);
                circle[y * texture_size + x] =
                    static_cast<uint8_t>(std::clamp((1.02f - distance) * 2550.f, 0.f, 255.f));
            }
        }
        circle_texture_ = klvk::Texture::CreateR8(GetDeviceContext(), {texture_size, texture_size}, circle);
        shape_renderer_ = std::make_unique<klvk::InstancedSpriteRenderer2d>(*this, *white_texture_);
        circle_renderer_ = std::make_unique<klvk::InstancedSpriteRenderer2d>(*this, *circle_texture_);
    }

    static void AddLine(klvk::InstancedSpriteRenderer2d& renderer, Vec2f a, Vec2f b, float width, Vec4u8 color)
    {
        const Vec2f delta = b - a;
        renderer.Add((a + b) * 0.5f, color, {delta.Length() * 0.5f, width * 0.5f}, std::atan2(delta.y(), delta.x()));
    }

    static void
    AddRectOutline(klvk::InstancedSpriteRenderer2d& renderer, Vec2f center, Vec2f half_size, float width, Vec4u8 color)
    {
        AddLine(
            renderer,
            center + Vec2f{-half_size.x(), -half_size.y()},
            center + Vec2f{half_size.x(), -half_size.y()},
            width,
            color);
        AddLine(
            renderer,
            center + Vec2f{half_size.x(), -half_size.y()},
            center + Vec2f{half_size.x(), half_size.y()},
            width,
            color);
        AddLine(
            renderer,
            center + Vec2f{half_size.x(), half_size.y()},
            center + Vec2f{-half_size.x(), half_size.y()},
            width,
            color);
        AddLine(
            renderer,
            center + Vec2f{-half_size.x(), half_size.y()},
            center + Vec2f{-half_size.x(), -half_size.y()},
            width,
            color);
    }

    void Tick() override
    {
        klvk::Application::Tick();
        constexpr Vec4u8 red{230, 50, 55, 255};
        constexpr Vec4u8 green{40, 220, 100, 255};
        constexpr Vec4u8 yellow{255, 220, 40, 255};
        constexpr Vec4u8 white{255, 255, 255, 255};

        shape_renderer_->Clear();
        circle_renderer_->Clear();

        shape_renderer_->Add({}, red, {0.5f, 0.5f});
        AddRectOutline(*shape_renderer_, {}, {0.5f, 0.5f}, outline_width_, white);
        circle_renderer_->Add({-0.22f, 0.2f}, green, {0.11f, 0.11f});
        circle_renderer_->Add({0.22f, 0.2f}, green, {0.11f, 0.11f});
        AddLine(*shape_renderer_, {-0.3f, -0.2f}, {0.3f, -0.2f}, 0.11f, green);

        // Rotated rectangles form the ears and exercise the painter's oriented primitives.
        shape_renderer_->Add({-0.38f, 0.55f}, yellow, {0.18f, 0.055f}, 0.55f);
        shape_renderer_->Add({0.38f, 0.55f}, yellow, {0.18f, 0.055f}, -0.55f);
        AddLine(*shape_renderer_, line_a_, line_b_, line_width_, white);

        const Mat3f identity = Mat3f::Identity();
        shape_renderer_->Render(identity);
        circle_renderer_->Render(identity);

        ImGui::Begin("Painter controls");
        ImGui::SliderFloat2("Line a", &line_a_.x(), -1.f, 1.f);
        ImGui::SliderFloat2("Line b", &line_b_.x(), -1.f, 1.f);
        ImGui::SliderFloat("Line width", &line_width_, 0.005f, 0.2f);
        ImGui::SliderFloat("Outline width", &outline_width_, 0.005f, 0.1f);
        ImGui::Text("Primitives: %zu", shape_renderer_->GetInstanceCount() + circle_renderer_->GetInstanceCount());
        ImGui::End();
    }

public:
    ~Painter2dApp() override
    {
        if (shape_renderer_) GetDeviceContext().WaitIdle();
    }

private:
    std::unique_ptr<klvk::Texture> white_texture_;
    std::unique_ptr<klvk::Texture> circle_texture_;
    std::unique_ptr<klvk::InstancedSpriteRenderer2d> shape_renderer_;
    std::unique_ptr<klvk::InstancedSpriteRenderer2d> circle_renderer_;
    Vec2f line_a_{-0.8f, -0.75f};
    Vec2f line_b_{0.8f, -0.55f};
    float line_width_ = 0.04f;
    float outline_width_ = 0.02f;
};

void Main()
{
    Painter2dApp app;
    app.Run();
}

}  // namespace

int main()
{
    klvk::ErrorHandling::InvokeAndCatchAll(Main);
    return 0;
}
