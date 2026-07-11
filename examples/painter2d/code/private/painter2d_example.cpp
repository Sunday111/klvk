#include <imgui.h>

#include <EverydayTools/Math/Math.hpp>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/rendering/instanced_sprite_renderer_2d.hpp"
#include "klvk/texture/procedural_texture_generator.hpp"
#include "klvk/ui/simple_type_widget.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/texture.hpp"
#include "klvk/window.hpp"

namespace
{

using namespace edt::lazy_matrix_aliases;  // NOLINT

// Draws the same face scene as klgl's painter2d example. There is no Painter2d
// class in klvk yet, so every primitive is a masked sprite: solid shapes use a
// white texture, circles and triangles use mask textures.
class Painter2dApp : public klvk::Application
{
    static constexpr uint32_t kMaskSize = 256;

    void Initialize() override
    {
        klvk::Application::Initialize();
        SetClearColor({});
        GetWindow().SetSize(1000, 1000);
        GetWindow().SetTitle("Painter 2d");

        constexpr std::array<uint8_t, 1> white{255};
        white_texture_ = klvk::Texture::CreateR8(GetDeviceContext(), {1, 1}, white);

        std::array<uint8_t, kMaskSize * kMaskSize> mask{};
        auto fill_mask = [&](auto&& sample)
        {
            for (uint32_t y = 0; y != kMaskSize; ++y)
            {
                for (uint32_t x = 0; x != kMaskSize; ++x)
                {
                    const Vec2f uv{
                        (static_cast<float>(x) + 0.5f) / kMaskSize * 2.f - 1.f,
                        (static_cast<float>(y) + 0.5f) / kMaskSize * 2.f - 1.f,
                    };
                    mask[y * kMaskSize + x] = sample(uv);
                }
            }
        };

        fill_mask(
            [](const Vec2f& uv)
            { return static_cast<uint8_t>(std::clamp((1.02f - uv.Length()) * 2550.f, 0.f, 255.f)); });
        circle_texture_ = klvk::Texture::CreateR8(GetDeviceContext(), {kMaskSize, kMaskSize}, mask);

        // Ear triangle in quad space: base along the bottom, apex at (1/3, 1),
        // matching the proportions of klgl's FillTriangle ears.
        fill_mask(
            [](const Vec2f& uv)
            {
                constexpr float apex_x = 1.f / 3.f;
                // Inside when right of the left edge (-1,-1)->(apex,1) and left of the right edge (1,-1)->(apex,1).
                const float left = (uv.x() + 1.f) * 2.f - (uv.y() + 1.f) * (apex_x + 1.f);
                const float right = (1.f - uv.x()) * 2.f - (uv.y() + 1.f) * (1.f - apex_x);
                return static_cast<uint8_t>((left >= 0.f && right >= 0.f) ? 255 : 0);
            });
        left_ear_texture_ = klvk::Texture::CreateR8(GetDeviceContext(), {kMaskSize, kMaskSize}, mask);
        klvk::ProceduralTextureGenerator::MirrorX({kMaskSize, kMaskSize}, mask);
        right_ear_texture_ = klvk::Texture::CreateR8(GetDeviceContext(), {kMaskSize, kMaskSize}, mask);

        shape_renderer_ = std::make_unique<klvk::InstancedSpriteRenderer2d>(*this, *white_texture_);
        circle_renderer_ = std::make_unique<klvk::InstancedSpriteRenderer2d>(*this, *circle_texture_);
        left_ear_renderer_ = std::make_unique<klvk::InstancedSpriteRenderer2d>(*this, *left_ear_texture_);
        right_ear_renderer_ = std::make_unique<klvk::InstancedSpriteRenderer2d>(*this, *right_ear_texture_);
    }

    static void AddLine(klvk::InstancedSpriteRenderer2d& renderer, Vec2f a, Vec2f b, float width, Vec4u8 color)
    {
        const Vec2f delta = b - a;
        renderer.Add((a + b) * 0.5f, color, {delta.Length() * 0.5f, width * 0.5f}, std::atan2(delta.y(), delta.x()));
    }

