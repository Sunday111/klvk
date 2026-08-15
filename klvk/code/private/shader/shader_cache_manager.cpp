#include "klvk/shader/shader_cache_manager.hpp"

#include <fmt/format.h>
#include <slang-com-ptr.h>
#include <slang-tag-version.h>
#include <slang.h>

#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <nlohmann/json.hpp>
#include <ranges>
#include <span>
#include <sstream>
#include <utility>

#include "klvk/error_handling.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/integral_aliases.hpp"
#include "klvk/platform/os/os.hpp"
#include "klvk/shader/shader_interface.hpp"
#include "shader/shader_interface_json.hpp"
#include "shader/shader_reflection_json.hpp"

namespace klvk
{
namespace
{

constexpr u64 kFnvOffset = 14695981039346656037ull;
constexpr u64 kFnvPrime = 1099511628211ull;
constexpr u32 kCacheFormatVersion = 3;
constexpr u32 kSpirvMagic = 0x07230203;
constexpr size_t kMaximumSpirvBytes = 256 * 1024 * 1024;
constexpr size_t kMaximumMetadataBytes = 16 * 1024 * 1024;
constexpr std::array<char, 8> kCacheMagic{'K', 'L', 'V', 'K', 'S', 'P', 'V', '2'};

struct CacheHeader
{
    std::array<char, 8> magic{};
    u32 format_version = 0;
    u32 reserved = 0;
    u64 key = 0;
    u64 word_count = 0;
    u64 metadata_size = 0;
    u64 spirv_hash = 0;
    u64 metadata_hash = 0;
};

static_assert(std::is_trivially_copyable_v<CacheHeader>);

u64 HashBytes(u64 hash, const void* bytes, size_t size)
{
    const auto* data = static_cast<const u8*>(bytes);
    for (size_t i = 0; i != size; ++i)
    {
        hash ^= data[i];
        hash *= kFnvPrime;
    }
    return hash;
}

template <typename T>
u64 HashValue(u64 hash, const T& value)
{
    return HashBytes(hash, &value, sizeof(value));
}

u64 HashWords(std::span<const u32> words)
{
    return HashBytes(kFnvOffset, words.data(), words.size_bytes());
}

u64 HashString(std::string_view text)
{
    return HashBytes(kFnvOffset, text.data(), text.size());
}

std::filesystem::path CachePath(const std::filesystem::path& root, u64 key)
{
    std::ostringstream name;
    name << std::hex << std::setfill('0') << std::setw(16) << key << ".spv.cache";
    return root / name.str();
}

constexpr std::string_view kSlangTargetProfile = "spirv_1_6";
constexpr SlangCompileTarget kSlangTargetFormat = SLANG_SPIRV;
constexpr SlangTargetFlags kSlangTargetFlags = kDefaultTargetFlags;
constexpr SlangFloatingPointMode kSlangFloatingPointMode = SLANG_FLOATING_POINT_MODE_DEFAULT;
constexpr SlangLineDirectiveMode kSlangLineDirectiveMode = SLANG_LINE_DIRECTIVE_MODE_DEFAULT;
constexpr bool kSlangForceScalarBufferLayout = false;
constexpr slang::SessionFlags kSlangSessionFlags = slang::kSessionFlags_None;
constexpr SlangMatrixLayoutMode kSlangMatrixLayout = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
constexpr bool kSlangEnableEffectAnnotations = false;
constexpr bool kSlangAllowGlslSyntax = false;
constexpr bool kSlangSkipSpirvValidation = false;

// Slang cache entries are currently restricted to self-contained source files.
// Hash every output-affecting target/session option used below as well as the
// compiler version. Dependency-bearing modules are rejected at the compiler
// boundary until their transitive contents can participate in this key.
u64 MakeSlangKey(std::string_view source)
{
    u64 hash = HashBytes(kFnvOffset, source.data(), source.size());
    constexpr std::string_view toolchain = SLANG_TAG_VERSION;
    hash = HashBytes(hash, toolchain.data(), toolchain.size());
    hash = HashValue(hash, kSlangTargetFormat);
    hash = HashBytes(hash, kSlangTargetProfile.data(), kSlangTargetProfile.size());
    hash = HashValue(hash, kSlangTargetFlags);
    hash = HashValue(hash, kSlangFloatingPointMode);
    hash = HashValue(hash, kSlangLineDirectiveMode);
    hash = HashValue(hash, kSlangForceScalarBufferLayout);
    hash = HashValue(hash, kSlangSessionFlags);
    hash = HashValue(hash, kSlangMatrixLayout);
    hash = HashValue(hash, kSlangEnableEffectAnnotations);
    hash = HashValue(hash, kSlangAllowGlslSyntax);
    hash = HashValue(hash, kSlangSkipSpirvValidation);
    hash = HashValue(hash, ShaderInterfaceJson::kVersion);
    hash = HashValue(hash, kCacheFormatVersion);
    return hash;
}

slang::IGlobalSession& SlangGlobalSession()
{
    // The Slang global session is a process-wide, reusable compiler. Created on
    // first use so a run that compiles only GLSL never pays to spin it up.
    static Slang::ComPtr<slang::IGlobalSession> session = []
    {
        Slang::ComPtr<slang::IGlobalSession> created;
        ErrorHandling::Ensure(
            SLANG_SUCCEEDED(slang::createGlobalSession(created.writeRef())) && created,
            "Failed to create Slang global session");
        return created;
    }();
    return *session;
}

std::string BlobText(slang::IBlob* blob)
{
    if (blob == nullptr || blob->getBufferSize() == 0) return {};
    std::string text(static_cast<const char*>(blob->getBufferPointer()), blob->getBufferSize());
    return text;
}

struct SlangCompileResult
{
    std::vector<u32> spirv;
    ShaderInterface interface;
};

SlangCompileResult CompileSlangToSpirv(const std::string& source, const std::filesystem::path& source_path)
{
    slang::IGlobalSession& global = SlangGlobalSession();

    slang::TargetDesc target{};
    target.format = kSlangTargetFormat;
    target.profile = global.findProfile(kSlangTargetProfile.data());
    target.flags = kSlangTargetFlags;
    target.floatingPointMode = kSlangFloatingPointMode;
    target.lineDirectiveMode = kSlangLineDirectiveMode;
    target.forceGLSLScalarBufferLayout = kSlangForceScalarBufferLayout;

    slang::SessionDesc session_desc{};
    session_desc.targets = &target;
    session_desc.targetCount = 1;
    session_desc.flags = kSlangSessionFlags;
    session_desc.defaultMatrixLayoutMode = kSlangMatrixLayout;
    session_desc.enableEffectAnnotations = kSlangEnableEffectAnnotations;
    session_desc.allowGLSLSyntax = kSlangAllowGlslSyntax;
    session_desc.skipSPIRVValidation = kSlangSkipSpirvValidation;

    Slang::ComPtr<slang::ISession> session;
    ErrorHandling::Ensure(
        SLANG_SUCCEEDED(global.createSession(session_desc, session.writeRef())) && session,
        "Failed to create Slang session for '{}'",
        source_path.string());

    const std::string module_name = source_path.stem().string();
    Slang::ComPtr<slang::IBlob> diagnostics;
    slang::IModule* module = session->loadModuleFromSourceString(
        module_name.c_str(),
        source_path.string().c_str(),
        source.c_str(),
        diagnostics.writeRef());
    ErrorHandling::Ensure(
        module != nullptr,
        "Failed to compile Slang shader '{}':\n{}",
        source_path.string(),
        BlobText(diagnostics));
    const SlangInt32 dependency_count = module->getDependencyFileCount();
    bool has_external_dependency = false;
    std::string external_dependencies;
    for (SlangInt32 index = 0; index != dependency_count; ++index)
    {
        const char* dependency_path = module->getDependencyFilePath(index);
        if (dependency_path == nullptr ||
            std::filesystem::weakly_canonical(dependency_path) != std::filesystem::weakly_canonical(source_path))
        {
            has_external_dependency = true;
            if (!external_dependencies.empty()) external_dependencies += ", ";
            external_dependencies += dependency_path == nullptr ? "<unknown>" : dependency_path;
        }
    }
    ErrorHandling::Ensure(
        !has_external_dependency,
        "Slang shader '{}' references external dependency files ({}); imports and includes are unsupported until "
        "shader "
        "cache keys track transitive dependency contents",
        source_path.string(),
        external_dependencies);

    // One stage per file, entry point named main so the pipeline's hardcoded
    // "main" entry name resolves in the emitted SPIR-V.
    Slang::ComPtr<slang::IEntryPoint> entry_point;
    ErrorHandling::Ensure(
        SLANG_SUCCEEDED(module->findEntryPointByName("main", entry_point.writeRef())) && entry_point,
        "Slang shader '{}' must define an entry point named 'main'",
        source_path.string());

    const std::array<slang::IComponentType*, 2> components{module, entry_point.get()};
    Slang::ComPtr<slang::IComponentType> composed;
    diagnostics.setNull();
    ErrorHandling::Ensure(
        SLANG_SUCCEEDED(session->createCompositeComponentType(
            components.data(),
            components.size(),
            composed.writeRef(),
            diagnostics.writeRef())) &&
            composed,
        "Failed to compose Slang program '{}':\n{}",
        source_path.string(),
        BlobText(diagnostics));

    Slang::ComPtr<slang::IComponentType> linked;
    diagnostics.setNull();
    ErrorHandling::Ensure(
        SLANG_SUCCEEDED(composed->link(linked.writeRef(), diagnostics.writeRef())) && linked,
        "Failed to link Slang program '{}':\n{}",
        source_path.string(),
        BlobText(diagnostics));

    Slang::ComPtr<slang::IBlob> spirv;
    diagnostics.setNull();
    ErrorHandling::Ensure(
        SLANG_SUCCEEDED(linked->getEntryPointCode(0, 0, spirv.writeRef(), diagnostics.writeRef())) && spirv,
        "Failed to emit SPIR-V for Slang shader '{}':\n{}",
        source_path.string(),
        BlobText(diagnostics));

    const size_t byte_size = spirv->getBufferSize();
    ErrorHandling::Ensure(
        byte_size != 0 && byte_size % sizeof(u32) == 0,
        "Slang produced an invalid SPIR-V size for '{}'",
        source_path.string());
    const auto* words = static_cast<const u32*>(spirv->getBufferPointer());
    const size_t word_count = byte_size / sizeof(u32);
    diagnostics.setNull();
    slang::ProgramLayout* layout = linked->getLayout(0, diagnostics.writeRef());
    ErrorHandling::Ensure(
        layout != nullptr,
        "Failed to reflect Slang shader '{}':\n{}",
        source_path.string(),
        BlobText(diagnostics));
    return {
        .spirv = std::vector<u32>(words, words + word_count),
        .interface = ShaderReflectionJson::Read(*layout),
    };
}

}  // namespace

ShaderCacheManager::ShaderCacheManager(const std::filesystem::path& source_root, std::filesystem::path cache_root)
    : ShaderCacheManager(source_root, std::move(cache_root), Settings{})
{
}

ShaderCacheManager::ShaderCacheManager(
    const std::filesystem::path& source_root,
    std::filesystem::path cache_root,
    Settings settings)
    : source_root_(std::filesystem::weakly_canonical(source_root)),
      cache_root_(std::move(cache_root)),
      settings_(settings)
{
    ErrorHandling::Ensure(settings_.flush_interval.count() > 0, "Shader cache flush interval must be positive");
    if (cache_root_.empty()) cache_root_ = source_root_.parent_path().parent_path() / "shader_cache";
    std::filesystem::create_directories(cache_root_);
    worker_ = std::thread(&ShaderCacheManager::WorkerMain, this);
}

ShaderCacheManager::~ShaderCacheManager()
{
    {
        std::scoped_lock lock(mutex_);
        stopping_ = true;
    }
    condition_.notify_all();
    if (worker_.joinable()) worker_.join();
}

std::shared_ptr<const CompiledShader> ShaderCacheManager::GetOrCompile(const std::filesystem::path& source_path)
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
    const u64 key = MakeSlangKey(source);

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

