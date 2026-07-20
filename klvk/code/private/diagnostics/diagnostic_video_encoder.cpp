#include "diagnostic_video_encoder.hpp"

#include <cstdarg>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/log.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <deque>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "klvk/diagnostics/diagnostic_run_config.hpp"
#include "klvk/error_handling.hpp"
#include "klvk/filesystem/filesystem.hpp"
#include "klvk/platform/os/os.hpp"
#include "klvk/signed_integral_aliases.hpp"

namespace klvk
{
namespace
{

std::string AvError(int error)
{
    std::array<char, AV_ERROR_MAX_STRING_SIZE> message{};
    av_strerror(error, message.data(), message.size());
    return message.data();
}

void EnsureAv(int result, std::string_view operation)
{
    ErrorHandling::Ensure(result >= 0, "FFmpeg {} failed: {}", operation, AvError(result));
}

spdlog::level::level_enum ToSpdlogLevel(int level)
{
    if (level <= AV_LOG_ERROR) return spdlog::level::err;
    if (level <= AV_LOG_WARNING) return spdlog::level::warn;
    if (level <= AV_LOG_INFO) return spdlog::level::info;
    if (level <= AV_LOG_VERBOSE) return spdlog::level::debug;
    return spdlog::level::trace;
}

void FfmpegLogCallback(void* context, int level, const char* format, va_list arguments) noexcept
{
    try
    {
        const std::shared_ptr<spdlog::logger> logger = spdlog::get("ffmpeg");
        if (logger == nullptr || !logger->should_log(ToSpdlogLevel(level))) return;

        std::array<char, 2048> message{};
        int print_prefix = 1;
        av_log_format_line2(
            context,
            level,
            format,
            arguments,
            message.data(),
            static_cast<int>(message.size()),
            &print_prefix);
        std::string_view text = message.data();
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.remove_suffix(1);
        if (!text.empty()) logger->log(ToSpdlogLevel(level), "{}", text);
    }
    catch (...)
    {
    }
}

std::shared_ptr<spdlog::logger> GetFfmpegLogger()
{
    if (std::shared_ptr<spdlog::logger> logger = spdlog::get("ffmpeg"); logger != nullptr) return logger;

    const std::shared_ptr<spdlog::logger> default_logger = spdlog::default_logger();
    auto logger =
        std::make_shared<spdlog::logger>("ffmpeg", default_logger->sinks().begin(), default_logger->sinks().end());
    spdlog::register_logger(logger);
    return logger;
}

const AVCodec* FindAv1Encoder()
{
    for (const char* name : {"libaom-av1", "librav1e"})
    {
        if (const AVCodec* codec = avcodec_find_encoder_by_name(name); codec != nullptr) return codec;
    }
    return nullptr;
}

std::string_view EncodingName(DiagnosticVideoEncoding encoding)
{
    switch (encoding)
    {
    case DiagnosticVideoEncoding::Av1:
        return "AV1";
    case DiagnosticVideoEncoding::H264:
        return "H.264";
    case DiagnosticVideoEncoding::Mpeg4:
        return "MPEG-4";
    }
    return "unknown";
}

std::string_view GpuEncoderName(DiagnosticVideoEncoding encoding)
{
    switch (encoding)
    {
    case DiagnosticVideoEncoding::Av1:
        return "av1_nvenc";
    case DiagnosticVideoEncoding::H264:
        return "h264_nvenc";
    case DiagnosticVideoEncoding::Mpeg4:
        return {};
    }
    return {};
}

const AVCodec* FindEncoder(DiagnosticVideoEncoding encoding, DiagnosticVideoEncodingDevice encoding_device)
{
    if (encoding_device == DiagnosticVideoEncodingDevice::Gpu)
    {
        return avcodec_find_encoder_by_name(GpuEncoderName(encoding).data());
    }
    switch (encoding)
    {
    case DiagnosticVideoEncoding::Av1:
        return FindAv1Encoder();
    case DiagnosticVideoEncoding::H264:
        return avcodec_find_encoder_by_name("libx264");
    case DiagnosticVideoEncoding::Mpeg4:
        return avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    }
    return nullptr;
}

void ConfigureCompression(
    DiagnosticVideoEncoding encoding,
    u8 compression_level,
    const AVCodec& codec,
    AVCodecContext& context)
{
    const std::string_view name = codec.name;
    if (name == "av1_nvenc" || name == "h264_nvenc")
    {
        constexpr u8 quantizer_step = 5;
        EnsureAv(av_opt_set(context.priv_data, "preset", "p4", 0), "NVENC preset selection");
        EnsureAv(av_opt_set(context.priv_data, "tune", "hq", 0), "NVENC tuning selection");
        EnsureAv(av_opt_set(context.priv_data, "rc", "constqp", 0), "NVENC rate-control selection");
        EnsureAv(
            av_opt_set_int(context.priv_data, "qp", static_cast<i64>(compression_level * quantizer_step), 0),
            "NVENC compression selection");
    }
    else if (encoding == DiagnosticVideoEncoding::Av1 && name == "libaom-av1")
    {
        constexpr u8 crf_step = 6;
        EnsureAv(av_opt_set_int(context.priv_data, "cpu-used", 8, 0), "libaom AV1 speed selection");
        EnsureAv(av_opt_set_int(context.priv_data, "row-mt", 1, 0), "libaom AV1 row threading");
        EnsureAv(
            av_opt_set_int(context.priv_data, "crf", static_cast<i64>(compression_level * crf_step), 0),
            "libaom AV1 compression selection");
    }
    else if (encoding == DiagnosticVideoEncoding::Av1 && name == "librav1e")
    {
        constexpr u8 quantizer_step = 20;
        EnsureAv(av_opt_set_int(context.priv_data, "speed", 10, 0), "rav1e speed selection");
        EnsureAv(
            av_opt_set_int(context.priv_data, "qp", static_cast<i64>(compression_level * quantizer_step), 0),
            "rav1e AV1 compression selection");
    }
    else if (encoding == DiagnosticVideoEncoding::H264 && name == "libx264")
    {
        constexpr u8 crf_step = 5;
        EnsureAv(av_opt_set(context.priv_data, "preset", "veryfast", 0), "libx264 speed selection");
        EnsureAv(
            av_opt_set_int(context.priv_data, "crf", static_cast<i64>(compression_level * crf_step), 0),
            "libx264 compression selection");
    }
    else if (encoding == DiagnosticVideoEncoding::Mpeg4)
    {
        constexpr int quantizer_step = 3;
        const int quantizer = 1 + static_cast<int>(compression_level) * quantizer_step;
        context.flags |= AV_CODEC_FLAG_QSCALE;
        context.global_quality = quantizer * FF_QP2LAMBDA;
    }
}

}  // namespace

class DiagnosticVideoEncoder::Impl
{
public:
    ~Impl()
    {
        {
            std::scoped_lock lock(queue_mutex_);
            abort_requested_ = true;
        }
        queue_changed_.notify_all();
        if (worker_.joinable()) worker_.join();

        if (packet_ != nullptr) av_packet_free(&packet_);
        if (frame_ != nullptr) av_frame_free(&frame_);
        sws_freeContext(sws_context_);
        if (codec_context_ != nullptr) avcodec_free_context(&codec_context_);
        if (format_context_ != nullptr)
        {
            if (format_context_->pb != nullptr) avio_closep(&format_context_->pb);
            avformat_free_context(format_context_);
        }
        if (!installed_ && !temporary_path_.empty())
        {
            std::error_code ignored;
            std::filesystem::remove(temporary_path_, ignored);
        }
        if (ffmpeg_callback_installed_) av_log_set_callback(av_log_default_callback);
        if (ffmpeg_logger_ != nullptr) ffmpeg_logger_->set_level(previous_ffmpeg_logger_level_);
    }

