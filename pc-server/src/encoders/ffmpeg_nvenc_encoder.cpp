#include "encoders/ffmpeg_nvenc_encoder.hpp"

#include "core/log.hpp"

#include <cstddef>
#include <cstring>
#include <format>
#include <string>

extern "C" {
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/pixdesc.h>
}

namespace argb::encode {
namespace {

std::string ffmpegError(int errorCode) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(errorCode, buffer, sizeof(buffer));
    return buffer;
}

AVPixelFormat toAvPixelFormat(PixelFormat format) {
    return format == PixelFormat::RGBA8 ? AV_PIX_FMT_RGBA : AV_PIX_FMT_BGRA;
}

} // namespace

FfmpegNvencEncoder::~FfmpegNvencEncoder() {
    shutdown();
}

bool FfmpegNvencEncoder::initialize(const EncoderConfig& config) {
    config_ = config;

    const AVCodec* codec = avcodec_find_encoder_by_name("h264_nvenc");
    if (codec == nullptr) {
        log::error("FFmpeg no expone el encoder 'h264_nvenc'.");
        return false;
    }

    context_ = avcodec_alloc_context3(codec);
    if (context_ == nullptr) {
        log::error("avcodec_alloc_context3 fallo.");
        return false;
    }

    context_->width = static_cast<int>(config.width);
    context_->height = static_cast<int>(config.height);
    context_->time_base = AVRational{1, static_cast<int>(config.fps)};
    context_->framerate = AVRational{static_cast<int>(config.fps), 1};
    context_->pix_fmt = toAvPixelFormat(config.inputFormat);
    context_->bit_rate = static_cast<std::int64_t>(config.bitrateBps);
    context_->rc_max_rate = static_cast<std::int64_t>(config.bitrateBps);
    context_->rc_buffer_size = static_cast<int>(config.bitrateBps);
    context_->gop_size = static_cast<int>(config.fps) * 2;
    context_->max_b_frames = 0;

    AVDictionary* options = nullptr;
    av_dict_set(&options, "preset", "p4", 0);
    av_dict_set(&options, "tune", "hq", 0);
    av_dict_set(&options, "rc", "cbr", 0);

    const int result = avcodec_open2(context_, codec, &options);
    av_dict_free(&options);
    if (result < 0) {
        log::error(std::format("avcodec_open2 fallo: {}", ffmpegError(result)));
        shutdown();
        return false;
    }

    frame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    if (frame_ == nullptr || packet_ == nullptr) {
        log::error("No se pudieron asignar AVFrame/AVPacket.");
        shutdown();
        return false;
    }

    frame_->format = context_->pix_fmt;
    frame_->width = context_->width;
    frame_->height = context_->height;
    if (av_frame_get_buffer(frame_, 0) < 0) {
        log::error("av_frame_get_buffer fallo.");
        shutdown();
        return false;
    }

    nextPts_ = 0;
    log::info(std::format("Encoder NVENC listo: {}x{} @ {} fps | {} kbps | pix_fmt {}",
                          config.width,
                          config.height,
                          config.fps,
                          config.bitrateBps / 1000,
                          av_get_pix_fmt_name(context_->pix_fmt)));
    return true;
}

bool FfmpegNvencEncoder::encode(const FrameView& frame, std::vector<EncodedPacket>& packets) {
    if (context_ == nullptr || frame_ == nullptr || frame.data == nullptr) {
        return false;
    }
    if (frame.width != static_cast<std::uint32_t>(context_->width) ||
        frame.height != static_cast<std::uint32_t>(context_->height)) {
        log::error("Las dimensiones del frame no coinciden con el encoder.");
        return false;
    }

    const int writable = av_frame_make_writable(frame_);
    if (writable < 0) {
        log::error(std::format("av_frame_make_writable fallo: {}", ffmpegError(writable)));
        return false;
    }

    const std::size_t bytesPerPixel = 4;
    const std::size_t rowBytes = static_cast<std::size_t>(frame.width) * bytesPerPixel;
    if (frame.stride < rowBytes) {
        log::error("Stride de entrada invalido.");
        return false;
    }

    for (std::uint32_t y = 0; y < frame.height; ++y) {
        std::memcpy(frame_->data[0] + static_cast<std::ptrdiff_t>(y) * frame_->linesize[0],
                    frame.data + static_cast<std::size_t>(y) * frame.stride,
                    rowBytes);
    }

    frame_->pts = nextPts_++;

    const int sendResult = avcodec_send_frame(context_, frame_);
    if (sendResult < 0) {
        log::error(std::format("avcodec_send_frame fallo: {}", ffmpegError(sendResult)));
        return false;
    }

    return drain(packets);
}

bool FfmpegNvencEncoder::flush(std::vector<EncodedPacket>& packets) {
    if (context_ == nullptr) return false;

    const int sendResult = avcodec_send_frame(context_, nullptr);
    if (sendResult < 0 && sendResult != AVERROR_EOF) {
        log::error(std::format("avcodec_send_frame(flush) fallo: {}", ffmpegError(sendResult)));
        return false;
    }
    return drain(packets);
}

bool FfmpegNvencEncoder::drain(std::vector<EncodedPacket>& packets) {
    while (true) {
        const int result = avcodec_receive_packet(context_, packet_);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
            break;
        }
        if (result < 0) {
            log::error(std::format("avcodec_receive_packet fallo: {}", ffmpegError(result)));
            return false;
        }

        EncodedPacket output;
        output.data.resize(static_cast<std::size_t>(packet_->size));
        std::memcpy(output.data.data(), packet_->data, static_cast<std::size_t>(packet_->size));
        output.keyframe = (packet_->flags & AV_PKT_FLAG_KEY) != 0;
        output.pts = packet_->pts;
        packets.push_back(std::move(output));

        av_packet_unref(packet_);
    }
    return true;
}

void FfmpegNvencEncoder::shutdown() {
    if (frame_ != nullptr) {
        av_frame_free(&frame_);
    }
    if (packet_ != nullptr) {
        av_packet_free(&packet_);
    }
    if (context_ != nullptr) {
        avcodec_free_context(&context_);
    }
}

} // namespace argb::encode
