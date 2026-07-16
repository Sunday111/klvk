#include <imgui.h>

#include <EverydayTools/Math/Math.hpp>
#include <EverydayTools/Template/TaggedIdentifier.hpp>
#include <ass/fixed_bitset.hpp>
#include <set>

#include "klvk/application.hpp"
#include "klvk/camera/camera_2d.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/events/event_listener_method.hpp"
#include "klvk/events/event_manager.hpp"
#include "klvk/events/mouse_events.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/rendering/instanced_sprite_renderer_2d.hpp"
#include "klvk/signed_integral_aliases.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/texture.hpp"
#include "klvk/window.hpp"

namespace
{
using namespace edt::lazy_matrix_aliases;  // NOLINT

template <typename T>
struct TaggedIdHasher
{
    [[nodiscard]] constexpr size_t operator()(const T& value) const noexcept
    {
        return std::hash<typename T::Repr>{}(value.GetValue());
    }
};

struct ParticleIdTag
{
};
using ParticleId = edt::TaggedIdentifier<ParticleIdTag, u32>;

struct GridRegion
{
    static constexpr edt::Vec2<u16> kSize{256, 256};
    std::array<ass::FixedBitset<kSize.x()>, kSize.y()> bits{};
    std::array<std::array<ParticleId, kSize.x()>, kSize.y()> ids{};
};

using RegionId = Vec2i;

[[nodiscard]] constexpr RegionId ToRegionId(Vec2i coordinate) noexcept
{
    const Vec2i negative{coordinate.x() < 0, coordinate.y() < 0};
    return ((coordinate + negative) / GridRegion::kSize.Cast<i32>()) - negative;
}

[[nodiscard]] constexpr std::pair<Vec2i, Vec2i> RegionRange(RegionId id) noexcept
{
    const auto size = GridRegion::kSize.Cast<i32>();
    const auto begin = id * size;
    return {begin, begin + size};
}

struct RegionIdHasher
{
    [[nodiscard]] u64 operator()(const RegionId& value) const noexcept
    {
        return (u64{std::bit_cast<u32>(value.x())} << 32) | u64{std::bit_cast<u32>(value.y())};
    }
};

struct RegionIdCmp
{
    [[nodiscard]] constexpr bool operator()(const RegionId& a, const RegionId& b) const noexcept
    {
        return a.Tuple() < b.Tuple();
    }
};

struct ParticleData
{
    Vec4u8 color{};
    Vec2i position{};
};

struct ParticleGrid
{
    [[nodiscard]] bool HasParticleAt(Vec2i position) const
    {
        const RegionId region_id = ToRegionId(position);
        const auto iterator = containers.find(region_id);
        if (iterator == containers.end()) return false;
        const auto [begin, end] = RegionRange(region_id);
        const auto in_region = (position - begin).Cast<u32>();
        return iterator->second.bits[in_region.y()].Get(in_region.x());
    }

    ParticleId AddParticleAt(ParticleData data)
    {
        const RegionId region_id = ToRegionId(data.position);
        order.insert(region_id);
        GridRegion& container = containers[region_id];
        const auto [begin, end] = RegionRange(region_id);
        const auto in_region = (data.position - begin).Cast<u32>();
        const ParticleId id = std::exchange(next_particle_id, ParticleId::FromValue(next_particle_id.GetValue() + 1));
        particles.emplace(id, data);
        container.bits[in_region.y()].Set(in_region.x(), true);
        container.ids[in_region.y()][in_region.x()] = id;
        return id;
    }

    ParticleData RemoveParticleAt(Vec2i position)
    {
        const RegionId region_id = ToRegionId(position);
        GridRegion& container = containers.at(region_id);
        const auto [begin, end] = RegionRange(region_id);
        const auto in_region = (position - begin).Cast<u32>();
        container.bits[in_region.y()].Set(in_region.x(), false);
        const ParticleId id = std::exchange(container.ids[in_region.y()][in_region.x()], ParticleId{});
        return particles.extract(id).mapped();
    }