    void Initialize(
        std::filesystem::path path,
        u32 width,
        u32 height,
        double frame_duration_seconds,
        DiagnosticVideoEncoding encoding,
        DiagnosticVideoEncodingDevice encoding_device,
        u8 compression_level,
        bool log_ffmpeg)
    {
        ffmpeg_logger_ = GetFfmpegLogger();
        previous_ffmpeg_logger_level_ = ffmpeg_logger_->level();
        ffmpeg_logger_->set_level(log_ffmpeg ? spdlog::level::info : spdlog::level::off);
        av_log_set_callback(FfmpegLogCallback);
        ffmpeg_callback_installed_ = true;
        output_path_ = std::move(path);
        if (output_path_.has_parent_path()) std::filesystem::create_directories(output_path_.parent_path());
        temporary_path_ = output_path_;
        static std::atomic<u64> temporary_sequence = 0;
        temporary_path_ +=
            fmt::format(".tmp.{}.{}", os::GetProcessId(), temporary_sequence.fetch_add(1, std::memory_order_relaxed));

        EnsureAv(
            avformat_alloc_output_context2(&format_context_, nullptr, "mp4", temporary_path_.string().c_str()),
            "output context creation");
        ErrorHandling::Ensure(format_context_ != nullptr, "FFmpeg did not create an MP4 output context");

        const AVCodec* codec = FindEncoder(encoding, encoding_device);
        const std::string_view encoding_name = EncodingName(encoding);
        if (encoding_device == DiagnosticVideoEncodingDevice::Gpu)
        {
            const std::string_view encoder_name = GpuEncoderName(encoding);
            ErrorHandling::Ensure(
                codec != nullptr,
                "GPU {} encoding was requested, but the system FFmpeg does not provide the '{}' encoder",
                encoding_name,
                encoder_name);
        }
        else
        {
            ErrorHandling::Ensure(codec != nullptr, "The system FFmpeg has no supported {} encoder", encoding_name);
        }
        stream_ = avformat_new_stream(format_context_, nullptr);
        ErrorHandling::Ensure(stream_ != nullptr, "FFmpeg failed to create the diagnostic video stream");
        codec_context_ = avcodec_alloc_context3(codec);
        ErrorHandling::Ensure(codec_context_ != nullptr, "FFmpeg failed to allocate the diagnostic video encoder");

        ErrorHandling::Ensure(
            width <= static_cast<u32>(std::numeric_limits<int>::max()) &&
                height <= static_cast<u32>(std::numeric_limits<int>::max()),
            "Diagnostic video dimensions exceed FFmpeg's integer size limit");
        codec_context_->codec_id = codec->id;
        codec_context_->codec_type = AVMEDIA_TYPE_VIDEO;
        codec_context_->width = static_cast<int>(width);
        codec_context_->height = static_cast<int>(height);
        codec_context_->pix_fmt = AV_PIX_FMT_YUV420P;
        codec_context_->bit_rate = 0;
        codec_context_->time_base = av_d2q(frame_duration_seconds, 1'000'000);
        codec_context_->framerate = av_inv_q(codec_context_->time_base);
        codec_context_->gop_size = 12;
        codec_context_->max_b_frames = 0;
        if ((format_context_->oformat->flags & AVFMT_GLOBALHEADER) != 0)
        {
            codec_context_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        ConfigureCompression(encoding, compression_level, *codec, *codec_context_);
        const int open_result = avcodec_open2(codec_context_, codec, nullptr);
        if (encoding_device == DiagnosticVideoEncodingDevice::Gpu)
        {
            const std::string_view encoder_name = GpuEncoderName(encoding);
            ErrorHandling::Ensure(
                open_result >= 0,
                "GPU {} encoding was requested, but FFmpeg could not initialize '{}'. "
                "The selected NVIDIA GPU or driver does not support this hardware encoder: {}",
                encoding_name,
                encoder_name,
                AvError(open_result));
        }
        EnsureAv(open_result, "encoder initialization");
        EnsureAv(avcodec_parameters_from_context(stream_->codecpar, codec_context_), "stream parameter initialization");
        stream_->time_base = codec_context_->time_base;

        frame_ = av_frame_alloc();
        ErrorHandling::Ensure(frame_ != nullptr, "FFmpeg failed to allocate a diagnostic video frame");
        frame_->format = codec_context_->pix_fmt;
        frame_->width = codec_context_->width;
        frame_->height = codec_context_->height;
        EnsureAv(av_frame_get_buffer(frame_, 32), "video frame buffer allocation");

        packet_ = av_packet_alloc();
        ErrorHandling::Ensure(packet_ != nullptr, "FFmpeg failed to allocate a diagnostic video packet");

        EnsureAv(avio_open(&format_context_->pb, temporary_path_.string().c_str(), AVIO_FLAG_WRITE), "output opening");
        EnsureAv(avformat_write_header(format_context_, nullptr), "container header writing");
        worker_ = std::thread([this] { WorkerMain(); });
    }

    void WriteFrame(std::vector<std::byte> pixels, bool bgra, u64 frame_index)
    {
        std::unique_lock lock(queue_mutex_);
        ErrorHandling::Ensure(!finish_requested_, "Cannot append a frame to a finishing diagnostic video");
        ErrorHandling::Ensure(
            frame_index == submitted_frame_count_,
            "Diagnostic video frames were submitted out of order");
        queue_changed_.wait(
            lock,
            [this]
            { return queued_frames_.size() < kMaximumQueuedFrames || worker_error_ != nullptr || abort_requested_; });
        RethrowWorkerError();
        ErrorHandling::Ensure(!abort_requested_, "Diagnostic video encoder was aborted");
        queued_frames_.push_back(QueuedFrame{.pixels = std::move(pixels), .bgra = bgra, .frame_index = frame_index});
        ++submitted_frame_count_;
        lock.unlock();
        queue_changed_.notify_all();
    }

    void Finish()
    {
        std::unique_lock lock(queue_mutex_);
        if (!finish_requested_)
        {
            finish_requested_ = true;
            queue_changed_.notify_all();
        }
        queue_changed_.wait(lock, [this] { return worker_finished_; });
        RethrowWorkerError();
        lock.unlock();
        if (worker_.joinable()) worker_.join();
    }

private:
    struct QueuedFrame
    {
        std::vector<std::byte> pixels;
        bool bgra = false;
        u64 frame_index = 0;
    };

    static constexpr size_t kMaximumQueuedFrames = 3;

    void WorkerMain() noexcept
    {
        try
        {
            while (true)
            {
                std::unique_lock lock(queue_mutex_);
                queue_changed_.wait(
                    lock,
                    [this] { return !queued_frames_.empty() || finish_requested_ || abort_requested_; });
                if (abort_requested_) return;
                if (!queued_frames_.empty())
                {
                    QueuedFrame frame = std::move(queued_frames_.front());
                    queued_frames_.pop_front();
                    lock.unlock();
                    queue_changed_.notify_all();
                    EncodeFrame(std::move(frame.pixels), frame.bgra, frame.frame_index);
                    continue;
                }

                ErrorHandling::Ensure(finish_requested_, "Diagnostic video worker woke without work");
                lock.unlock();
                FinishEncoding();
                lock.lock();
                worker_finished_ = true;
                lock.unlock();
                queue_changed_.notify_all();
                return;
            }
        }
        catch (...)
        {
            std::scoped_lock lock(queue_mutex_);
            worker_error_ = std::current_exception();
            queued_frames_.clear();
            worker_finished_ = true;
            queue_changed_.notify_all();
        }
    }

    void EncodeFrame(std::vector<std::byte> pixels, bool bgra, u64 frame_index)
    {
        ErrorHandling::Ensure(!encoding_finished_, "Cannot append a frame to a finished diagnostic video");
        ErrorHandling::Ensure(frame_index == frame_count_, "Diagnostic video frames arrived out of order");
        const size_t expected_size =
            static_cast<size_t>(codec_context_->width) * static_cast<size_t>(codec_context_->height) * 4;
        ErrorHandling::Ensure(
            pixels.size() == expected_size,
            "Diagnostic video frame has {} bytes, expected {}",
            pixels.size(),
            expected_size);

        if (sws_context_ == nullptr || bgra != source_is_bgra_)
        {
            sws_freeContext(sws_context_);
            source_is_bgra_ = bgra;
            sws_context_ = sws_getContext(
                codec_context_->width,
                codec_context_->height,
                bgra ? AV_PIX_FMT_BGRA : AV_PIX_FMT_RGBA,
                codec_context_->width,
                codec_context_->height,
                codec_context_->pix_fmt,
                SWS_BILINEAR,
                nullptr,
                nullptr,
                nullptr);
            ErrorHandling::Ensure(sws_context_ != nullptr, "FFmpeg failed to create the diagnostic pixel converter");
        }

        EnsureAv(av_frame_make_writable(frame_), "video frame preparation");
        const auto* source = reinterpret_cast<const u8*>(pixels.data());
        const std::array<const u8*, 4> source_planes{source, nullptr, nullptr, nullptr};
        const std::array<int, 4> source_strides{codec_context_->width * 4, 0, 0, 0};
        const int converted_rows = sws_scale(
            sws_context_,
            source_planes.data(),
            source_strides.data(),
            0,
            codec_context_->height,
            frame_->data,
            frame_->linesize);
        ErrorHandling::Ensure(
            converted_rows == codec_context_->height,
            "FFmpeg converted {} of {} diagnostic video rows",
            converted_rows,
            codec_context_->height);

        ErrorHandling::Ensure(
            frame_index <= static_cast<u64>(std::numeric_limits<i64>::max()),
            "Diagnostic video contains too many frames");
        frame_->pts = static_cast<i64>(frame_index);
        EnsureAv(avcodec_send_frame(codec_context_, frame_), "frame submission");
        DrainPackets(false);
        ++frame_count_;
    }

    void FinishEncoding()
    {
        if (encoding_finished_) return;
        ErrorHandling::Ensure(frame_count_ != 0, "Cannot finish an empty diagnostic video");
        EnsureAv(avcodec_send_frame(codec_context_, nullptr), "encoder flushing");
        DrainPackets(true);
        EnsureAv(av_write_trailer(format_context_), "container trailer writing");
        EnsureAv(avio_closep(&format_context_->pb), "output closing");
        Filesystem::InstallFileAtomically(temporary_path_, output_path_);
        installed_ = true;
        encoding_finished_ = true;
        fmt::println(
            "klvk: captured {}-frame {}x{} video to {}",
            frame_count_,
            codec_context_->width,
            codec_context_->height,
            output_path_.string());
    }

    void RethrowWorkerError() const
    {
        if (worker_error_ != nullptr) std::rethrow_exception(worker_error_);
    }

    void DrainPackets(bool flushing)
    {
        while (true)
        {
            const int result = avcodec_receive_packet(codec_context_, packet_);
            if (result == AVERROR(EAGAIN))
            {
                ErrorHandling::Ensure(!flushing, "FFmpeg requested more input while flushing the video encoder");
                return;
            }
            if (result == AVERROR_EOF)
            {
                ErrorHandling::Ensure(flushing, "FFmpeg finished the video encoder before flushing");
                return;
            }
            EnsureAv(result, "encoded packet retrieval");
            av_packet_rescale_ts(packet_, codec_context_->time_base, stream_->time_base);
            if (packet_->duration <= 0)
            {
                packet_->duration = av_rescale_q(1, codec_context_->time_base, stream_->time_base);
            }
            packet_->stream_index = stream_->index;
            EnsureAv(av_interleaved_write_frame(format_context_, packet_), "encoded packet writing");
            av_packet_unref(packet_);
        }
    }

    std::filesystem::path output_path_;
    std::filesystem::path temporary_path_;
    AVFormatContext* format_context_ = nullptr;
    AVCodecContext* codec_context_ = nullptr;
    AVStream* stream_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    SwsContext* sws_context_ = nullptr;
    std::thread worker_;
    std::mutex queue_mutex_;
    std::condition_variable queue_changed_;
    std::deque<QueuedFrame> queued_frames_;
    std::exception_ptr worker_error_;
    std::shared_ptr<spdlog::logger> ffmpeg_logger_;
    spdlog::level::level_enum previous_ffmpeg_logger_level_ = spdlog::level::info;
    u64 frame_count_ = 0;
    u64 submitted_frame_count_ = 0;
    bool source_is_bgra_ = false;
    bool installed_ = false;
    bool encoding_finished_ = false;
    bool finish_requested_ = false;
    bool worker_finished_ = false;
    bool abort_requested_ = false;
    bool ffmpeg_callback_installed_ = false;
};

DiagnosticVideoEncoder::DiagnosticVideoEncoder(
    std::filesystem::path path,
    u32 width,
    u32 height,
    double frame_duration_seconds,
    DiagnosticVideoEncoding encoding,
    DiagnosticVideoEncodingDevice encoding_device,
    u8 compression_level,
    bool log_ffmpeg)
    : impl_(std::make_unique<Impl>())
{
    impl_->Initialize(
        std::move(path),
        width,
        height,
        frame_duration_seconds,
        encoding,
        encoding_device,
        compression_level,
        log_ffmpeg);
}

DiagnosticVideoEncoder::~DiagnosticVideoEncoder() = default;

void DiagnosticVideoEncoder::WriteFrame(std::vector<std::byte> pixels, bool bgra, u64 frame_index)
{
    impl_->WriteFrame(std::move(pixels), bgra, frame_index);
}

void DiagnosticVideoEncoder::Finish()
{
    impl_->Finish();
}

}  // namespace klvk