        std::scoped_lock lock(mutex_);
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
                std::scoped_lock lock(mutex_);
                job.entry->shader = std::move(cached);
                job.entry->state = EntryState::Ready;
            }
            condition_.notify_all();
            return;
        }

        SlangCompileResult result = CompileSlangToSpirv(job.source, job.source_path);
        std::vector<u32> spirv_words = std::move(result.spirv);
        std::shared_ptr<const ShaderInterface> interface =
            std::make_shared<const ShaderInterface>(std::move(result.interface));

        auto words = std::make_shared<const std::vector<u32>>(std::move(spirv_words));
        ErrorHandling::Ensure(!words->empty() && words->front() == kSpirvMagic, "Compiler returned invalid SPIR-V");
        auto shader = std::make_shared<const CompiledShader>(CompiledShader{
            .spirv = std::move(words),
            .interface = std::move(interface),
        });
        {
            std::scoped_lock lock(mutex_);
            job.entry->shader = std::move(shader);
            job.entry->generation = next_generation_++;
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

void ShaderCacheManager::FlushDirtyEntries()
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
        std::filesystem::path temporary;
        try
        {
            const std::filesystem::path destination = CachePath(cache_root_, snapshot.key);
            temporary = destination;
            static std::atomic<u64> temporary_sequence = 0;
            temporary += fmt::format(
                ".tmp.{}.{}",
                os::GetProcessId(),
                temporary_sequence.fetch_add(1, std::memory_order_relaxed));
            const std::string metadata =
                snapshot.shader->interface ? ShaderInterfaceJson::Write(*snapshot.shader->interface) : std::string{};
            const CacheHeader header{
                .magic = kCacheMagic,
                .format_version = kCacheFormatVersion,
                .key = snapshot.key,
                .word_count = snapshot.shader->spirv->size(),
                .metadata_size = metadata.size(),
                .spirv_hash = HashWords(*snapshot.shader->spirv),
                .metadata_hash = HashString(metadata),
            };
            {
                std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
                ErrorHandling::Ensure(file.is_open(), "Failed to open shader cache file '{}'", temporary.string());
                file.write(reinterpret_cast<const char*>(&header), sizeof(header));
                file.write(
                    reinterpret_cast<const char*>(snapshot.shader->spirv->data()),
                    static_cast<std::streamsize>(snapshot.shader->spirv->size() * sizeof(u32)));
                file.write(metadata.data(), static_cast<std::streamsize>(metadata.size()));
                file.flush();
                ErrorHandling::Ensure(file.good(), "Failed to write shader cache file '{}'", temporary.string());
                file.close();
                ErrorHandling::Ensure(!file.fail(), "Failed to close shader cache file '{}'", temporary.string());
            }
            Filesystem::InstallFileAtomically(temporary, destination);
            std::scoped_lock lock(mutex_);
            if (snapshot.entry->generation == snapshot.generation)
            {
                snapshot.entry->persisted_generation = snapshot.generation;
            }
        }
        catch (const std::exception& exception)
        {
            if (!temporary.empty())
            {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
            }
            fmt::println(stderr, "[shader cache] {}", exception.what());
        }
    }
}