    std::set<RegionId, RegionIdCmp> order;
    std::unordered_map<RegionId, GridRegion, RegionIdHasher> containers;
    std::unordered_map<ParticleId, ParticleData, TaggedIdHasher<ParticleId>> particles;
    ParticleId next_particle_id = ParticleId::FromValue(0);
};

static_assert(ToRegionId({0, 0}) == Vec2i{0, 0});
static_assert(ToRegionId({-1, 0}) == Vec2i{-1, 0});
static_assert(ToRegionId({-256, 0}) == Vec2i{-1, 0});
static_assert(ToRegionId({-257, 0}) == Vec2i{-2, 0});
static_assert(RegionRange({-1, 0}) == std::pair{Vec2i{-256, 0}, Vec2i{0, 256}});

class FallingSandApp : public klvk::Application
{
    static constexpr Vec4u8 kRed{255, 0, 0, 255};
    static constexpr Vec4u8 kGreen{0, 255, 0, 255};
    static constexpr Vec4u8 kBlue{0, 0, 255, 255};
    static constexpr float kParticleSize = 0.01f;

    void Initialize() override
    {
        klvk::Application::Initialize();
        listener_ = klvk::events::EventListenerMethodCallbacks<&FallingSandApp::OnMouseScroll>::CreatePtr(this);
        GetEventManager().AddEventListener(*listener_);
        SetClearColor({});
        GetWindow().SetSize(1000, 1000);
        GetWindow().SetTitle("Painter 2d");
        SetTargetFramerate(90.f);

        constexpr std::array<u8, 1> white{255};
        texture_ = klvk::Texture::CreateR8(GetDeviceContext(), {1, 1}, white);
        renderer_ = std::make_unique<klvk::InstancedSpriteRenderer2d>(*this, *texture_);
        grid_.containers.reserve(64);
    }

    void Simulate()
    {
        constexpr Vec2i right = Vec2i::AxisX();
        constexpr Vec2i left = -right;
        constexpr Vec2i down = -Vec2i::AxisY();
        for (const RegionId& region_id : grid_.order)
        {
            GridRegion& container = grid_.containers[region_id];
            for (u32 y = 0; y != GridRegion::kSize.y(); ++y)
            {
                for (u32 x = 0; x != GridRegion::kSize.x(); ++x)
                {
                    if (!container.bits[y].Get(x)) continue;
                    const auto [begin, end] = RegionRange(region_id);
                    const Vec2i particle = begin + Vec2u32{x, y}.Cast<int>();
                    std::optional<Vec2i> destination;
                    if (particle.y() > -100)
                    {
                        const Vec2i down_point = particle + down;
                        const Vec2i down_left = down_point + left;
                        const Vec2i down_right = down_point + right;
                        if (!grid_.HasParticleAt(down_point))
                            destination = down_point;
                        else if (!grid_.HasParticleAt(down_left))
                            destination = down_left;
                        else if (!grid_.HasParticleAt(down_right))
                            destination = down_right;
                    }
                    if (destination)
                    {
                        ParticleData data = grid_.RemoveParticleAt(particle);
                        data.position = *destination;
                        grid_.AddParticleAt(data);
                    }
                }
            }
        }
    }

    static void AddLine(klvk::InstancedSpriteRenderer2d& renderer, Vec2f a, Vec2f b, float width, Vec4u8 color)
    {
        const Vec2f delta = b - a;
        renderer.Add((a + b) * 0.5f, color, {delta.Length() * 0.5f, width * 0.5f}, std::atan2(delta.y(), delta.x()));
    }

    static void
    AddRectOutline(klvk::InstancedSpriteRenderer2d& renderer, Vec2f center, Vec2f size, float half_width, Vec4u8 color)
    {
        const Vec2f half = size * 0.5f;
        const float width = half_width * 2.f;
        AddLine(renderer, center + Vec2f{-half.x(), -half.y()}, center + Vec2f{half.x(), -half.y()}, width, color);
        AddLine(renderer, center + Vec2f{half.x(), -half.y()}, center + Vec2f{half.x(), half.y()}, width, color);
        AddLine(renderer, center + Vec2f{half.x(), half.y()}, center + Vec2f{-half.x(), half.y()}, width, color);
        AddLine(renderer, center + Vec2f{-half.x(), half.y()}, center + Vec2f{-half.x(), -half.y()}, width, color);
    }

