#pragma once

#include <cstdint>
#include <vector>

namespace argb::encode {

enum class PixelFormat { BGRA8, RGBA8 };

struct FrameView {
    const std::uint8_t* data = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stride = 0;
    PixelFormat format = PixelFormat::BGRA8;
};

struct EncoderConfig {
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    std::uint32_t fps = 30;
    std::uint32_t bitrateBps = 8'000'000;
    PixelFormat inputFormat = PixelFormat::BGRA8;
};

struct EncodedPacket {
    std::vector<std::uint8_t> data;
    bool keyframe = false;
    std::int64_t pts = 0;
};

class FrameEncoder {
public:
    virtual ~FrameEncoder() = default;

    virtual bool initialize(const EncoderConfig& config) = 0;
    virtual bool encode(const FrameView& frame, std::vector<EncodedPacket>& packets) = 0;
    virtual bool flush(std::vector<EncodedPacket>& packets) = 0;
    virtual void shutdown() = 0;
};

} // namespace argb::encode
