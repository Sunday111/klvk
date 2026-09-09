#pragma once

#include <slang-com-ptr.h>
#include <slang.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include "klvk/integral_aliases.hpp"
#include "klvk/shader/shader_interface.hpp"

namespace klvk
{

class SlangShaderCompiler
{
public:
    SlangShaderCompiler() = default;
    SlangShaderCompiler(const SlangShaderCompiler&) = delete;
    SlangShaderCompiler(SlangShaderCompiler&&) = delete;
    SlangShaderCompiler& operator=(const SlangShaderCompiler&) = delete;
    SlangShaderCompiler& operator=(SlangShaderCompiler&&) = delete;

    [[nodiscard]] static u64 MakeKey(std::string_view source) noexcept;
    [[nodiscard]] std::shared_ptr<const CompiledShader> Compile(
        const std::string& source,
        const std::filesystem::path& source_path);

private:
    static constexpr std::string_view kTargetProfile = "spirv_1_6";
    static constexpr SlangCompileTarget kTargetFormat = SLANG_SPIRV;
    static constexpr SlangTargetFlags kTargetFlags = kDefaultTargetFlags;
    static constexpr SlangFloatingPointMode kFloatingPointMode = SLANG_FLOATING_POINT_MODE_DEFAULT;
    static constexpr SlangLineDirectiveMode kLineDirectiveMode = SLANG_LINE_DIRECTIVE_MODE_DEFAULT;
    static constexpr bool kForceScalarBufferLayout = false;
    static constexpr slang::SessionFlags kSessionFlags = slang::kSessionFlags_None;
    static constexpr SlangMatrixLayoutMode kMatrixLayout = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
    static constexpr bool kEnableEffectAnnotations = false;
    static constexpr bool kAllowGlslSyntax = false;
    static constexpr bool kSkipSpirvValidation = false;
    static constexpr u32 kSpirvMagic = 0x07230203;

    [[nodiscard]] slang::IGlobalSession& GetGlobalSession();
    [[nodiscard]] static std::string GetBlobText(slang::IBlob* blob);
    static void EnsureSelfContained(slang::IModule& module, const std::filesystem::path& source_path);

    std::mutex mutex_;
    Slang::ComPtr<slang::IGlobalSession> global_session_;
};

}  // namespace klvk
