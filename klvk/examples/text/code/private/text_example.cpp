#include <edt/math/matrix.hpp>
#include <random>
#include <vector>

#include "klvk/application.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/text/glyph_atlas.hpp"
#include "klvk/vulkan/descriptor_sets.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/gpu_buffer.hpp"
#include "klvk/vulkan/graphics_pipeline_builder.hpp"
#include "klvk/vulkan/texture.hpp"
#include "klvk/vulkan/vk_object.hpp"
#include "klvk/vulkan/vulkan_api.hpp"
#include "klvk/window.hpp"

// Vulkan create-info structs are designed for partial designated initialization;
// unlisted fields must be zero.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

namespace
{

using namespace edt::lazy_matrix_aliases;  // NOLINT

// The alphabet characters are drawn from. Every one of them is packed into the
// atlas up front, so the frame loop only picks and places.
constexpr std::u32string_view kAlphabet = U"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

// One pass per size: the screen is cleared, the atlas is rebuilt, and a character
// is added on each of the frames that follow. Capturing every frame of that walks
// the atlas from one glyph to many at each size.
constexpr std::array<u32, 3> kPixelSizes{16, 32, 64};
constexpr u32 kFramesPerSize = 8;

struct GlyphVertex
{
    Vec2f position{};
    Vec2f uv{};
};

// Matches the push constant block in glyph.vert and glyph.frag.
struct PushConstants
{
    Vec2f inverse_half_extent{};
    Vec2f padding{};
    Vec4f color{};
};

class TextApp : public klvk::Application
{
    void Initialize() override
    {
        klvk::Application::Initialize();

        SetClearColor({0.07f, 0.08f, 0.10f, 1.f});
        GetWindow().SetSize(640, 480);
        GetWindow().SetTitle("Text");

        font_ = klvk::FontFace::FromFile(GetContentDir() / "fonts" / "DejaVuSansMono.ttf");

        klvk::DeviceContext& context = GetDeviceContext();
        VkDevice device = context.GetDevice();

        descriptor_sets_ = klvk::DescriptorSets::Builder(context)
                               .Binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
                               .Build(kFramesInFlight);

        const VkPushConstantRange push_constant_range{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0,
            .size = sizeof(PushConstants),
        };
        const std::array set_layouts{descriptor_sets_.GetLayoutView()};
        pipeline_layout_ = klvk::PipelineLayout{context, set_layouts, std::span{&push_constant_range, 1}};

        const std::filesystem::path shader_dir = GetShaderDir() / "text";
        pipeline_ = klvk::VkObject<VkPipeline>{
            device,
            klvk::GraphicsPipelineBuilder(*this)
                .Layout(pipeline_layout_)
                .VertexShaderFile(shader_dir / "glyph.vert.slang")
                .FragmentShaderFile(shader_dir / "glyph.frag.slang")
                .VertexBinding(0, sizeof(GlyphVertex), VK_VERTEX_INPUT_RATE_VERTEX)
                .VertexAttribute(0, 0, VK_FORMAT_R32G32_SFLOAT, 0)
                .VertexAttribute(1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(Vec2f))
                .AlphaBlend()
                .Build()};

        // A seed from the diagnostic config keeps a scripted run reproducible.
        const nlohmann::json* config = GetDiagnosticApplicationConfig();
        const auto seed = config != nullptr ? config->value("seed", 7u) : 7u;
        random_ = std::mt19937{seed};

        UseSize(0);
    }

    void UseSize(size_t index)
    {
        size_index_ = index % kPixelSizes.size();
        atlas_ = std::make_unique<klvk::GlyphAtlas>(*font_, kPixelSizes[size_index_]);
        klvk::ErrorHandling::Ensure(atlas_->Add(kAlphabet), "The alphabet does not fit the atlas");
        atlas_->Upload(GetDeviceContext());

        for (size_t frame = 0; frame != kFramesInFlight; ++frame)
        {
            descriptor_sets_.WriteImage(frame, 0, atlas_->GetTexture()->GetView(), atlas_->GetTexture()->GetSampler());
        }
        text_.clear();
    }

    void AppendRandomCharacter()
    {
        std::uniform_int_distribution<size_t> pick{0, kAlphabet.size() - 1};
        text_.push_back(kAlphabet[pick(random_)]);
    }

