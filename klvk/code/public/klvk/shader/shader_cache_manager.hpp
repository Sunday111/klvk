#pragma once

#include <chrono>
#include <condition_variable>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "klvk/integral_aliases.hpp"
#include "klvk/shader/shader_interface.hpp"

namespace klvk
{

// Compiles GLSL or Slang on one background thread and owns the process-local SPIR-V cache.
// Concurrent requests for the same source are coalesced. Callers block only when
// their requested shader is neither in memory nor in the persistent cache.
class ShaderCacheManager
{
public:
    struct Settings
    {
        std::chrono::milliseconds flush_interval{2000};
    };

    explicit ShaderCacheManager(const std::filesystem::path& source_root, std::filesystem::path cache_root = {});
    ShaderCacheManager(const std::filesystem::path& source_root, std::filesystem::path cache_root, Settings settings);
    ShaderCacheManager(const ShaderCacheManager&) = delete;
    ShaderCacheManager(ShaderCacheManager&&) = delete;
    ~ShaderCacheManager();

    // source_path must name a GLSL or Slang stage below source_root. Slang stages must be
    // self-contained: imports and includes are rejected until persistent cache entries track
    // transitive dependency contents. The returned data is immutable and remains alive as long
    // as either the caller or manager holds it.
    [[nodiscard]] std::shared_ptr<const CompiledShader> GetOrCompile(const std::filesystem::path& source_path);

    [[nodiscard]] const std::filesystem::path& GetSourceRoot() const noexcept { return source_root_; }
    [[nodiscard]] const std::filesystem::path& GetCacheRoot() const noexcept { return cache_root_; }

private:
    enum class EntryState : u8
    {
        Pending,
        Ready,
        Failed
    };

    struct Entry
    {
        EntryState state = EntryState::Pending;
        std::shared_ptr<const CompiledShader> shader;
        std::exception_ptr failure;
        u64 generation = 0;
        u64 persisted_generation = 0;
    };

    struct CompileJob
    {
        u64 key = 0;
        std::filesystem::path source_path;
        std::string source;
        std::shared_ptr<Entry> entry;
    };

    void WorkerMain();
    void Compile(const CompileJob& job);
    void FlushDirtyEntries();
    [[nodiscard]] std::shared_ptr<const CompiledShader> TryLoad(u64 key) const;

    std::filesystem::path source_root_;
    std::filesystem::path cache_root_;
    Settings settings_;

    std::mutex mutex_;
    std::condition_variable condition_;
    std::unordered_map<u64, std::shared_ptr<Entry>> entries_;
    std::queue<CompileJob> jobs_;
    bool stopping_ = false;
    u64 next_generation_ = 1;
    std::thread worker_;
};

}  // namespace klvk
