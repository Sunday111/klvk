#pragma once

#include <filesystem>
#include <string_view>

class ShaderCacheComponentTests
{
public:
    static void Run(const std::filesystem::path& root);

private:
    static void TestHash();
    static void TestCompiler(const std::filesystem::path& root);
    static void TestStore(const std::filesystem::path& root);
    static void Ensure(bool condition, std::string_view message);
};
