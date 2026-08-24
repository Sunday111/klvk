#include "diagnostic_window.hpp"

#include <fmt/format.h>
#include <imgui.h>

#include <atomic>
#include <cfloat>
#include <exception>
#include <filesystem>
#include <ranges>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "edt/threading/thread_name.hpp"
#include "klvk/diagnostics/perf_recorder.hpp"
#include "klvk/diagnostics/speedscope_exporter.hpp"

namespace klvk
{
namespace
{

[[nodiscard]] std::string_view CaptureStateName(PerfRecorder::CaptureState state)
{
    switch (state)
    {
    case PerfRecorder::CaptureState::Recording:
        return "Recording";
    case PerfRecorder::CaptureState::Paused:
        return "Paused";
    case PerfRecorder::CaptureState::Finalizing:
        return "Stopping";
    case PerfRecorder::CaptureState::Captured:
        return "Stopped";
    case PerfRecorder::CaptureState::Failed:
        return "Failed";
    }
    return "Unknown";
}

void DrawWrappedText(std::string_view text)
{
    ImGui::PushTextWrapPos(0.f);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopTextWrapPos();
}

[[nodiscard]] std::filesystem::path SpeedscopePath(const PerfRecorder::Capture& capture)
{
    std::filesystem::path result = capture.data_path;
    result.replace_extension(".linux-perf.txt");
    return result;
}

}  // namespace

struct DiagnosticWindow::ExportJob
{
    std::filesystem::path perf_data_path;
    std::filesystem::path output_path;
    std::filesystem::path log_path;
    SpeedscopeExporter::Result result;
    std::atomic<bool> finished = false;
    std::jthread worker;
};

DiagnosticWindow::DiagnosticWindow(PerfRecorder& perf_recorder, SpeedscopeExporter& speedscope_exporter) noexcept
    : perf_recorder_(perf_recorder),
      speedscope_exporter_(speedscope_exporter)
{
}

DiagnosticWindow::~DiagnosticWindow()
{
    perf_recorder_.Finish();
    for (const std::unique_ptr<ExportJob>& job : export_jobs_) job->worker.request_stop();
    export_jobs_.clear();
}

void DiagnosticWindow::StartExport(
    const std::filesystem::path& perf_data_path,
    const std::filesystem::path& output_path,
    const std::filesystem::path& log_path)
{
    auto job = std::make_unique<ExportJob>();
    job->perf_data_path = perf_data_path;
    job->output_path = output_path;
    job->log_path = log_path;
    ExportJob* const job_ptr = job.get();
    export_jobs_.push_back(std::move(job));
    job_ptr->worker = std::jthread(
        [this, job_ptr](const std::stop_token& stop_token)
        {
            edt::SetCurrentThreadName("klvk_speedscope");
            try
            {
                job_ptr->result = speedscope_exporter_.Export(
                    job_ptr->perf_data_path,
                    job_ptr->output_path,
                    job_ptr->log_path,
                    stop_token);
            }
            catch (const std::exception& error)
            {
                job_ptr->result = {
                    .state = SpeedscopeExporter::ResultState::Failed,
                    .error = error.what(),
                };
            }
            job_ptr->finished.store(true, std::memory_order_release);
        });
}

DiagnosticWindow::ExportJob* DiagnosticWindow::FindExport(const std::filesystem::path& perf_data_path) noexcept
{
    for (const std::unique_ptr<ExportJob>& export_job : export_jobs_ | std::views::reverse)
    {
        if (export_job->perf_data_path == perf_data_path) return export_job.get();
    }
    return nullptr;
}

void DiagnosticWindow::Draw()
{
    perf_recorder_.Update();

    ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
    const bool expanded = ImGui::Begin("klvk Diagnostics");
    if (expanded && ImGui::CollapsingHeader("CPU profiler", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (!perf_recorder_.IsAvailable())
        {
            ImGui::TextUnformatted(perf_recorder_.GetLastError().c_str());
        }
        else
        {
            if (perf_recorder_.IsRecording())
            {
                ImGui::TextUnformatted("Recording");
                if (ImGui::Button("Pause")) perf_recorder_.Pause();
                ImGui::SameLine();
                if (ImGui::Button("Stop")) perf_recorder_.Stop();
            }
            else if (perf_recorder_.IsPaused())
            {
                ImGui::TextUnformatted("Paused");
                if (ImGui::Button("Resume")) perf_recorder_.Resume();
                ImGui::SameLine();
                if (ImGui::Button("Stop")) perf_recorder_.Stop();
            }
            else if (perf_recorder_.IsFinalizing())
            {
                ImGui::TextUnformatted("Stopping record...");
                ImGui::BeginDisabled();
                ImGui::Button("New record");
                ImGui::EndDisabled();
            }
            else if (ImGui::Button("New record"))
            {
                (void)perf_recorder_.Start();
            }

            ImGui::TextUnformatted("Output directory:");
            const std::string output = perf_recorder_.GetOutputDirectory().string();
            DrawWrappedText(output);
            if (ImGui::Button("Copy output path")) ImGui::SetClipboardText(output.c_str());

            bool export_running = false;
            for (const std::unique_ptr<ExportJob>& job : export_jobs_)
            {
                if (!job->finished.load(std::memory_order_acquire)) export_running = true;
            }

            const std::span captures = perf_recorder_.GetCaptures();
            if (!captures.empty())
            {
                ImGui::Separator();
                for (const PerfRecorder::Capture& capture : captures)
                {
                    ImGui::PushID(static_cast<const void*>(&capture));
                    const std::string summary =
                        fmt::format("Record {}: {}", capture.number, CaptureStateName(capture.state));
                    ImGui::TextUnformatted(summary.c_str());
                    if (capture.state == PerfRecorder::CaptureState::Captured)
                    {
                        ExportJob* const export_job = FindExport(capture.data_path);
                        if (export_job == nullptr)
                        {
                            ImGui::BeginDisabled(export_running || !speedscope_exporter_.IsAvailable());
                            if (ImGui::Button("Export to Speedscope"))
                            {
                                StartExport(capture.data_path, SpeedscopePath(capture), capture.log_path);
                            }
                            ImGui::EndDisabled();
                            if (!speedscope_exporter_.IsAvailable())
                            {
                                DrawWrappedText(speedscope_exporter_.GetUnavailableReason());
                            }
                        }
                        else if (!export_job->finished.load(std::memory_order_acquire))
                        {
                            ImGui::ProgressBar(-static_cast<float>(ImGui::GetTime()), {-FLT_MIN, 0.f}, "Exporting...");
                            if (ImGui::Button("Cancel export")) export_job->worker.request_stop();
                        }
                        else if (export_job->result.state == SpeedscopeExporter::ResultState::Complete)
                        {
                            ImGui::TextUnformatted("Export complete");
                            if (ImGui::Button("Copy Speedscope path"))
                            {
                                const std::string path = export_job->output_path.string();
                                ImGui::SetClipboardText(path.c_str());
                            }
                        }
                        else if (export_job->result.state == SpeedscopeExporter::ResultState::Empty)
                        {
                            ImGui::TextUnformatted("No samples to export");
                        }
                        else
                        {
                            if (export_job->result.state == SpeedscopeExporter::ResultState::Cancelled)
                            {
                                ImGui::TextUnformatted("Export cancelled");
                            }
                            else
                            {
                                DrawWrappedText(export_job->result.error);
                            }
                            ImGui::BeginDisabled(export_running);
                            if (ImGui::Button("Retry export"))
                            {
                                StartExport(capture.data_path, SpeedscopePath(capture), capture.log_path);
                            }
                            ImGui::EndDisabled();
                        }
                    }
                    if (!capture.error.empty()) DrawWrappedText(capture.error);
                    ImGui::PopID();
                }
            }

            if (!perf_recorder_.GetLastError().empty())
            {
                ImGui::Separator();
                DrawWrappedText(perf_recorder_.GetLastError());
            }
        }
    }
    ImGui::End();
}

}  // namespace klvk