    static void
    AddRectOutline(klvk::InstancedSpriteRenderer2d& renderer, Vec2f center, Vec2f half_size, float width, Vec4u8 color)
    {
        const float x = half_size.x();
        const float y = half_size.y();
        AddLine(renderer, center + Vec2f{-x, -y}, center + Vec2f{x, -y}, width, color);
        AddLine(renderer, center + Vec2f{x, -y}, center + Vec2f{x, y}, width, color);
        AddLine(renderer, center + Vec2f{x, y}, center + Vec2f{-x, y}, width, color);
        AddLine(renderer, center + Vec2f{-x, y}, center + Vec2f{-x, -y}, width, color);
    }

    void Tick() override
    {
        klvk::Application::Tick();
        constexpr Vec4u8 red{255, 0, 0, 255};
        constexpr Vec4u8 green{0, 255, 0, 255};
        constexpr Vec4u8 white{255, 255, 255, 255};

        shape_renderer_->Clear();
        circle_renderer_->Clear();
        left_ear_renderer_->Clear();
        right_ear_renderer_->Clear();

        shape_renderer_->Add({}, red, {0.5f, 0.5f});
        AddRectOutline(*shape_renderer_, {}, {0.5f, 0.5f}, 0.02f, white);
        circle_renderer_->Add({-0.3f, 0.3f}, green, {0.1f, 0.1f});
        circle_renderer_->Add({0.3f, 0.3f}, green, {0.1f, 0.1f});
        circle_renderer_->Add({0.f, -0.25f}, green, {0.4f, 0.1f});
        left_ear_renderer_->Add({-0.35f, 0.55f}, red, {0.15f, 0.05f});
        right_ear_renderer_->Add({0.35f, 0.55f}, red, {0.15f, 0.05f});
        AddLine(*shape_renderer_, triangle_a_, triangle_b_, triangle_line_width_ * 2, white);
        AddLine(*shape_renderer_, triangle_b_, triangle_c_, triangle_line_width_ * 2, white);
        AddLine(*shape_renderer_, triangle_c_, triangle_a_, triangle_line_width_ * 2, white);
        // klgl's DrawLine width is the half-thickness.
        AddLine(*shape_renderer_, line_a_, line_b_, line_width_ * 2, white);

        // klgl draws everything in one submission-ordered batch; here the white lines
        // share a pass with the face, so they land under the mouth instead of above it.
        const Mat3f identity = Mat3f::Identity();
        shape_renderer_->Render(identity);
        circle_renderer_->Render(identity);
        left_ear_renderer_->Render(identity);
        right_ear_renderer_->Render(identity);

        if (ImGui::CollapsingHeader("Triangle"))
        {
            klvk::SimpleTypeWidget("a", triangle_a_);
            klvk::SimpleTypeWidget("b", triangle_b_);
            klvk::SimpleTypeWidget("c", triangle_c_);
            ImGui::SliderFloat("line width", &triangle_line_width_, 0.f, 0.5f);
        }

        if (ImGui::CollapsingHeader("Line"))
        {
            klvk::SimpleTypeWidget("a", line_a_);
            klvk::SimpleTypeWidget("b", line_b_);
            klvk::SimpleTypeWidget("w", line_width_);
        }
    }

public:
    ~Painter2dApp() override
    {
        if (shape_renderer_) GetDeviceContext().WaitIdle();
    }

private:
    std::unique_ptr<klvk::Texture> white_texture_;
    std::unique_ptr<klvk::Texture> circle_texture_;
    std::unique_ptr<klvk::Texture> left_ear_texture_;
    std::unique_ptr<klvk::Texture> right_ear_texture_;
    std::unique_ptr<klvk::InstancedSpriteRenderer2d> shape_renderer_;
    std::unique_ptr<klvk::InstancedSpriteRenderer2d> circle_renderer_;
    std::unique_ptr<klvk::InstancedSpriteRenderer2d> left_ear_renderer_;
    std::unique_ptr<klvk::InstancedSpriteRenderer2d> right_ear_renderer_;

    Vec2f triangle_a_{-1, -1};
    Vec2f triangle_b_{0.2f, -1};
    Vec2f triangle_c_{-1, 1};
    float triangle_line_width_ = 0.01f;

    Vec2f line_a_{-1, 0};
    Vec2f line_b_{1, 0};
    float line_width_ = 0.05f;
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