    // Lays the string out from the top left, wrapping at the window's edge.
    [[nodiscard]] std::vector<GlyphVertex> BuildVertices() const
    {
        const Vec2f extent = GetWindow().GetFramebufferSize().Cast<float>();
        const float line_height = atlas_->GetLineHeight();

        std::vector<GlyphVertex> vertices;
        vertices.reserve(text_.size() * 6);

        Vec2f pen{line_height * 0.5f, line_height};
        for (const char32_t codepoint : text_)
        {
            const std::optional<klvk::GlyphAtlas::Glyph> glyph = atlas_->Find(codepoint);
            if (!glyph.has_value()) continue;

            if (pen.x() + glyph->advance > extent.x() - (line_height * 0.5f))
            {
                pen = {line_height * 0.5f, pen.y() + line_height};
            }

            // The bearing is measured y up from the pen; the layout runs y down.
            const Vec2f min{pen.x() + glyph->bearing.x(), pen.y() - glyph->bearing.y()};
            const Vec2f max = min + glyph->size;
            pen.x() += glyph->advance;

            if (glyph->size.x() <= 0.f || glyph->size.y() <= 0.f) continue;

            const GlyphVertex top_left{.position = {min.x(), min.y()}, .uv = {glyph->uv_min.x(), glyph->uv_min.y()}};
            const GlyphVertex top_right{.position = {max.x(), min.y()}, .uv = {glyph->uv_max.x(), glyph->uv_min.y()}};
            const GlyphVertex bottom_right{
                .position = {max.x(), max.y()},
                .uv = {glyph->uv_max.x(), glyph->uv_max.y()}};
            const GlyphVertex bottom_left{.position = {min.x(), max.y()}, .uv = {glyph->uv_min.x(), glyph->uv_max.y()}};

            vertices.insert(vertices.end(), {top_left, top_right, bottom_right, top_left, bottom_right, bottom_left});
        }
        return vertices;
    }

    void Tick() override
    {
        klvk::Application::Tick();

        // A new size starts a new pass with a cleared screen.
        const size_t pass = frame_ / kFramesPerSize;
        if (pass != pass_)
        {
            pass_ = pass;
            UseSize(pass_);
        }
        AppendRandomCharacter();
        ++frame_;

        const std::vector<GlyphVertex> vertices = BuildVertices();
        if (vertices.empty()) return;

        const size_t frame_index = GetFrameInFlightIndex();
        const size_t bytes = vertices.size() * sizeof(GlyphVertex);
        EnsureVertexCapacity(frame_index, bytes);
        vertex_buffers_[frame_index].Write(std::as_bytes(std::span{vertices}), 0);

        const Vec2f extent = GetWindow().GetFramebufferSize().Cast<float>();
        const PushConstants push_constants{
            .inverse_half_extent = {2.f / extent.x(), 2.f / extent.y()},
            .color = {0.95f, 0.93f, 0.85f, 1.f},
        };

        VkCommandBuffer command_buffer = GetCurrentCommandBuffer();
        const std::array descriptor_set{descriptor_sets_.Get(frame_index)};
        const std::array buffers{vertex_buffers_[frame_index].GetHandle()};
        const std::array<VkDeviceSize, 1> offsets{0};

        klvk::Vulkan::CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        klvk::Vulkan::CmdBindDescriptorSets(
            command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline_layout_.GetHandle(),
            0,
            descriptor_set);
        klvk::Vulkan::CmdBindVertexBuffers(command_buffer, 0, buffers, offsets);
        klvk::Vulkan::CmdPushConstants(
            command_buffer,
            pipeline_layout_.GetHandle(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            push_constants);
        klvk::Vulkan::CmdDraw(command_buffer, static_cast<u32>(vertices.size()), 1, 0, 0);
    }

    void EnsureVertexCapacity(size_t frame_index, size_t bytes)
    {
        klvk::GpuBuffer& buffer = vertex_buffers_[frame_index];
        if (buffer.IsValid() && buffer.GetSize() >= bytes) return;

        size_t capacity = std::max<size_t>(buffer.GetSize(), 4096);
        while (capacity < bytes) capacity *= 2;

        GetDeviceContext().WaitIdle();
        buffer = klvk::GpuBuffer{
            GetDeviceContext(),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            capacity,
            klvk::GpuBufferHostAccess::SequentialWrite};
    }

public:
    ~TextApp() override
    {
        if (font_) GetDeviceContext().WaitIdle();
    }

private:
    std::unique_ptr<klvk::FontFace> font_;
    std::unique_ptr<klvk::GlyphAtlas> atlas_;
    klvk::DescriptorSets descriptor_sets_;
    klvk::PipelineLayout pipeline_layout_;
    klvk::VkObject<VkPipeline> pipeline_;
    std::array<klvk::GpuBuffer, kFramesInFlight> vertex_buffers_{};

    std::mt19937 random_{7};
    std::u32string text_;
    size_t size_index_ = 0;
    size_t pass_ = 0;
    u64 frame_ = 0;
};

void Main(int argc, char** argv)
{
    TextApp app;
    app.RunWithArguments(argc, argv);
}

}  // namespace

int main(int argc, char** argv)
{
    return klvk::ErrorHandling::InvokeAndCatchAll(Main, argc, argv);
}
