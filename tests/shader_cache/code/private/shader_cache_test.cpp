#include <fmt/format.h>

#include <atomic>
#include <fstream>
#include <future>

#include "klvk/integral_aliases.hpp"
#include "klvk/shader/shader_cache_manager.hpp"

namespace
{

void Ensure(bool condition, std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

template <typename... Args>
    requires(sizeof...(Args) > 0)
void Ensure(bool condition, fmt::format_string<Args...> format, Args&&... args)
{
    if (!condition) throw std::runtime_error(fmt::format(format, std::forward<Args>(args)...));
}

void Write(const std::filesystem::path& path, std::string_view text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    Ensure(file.is_open(), "failed to open test shader");
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    Ensure(file.good(), "failed to write test shader");
}

std::vector<std::filesystem::path> CacheFiles(const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> result;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".cache") result.push_back(entry.path());
    }
    return result;
}

void TestPureValidation()
{
    klvk::ShaderValueType double_vector{};
    double_vector.scalar = klvk::ShaderScalarType::Float64;
    double_vector.rows = 3;
    Ensure(klvk::ShaderValueLocationCount(double_vector) == 2, "64-bit interface location accounting is incorrect");

    auto vertex = std::make_shared<klvk::ShaderInterface>();
    vertex->stage = vk::ShaderStageFlagBits::eVertex;
    klvk::ShaderDescriptorBinding vertex_descriptor{};
    vertex_descriptor.name = "scene";
    vertex_descriptor.type = vk::DescriptorType::eUniformBuffer;
    vertex_descriptor.stages = vertex->stage;
    vertex->descriptors.push_back(vertex_descriptor);
    klvk::ShaderInterfaceVariable output{};
    output.name = "color";
    output.location = 0;
    output.type.scalar = klvk::ShaderScalarType::Float32;
    output.type.rows = 4;
    vertex->outputs.push_back(output);

    auto fragment = std::make_shared<klvk::ShaderInterface>();
    fragment->stage = vk::ShaderStageFlagBits::eFragment;
    klvk::ShaderDescriptorBinding fragment_descriptor = vertex_descriptor;
    fragment_descriptor.stages = fragment->stage;
    fragment->descriptors.push_back(fragment_descriptor);
    klvk::ShaderInterfaceVariable input = output;
    fragment->inputs.push_back(input);

    const auto program = klvk::MergeShaderInterfaces({vertex, fragment});
    Ensure(program.descriptors.size() == 1, "matching descriptors were not merged");
    Ensure(
        program.descriptors.front().stages == (vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment),
        "descriptor stage masks were not merged");

    fragment->descriptors.front().type = vk::DescriptorType::eStorageBuffer;
    try
    {
        (void)klvk::MergeShaderInterfaces({vertex, fragment});
        throw std::runtime_error("descriptor conflict was accepted");
    }
    catch (const std::exception& error)
    {
        Ensure(
            std::string_view(error.what()).contains("descriptor conflict"),
            "descriptor conflict produced an unclear error");
    }
}

void TestTessellationReflection(const std::filesystem::path& cache)
{
    std::filesystem::path repository = std::filesystem::path{__FILE__}.parent_path();
    for (size_t i = 0; i != 4; ++i) repository = repository.parent_path();
    const auto sources = repository / "klvk/content/shaders/klvk";
    const std::array paths{
        sources / "curve2d.vert.slang",
        sources / "curve2d.hull.slang",
        sources / "curve2d.domain.slang",
        sources / "curve2d.geom.slang",
        sources / "curve2d.frag.slang",
    };
    std::vector<std::shared_ptr<const klvk::ShaderInterface>> cold_interfaces;
    {
        klvk::ShaderCacheManager manager(sources, cache);
        for (const auto& path : paths)
        {
            const auto compiled = manager.GetOrCompile(path);
            Ensure(compiled->interface != nullptr, "curve tessellation stage was not reflected");
            cold_interfaces.push_back(compiled->interface);
        }
        const auto program = klvk::MergeShaderInterfaces(cold_interfaces);
        constexpr vk::ShaderStageFlags expected_stages =
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eTessellationControl |
            vk::ShaderStageFlagBits::eTessellationEvaluation | vk::ShaderStageFlagBits::eGeometry |
            vk::ShaderStageFlagBits::eFragment;
        Ensure(program.stages == expected_stages, "curve tessellation stage mask is incomplete");
        Ensure(program.push_constants.size() == 1, "curve push constants were not merged");
        // Every stage that reads the push constants has to be named here, and the geometry
        // stage reads them too.
        constexpr vk::ShaderStageFlags push_stages =
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eTessellationControl |
            vk::ShaderStageFlagBits::eTessellationEvaluation | vk::ShaderStageFlagBits::eGeometry;
        Ensure(
            program.push_constants.front().stages == push_stages,
            "curve push-constant stages are incorrect: expected {}, got {}",
            vk::to_string(push_stages),
            vk::to_string(program.push_constants.front().stages));
    }
    {
        klvk::ShaderCacheManager manager(sources, cache);
        for (size_t i = 0; i != paths.size(); ++i)
        {
            const auto warm = manager.GetOrCompile(paths[i]);
            Ensure(*warm->interface == *cold_interfaces[i], "warm tessellation reflection differs from cold");
        }
    }
}

