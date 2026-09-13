#include <array>
#include <exception>

#include "klvk/error_handling.hpp"
#include "klvk/platform/os/os.hpp"
#include "klvk/vulkan/device_context.hpp"
#include "klvk/vulkan/graphics_pipeline_builder.hpp"
#include "klvk/vulkan/pipeline_layout.hpp"

int main()
{
    return klvk::ErrorHandling::InvokeAndCatchAll(
        []
        {
            klvk::DeviceContext context(nullptr);
            const bool supported = context.GetPhysicalDevice().getFeatures().fillModeNonSolid == vk::True;
            klvk::ErrorHandling::Ensure(
                context.IsFillModeNonSolidEnabled() == supported,
                "non-solid polygon feature was not negotiated");
            const auto shaders = klvk::os::GetExecutableDir() / "content/shaders";
            context.InitializeShaderCache(shaders);
            klvk::PipelineLayout layout(context, {});
            for (vk::PolygonMode mode :
                 std::array{vk::PolygonMode::eFill, vk::PolygonMode::eLine, vk::PolygonMode::ePoint})
            {
                klvk::GraphicsPipelineBuilder builder(context);
                if (!supported && mode != vk::PolygonMode::eFill)
                {
                    bool rejected = false;
                    try
                    {
                        builder.PolygonMode(mode);
                    }
                    catch (const std::exception&)
                    {
                        rejected = true;
                    }
                    klvk::ErrorHandling::Ensure(rejected, "unsupported non-solid polygon mode was accepted");
                    continue;
                }
                const auto pipeline = builder.Layout(layout)
                                          .ColorFormat(vk::Format::eR8G8B8A8Unorm)
                                          .PolygonMode(mode)
                                          .VertexShaderFile(shaders / "polygon_modes/triangle.vert.slang")
                                          .FragmentShaderFile(shaders / "polygon_modes/triangle.frag.slang")
                                          .Build();
                klvk::ErrorHandling::Ensure(static_cast<bool>(pipeline), "polygon mode pipeline creation failed");
            }
        });
}
