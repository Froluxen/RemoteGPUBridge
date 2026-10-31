#include "encoders/video_packetizer.hpp"

namespace argb::encode {

bool VideoPacketizer::open(const std::string& path) {
    file_.open(path, std::ios::binary | std::ios::trunc);
    return file_.is_open();
}

bool VideoPacketizer::writePacket(const EncodedPacket& packet) {
    if (!file_.is_open() || packet.data.empty()) {
        return false;
    }

    file_.write(reinterpret_cast<const char*>(packet.data.data()),
                static_cast<std::streamsize>(packet.data.size()));
    if (!file_.good()) {
        return false;
    }

    totalBytes_ += packet.data.size();
    ++packetCount_;
    if (packet.keyframe) {
        ++keyframeCount_;
    }
    return true;
}

void VideoPacketizer::close() {
    if (file_.is_open()) {
        file_.close();
    }
}

} // namespace argb::encode
