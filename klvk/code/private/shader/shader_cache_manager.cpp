#include "klvk/shader/shader_cache_manager.hpp"

#include <array>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <shaderc/shaderc.hpp>
#include <span>
#include <sstream>

#include "klvk/error_handling.hpp"
#include "klvk/filesystem/filesystem.hpp"

namespace klvk
{
namespace
{

constexpr uint64_t kFnvOffset = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;
constexpr uint32_t kCacheFormatVersion = 1;
constexpr uint32_t kSpirvMagic = 0x07230203;
constexpr size_t kMaximumSpirvBytes = 256 * 1024 * 1024;
constexpr std::array<char, 8> kCacheMagic{'K', 'L', 'V', 'K', 'S', 'P', 'V', '1'};

struct CacheHeader
{
    std::array<char, 8> magic{};
    uint32_t format_version = 0;
    uint32_t spirv_version = 0;
    uint32_t spirv_revision = 0;
    uint32_t reserved = 0;
    uint64_t key = 0;
    uint64_t word_count = 0;
    uint64_t payload_hash = 0;
};

static_assert(std::is_trivially_copyable_v<CacheHeader>);

uint64_t HashBytes(uint64_t hash, const void* bytes, size_t size)
{
    const auto* data = static_cast<const uint8_t*>(bytes);
    for (size_t i = 0; i != size; ++i)
    {
        hash ^= data[i];
        hash *= kFnvPrime;
    }
    return hash;
}

template <typename T>
uint64_t HashValue(uint64_t hash, const T& value)
{
    return HashBytes(hash, &value, sizeof(value));
}

uint64_t HashWords(std::span<const uint32_t> words)
{
    return HashBytes(kFnvOffset, words.data(), words.size_bytes());
}

std::filesystem::path CachePath(const std::filesystem::path& root, uint64_t key)
{
    std::ostringstream name;
    name << std::hex << std::setfill('0') << std::setw(16) << key << ".spv.cache";
    return root / name.str();
}

shaderc_shader_kind ShaderKind(const std::filesystem::path& path)
{
    const std::string extension = path.extension().string();
    if (extension == ".vert") return shaderc_glsl_vertex_shader;
    if (extension == ".frag") return shaderc_glsl_fragment_shader;
    if (extension == ".geom") return shaderc_glsl_geometry_shader;
    if (extension == ".comp") return shaderc_glsl_compute_shader;
    if (extension == ".tesc") return shaderc_glsl_tess_control_shader;
    if (extension == ".tese") return shaderc_glsl_tess_evaluation_shader;
    ErrorHandling::ThrowWithMessage("Unsupported shader stage extension '{}' for {}", extension, path.string());
    return shaderc_glsl_infer_from_source;
}

uint64_t MakeKey(std::string_view source, shaderc_shader_kind kind, uint32_t spirv_version, uint32_t spirv_revision)
{
    uint64_t hash = HashBytes(kFnvOffset, source.data(), source.size());
    hash = HashValue(hash, kind);
    hash = HashValue(hash, spirv_version);
    hash = HashValue(hash, spirv_revision);
    hash = HashValue(hash, kCacheFormatVersion);
#ifdef NDEBUG
    constexpr bool optimize = true;
#else
    constexpr bool optimize = false;
#endif
    return HashValue(hash, optimize);
}

}  // namespace

ShaderCacheManager::ShaderCacheManager(std::filesystem::path source_root, std::filesystem::path cache_root)
    : ShaderCacheManager(std::move(source_root), std::move(cache_root), Settings{})
{
}

ShaderCacheManager::ShaderCacheManager(
    std::filesystem::path source_root,
    std::filesystem::path cache_root,
    Settings settings)
    : source_root_(std::filesystem::weakly_canonical(std::move(source_root))),
      cache_root_(std::move(cache_root)),
      settings_(settings)
{
    ErrorHandling::Ensure(settings_.flush_interval.count() > 0, "Shader cache flush interval must be positive");
    if (cache_root_.empty()) cache_root_ = source_root_.parent_path().parent_path() / "shader_cache";
    std::filesystem::create_directories(cache_root_);
    shaderc_get_spv_version(&compiler_spirv_version_, &compiler_spirv_revision_);
    worker_ = std::thread(&ShaderCacheManager::WorkerMain, this);
}

ShaderCacheManager::~ShaderCacheManager()
{
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    condition_.notify_all();
    if (worker_.joinable()) worker_.join();
}

std::shared_ptr<const std::vector<uint32_t>> ShaderCacheManager::GetOrCompile(const std::filesystem::path& source_path)
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
    const shaderc_shader_kind kind = ShaderKind(canonical_path);
    const uint64_t key = MakeKey(source, kind, compiler_spirv_version_, compiler_spirv_revision_);

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
    return entry->spirv;
}

void ShaderCacheManager::WorkerMain()
{
    auto next_flush = std::chrono::steady_clock::now() + settings_.flush_interval;
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
            next_flush = std::chrono::steady_clock::now() + settings_.flush_interval;
        }

        std::lock_guard lock(mutex_);
        // Shutdown makes one final best-effort flush. A read-only/full disk must
        // never turn application destruction into an infinite join.
        if (stopping_ && jobs_.empty()) break;
    }
}

