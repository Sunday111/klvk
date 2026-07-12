#include <EverydayTools/Math/Math.hpp>
#include <random>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/ui/imgui_helpers.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/gpu_buffer.hpp"
#include "klvk/vulkan/graphics_pipeline_builder.hpp"
#include "klvk/vulkan/vulkan_api.hpp"
#include "klvk/window.hpp"

// Vulkan create-info structs are designed for partial designated initialization;
// unlisted fields must be zero.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

enum class ShapeType : uint32_t
{
    Quad = 0,
    Circle,
    Triangle
};

// Matches the Object struct in points_to_quads_2d.vert (std430).
struct Object
{
    std::array<edt::Vec4f, 3> transform_columns{};
    edt::Vec4u8 color{};
    ShapeType type = ShapeType::Quad;
    uint32_t padding0 = 0;
    uint32_t padding1 = 0;
};

static_assert(sizeof(Object) == 64);

class GeometryShaderApp : public klvk::Application
{
    static constexpr size_t kMaxObjects = 1000;

    void Initialize() override
    {
        klvk::Application::Initialize();

        SetClearColor({});
        GetWindow().SetSize(2000, 2000);
        GetWindow().SetTitle("Geometry Shader Quads");

        klvk::DeviceContext& context = GetDeviceContext();
        klvk::ErrorHandling::Ensure(
            context.IsGeometryShaderEnabled(),
            "This example requires a device with geometry shader support");
        VkDevice device = context.GetDevice();

        objects_.resize(kMaxObjects);
        std::mt19937 rnd;  // NOLINT
        std::uniform_int_distribution<uint32_t> type_distribution(0, 2);
        for (auto& object : objects_)
        {
            object.type = static_cast<ShapeType>(type_distribution(rnd));
        }

        // One storage buffer and descriptor set per frame in flight
        {
            const VkDescriptorSetLayoutBinding binding{
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            };
            set_layout_ = klvk::Vulkan::CreateDescriptorSetLayout(
                device,
                {
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                    .bindingCount = 1,
                    .pBindings = &binding,
                });

            constexpr auto frames = static_cast<uint32_t>(kFramesInFlight);
            const VkDescriptorPoolSize pool_size{
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = frames,
            };
            descriptor_pool_ = klvk::Vulkan::CreateDescriptorPool(
                device,
                {
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                    .maxSets = frames,
                    .poolSizeCount = 1,
                    .pPoolSizes = &pool_size,
                });

            std::array<VkDescriptorSetLayout, kFramesInFlight> layouts{};
            layouts.fill(set_layout_);
            const std::vector<VkDescriptorSet> sets = klvk::Vulkan::AllocateDescriptorSets(
                device,
                {
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                    .descriptorPool = descriptor_pool_,
                    .descriptorSetCount = frames,
                    .pSetLayouts = layouts.data(),
                });

            for (size_t index = 0; index != kFramesInFlight; ++index)
            {
                descriptor_sets_[index] = sets[index];
                object_buffers_[index] =
                    klvk::GpuBuffer(context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kMaxObjects * sizeof(Object), true);

                const VkDescriptorBufferInfo buffer_info{
                    .buffer = object_buffers_[index].GetHandle(),
                    .offset = 0,
                    .range = VK_WHOLE_SIZE,
                };
                const VkWriteDescriptorSet write{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = descriptor_sets_[index],
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .pBufferInfo = &buffer_info,
                };
                klvk::Vulkan::UpdateDescriptorSets(device, std::span{&write, 1});
            }
        }

        // Pipeline with a geometry stage
        {
            const VkPushConstantRange push_constant_range{
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                .offset = 0,
                .size = sizeof(float),
            };
            pipeline_layout_ = klvk::Vulkan::CreatePipelineLayout(
                device,
                {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                    .setLayoutCount = 1,
                    .pSetLayouts = &set_layout_,
                    .pushConstantRangeCount = 1,
                    .pPushConstantRanges = &push_constant_range,
                });

            const std::filesystem::path shader_dir = GetShaderDir() / "points_to_quads_2d";
            pipeline_ = klvk::GraphicsPipelineBuilder(*this)
                            .Layout(pipeline_layout_)
                            .VertexShaderFile(shader_dir / "points_to_quads_2d.vert")
                            .GeometryShaderFile(shader_dir / "points_to_quads_2d.geom")
                            .FragmentShaderFile(shader_dir / "points_to_quads_2d.frag")
                            .Topology(VK_PRIMITIVE_TOPOLOGY_POINT_LIST)
                            .AlphaBlend()
                            .Build();
        }
    }

