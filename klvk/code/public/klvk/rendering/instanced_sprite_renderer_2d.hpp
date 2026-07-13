#pragma once

#include <array>
#include <vector>

#include "EverydayTools/Math/Matrix.hpp"
#include "klvk/application.hpp"
#include "klvk/vulkan/descriptor_sets.hpp"
#include "klvk/vulkan/gpu_buffer.hpp"
#include "klvk/vulkan/vk_object.hpp"

namespace klvk
{

using namespace edt::lazy_matrix_aliases;  // NOLINT

class Texture;

// Draws textured quads with a translation, scale and color per instance.
// Instances are collected on the CPU every frame and uploaded to a per-frame
// storage buffer, so the caller just re-adds everything each tick.
class InstancedSpriteRenderer2d
{
public:
    struct Instance
    {
        Vec2f translation{};
        Vec2f scale{};
        Vec4u8 color{};
        float rotation_radians = 0.f;
    };

    static_assert(sizeof(Instance) == 24);  // must match the std430 layout in the shader

    InstancedSpriteRenderer2d(Application& app, const Texture& texture);
    InstancedSpriteRenderer2d(const InstancedSpriteRenderer2d&) = delete;
    InstancedSpriteRenderer2d(InstancedSpriteRenderer2d&&) = delete;
    ~InstancedSpriteRenderer2d();

    void Clear() { instances_.clear(); }

    void Add(const Vec2f& translation, const Vec4u8& color, const Vec2f& scale, float rotation_radians = 0.f)
    {
        instances_.push_back(
            {.translation = translation, .scale = scale, .color = color, .rotation_radians = rotation_radians});
    }

    [[nodiscard]] size_t GetInstanceCount() const noexcept { return instances_.size(); }

    void SetInstanceCount(size_t count) { instances_.resize(count); }
    [[nodiscard]] Instance& GetInstance(size_t index) { return instances_[index]; }

    // Records the draw into the frame that is currently recorded by the application.
    void Render(const Mat3f& world_to_view);

private:
    void EnsureFrameBufferCapacity(size_t frame_index, size_t bytes);

    Application* app_ = nullptr;
    std::vector<Instance> instances_;

    DescriptorSets descriptor_sets_;
    VkObject<VkPipelineLayout> pipeline_layout_;
    VkObject<VkPipeline> pipeline_;
    std::array<GpuBuffer, Application::kFramesInFlight> instance_buffers_{};
};

}  // namespace klvk
