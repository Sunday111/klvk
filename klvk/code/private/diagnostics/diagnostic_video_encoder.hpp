#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <vector>

#include "klvk/integral_aliases.hpp"

namespace klvk
{

enum class DiagnosticVideoEncoding : u8;
enum class DiagnosticVideoEncodingDevice : u8;

class DiagnosticVideoEncoder
{
public:
    DiagnosticVideoEncoder(
        std::filesystem::path path,
        u32 width,
        u32 height,
        double frame_duration_seconds,
        DiagnosticVideoEncoding encoding,
        DiagnosticVideoEncodingDevice encoding_device,
        u8 compression_level,
        bool log_ffmpeg);
    ~DiagnosticVideoEncoder();

    DiagnosticVideoEncoder(const DiagnosticVideoEncoder&) = delete;
    DiagnosticVideoEncoder& operator=(const DiagnosticVideoEncoder&) = delete;
    DiagnosticVideoEncoder(DiagnosticVideoEncoder&&) = delete;
    DiagnosticVideoEncoder& operator=(DiagnosticVideoEncoder&&) = delete;

    void WriteFrame(std::vector<std::byte> pixels, bool bgra, u64 frame_index);
    void Finish();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace klvk
