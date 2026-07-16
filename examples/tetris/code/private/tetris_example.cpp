#include <imgui.h>

#include <EverydayTools/Math/Math.hpp>
#include <EverydayTools/Template/TaggedIdentifier.hpp>
#include <limits>
#include <optional>
#include <random>
#include <variant>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/rendering/instanced_sprite_renderer_2d.hpp"
#include "klvk/timing/timer_manager.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/texture.hpp"
#include "klvk/window.hpp"
#include "prefabs.hpp"

namespace
{
namespace colors
{
constexpr edt::Vec4u8 kBlack{0, 0, 0, 255};
constexpr edt::Vec4u8 kRed{255, 0, 0, 255};
}  // namespace colors

constexpr float kGameStepSeconds = 0.06f;

struct BlockIdTag
{
};
using BlockId = edt::TaggedIdentifier<BlockIdTag, u32>;
constexpr BlockId kInvalidBlockId{};

struct Block
{
    size_t prefab_index = 0;
    size_t rotation_index = 0;
    edt::Vec2i position{};
    edt::Vec4u8 color = colors::kRed;
};

struct BlockIdHash
{
    size_t operator()(BlockId id) const noexcept { return std::hash<u32>{}(id.GetValue()); }
};

struct TetrisCell
{
    edt::Vec4u8 color = colors::kBlack;
    BlockId block_id = kInvalidBlockId;
};

class TetrisGrid
{
public:
    static constexpr edt::Vec2<size_t> kSize{10, 20};

    [[nodiscard]] constexpr bool IsInside(edt::Vec2i coordinate) const
    {
        return coordinate.x() >= 0 && coordinate.y() >= 0 && static_cast<size_t>(coordinate.x()) < kSize.x() &&
               static_cast<size_t>(coordinate.y()) < kSize.y();
    }

    [[nodiscard]] auto BlockGridCoords(const BlockPrefab& prefab, edt::Vec2i position) const
    {
        return prefab.AllCoords() |
               std::views::transform([position](edt::Vec2<size_t> value) { return position + value.Cast<int>(); });
    }

    [[nodiscard]] bool AllCellsValid(const BlockPrefab& prefab, edt::Vec2i position, BlockId expected) const
    {
        return std::ranges::all_of(
            BlockGridCoords(prefab, position),
            [&](edt::Vec2i coordinate)
            { return IsInside(coordinate) && GetCell(coordinate.Cast<size_t>()).block_id == expected; });
    }

    [[nodiscard]] BlockId AddBlock(const Block& block)
    {
        const BlockPrefab& prefab = GetBlockPrefab(block.prefab_index, block.rotation_index);
        if (!AllCellsValid(prefab, block.position, kInvalidBlockId)) return kInvalidBlockId;
        const BlockId id = next_block_id_;
        next_block_id_ = BlockId::FromValue(next_block_id_.GetValue() + 1);
        blocks_[id] = block;
        for (edt::Vec2i coordinate : BlockGridCoords(prefab, block.position))
        {
            TetrisCell& cell = GetCell(coordinate.Cast<size_t>());
            cell.block_id = id;
            cell.color = block.color;
        }
        return id;
    }

    [[nodiscard]] std::optional<Block> RemoveBlock(BlockId id)
    {
        const auto iterator = blocks_.find(id);
        if (iterator == blocks_.end()) return {};
        const Block& block = iterator->second;
        const BlockPrefab& prefab = GetBlockPrefab(block.prefab_index, block.rotation_index);
        if (!AllCellsValid(prefab, block.position, id)) return {};
        for (edt::Vec2i coordinate : BlockGridCoords(prefab, block.position))
        {
            TetrisCell& cell = GetCell(coordinate.Cast<size_t>());
            cell = {};
        }
        const Block copy = block;
        blocks_.erase(iterator);
        return copy;
    }

    std::tuple<BlockId, bool> ReplaceBlock(BlockId id, const Block& replacement)
    {
        const auto old = RemoveBlock(id);
        klvk::ErrorHandling::Ensure(old.has_value(), "Failed to remove block from grid");
        if (const BlockId replacement_id = AddBlock(replacement); replacement_id.IsValid())
            return {replacement_id, true};
        const BlockId reverted = AddBlock(*old);
        klvk::ErrorHandling::Ensure(reverted.IsValid(), "Failed to restore block");
        return {reverted, false};
    }