    void Tick() override
    {
        klvk::Application::Tick();
        Simulate();
        const auto viewport = klvk::Viewport::FromWindowSize(GetWindow().GetSize());
        transforms_.Update(camera_, viewport, klvk::AspectRatioPolicy::ShrinkToFit);
        renderer_->Clear();

        for (const RegionId& region_id : grid_.order)
        {
            const auto [begin, end] = RegionRange(region_id);
            const Vec2f region_size = GridRegion::kSize.Cast<float>() * kParticleSize;
            const Vec2f region_center = begin.Cast<float>() * kParticleSize + region_size * 0.5f;
            AddRectOutline(*renderer_, region_center, region_size, 0.01f, kBlue);

            const GridRegion& container = grid_.containers.at(region_id);
            for (u32 y = 0; y != GridRegion::kSize.y(); ++y)
                for (u32 x = 0; x != GridRegion::kSize.x(); ++x)
                    if (container.bits[y].Get(x))
                    {
                        const Vec2i particle = begin + Vec2u32{x, y}.Cast<int>();
                        renderer_->Add(
                            particle.Cast<float>() * kParticleSize,
                            kRed,
                            Vec2f{kParticleSize, kParticleSize} * 0.5f);
                    }
        }

        for (const auto& [id, data] : grid_.particles)
            renderer_->Add(
                data.position.Cast<float>() * kParticleSize,
                data.color,
                Vec2f{kParticleSize, kParticleSize} * 0.5f);

        HandleInput();
        renderer_->Render(transforms_.world_to_view);
        ImGui::Text("Fps: %f", static_cast<double>(GetFramerate()));
        ImGui::Text("Count: %zu", grid_.particles.size());
    }

    void HandleInput()
    {
        if (!ImGui::GetIO().WantCaptureKeyboard)
        {
            int right = 0;
            int up = 0;
            if (ImGui::IsKeyDown(ImGuiKey_W)) ++up;
            if (ImGui::IsKeyDown(ImGuiKey_S)) --up;
            if (ImGui::IsKeyDown(ImGuiKey_D)) ++right;
            if (ImGui::IsKeyDown(ImGuiKey_A)) --right;
            if (std::abs(right) + std::abs(up))
            {
                const Vec2f delta =
                    Vec2f::AxisX() * static_cast<float>(right) + Vec2f::AxisY() * static_cast<float>(up);
                camera_.eye += delta * move_speed_ * GetLastFrameDurationSeconds() / camera_.zoom;
            }
        }

        constexpr Vec2i offset{10, 10};
        const Vec2i center = (GetMousePositionInWorldCoordinates() / kParticleSize).Cast<int>();
        const Vec2i begin = center - offset;
        const Vec2i end = center + offset;
        AddRectOutline(
            *renderer_,
            center.Cast<float>() * kParticleSize,
            (offset * 2).Cast<float>() * kParticleSize,
            0.01f,
            kGreen);
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::GetIO().WantCaptureMouse)
        {
            for (Vec2i position = begin; position.y() != end.y(); ++position.y())
            {
                for (position.x() = begin.x(); position.x() != end.x(); ++position.x())
                    if (!grid_.HasParticleAt(position)) grid_.AddParticleAt({.color = kRed, .position = position});
            }
        }
    }

    [[nodiscard]] Vec2f GetMousePositionInWorldCoordinates() const
    {
        const Vec2f screen_size = GetWindow().GetSize2f();
        Vec2f position{ImGui::GetMousePos().x, ImGui::GetMousePos().y};
        position.y() = screen_size.y() - position.y();
        return edt::Math::TransformPos(transforms_.screen_to_world, position);
    }

    void OnMouseScroll(const klvk::events::OnMouseScroll& event)
    {
        if (ImGui::GetIO().WantCaptureMouse) return;
        zoom_power_ += event.value.y();
        camera_.zoom = std::max(std::pow(1.1f, zoom_power_), 0.1f);
    }

public:
    ~FallingSandApp() override
    {
        if (renderer_) GetDeviceContext().WaitIdle();
    }

private:
    std::unique_ptr<klvk::Texture> texture_;
    std::unique_ptr<klvk::InstancedSpriteRenderer2d> renderer_;
    std::unique_ptr<klvk::events::IEventListener> listener_;
    klvk::Camera2d camera_{};
    klvk::RenderTransforms2d transforms_{};
    float move_speed_ = 0.5f;
    float zoom_power_ = 0.f;
    ParticleGrid grid_;
};

void Main(int argc, char** argv)
{
    FallingSandApp app;
    app.RunWithArguments(argc, argv);
}
}  // namespace

int main(int argc, char** argv)
{
    return klvk::ErrorHandling::InvokeAndCatchAll(Main, argc, argv);
}
