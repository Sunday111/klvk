#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <type_traits>

#include "klvk/integral_aliases.hpp"
#include "klvk/shader/shader_interface.hpp"

namespace klvk
{

class ShaderCacheStore
{
public:
    explicit ShaderCacheStore(std::filesystem::path root);

    [[nodiscard]] static u64 MakeKey(u64 compiler_key) noexcept;
    [[nodiscard]] std::shared_ptr<const CompiledShader> TryLoad(u64 key) const;
    void Save(u64 key, const CompiledShader& shader) const;

    [[nodiscard]] const std::filesystem::path& GetRoot() const noexcept { return root_; }

private:
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

    static constexpr u32 kFormatVersion = 3;
    static constexpr u32 kSpirvMagic = 0x07230203;
    static constexpr size_t kMaximumSpirvBytes = 256 * 1024 * 1024;
    static constexpr size_t kMaximumMetadataBytes = 16 * 1024 * 1024;
    static constexpr std::array<char, 8> kMagic{'K', 'L', 'V', 'K', 'S', 'P', 'V', '2'};

    [[nodiscard]] std::filesystem::path GetPath(u64 key) const;

    std::filesystem::path root_;
};

}  // namespace klvk
