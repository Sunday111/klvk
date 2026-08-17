#include "klvk/shader/shader_cache_manager.hpp"

#include <utility>

#include "shader_cache_coordinator.hpp"

namespace klvk
{

class ShaderCacheManager::Impl
{
public:
    Impl(
        const std::filesystem::path& source_root,
        std::filesystem::path cache_root,
        std::chrono::milliseconds flush_interval)
        : coordinator_(source_root, std::move(cache_root), flush_interval)
    {
    }

    ShaderCacheCoordinator coordinator_;
};

ShaderCacheManager::ShaderCacheManager(const std::filesystem::path& source_root, std::filesystem::path cache_root)
    : ShaderCacheManager(source_root, std::move(cache_root), Settings{})
{
}

ShaderCacheManager::ShaderCacheManager(
    const std::filesystem::path& source_root,
    std::filesystem::path cache_root,
    Settings settings)
    : impl_(std::make_unique<Impl>(source_root, std::move(cache_root), settings.flush_interval))
{
}

ShaderCacheManager::~ShaderCacheManager() = default;

std::shared_ptr<const CompiledShader> ShaderCacheManager::GetOrCompile(const std::filesystem::path& source_path)
{
    return impl_->coordinator_.GetOrCompile(source_path);
}

const std::filesystem::path& ShaderCacheManager::GetSourceRoot() const noexcept
{
    return impl_->coordinator_.GetSourceRoot();
}

const std::filesystem::path& ShaderCacheManager::GetCacheRoot() const noexcept
{
    return impl_->coordinator_.GetCacheRoot();
}

}  // namespace klvk
