#pragma once

#include <optional>
#include <vector>

#include "EverydayTools/Math/Matrix.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/vulkan/vulkan_common.hpp"

namespace klvk
{

struct GeneratedMeshData2d
{
    std::vector<edt::Vec2f> vertices;
    std::vector<edt::Vec2f> texture_coordinates;
    std::vector<u32> indices;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
};

struct GeneratedMeshData3d
{
    std::vector<edt::Vec3f> vertices;
    std::vector<edt::Vec3f> normals;
    std::vector<edt::Vec2f> texture_coordinates;
    std::vector<u32> indices;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
};

class ProceduralMeshGenerator
{
public:
    [[nodiscard]] static std::optional<GeneratedMeshData2d> GenerateCircleMesh(size_t triangles_count);
    [[nodiscard]] static GeneratedMeshData2d GenerateQuadMesh();
    [[nodiscard]] static GeneratedMeshData3d GenerateCubeMesh();
};

}  // namespace klvk