    void Tick() override
    {
        klvk::Application::Tick();

        klvk::ImGuiHelper::SliderGetterSetter(
            "Border",
            0.001f,
            0.3f,
            [&] { return figure_border_; },
            [&](float value) { figure_border_ = value; });

        const float color_width = 0.15f;
        const float spiral_rotation = GetTimeSeconds();
        const float p = color_width + (1.f - color_width) * std::abs(std::sin(GetTimeSeconds()));

        const size_t n = 1 + static_cast<size_t>(999 * std::abs(std::sin(GetTimeSeconds() / 4)));
        for (const size_t i : std::views::iota(size_t{0}, n))
        {
            const float fi = static_cast<float>(i);
            const float k = fi / static_cast<float>(n - 1);
            const float rotation_around_origin = edt::Math::DegToRad(15.f - 6 * k) * fi - spiral_rotation;

            auto model = edt::Math::ScaleMatrix(edt::Vec2f{1, 1} * (0.01f + k * 0.02f));
            model = edt::Math::RotationMatrix2d(-GetTimeSeconds() * k * 10.f - rotation_around_origin).MatMul(model);
            model = edt::Math::TranslationMatrix(edt::Vec2f{0.03f, 0} + 0.85f * k).MatMul(model);
            model = edt::Math::RotationMatrix2d(rotation_around_origin).MatMul(model);

            for (size_t column = 0; column != 3; ++column)
            {
                const edt::Vec3f matrix_column = model.GetColumn(column);
                objects_[i].transform_columns[column] =
                    edt::Vec4f{matrix_column.x(), matrix_column.y(), matrix_column.z(), 0.f};
            }
            objects_[i].color =
                edt::Math::GetRainbowColorsA((std::clamp(k, p - color_width, p + color_width) + color_width - p) * 6);
        }

        const size_t frame_index = GetFrameInFlightIndex();
        object_buffers_[frame_index].Write(std::as_bytes(std::span{objects_}.first(n)));

        VkCommandBuffer command_buffer = GetCurrentCommandBuffer();
        klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        klvk::Vulkan::CmdBindDescriptorSets(
            command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline_layout_,
            0,
            std::span{&descriptor_sets_[frame_index], 1});
        klvk::Vulkan::CmdPushConstants(
            command_buffer,
            pipeline_layout_,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            figure_border_);
        klvk::Vulkan::CmdDraw(command_buffer, static_cast<uint32_t>(n), 1, 0, 0);
    }

public:
    ~GeometryShaderApp() override
    {
        // Initialize either succeeded entirely or threw before creating the pipeline.
        if (pipeline_ == VK_NULL_HANDLE) return;

        klvk::DeviceContext& context = GetDeviceContext();
        context.WaitIdle();
        VkDevice device = context.GetDevice();
        klvk::Vulkan::DestroyPipelineNE(device, pipeline_);
        klvk::Vulkan::DestroyPipelineLayoutNE(device, pipeline_layout_);
        klvk::Vulkan::DestroyDescriptorPoolNE(device, descriptor_pool_);
        klvk::Vulkan::DestroyDescriptorSetLayoutNE(device, set_layout_);
    }

private:
    float figure_border_ = 0.05f;
    std::vector<Object> objects_;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kFramesInFlight> descriptor_sets_{};
    std::array<klvk::GpuBuffer, kFramesInFlight> object_buffers_{};
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

void Main()
{
    GeometryShaderApp app;
    app.Run();
}

int main()
{
    klvk::ErrorHandling::InvokeAndCatchAll(Main);
    return 0;
}
