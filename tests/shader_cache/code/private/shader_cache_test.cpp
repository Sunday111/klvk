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
        if (entry.is_regular_file() && entry.path().extension() == ".cache") result.push_back(entry.path());
    return result;
}

void Run()
{
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("klvk_shader_cache_test_" + std::to_string(nonce));
    const std::filesystem::path sources = root / "sources";
    const std::filesystem::path cache = root / "cache";
    std::filesystem::create_directories(sources);
    const std::filesystem::path shader = sources / "test.comp";
    Write(shader, "#version 450\nlayout(local_size_x=1) in; void main() {}\n");

    std::shared_ptr<const std::vector<u32>> expected;
    {
        klvk::ShaderCacheManager manager(sources, cache, {.flush_interval = std::chrono::milliseconds(20)});
        std::vector<std::future<std::shared_ptr<const std::vector<u32>>>> futures;
        for (size_t i = 0; i != 16; ++i)
            futures.push_back(std::async(std::launch::async, [&] { return manager.GetOrCompile(shader); }));
        expected = futures.front().get();
        for (size_t i = 1; i != futures.size(); ++i)
            Ensure(futures[i].get() == expected, "concurrent requests were not coalesced");

        Write(shader, "#version 450\nthis is not GLSL\n");
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

        Write(shader, "#version 450\nlayout(local_size_x=2) in; void main() {}\n");
        Ensure(manager.GetOrCompile(shader) != nullptr, "cache did not recover after source correction");
    }

    const auto files = CacheFiles(cache);
    Ensure(files.size() == 2, "successful cache entries were not flushed at shutdown");
    {
        klvk::ShaderCacheManager manager(sources, cache);
        Ensure(manager.GetOrCompile(shader) != nullptr, "persistent entry could not be loaded");
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
