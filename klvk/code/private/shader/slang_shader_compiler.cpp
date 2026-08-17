#include "slang_shader_compiler.hpp"

#include <slang-com-ptr.h>
#include <slang-tag-version.h>
#include <slang.h>

#include <array>
#include <cstring>
#include <utility>
#include <vector>

#include "klvk/error_handling.hpp"
#include "shader/shader_reflection_json.hpp"
#include "shader_cache_hash.hpp"

namespace klvk
{

slang::IGlobalSession& SlangShaderCompiler::GetGlobalSession()
{
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

std::string SlangShaderCompiler::GetBlobText(slang::IBlob* blob)
{
    if (blob == nullptr || blob->getBufferSize() == 0) return {};
    return {static_cast<const char*>(blob->getBufferPointer()), blob->getBufferSize()};
}

void SlangShaderCompiler::EnsureSelfContained(slang::IModule& module, const std::filesystem::path& source_path)
{
    const SlangInt32 dependency_count = module.getDependencyFileCount();
    std::string external_dependencies;
    for (SlangInt32 index = 0; index != dependency_count; ++index)
    {
        const char* dependency_path = module.getDependencyFilePath(index);
        if (dependency_path != nullptr &&
            std::filesystem::weakly_canonical(dependency_path) == std::filesystem::weakly_canonical(source_path))
        {
            continue;
        }
        if (!external_dependencies.empty()) external_dependencies += ", ";
        external_dependencies += dependency_path == nullptr ? "<unknown>" : dependency_path;
    }
    ErrorHandling::Ensure(
        external_dependencies.empty(),
        "Slang shader '{}' references external dependency files ({}); imports and includes are unsupported until "
        "shader "
        "cache keys track transitive dependency contents",
        source_path.string(),
        external_dependencies);
}

u64 SlangShaderCompiler::MakeKey(std::string_view source) noexcept
{
    u64 hash = ShaderCacheHash::String(source);
    constexpr std::string_view toolchain = SLANG_TAG_VERSION;
    hash = ShaderCacheHash::String(hash, toolchain);
    hash = ShaderCacheHash::Value(hash, kTargetFormat);
    hash = ShaderCacheHash::String(hash, kTargetProfile);
    hash = ShaderCacheHash::Value(hash, kTargetFlags);
    hash = ShaderCacheHash::Value(hash, kFloatingPointMode);
    hash = ShaderCacheHash::Value(hash, kLineDirectiveMode);
    hash = ShaderCacheHash::Value(hash, kForceScalarBufferLayout);
    hash = ShaderCacheHash::Value(hash, kSessionFlags);
    hash = ShaderCacheHash::Value(hash, kMatrixLayout);
    hash = ShaderCacheHash::Value(hash, kEnableEffectAnnotations);
    hash = ShaderCacheHash::Value(hash, kAllowGlslSyntax);
    return ShaderCacheHash::Value(hash, kSkipSpirvValidation);
}

std::shared_ptr<const CompiledShader> SlangShaderCompiler::Compile(
    const std::string& source,
    const std::filesystem::path& source_path) const
{
    slang::IGlobalSession& global = GetGlobalSession();

    slang::TargetDesc target{};
    target.format = kTargetFormat;
    target.profile = global.findProfile(kTargetProfile.data());
    target.flags = kTargetFlags;
    target.floatingPointMode = kFloatingPointMode;
    target.lineDirectiveMode = kLineDirectiveMode;
    target.forceGLSLScalarBufferLayout = kForceScalarBufferLayout;

    slang::SessionDesc session_desc{};
    session_desc.targets = &target;
    session_desc.targetCount = 1;
    session_desc.flags = kSessionFlags;
    session_desc.defaultMatrixLayoutMode = kMatrixLayout;
    session_desc.enableEffectAnnotations = kEnableEffectAnnotations;
    session_desc.allowGLSLSyntax = kAllowGlslSyntax;
    session_desc.skipSPIRVValidation = kSkipSpirvValidation;

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
        GetBlobText(diagnostics));
    EnsureSelfContained(*module, source_path);

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
        GetBlobText(diagnostics));

    Slang::ComPtr<slang::IComponentType> linked;
    diagnostics.setNull();
    ErrorHandling::Ensure(
        SLANG_SUCCEEDED(composed->link(linked.writeRef(), diagnostics.writeRef())) && linked,
        "Failed to link Slang program '{}':\n{}",
        source_path.string(),
        GetBlobText(diagnostics));

    Slang::ComPtr<slang::IBlob> spirv;
    diagnostics.setNull();
    ErrorHandling::Ensure(
        SLANG_SUCCEEDED(linked->getEntryPointCode(0, 0, spirv.writeRef(), diagnostics.writeRef())) && spirv,
        "Failed to emit SPIR-V for Slang shader '{}':\n{}",
        source_path.string(),
        GetBlobText(diagnostics));

    const size_t byte_size = spirv->getBufferSize();
    ErrorHandling::Ensure(
        byte_size != 0 && byte_size % sizeof(u32) == 0,
        "Slang produced an invalid SPIR-V size for '{}'",
        source_path.string());
    auto mutable_spirv_words = std::make_shared<std::vector<u32>>(byte_size / sizeof(u32));
    std::memcpy(mutable_spirv_words->data(), spirv->getBufferPointer(), byte_size);
    ErrorHandling::Ensure(
        !mutable_spirv_words->empty() && mutable_spirv_words->front() == kSpirvMagic,
        "Slang produced invalid SPIR-V for '{}'",
        source_path.string());
    std::shared_ptr<const std::vector<u32>> spirv_words = std::move(mutable_spirv_words);

    diagnostics.setNull();
    slang::ProgramLayout* layout = linked->getLayout(0, diagnostics.writeRef());
    ErrorHandling::Ensure(
        layout != nullptr,
        "Failed to reflect Slang shader '{}':\n{}",
        source_path.string(),
        GetBlobText(diagnostics));
    return std::make_shared<const CompiledShader>(CompiledShader{
        .spirv = std::move(spirv_words),
        .interface = std::make_shared<const ShaderInterface>(ShaderReflectionJson::Read(*layout)),
    });
}

}  // namespace klvk