    std::tuple<BlockId, bool> MoveBlock(BlockId id, edt::Vec2i delta)
    {
        const auto iterator = blocks_.find(id);
        klvk::ErrorHandling::Ensure(iterator != blocks_.end(), "Block not found");
        Block replacement = iterator->second;
        replacement.position += delta;
        return ReplaceBlock(id, replacement);
    }

    std::tuple<BlockId, bool> RotateBlock(BlockId id, int delta)
    {
        const auto iterator = blocks_.find(id);
        klvk::ErrorHandling::Ensure(iterator != blocks_.end(), "Block not found");
        Block replacement = iterator->second;
        const size_t rotation = static_cast<size_t>(delta % 4 + 4);
        replacement.rotation_index = (replacement.rotation_index + rotation) % 4;
        return ReplaceBlock(id, replacement);
    }

    [[nodiscard]] std::optional<size_t> FindFullRow(size_t first) const
    {
        for (size_t y = first; y != kSize.y(); ++y)
            if (std::ranges::all_of(
                    std::views::iota(size_t{0}, kSize.x()),
                    [&](size_t x) { return GetCell({x, y}).block_id.IsValid(); }))
                return y;
        return {};
    }

    [[nodiscard]] static constexpr auto AllCoords() { return Make2dCoords(kSize); }
    [[nodiscard]] TetrisCell& GetCell(edt::Vec2<size_t> coordinate)
    {
        return cells_[Coord2dToIndex(coordinate, kSize.x())];
    }
    [[nodiscard]] const TetrisCell& GetCell(edt::Vec2<size_t> coordinate) const
    {
        return cells_[Coord2dToIndex(coordinate, kSize.x())];
    }

private:
    std::array<TetrisCell, kSize.x() * kSize.y()> cells_{};
    std::unordered_map<BlockId, Block, BlockIdHash> blocks_;
    BlockId next_block_id_ = BlockId::FromValue(0);
};

enum class KeyboardKey : u8
{
    W,
    A,
    S,
    D,
    E,
    Q,
    Count
};

constexpr std::array<ImGuiKey, static_cast<size_t>(KeyboardKey::Count)>
    kImGuiKeys{ImGuiKey_W, ImGuiKey_A, ImGuiKey_S, ImGuiKey_D, ImGuiKey_E, ImGuiKey_Q};

struct KeyboardState
{
    float down_since = 0.f;
    float up_since = 0.f;
    bool is_down = false;
    bool changed = false;
};

struct Rect
{
    edt::Vec2f center{};
    edt::Vec2f size{};
    edt::Vec4u8 color{};
    float rotation_degrees = 0.f;
};

struct AnimatedRect
{
    Rect start{};
    Rect finish{};
    float start_time = 0.f;
    float duration = 0.f;
};

class TetrisApp : public klvk::Application
{
    struct SpawnBlockState
    {
    };
    struct BlockFallState
    {
    };
    struct DeleteRowsState
    {
        size_t row_index = 0;
        size_t next_x = 0;
    };
    struct MoveDeletedRowUp
    {
        size_t deleted_row = 0;
        size_t current_row = 0;
    };

    void Initialize() override
    {
        klvk::Application::Initialize();
        SetClearColor({0.2f, 0.2f, 0.2f, 1.f});
        GetWindow().SetSize(500, 1000);
        GetWindow().SetTitle("Tetris Example");
        SetTargetFramerate(60.f);
        constexpr std::array<u8, 1> white{255};
        texture_ = klvk::Texture::CreateR8(GetDeviceContext(), {1, 1}, white);
        renderer_ = std::make_unique<klvk::InstancedSpriteRenderer2d>(*this, *texture_);
        state_ = SpawnBlockState{};
        const klvk::TimerDuration interval{static_cast<double>(kGameStepSeconds)};
        game_step_timer_ = GetTimerManager().ScheduleEveryAt(
            interval,
            interval,
            [this](const klvk::TimerEvent& event)
            {
                klvk::ErrorHandling::Ensure(
                    event.occurrence != std::numeric_limits<u64>::max(),
                    "Tetris game-step counter overflow");
                pending_game_step_ = event.occurrence + 1;
            },
            klvk::TimerMissedTickPolicy::Coalesce);
    }

    [[nodiscard]] bool SpawnNewBlock()
    {
        const Block block{
            .prefab_index = prefab_distribution_(random_),
            .rotation_index = rotation_distribution_(random_),
            .position = {3, 16},
            .color = edt::Vec4u8(edt::Math::GetRainbowColors(GetTimeSeconds()), 255)};
        const BlockId id = grid_.AddBlock(block);
        if (!id.IsValid()) return false;
        current_block_ = id;
        return true;
    }