void Run()
{
    TestPureValidation();
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("klvk_shader_cache_test_" + std::to_string(nonce));
    const std::filesystem::path sources = root / "sources";
    const std::filesystem::path cache = root / "cache";
    std::filesystem::create_directories(sources);
    TestTessellationReflection(root / "tessellation_cache");
    const std::filesystem::path shader = sources / "coalesce.comp.slang";
    const std::filesystem::path slang_shader = sources / "test.comp.slang";
    const std::filesystem::path reflection_shader = sources / "reflection.comp.slang";
    const std::filesystem::path vertex_shader = sources / "varying.vert.slang";
    const std::filesystem::path geometry_shader = sources / "varying.geom.slang";
    const std::filesystem::path fragment_shader = sources / "varying.frag.slang";
    const std::filesystem::path unsupported_patch_shader = sources / "unsupported_patch.domain.slang";
    const std::filesystem::path slang_dependency = sources / "dependency.slang";
    Write(shader, "[shader(\"compute\")] [numthreads(8, 1, 1)] void main() {}\n");

    std::shared_ptr<const klvk::CompiledShader> expected;
    std::shared_ptr<const klvk::ShaderInterface> expected_interface;
    {
        klvk::ShaderCacheManager manager(sources, cache, {.flush_interval = std::chrono::milliseconds(20)});
        std::vector<std::future<std::shared_ptr<const klvk::CompiledShader>>> futures;
        for (size_t i = 0; i != 16; ++i)
        {
            futures.push_back(std::async(std::launch::async, [&] { return manager.GetOrCompile(shader); }));
        }
        expected = futures.front().get();
        for (size_t i = 1; i != futures.size(); ++i)
        {
            Ensure(futures[i].get() == expected, "concurrent requests were not coalesced");
        }

        Write(shader, "this is not Slang\n");
        std::atomic<size_t> failures = 0;
        std::vector<std::future<void>> bad_futures;
        for (size_t i = 0; i != 8; ++i)
        {
            bad_futures.push_back(
                std::async(
                    std::launch::async,
                    [&]
                    {
                        try
                        {
                            (void)manager.GetOrCompile(shader);
                        }
                        catch (...)
                        {
                            ++failures;
                        }
                    }));
        }
        for (auto& future : bad_futures) future.get();
        Ensure(failures == bad_futures.size(), "compile failure was not delivered to every waiter");

        Write(shader, "[shader(\"compute\")] [numthreads(2, 1, 1)] void main() {}\n");
        Ensure(manager.GetOrCompile(shader) != nullptr, "cache did not recover after source correction");

        Write(slang_shader, "[shader(\"compute\")] [numthreads(1, 1, 1)] void main() {}\n");
        Ensure(manager.GetOrCompile(slang_shader) != nullptr, "self-contained Slang shader did not compile");

        Write(
            reflection_shader,
            "struct Item { float4 position; uint id; };\n"
            "[[vk::binding(2, 1)]] RWStructuredBuffer<Item> items;\n"
            "struct PushConstants { float2 offset; uint count; };\n"
            "[[vk::push_constant]] PushConstants pc;\n"
            "[[vk::constant_id(7)]] const uint MODE = 3;\n"
            "[shader(\"compute\")] [numthreads(8, 4, 2)]\n"
            "void main(uint3 id: SV_DispatchThreadID) {\n"
            "  if (id.x < pc.count) items[id.x].position.xy += pc.offset * MODE;\n"
            "}\n");
        const auto reflected = manager.GetOrCompile(reflection_shader);
        Ensure(reflected->interface != nullptr, "Slang reflection was not retained");
        Ensure(reflected->interface->stage == vk::ShaderStageFlagBits::eCompute, "compute stage was not reflected");
        Ensure(reflected->interface->workgroup_size == std::array<u32, 3>{8, 4, 2}, "workgroup size mismatch");
        Ensure(reflected->interface->descriptors.size() == 1, "descriptor binding was not reflected");
        Ensure(reflected->interface->descriptors.front().set == 1, "descriptor set was not reflected");
        Ensure(reflected->interface->descriptors.front().binding == 2, "descriptor binding index mismatch");
        Ensure(reflected->interface->push_constants.size() == 1, "push constants were not reflected");
        Ensure(reflected->interface->specialization_constants.size() == 1, "specialization constant was not reflected");
        Ensure(reflected->interface->specialization_constants.front().id == 7, "specialization constant id mismatch");
        expected_interface = reflected->interface;

        Write(
            vertex_shader,
            "struct Varying {\n"
            "  float4 position : SV_Position;\n"
            "  [[vk::location(0)]] float4 color : COLOR;\n"
            "};\n"
            "[shader(\"vertex\")] Varying main(uint id : SV_VertexID) {\n"
            "  Varying result;\n"
            "  result.position = float4(float(id), 0, 0, 1);\n"
            "  result.color = float4(1, 0, 0, 1);\n"
            "  return result;\n"
            "}\n");
        Write(
            geometry_shader,
            "struct Varying {\n"
            "  float4 position : SV_Position;\n"
            "  [[vk::location(0)]] float4 color : COLOR;\n"
            "};\n"
            "[shader(\"geometry\")] [maxvertexcount(1)]\n"
            "void main(point Varying input[1], inout PointStream<Varying> output) {\n"
            "  output.Append(input[0]);\n"
            "}\n");
        Write(
            fragment_shader,
            "struct Input { [[vk::location(0)]] float4 color : COLOR; };\n"
            "[shader(\"fragment\")] float4 main(Input input) : SV_Target {\n"
            "  return input.color;\n"
            "}\n");
        const auto vertex_reflected = manager.GetOrCompile(vertex_shader);
        const auto geometry_reflected = manager.GetOrCompile(geometry_shader);
        const auto fragment_reflected = manager.GetOrCompile(fragment_shader);
        Ensure(geometry_reflected->interface->inputs.size() == 2, "geometry primitive-array input was not flattened");
        Ensure(geometry_reflected->interface->outputs.size() == 2, "geometry output stream was not flattened");
        const auto varying_program = klvk::MergeShaderInterfaces(
            {vertex_reflected->interface, geometry_reflected->interface, fragment_reflected->interface});
        Ensure(
            varying_program.stages == (vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eGeometry |
                                       vk::ShaderStageFlagBits::eFragment),
            "vertex-geometry-fragment interface merge failed");

        Write(
            unsupported_patch_shader,
            "struct ControlPoint { float4 position : SV_Position; };\n"
            "struct PatchConstants { float factors[2] : SV_TessFactor; float user_value : TEXCOORD0; };\n"
            "[shader(\"domain\")] [domain(\"isoline\")]\n"
            "float4 main(PatchConstants constants, float2 uv : SV_DomainLocation,\n"
            "  const OutputPatch<ControlPoint, 2> patch) : SV_Position {\n"
            "  return patch[0].position + constants.user_value * 0.0 + uv.x * 0.0;\n"
            "}\n");
        try
        {
            (void)manager.GetOrCompile(unsupported_patch_shader);
            throw std::runtime_error("user patch constants were accepted");
        }
        catch (const std::exception& error)
        {
            Ensure(
                std::string_view(error.what())
                        .find("Slang JSON reflection cannot represent domain interfaces with user patch constants") !=
                    std::string_view::npos,
                "user patch constants produced an unclear reflection error");
        }

        Write(slang_dependency, "static const uint dependency_value = 1;\n");
        Write(
            slang_shader,
            "#include \"dependency.slang\"\n"
            "[shader(\"compute\")] [numthreads(1, 1, 1)] void main() { uint value = dependency_value; }\n");
        try
        {
            (void)manager.GetOrCompile(slang_shader);
            throw std::runtime_error("dependency-bearing Slang shader was accepted by the persistent cache");
        }
        catch (const std::exception& error)
        {
            Ensure(
                std::string_view(error.what()).contains("imports and includes are unsupported"),
                "dependency-bearing Slang shader did not report the cache restriction");
        }
    }

    const auto files = CacheFiles(cache);
    Ensure(files.size() == 7, "successful cache entries were not flushed at shutdown");
    {
        klvk::ShaderCacheManager manager(sources, cache);
        Ensure(manager.GetOrCompile(shader) != nullptr, "persistent entry could not be loaded");
        const auto reflected = manager.GetOrCompile(reflection_shader);
        Ensure(
            reflected->interface != nullptr && *reflected->interface == *expected_interface,
            "warm-cache reflection differs from cold compilation");
    }

    // Every corrupt persistent record must be treated as a miss, never passed
    // to Vulkan or surfaced as a compilation failure.
    for (const auto& path : CacheFiles(cache)) Write(path, "corrupt");
    {
        klvk::ShaderCacheManager manager(sources, cache);
        Ensure(manager.GetOrCompile(shader) != nullptr, "corrupt cache did not fall back to compilation");
    }
    std::filesystem::remove_all(root);
}

}  // namespace

int main()
{
    try
    {
        Run();
        fmt::println("shader cache tests passed");
        return 0;
    }
    catch (const std::exception& exception)
    {
        fmt::println(stderr, "{}", exception.what());
        return 1;
    }
}