std::shared_ptr<const CompiledShader> ShaderCacheManager::TryLoad(u64 key) const
{
    const std::filesystem::path path = CachePath(cache_root_, key);
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};
    const std::streamsize size = file.tellg();
    if (std::cmp_less(size, sizeof(CacheHeader)) ||
        std::cmp_greater(size, sizeof(CacheHeader) + kMaximumSpirvBytes + kMaximumMetadataBytes))
    {
        return {};
    }
    file.seekg(0);
    CacheHeader header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file || header.magic != kCacheMagic || header.format_version != kCacheFormatVersion || header.key != key ||
        header.word_count == 0 || header.word_count > kMaximumSpirvBytes / sizeof(u32) ||
        header.metadata_size > kMaximumMetadataBytes ||
        std::cmp_not_equal(size, sizeof(CacheHeader) + header.word_count * sizeof(u32) + header.metadata_size))
    {
        return {};
    }
    auto words = std::make_shared<std::vector<u32>>(static_cast<size_t>(header.word_count));
    file.read(reinterpret_cast<char*>(words->data()), static_cast<std::streamsize>(words->size() * sizeof(u32)));
    std::string metadata(static_cast<size_t>(header.metadata_size), '\0');
    file.read(metadata.data(), static_cast<std::streamsize>(metadata.size()));
    if (!file || words->front() != kSpirvMagic || HashWords(*words) != header.spirv_hash ||
        HashString(metadata) != header.metadata_hash)
    {
        return {};
    }
    std::shared_ptr<const ShaderInterface> interface;
    if (!metadata.empty())
    {
        try
        {
            interface = std::make_shared<const ShaderInterface>(ShaderInterfaceJson::Read(metadata));
        }
        catch (const std::exception&)
        {
            return {};
        }
    }
    return std::make_shared<const CompiledShader>(CompiledShader{
        .spirv = std::move(words),
        .interface = std::move(interface),
    });
}

}  // namespace klvk
