#pragma once

#include <chrono>
#include <filesystem>
#include <memory>

#include "klvk/shader/shader_interface.hpp"

namespace klvk
{

// Compiles Slang on one background thread and owns the process-local SPIR-V cache.
// Concurrent requests for the same source are coalesced.
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

    // The source must be a self-contained Slang stage below source_root.
    [[nodiscard]] std::shared_ptr<const CompiledShader> GetOrCompile(const std::filesystem::path& source_path);

    [[nodiscard]] const std::filesystem::path& GetSourceRoot() const noexcept;
    [[nodiscard]] const std::filesystem::path& GetCacheRoot() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace klvk
