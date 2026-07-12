#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace klvk
{

// Compiles GLSL on one background thread and owns the process-local SPIR-V cache.
// Concurrent requests for the same source are coalesced. Callers block only when
// their requested shader is neither in memory nor in the persistent cache.
class ShaderCacheManager
{
public:
    struct Settings
    {
        std::chrono::milliseconds flush_interval{2000};
    };

    ShaderCacheManager(std::filesystem::path source_root, std::filesystem::path cache_root = {});
    ShaderCacheManager(std::filesystem::path source_root, std::filesystem::path cache_root, Settings settings);
    ShaderCacheManager(const ShaderCacheManager&) = delete;
    ShaderCacheManager(ShaderCacheManager&&) = delete;
    ~ShaderCacheManager();

    // source_path must name a GLSL stage below source_root. The returned data is
    // immutable and remains alive as long as either the caller or manager holds it.
    [[nodiscard]] std::shared_ptr<const std::vector<uint32_t>> GetOrCompile(const std::filesystem::path& source_path);

    [[nodiscard]] const std::filesystem::path& GetSourceRoot() const noexcept { return source_root_; }
    [[nodiscard]] const std::filesystem::path& GetCacheRoot() const noexcept { return cache_root_; }

private:
    enum class EntryState : uint8_t
    {
        Pending,
        Ready,
        Failed
    };

    struct Entry
    {
        EntryState state = EntryState::Pending;
        std::shared_ptr<const std::vector<uint32_t>> spirv;
        std::exception_ptr failure;
        uint64_t generation = 0;
        uint64_t persisted_generation = 0;
    };

    struct CompileJob
    {
        uint64_t key = 0;
        std::filesystem::path source_path;
        std::string source;
        std::shared_ptr<Entry> entry;
    };

    void WorkerMain();
    void Compile(const CompileJob& job);
    void FlushDirtyEntries();
    [[nodiscard]] std::shared_ptr<const std::vector<uint32_t>> TryLoad(uint64_t key) const;

    std::filesystem::path source_root_;
    std::filesystem::path cache_root_;
    Settings settings_;
    uint32_t compiler_spirv_version_ = 0;
    uint32_t compiler_spirv_revision_ = 0;

    std::mutex mutex_;
    std::condition_variable condition_;
    std::unordered_map<uint64_t, std::shared_ptr<Entry>> entries_;
    std::queue<CompileJob> jobs_;
    bool stopping_ = false;
    uint64_t next_generation_ = 1;
    std::thread worker_;
};

}  // namespace klvk
