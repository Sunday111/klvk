#include "shader_cache_store.hpp"

#include <fmt/format.h>

#include <atomic>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "klvk/error_handling.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/platform/os/os.hpp"
#include "shader/shader_interface_json.hpp"
#include "shader_cache_hash.hpp"

namespace klvk
{

ShaderCacheStore::ShaderCacheStore(std::filesystem::path root) : root_(std::move(root))
{
    std::filesystem::create_directories(root_);
}

u64 ShaderCacheStore::MakeKey(u64 compiler_key) noexcept
{
    u64 key = ShaderCacheHash::Value(compiler_key, ShaderInterfaceJson::kVersion);
    return ShaderCacheHash::Value(key, kFormatVersion);
}

std::filesystem::path ShaderCacheStore::GetPath(u64 key) const
{
    std::ostringstream name;
    name << std::hex << std::setfill('0') << std::setw(16) << key << ".spv.cache";
    return root_ / name.str();
}

std::shared_ptr<const CompiledShader> ShaderCacheStore::TryLoad(u64 key) const
{
    std::ifstream file(GetPath(key), std::ios::binary | std::ios::ate);
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
    if (!file || header.magic != kMagic || header.format_version != kFormatVersion || header.key != key ||
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
    if (!file || words->front() != kSpirvMagic || ShaderCacheHash::Words(*words) != header.spirv_hash ||
        ShaderCacheHash::String(metadata) != header.metadata_hash)
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

void ShaderCacheStore::Save(u64 key, const CompiledShader& shader) const
{
    ErrorHandling::Ensure(shader.spirv != nullptr, "Cannot persist a shader without SPIR-V");
    const std::filesystem::path destination = GetPath(key);
    std::filesystem::path temporary = destination;
    static std::atomic<u64> temporary_sequence = 0;
    temporary +=
        fmt::format(".tmp.{}.{}", os::GetProcessId(), temporary_sequence.fetch_add(1, std::memory_order_relaxed));
    try
    {
        const std::string metadata = shader.interface ? ShaderInterfaceJson::Write(*shader.interface) : std::string{};
        const CacheHeader header{
            .magic = kMagic,
            .format_version = kFormatVersion,
            .key = key,
            .word_count = shader.spirv->size(),
            .metadata_size = metadata.size(),
            .spirv_hash = ShaderCacheHash::Words(*shader.spirv),
            .metadata_hash = ShaderCacheHash::String(metadata),
        };
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        ErrorHandling::Ensure(file.is_open(), "Failed to open shader cache file '{}'", temporary.string());
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.write(
            reinterpret_cast<const char*>(shader.spirv->data()),
            static_cast<std::streamsize>(shader.spirv->size() * sizeof(u32)));
        file.write(metadata.data(), static_cast<std::streamsize>(metadata.size()));
        file.flush();
        ErrorHandling::Ensure(file.good(), "Failed to write shader cache file '{}'", temporary.string());
        file.close();
        ErrorHandling::Ensure(!file.fail(), "Failed to close shader cache file '{}'", temporary.string());
        Filesystem::InstallFileAtomically(temporary, destination);
    }
    catch (...)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

}  // namespace klvk
