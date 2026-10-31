#pragma once

#include "encoders/frame_encoder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace argb::encode {

class FfmpegNvencEncoder final : public FrameEncoder {
public:
    FfmpegNvencEncoder() = default;
    ~FfmpegNvencEncoder() override;

    FfmpegNvencEncoder(const FfmpegNvencEncoder&) = delete;
    FfmpegNvencEncoder& operator=(const FfmpegNvencEncoder&) = delete;

    bool initialize(const EncoderConfig& config) override;
    bool encode(const FrameView& frame, std::vector<EncodedPacket>& packets) override;
    bool flush(std::vector<EncodedPacket>& packets) override;
    void shutdown() override;

private:
    bool drain(std::vector<EncodedPacket>& packets);

    AVCodecContext* context_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    std::int64_t nextPts_ = 0;
    EncoderConfig config_{};
};

} // namespace argb::encode