void ShaderCacheManager::Compile(const CompileJob& job)
{
    try
    {
        if (auto cached = TryLoad(job.key))
        {
            {
                std::lock_guard lock(mutex_);
                job.entry->spirv = std::move(cached);
                job.entry->state = EntryState::Ready;
            }
            condition_.notify_all();
            return;
        }

        shaderc::Compiler compiler;
        shaderc::CompileOptions options;
        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
        options.SetTargetSpirv(shaderc_spirv_version_1_6);
#ifdef NDEBUG
        options.SetOptimizationLevel(shaderc_optimization_level_performance);
#else
        options.SetOptimizationLevel(shaderc_optimization_level_zero);
        options.SetGenerateDebugInfo();
#endif
        const auto result = compiler.CompileGlslToSpv(
            job.source,
            ShaderKind(job.source_path),
            job.source_path.string().c_str(),
            options);
        ErrorHandling::Ensure(
            result.GetCompilationStatus() == shaderc_compilation_status_success,
            "Failed to compile shader '{}':\n{}",
            job.source_path.string(),
            result.GetErrorMessage());
        auto words = std::make_shared<const std::vector<uint32_t>>(result.cbegin(), result.cend());
        ErrorHandling::Ensure(!words->empty() && words->front() == kSpirvMagic, "Compiler returned invalid SPIR-V");
        {
            std::lock_guard lock(mutex_);
            job.entry->spirv = std::move(words);
            job.entry->generation = next_generation_++;
            job.entry->state = EntryState::Ready;
        }
    }
    catch (...)
    {
        std::lock_guard lock(mutex_);
        job.entry->failure = std::current_exception();
        job.entry->state = EntryState::Failed;
    }
    condition_.notify_all();
}

void ShaderCacheManager::FlushDirtyEntries()
{
    struct Snapshot
    {
        uint64_t key = 0;
        uint64_t generation = 0;
        std::shared_ptr<const std::vector<uint32_t>> spirv;
        std::shared_ptr<Entry> entry;
    };
    std::vector<Snapshot> snapshots;
    {
        std::lock_guard lock(mutex_);
        for (const auto& [key, entry] : entries_)
        {
            if (entry->state == EntryState::Ready && entry->spirv && entry->generation != entry->persisted_generation)
                snapshots.push_back({key, entry->generation, entry->spirv, entry});
        }
    }

    for (const Snapshot& snapshot : snapshots)
    {
        std::filesystem::path temporary;
        try
        {
            const std::filesystem::path destination = CachePath(cache_root_, snapshot.key);
            temporary = destination;
            temporary += ".tmp." + std::to_string(reinterpret_cast<uintptr_t>(this)) + "." +
                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            const CacheHeader header{
                .magic = kCacheMagic,
                .format_version = kCacheFormatVersion,
                .spirv_version = compiler_spirv_version_,
                .spirv_revision = compiler_spirv_revision_,
                .key = snapshot.key,
                .word_count = snapshot.spirv->size(),
                .payload_hash = HashWords(*snapshot.spirv),
            };
            {
                std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
                ErrorHandling::Ensure(file.is_open(), "Failed to open shader cache file '{}'", temporary.string());
                file.write(reinterpret_cast<const char*>(&header), sizeof(header));
                file.write(
                    reinterpret_cast<const char*>(snapshot.spirv->data()),
                    static_cast<std::streamsize>(snapshot.spirv->size() * sizeof(uint32_t)));
                file.flush();
                ErrorHandling::Ensure(file.good(), "Failed to write shader cache file '{}'", temporary.string());
            }
            std::error_code error;
            std::filesystem::rename(temporary, destination, error);
            if (error)
            {
                std::filesystem::remove(destination, error);
                error.clear();
                std::filesystem::rename(temporary, destination, error);
            }
            ErrorHandling::Ensure(
                !error,
                "Failed to install shader cache file '{}': {}",
                destination.string(),
                error.message());
            std::lock_guard lock(mutex_);
            if (snapshot.entry->generation == snapshot.generation)
                snapshot.entry->persisted_generation = snapshot.generation;
        }
        catch (const std::exception& exception)
        {
            if (!temporary.empty())
            {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
            }
            std::cerr << "[shader cache] " << exception.what() << '\n';
        }
    }
}

std::shared_ptr<const std::vector<uint32_t>> ShaderCacheManager::TryLoad(uint64_t key) const
{
    const std::filesystem::path path = CachePath(cache_root_, key);
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};
    const std::streamsize size = file.tellg();
    if (size < static_cast<std::streamsize>(sizeof(CacheHeader)) ||
        size > static_cast<std::streamsize>(sizeof(CacheHeader) + kMaximumSpirvBytes))
        return {};
    file.seekg(0);
    CacheHeader header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file || header.magic != kCacheMagic || header.format_version != kCacheFormatVersion || header.key != key ||
        header.spirv_version != compiler_spirv_version_ || header.spirv_revision != compiler_spirv_revision_ ||
        header.word_count == 0 || header.word_count > kMaximumSpirvBytes / sizeof(uint32_t) ||
        size != static_cast<std::streamsize>(sizeof(CacheHeader) + header.word_count * sizeof(uint32_t)))
        return {};
    auto words = std::make_shared<std::vector<uint32_t>>(static_cast<size_t>(header.word_count));
    file.read(reinterpret_cast<char*>(words->data()), static_cast<std::streamsize>(words->size() * sizeof(uint32_t)));
    if (!file || words->front() != kSpirvMagic || HashWords(*words) != header.payload_hash) return {};
    return words;
}

}  // namespace klvk
