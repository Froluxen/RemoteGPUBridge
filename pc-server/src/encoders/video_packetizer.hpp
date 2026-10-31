#pragma once

#include "encoders/frame_encoder.hpp"

#include <cstdint>
#include <fstream>
#include <string>

namespace argb::encode {

class VideoPacketizer {
public:
    bool open(const std::string& path);
    bool writePacket(const EncodedPacket& packet);
    void close();

    std::uint64_t totalBytes() const { return totalBytes_; }
    std::uint64_t packetCount() const { return packetCount_; }
    std::uint64_t keyframeCount() const { return keyframeCount_; }

private:
    std::ofstream file_;
    std::uint64_t totalBytes_ = 0;
    std::uint64_t packetCount_ = 0;
    std::uint64_t keyframeCount_ = 0;
};

} // namespace argb::encode
