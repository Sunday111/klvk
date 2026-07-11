#include "klvk/mesh/procedural_mesh_generator.hpp"

#include "EverydayTools/Math/Constants.hpp"
#include "EverydayTools/Math/Math.hpp"

namespace klvk
{

GeneratedMeshData2d ProceduralMeshGenerator::GenerateQuadMesh()
{
    return {
        .vertices{{-1, +1}, {+1, +1}, {-1, -1}, {+1, -1}},
        .texture_coordinates{{0, 1}, {1, 1}, {0, 0}, {1, 0}},
        .indices{3, 0, 2, 3, 1, 0},
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
}

GeneratedMeshData3d ProceduralMeshGenerator::GenerateCubeMesh()
{
    GeneratedMeshData3d result{
        .vertices{
            {-1, -1, -1}, {-1, +1, -1}, {+1, +1, -1}, {+1, -1, -1}, {-1, -1, +1}, {-1, +1, +1},
            {+1, +1, +1}, {+1, -1, +1}, {-1, -1, -1}, {-1, -1, +1}, {-1, +1, +1}, {-1, +1, -1},
            {+1, -1, -1}, {+1, -1, +1}, {+1, +1, +1}, {+1, +1, -1}, {+1, -1, -1}, {+1, -1, +1},
            {-1, -1, +1}, {-1, -1, -1}, {+1, +1, -1}, {+1, +1, +1}, {-1, +1, +1}, {-1, +1, -1},
        },
        .normals{
            {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, +1}, {0, 0, +1}, {0, 0, +1}, {0, 0, +1},
            {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}, {+1, 0, 0}, {+1, 0, 0}, {+1, 0, 0}, {+1, 0, 0},
            {0, -1, 0}, {0, -1, 0}, {0, -1, 0}, {0, -1, 0}, {0, +1, 0}, {0, +1, 0}, {0, +1, 0}, {0, +1, 0},
        },
        .texture_coordinates{},
        .indices{0,  1,  2,  0,  2,  3,  4,  6,  5,  4,  7,  6,  8,  9,  10, 8,  10, 11,
                 12, 14, 13, 12, 15, 14, 16, 17, 18, 16, 18, 19, 20, 22, 21, 20, 23, 22},
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };

    result.texture_coordinates.reserve(result.vertices.size());
    for (size_t face = 0; face != 6; ++face)
    {
        result.texture_coordinates.insert(
            result.texture_coordinates.end(),
            {{0.f, 0.f}, {0.f, 1.f}, {1.f, 1.f}, {1.f, 0.f}});
    }
    return result;
}

std::optional<GeneratedMeshData2d> ProceduralMeshGenerator::GenerateCircleMesh(size_t triangles_count)
{
    if (triangles_count < 3) return std::nullopt;

    GeneratedMeshData2d result;
    result.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    result.vertices.resize(triangles_count + 1);
    result.indices.resize(triangles_count + 2);
    result.vertices[1] = {0.f, 1.f};

    const edt::Mat3f rotation = edt::Math::RotationMatrix2d(2 * edt::kPi<float> / static_cast<float>(triangles_count));
    for (size_t index = 2; index != result.vertices.size(); ++index)
    {
        result.vertices[index] = edt::Math::TransformVector(rotation, result.vertices[index - 1]);
    }
    for (uint32_t index = 0; index != static_cast<uint32_t>(result.indices.size()); ++index)
    {
        result.indices[index] = index;
    }
    result.indices.back() = 1;

    result.texture_coordinates.reserve(result.vertices.size());
    for (const edt::Vec2f& vertex : result.vertices)
    {
        result.texture_coordinates.push_back((vertex + 1.f) / 2.f);
    }
    return result;
}

}  // namespace klvk
