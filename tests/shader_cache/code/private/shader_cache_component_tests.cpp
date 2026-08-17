#include "shader_cache_component_tests.hpp"

#include <array>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "shader/shader_cache_hash.hpp"
#include "shader/shader_cache_store.hpp"
#include "shader/slang_shader_compiler.hpp"

void ShaderCacheComponentTests::Run(const std::filesystem::path& root)
{
    TestHash();
    TestCompiler(root / "compiler");
    TestStore(root / "store");
}

void ShaderCacheComponentTests::TestHash()
{
    constexpr u64 expected_hello_hash = 0xa430d84680aabd0b;
    Ensure(klvk::ShaderCacheHash::String("hello") == expected_hello_hash, "shader cache hash changed");
    Ensure(
        klvk::ShaderCacheHash::String("shader one") != klvk::ShaderCacheHash::String("shader two"),
        "shader cache hash ignored its input");
}

void ShaderCacheComponentTests::TestCompiler(const std::filesystem::path& root)
{
    std::filesystem::create_directories(root);
    const std::filesystem::path path = root / "component.comp.slang";
    const std::string source =
        "[[vk::constant_id(3)]] const uint MODE = 1;\n"
        "[shader(\"compute\")] [numthreads(4, 2, 1)] void main() { uint value = MODE; }\n";
    const u64 key = klvk::SlangShaderCompiler::MakeKey(source);
    Ensure(key == klvk::SlangShaderCompiler::MakeKey(source), "compiler key was not deterministic");
    Ensure(key != klvk::SlangShaderCompiler::MakeKey(source + "\n"), "compiler key ignored shader source changes");

    klvk::SlangShaderCompiler compiler;
    const std::shared_ptr<const klvk::CompiledShader> compiled = compiler.Compile(source, path);
    Ensure(compiled->spirv != nullptr && !compiled->spirv->empty(), "compiler returned no SPIR-V");
    Ensure(compiled->spirv->front() == 0x07230203, "compiler returned invalid SPIR-V");
    Ensure(compiled->interface != nullptr, "compiler returned no reflection");
    Ensure(compiled->interface->stage == vk::ShaderStageFlagBits::eCompute, "compiler reflected the wrong stage");
    Ensure(
        compiled->interface->workgroup_size == std::array<u32, 3>{4, 2, 1},
        "compiler reflected the wrong workgroup size");
    Ensure(
        compiled->interface->specialization_constants.size() == 1 &&
            compiled->interface->specialization_constants.front().id == 3,
        "compiler did not reflect the specialization constant");
}

void ShaderCacheComponentTests::TestStore(const std::filesystem::path& root)
{
    klvk::ShaderCacheStore store(root);
    Ensure(std::filesystem::is_directory(root), "store did not create its root");

    const u64 key = klvk::ShaderCacheStore::MakeKey(41);
    Ensure(key == klvk::ShaderCacheStore::MakeKey(41), "store key was not deterministic");
    Ensure(key != klvk::ShaderCacheStore::MakeKey(42), "store key ignored the compiler key");
    Ensure(store.TryLoad(key) == nullptr, "missing cache entry was reported as present");

    auto interface = std::make_shared<klvk::ShaderInterface>();
    interface->stage = vk::ShaderStageFlagBits::eCompute;
    interface->workgroup_size = {8, 4, 2};
    auto spirv = std::make_shared<const std::vector<u32>>(std::initializer_list<u32>{0x07230203, 0x00010600, 0, 1, 0});
    store.Save(key, {.spirv = spirv, .interface = interface});

    const std::shared_ptr<const klvk::CompiledShader> loaded = store.TryLoad(key);
    Ensure(loaded != nullptr, "store did not load a saved entry");
    Ensure(loaded->spirv != nullptr && *loaded->spirv == *spirv, "store changed saved SPIR-V");
    Ensure(loaded->interface != nullptr && *loaded->interface == *interface, "store changed saved reflection");

    std::filesystem::path cache_file;
    for (const auto& entry : std::filesystem::directory_iterator(root))
    {
        if (entry.path().extension() == ".cache") cache_file = entry.path();
    }
    Ensure(!cache_file.empty(), "store did not create a cache file");
    std::fstream file(cache_file, std::ios::binary | std::ios::in | std::ios::out);
    Ensure(file.is_open(), "failed to open cache entry for corruption test");
    file.seekp(-1, std::ios::end);
    const char corrupt = '\xff';
    file.write(&corrupt, 1);
    file.close();
    Ensure(store.TryLoad(key) == nullptr, "store accepted a corrupt cache entry");
}

void ShaderCacheComponentTests::Ensure(bool condition, std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}
