#include "shader_cache_coordinator.hpp"

#include <fmt/base.h>

#include <edt/threading/thread_name.hpp>
#include <optional>
#include <utility>
#include <vector>

#include "klvk/error_handling.hpp"
#include "klvk/filesystem/filesystem.hpp"

namespace klvk
{

std::filesystem::path ShaderCacheCoordinator::ResolveCacheRoot(
    const std::filesystem::path& source_root,
    std::filesystem::path cache_root)
{
    if (cache_root.empty()) return source_root.parent_path().parent_path() / "shader_cache";
    return cache_root;
}

std::chrono::milliseconds ShaderCacheCoordinator::ValidateFlushInterval(std::chrono::milliseconds flush_interval)
{
    ErrorHandling::Ensure(flush_interval.count() > 0, "Shader cache flush interval must be positive");
    return flush_interval;
}

ShaderCacheCoordinator::ShaderCacheCoordinator(
    const std::filesystem::path& source_root,
    std::filesystem::path cache_root,
    std::chrono::milliseconds flush_interval)
    : source_root_(std::filesystem::weakly_canonical(source_root)),
      flush_interval_(ValidateFlushInterval(flush_interval)),
      store_(ResolveCacheRoot(source_root_, std::move(cache_root)))
{
    worker_ = std::thread(&ShaderCacheCoordinator::WorkerMain, this);
}

ShaderCacheCoordinator::~ShaderCacheCoordinator()
{
    {
        std::scoped_lock lock(mutex_);
        stopping_ = true;
    }
    condition_.notify_all();
    if (worker_.joinable()) worker_.join();
}

std::shared_ptr<const CompiledShader> ShaderCacheCoordinator::GetOrCompile(const std::filesystem::path& source_path)
{
    const std::filesystem::path canonical_path = std::filesystem::weakly_canonical(source_path);
    const auto relative = canonical_path.lexically_relative(source_root_);
    ErrorHandling::Ensure(
        !relative.empty() && *relative.begin() != "..",
        "Shader source '{}' is outside shader root '{}'",
        canonical_path.string(),
        source_root_.string());
    std::string source;
    Filesystem::ReadFile(canonical_path, source);
    ErrorHandling::Ensure(
        canonical_path.extension() == ".slang",
        "Shader source '{}' is not Slang; it is the only language this compiles",
        canonical_path.string());
    const u64 key = ShaderCacheStore::MakeKey(SlangShaderCompiler::MakeKey(source));

    std::unique_lock lock(mutex_);
    auto iterator = entries_.find(key);
    if (iterator == entries_.end())
    {
        auto entry = std::make_shared<Entry>();
        jobs_.push({.key = key, .source_path = canonical_path, .source = std::move(source), .entry = entry});
        iterator = entries_.emplace(key, std::move(entry)).first;
        condition_.notify_one();
    }

    const std::shared_ptr<Entry> entry = iterator->second;
    condition_.wait(lock, [&] { return entry->state != EntryState::Pending; });
    if (entry->state == EntryState::Failed) std::rethrow_exception(entry->failure);
    return entry->shader;
}

void ShaderCacheCoordinator::WorkerMain()
{
    edt::SetCurrentThreadName("klvk_shader");
    auto next_flush = std::chrono::steady_clock::now() + flush_interval_;
    for (;;)
    {
        std::optional<CompileJob> job;
        bool should_flush = false;
        {
            std::unique_lock lock(mutex_);
            condition_.wait_until(lock, next_flush, [&] { return stopping_ || !jobs_.empty(); });
            if (!jobs_.empty())
            {
                job = std::move(jobs_.front());
                jobs_.pop();
            }
            should_flush = std::chrono::steady_clock::now() >= next_flush || (stopping_ && jobs_.empty());
            if (stopping_ && !job && jobs_.empty() && !should_flush) should_flush = true;
        }

        if (job) Compile(*job);
        if (should_flush)
        {
            FlushDirtyEntries();
            next_flush = std::chrono::steady_clock::now() + flush_interval_;
        }

        std::scoped_lock lock(mutex_);
        if (stopping_ && jobs_.empty()) break;
    }
}

void ShaderCacheCoordinator::Compile(const CompileJob& job)
{
    try
    {
        std::shared_ptr<const CompiledShader> shader = store_.TryLoad(job.key);
        const bool needs_persisting = shader == nullptr;
        if (needs_persisting) shader = compiler_.Compile(job.source, job.source_path);
        {
            std::scoped_lock lock(mutex_);
            job.entry->shader = std::move(shader);
            if (needs_persisting) job.entry->generation = next_generation_++;
            job.entry->state = EntryState::Ready;
        }
    }
    catch (...)
    {
        std::scoped_lock lock(mutex_);
        job.entry->failure = std::current_exception();
        job.entry->state = EntryState::Failed;
    }
    condition_.notify_all();
}

void ShaderCacheCoordinator::FlushDirtyEntries()
{
    struct Snapshot
    {
        u64 key = 0;
        u64 generation = 0;
        std::shared_ptr<const CompiledShader> shader;
        std::shared_ptr<Entry> entry;
    };
    std::vector<Snapshot> snapshots;
    {
        std::scoped_lock lock(mutex_);
        for (const auto& [key, entry] : entries_)
        {
            if (entry->state == EntryState::Ready && entry->shader && entry->generation != entry->persisted_generation)
            {
                snapshots.push_back(
                    {.key = key, .generation = entry->generation, .shader = entry->shader, .entry = entry});
            }
        }
    }

    for (const Snapshot& snapshot : snapshots)
    {
        try
        {
            store_.Save(snapshot.key, *snapshot.shader);
            std::scoped_lock lock(mutex_);
            if (snapshot.entry->generation == snapshot.generation)
            {
                snapshot.entry->persisted_generation = snapshot.generation;
            }
        }
        catch (const std::exception& exception)
        {
            fmt::println(stderr, "[shader cache] {}", exception.what());
        }
    }
}

}  // namespace klvk