    void Tick(BlockFallState&)
    {
        int rotation = 0;
        if (KeyReleased(KeyboardKey::E)) ++rotation;
        if (KeyReleased(KeyboardKey::Q)) --rotation;
        if (rotation != 0 && current_block_.IsValid())
            current_block_ = std::get<0>(grid_.RotateBlock(current_block_, rotation));

        auto instant_move = [&](KeyboardKey key, edt::Vec2i delta)
        {
            const KeyboardState& state = keys_[static_cast<size_t>(key)];
            if (state.is_down && state.changed) current_block_ = std::get<0>(grid_.MoveBlock(current_block_, delta));
        };
        instant_move(KeyboardKey::A, {-1, 0});
        instant_move(KeyboardKey::D, {1, 0});
        instant_move(KeyboardKey::S, {0, -1});
        instant_move(KeyboardKey::W, {0, 1});
    }

    void TimeStep(BlockFallState&, u64 step)
    {
        constexpr size_t gravity_period = 15;
        const bool gravity_step = step % gravity_period == 0;
        if (gravity_step && current_block_.IsValid())
        {
            auto [id, success] = grid_.MoveBlock(current_block_, {0, -1});
            current_block_ = id;
            if (!success)
            {
                if (const auto row = grid_.FindFullRow(0))
                    state_ = DeleteRowsState{.row_index = *row};
                else
                    state_ = SpawnBlockState{};
            }
        }

        const float time = GetTimeSeconds();
        auto held_move = [&](KeyboardKey key, edt::Vec2i delta)
        {
            const KeyboardState& state = keys_[static_cast<size_t>(key)];
            if (!state.is_down || state.changed || time < state.down_since + 0.5f) return;
            current_block_ = std::get<0>(grid_.MoveBlock(current_block_, delta));
        };
        held_move(KeyboardKey::A, {-1, 0});
        held_move(KeyboardKey::D, {1, 0});
        if (!gravity_step) held_move(KeyboardKey::S, {0, -1});
    }

    void Tick(DeleteRowsState&) {}
    void TimeStep(DeleteRowsState& state, u64)
    {
        if (state.next_x < TetrisGrid::kSize.x())
        {
            const edt::Vec2<size_t> coordinate{state.next_x, state.row_index};
            TetrisCell& cell = grid_.GetCell(coordinate);
            const edt::Vec4u8 color = cell.color;
            cell = {};
            Rect subdivision = MakeCellRect(coordinate);
            constexpr size_t subdivisions = 4;
            subdivision.center += subdivision.size / static_cast<float>(subdivisions * 2) - subdivision.size / 2.f;
            subdivision.size /= static_cast<float>(subdivisions);
            std::uniform_real_distribution<float> distance(0.f, subdivision.size.x() * 5.f);
            for (size_t x = 0; x != subdivisions; ++x)
                for (size_t y = 0; y != subdivisions; ++y)
                {
                    AnimatedRect& animation = animations_.emplace_back();
                    animation.start_time = GetTimeSeconds();
                    animation.duration = 2.f;
                    animation.start = subdivision;
                    animation.start.center += edt::Vec2<size_t>{x, y}.Cast<float>() * subdivision.size;
                    animation.start.color = color;
                    animation.finish = animation.start;
                    animation.finish.color.w() = 0;
                    animation.finish.center += edt::Vec2f{distance(random_), distance(random_)};
                }
            ++state.next_x;
        }
        else
            state_ = MoveDeletedRowUp{.deleted_row = state.row_index, .current_row = state.row_index};
    }

    void Tick(SpawnBlockState&) {}
    void TimeStep(SpawnBlockState&, u64)
    {
        if (SpawnNewBlock()) state_ = BlockFallState{};
    }

    void Tick(MoveDeletedRowUp&) {}
    void TimeStep(MoveDeletedRowUp& state, u64)
    {
        const size_t next = state.current_row + 1;
        if (next < TetrisGrid::kSize.y())
        {
            for (size_t x = 0; x != TetrisGrid::kSize.x(); ++x)
                std::swap(grid_.GetCell({x, state.current_row}), grid_.GetCell({x, next}));
            ++state.current_row;
        }
        else if (const auto row = grid_.FindFullRow(state.deleted_row))
            state_ = DeleteRowsState{.row_index = *row};
        else
            state_ = SpawnBlockState{};
    }

