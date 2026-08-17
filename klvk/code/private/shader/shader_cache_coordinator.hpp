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

#include "klvk/integral_aliases.hpp"
#include "klvk/shader/shader_interface.hpp"
#include "shader_cache_store.hpp"
#include "slang_shader_compiler.hpp"

namespace klvk
{

class ShaderCacheCoordinator
{
public:
    ShaderCacheCoordinator(
        const std::filesystem::path& source_root,
        std::filesystem::path cache_root,
        std::chrono::milliseconds flush_interval);
    ShaderCacheCoordinator(const ShaderCacheCoordinator&) = delete;
    ShaderCacheCoordinator(ShaderCacheCoordinator&&) = delete;
    ~ShaderCacheCoordinator();

    [[nodiscard]] std::shared_ptr<const CompiledShader> GetOrCompile(const std::filesystem::path& source_path);
    [[nodiscard]] const std::filesystem::path& GetSourceRoot() const noexcept { return source_root_; }
    [[nodiscard]] const std::filesystem::path& GetCacheRoot() const noexcept { return store_.GetRoot(); }

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
    [[nodiscard]] static std::filesystem::path ResolveCacheRoot(
        const std::filesystem::path& source_root,
        std::filesystem::path cache_root);
    [[nodiscard]] static std::chrono::milliseconds ValidateFlushInterval(std::chrono::milliseconds flush_interval);

    std::filesystem::path source_root_;
    std::chrono::milliseconds flush_interval_;
    SlangShaderCompiler compiler_;
    ShaderCacheStore store_;

    std::mutex mutex_;
    std::condition_variable condition_;
    std::unordered_map<u64, std::shared_ptr<Entry>> entries_;
    std::queue<CompileJob> jobs_;
    bool stopping_ = false;
    u64 next_generation_ = 1;
    std::thread worker_;
};

}  // namespace klvk
