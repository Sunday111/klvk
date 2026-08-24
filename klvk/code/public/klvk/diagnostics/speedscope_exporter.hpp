#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>

#include "klvk/integral_aliases.hpp"

namespace klvk
{

class SpeedscopeExporter
{
public:
    enum class ResultState : u8
    {
        Complete,
        Empty,
        Cancelled,
        Failed,
    };

    struct Result
    {
        ResultState state = ResultState::Failed;
        std::string error;
    };

    struct Config
    {
        std::string executable = "perf";
    };

    SpeedscopeExporter();
    explicit SpeedscopeExporter(Config config);
    SpeedscopeExporter(const SpeedscopeExporter&) = delete;
    SpeedscopeExporter(SpeedscopeExporter&&) = delete;
    ~SpeedscopeExporter();

    SpeedscopeExporter& operator=(const SpeedscopeExporter&) = delete;
    SpeedscopeExporter& operator=(SpeedscopeExporter&&) = delete;

    [[nodiscard]] bool IsAvailable() const noexcept;
    [[nodiscard]] const std::string& GetUnavailableReason() const noexcept;
    [[nodiscard]] uintmax_t GetProgress() const noexcept;
    [[nodiscard]] Result Export(
        const std::filesystem::path& perf_data_path,
        const std::filesystem::path& output_path,
        const std::filesystem::path& log_path,
        const std::stop_token& stop_token) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace klvk
