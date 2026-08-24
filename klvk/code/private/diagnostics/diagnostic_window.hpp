#pragma once

#include <filesystem>
#include <memory>
#include <vector>

namespace klvk
{

class PerfRecorder;
class SpeedscopeExporter;

class DiagnosticWindow
{
public:
    DiagnosticWindow(PerfRecorder& perf_recorder, SpeedscopeExporter& speedscope_exporter) noexcept;
    ~DiagnosticWindow();

    void Draw();

private:
    struct ExportJob;

    void StartExport(
        const std::filesystem::path& perf_data_path,
        const std::filesystem::path& output_path,
        const std::filesystem::path& log_path);
    [[nodiscard]] ExportJob* FindExport(const std::filesystem::path& perf_data_path) noexcept;

    PerfRecorder& perf_recorder_;
    SpeedscopeExporter& speedscope_exporter_;
    std::vector<std::unique_ptr<ExportJob>> export_jobs_;
};

}  // namespace klvk