    Rect MakeCellRect(edt::Vec2<size_t> coordinate) const
    {
        const edt::Vec2f size = edt::Vec2f{2.f, 2.f} / TetrisGrid::kSize.Cast<float>();
        return {
            .center = edt::Vec2f{-1.f, -1.f} + size * coordinate.Cast<float>() + size / 2.f,
            .size = size,
            .color = grid_.GetCell(coordinate).color};
    }

    void DrawAnimatedRects()
    {
        const float time = GetTimeSeconds();
        for (const AnimatedRect& animation : animations_)
        {
            const float factor = std::clamp((time - animation.start_time) / animation.duration, 0.f, 1.f);
            const Rect rectangle{
                .center = edt::Math::Lerp(animation.start.center, animation.finish.center, factor),
                .size = edt::Math::Lerp(animation.start.size, animation.finish.size, factor),
                .color =
                    edt::Math::Lerp(animation.start.color.Cast<float>(), animation.finish.color.Cast<float>(), factor)
                        .Cast<u8>(),
                .rotation_degrees =
                    std::lerp(animation.start.rotation_degrees, animation.finish.rotation_degrees, factor)};
            AddRect(rectangle);
        }
        std::erase_if(
            animations_,
            [&](const AnimatedRect& value) { return value.start_time + value.duration <= time; });
    }

    void AddRect(const Rect& rectangle)
    {
        renderer_->Add(
            rectangle.center,
            rectangle.color,
            rectangle.size * 0.5f,
            edt::Math::DegToRad(rectangle.rotation_degrees));
    }

    void Tick() override
    {
        klvk::Application::Tick();
        UpdateKeyboardState();
        std::visit([&](auto& state) { Tick(state); }, state_);
        if (pending_game_step_.has_value())
        {
            const u64 step = *pending_game_step_;
            pending_game_step_.reset();
            std::visit([&](auto& state) { TimeStep(state, step); }, state_);
        }

        renderer_->Clear();
        for (edt::Vec2<size_t> coordinate : TetrisGrid::AllCoords())
        {
            Rect rectangle = MakeCellRect(coordinate);
            if (!grid_.GetCell(coordinate).block_id.IsValid()) rectangle.size *= 0.97f;
            AddRect(rectangle);
        }
        DrawAnimatedRects();
        renderer_->Render(edt::Mat3f::Identity());
    }

    bool KeyReleased(KeyboardKey key) const
    {
        const KeyboardState& state = keys_[static_cast<size_t>(key)];
        return state.changed && !state.is_down;
    }

    void UpdateKeyboardState()
    {
        if (ImGui::GetIO().WantCaptureKeyboard) return;
        const float time = GetTimeSeconds();
        for (size_t index = 0; index != keys_.size(); ++index)
        {
            KeyboardState& state = keys_[index];
            const bool down = ImGui::IsKeyDown(kImGuiKeys[index]);
            state.changed = down != state.is_down;
            if (state.changed)
            {
                state.is_down = down;
                (down ? state.down_since : state.up_since) = time;
            }
        }
    }

public:
    ~TetrisApp() override
    {
        if (game_step_timer_.IsValid())
        {
            [[maybe_unused]] const bool cancelled = GetTimerManager().Cancel(game_step_timer_);
        }
        if (renderer_) GetDeviceContext().WaitIdle();
    }

private:
    BlockId current_block_;
    TetrisGrid grid_;
    std::unique_ptr<klvk::Texture> texture_;
    std::unique_ptr<klvk::InstancedSpriteRenderer2d> renderer_;
    std::mt19937 random_{0};
    std::uniform_int_distribution<size_t> prefab_distribution_{0, Prefabs.size() - 1};
    std::uniform_int_distribution<size_t> rotation_distribution_{0, 3};
    std::variant<SpawnBlockState, BlockFallState, DeleteRowsState, MoveDeletedRowUp> state_;
    std::array<KeyboardState, static_cast<size_t>(KeyboardKey::Count)> keys_{};
    std::vector<AnimatedRect> animations_;
    klvk::TimerHandle game_step_timer_;
    std::optional<u64> pending_game_step_;
};

void Main(int argc, char** argv)
{
    TetrisApp app;
    app.RunWithArguments(argc, argv);
}
}  // namespace

int main(int argc, char** argv)
{
    return klvk::ErrorHandling::InvokeAndCatchAll(Main, argc, argv);
}
