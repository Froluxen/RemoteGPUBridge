#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace argb::protocol {

constexpr std::uint32_t kMagic = 0x41524742u; // "ARGB" en little-endian
constexpr std::uint16_t kVersion = 1;
constexpr std::uint32_t kMaxPayload = 16u * 1024u * 1024u;
constexpr std::size_t kHeaderSize = 16;

enum class MessageType : std::uint16_t {
    Hello = 1,
    Config = 2,
    Video = 3,
    Input = 4,
    Stats = 5,
    Ping = 6,
    Error = 7,
};

inline const char* messageTypeName(MessageType type) {
    switch (type) {
        case MessageType::Hello: return "HELLO";
        case MessageType::Config: return "CONFIG";
        case MessageType::Video: return "VIDEO";
        case MessageType::Input: return "INPUT";
        case MessageType::Stats: return "STATS";
        case MessageType::Ping: return "PING";
        case MessageType::Error: return "ERROR";
        default: return "UNKNOWN";
    }
}

struct Header {
    std::uint32_t magic = kMagic;
    std::uint16_t version = kVersion;
    MessageType type = MessageType::Hello;
    std::uint32_t length = 0;
    std::uint32_t sequence = 0;
};

struct Message {
    MessageType type = MessageType::Hello;
    std::vector<std::uint8_t> payload;
    std::uint32_t sequence = 0;
};

// ---------------------------------------------------------------------------
// Serializacion little-endian
// ---------------------------------------------------------------------------

inline void writeU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

inline void writeU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

inline void writeU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
    }
}

inline void writeString(std::vector<std::uint8_t>& out, const std::string& value) {
    writeU32(out, static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

inline void writeBytes(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& value) {
    writeU32(out, static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

inline bool readU16(const std::vector<std::uint8_t>& in, std::size_t& offset, std::uint16_t& value) {
    if (offset + 2 > in.size()) return false;
    value = static_cast<std::uint16_t>(in[offset]) |
            static_cast<std::uint16_t>(in[offset + 1] << 8);
    offset += 2;
    return true;
}

inline bool readU32(const std::vector<std::uint8_t>& in, std::size_t& offset, std::uint32_t& value) {
    if (offset + 4 > in.size()) return false;
    value = static_cast<std::uint32_t>(in[offset]) |
            (static_cast<std::uint32_t>(in[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(in[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(in[offset + 3]) << 24);
    offset += 4;
    return true;
}

inline bool readU64(const std::vector<std::uint8_t>& in, std::size_t& offset, std::uint64_t& value) {
    if (offset + 8 > in.size()) return false;
    value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(in[offset + static_cast<std::size_t>(i)]) << (8 * i);
    }
    offset += 8;
    return true;
}

inline bool readString(const std::vector<std::uint8_t>& in, std::size_t& offset, std::string& value) {
    std::uint32_t length = 0;
    if (!readU32(in, offset, length)) return false;
    if (offset + length > in.size()) return false;
    value.assign(reinterpret_cast<const char*>(in.data() + offset), length);
    offset += length;
    return true;
}

inline bool readBytes(const std::vector<std::uint8_t>& in,
                      std::size_t& offset,
                      std::vector<std::uint8_t>& value) {
    std::uint32_t length = 0;
    if (!readU32(in, offset, length)) return false;
    if (offset + length > in.size()) return false;
    value.assign(in.begin() + static_cast<std::ptrdiff_t>(offset),
                 in.begin() + static_cast<std::ptrdiff_t>(offset + length));
    offset += length;
    return true;
}

// ---------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------

inline std::array<std::uint8_t, kHeaderSize> encodeHeader(const Header& header) {
    std::array<std::uint8_t, kHeaderSize> out{};
    const auto put32 = [&out](std::size_t offset, std::uint32_t value) {
        out[offset] = static_cast<std::uint8_t>(value & 0xFF);
        out[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
        out[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
        out[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    };

    put32(0, header.magic);
    out[4] = static_cast<std::uint8_t>(header.version & 0xFF);
    out[5] = static_cast<std::uint8_t>((header.version >> 8) & 0xFF);
    const auto type = static_cast<std::uint16_t>(header.type);
    out[6] = static_cast<std::uint8_t>(type & 0xFF);
    out[7] = static_cast<std::uint8_t>((type >> 8) & 0xFF);
    put32(8, header.length);
    put32(12, header.sequence);
    return out;
}

inline bool decodeHeader(const std::uint8_t* data, Header& header) {
    const auto get16 = [data](std::size_t offset) -> std::uint16_t {
        return static_cast<std::uint16_t>(data[offset]) |
               static_cast<std::uint16_t>(data[offset + 1] << 8);
    };
    const auto get32 = [data](std::size_t offset) -> std::uint32_t {
        return static_cast<std::uint32_t>(data[offset]) |
               (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
               (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
               (static_cast<std::uint32_t>(data[offset + 3]) << 24);
    };

    header.magic = get32(0);
    header.version = get16(4);
    header.type = static_cast<MessageType>(get16(6));
    header.length = get32(8);
    header.sequence = get32(12);

    return header.magic == kMagic && header.version == kVersion && header.length <= kMaxPayload;
}

// ---------------------------------------------------------------------------
// CONFIG
// ---------------------------------------------------------------------------

struct VideoConfig {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t fps = 0;
    std::uint32_t bitrateBps = 0;
    std::string codec = "H264";
    std::vector<std::uint8_t> sps;
    std::vector<std::uint8_t> pps;
};

inline std::vector<std::uint8_t> encodeVideoConfig(const VideoConfig& config) {
    std::vector<std::uint8_t> out;
    writeU32(out, config.width);
    writeU32(out, config.height);
    writeU32(out, config.fps);
    writeU32(out, config.bitrateBps);
    writeString(out, config.codec);
    writeBytes(out, config.sps);
    writeBytes(out, config.pps);
    return out;
}

inline bool decodeVideoConfig(const std::vector<std::uint8_t>& payload, VideoConfig& config) {
    std::size_t offset = 0;
    return readU32(payload, offset, config.width) &&
           readU32(payload, offset, config.height) &&
           readU32(payload, offset, config.fps) &&
           readU32(payload, offset, config.bitrateBps) &&
           readString(payload, offset, config.codec) &&
           readBytes(payload, offset, config.sps) &&
           readBytes(payload, offset, config.pps);
}

// ---------------------------------------------------------------------------
// H.264 Annex-B
// ---------------------------------------------------------------------------

struct NalUnit {
    std::size_t offset = 0; // inicio del NAL (tras el start code)
    std::size_t size = 0;
    std::uint8_t type = 0;
};

inline std::vector<NalUnit> splitAnnexB(const std::vector<std::uint8_t>& data) {
    std::vector<NalUnit> units;
    std::size_t index = 0;
    std::size_t currentStart = 0;
    bool haveCurrent = false;

    while (index + 2 < data.size()) {
        std::size_t startCodeLength = 0;
        if (index + 3 < data.size() &&
            data[index] == 0 && data[index + 1] == 0 &&
            data[index + 2] == 0 && data[index + 3] == 1) {
            startCodeLength = 4;
        } else if (data[index] == 0 && data[index + 1] == 0 && data[index + 2] == 1) {
            startCodeLength = 3;
        }

        if (startCodeLength > 0) {
            if (haveCurrent && index > currentStart) {
                NalUnit unit;
                unit.offset = currentStart;
                unit.size = index - currentStart;
                unit.type = data[currentStart] & 0x1F;
                units.push_back(unit);
            }
            currentStart = index + startCodeLength;
            haveCurrent = true;
            index += startCodeLength;
            continue;
        }
        ++index;
    }

    if (haveCurrent && currentStart < data.size()) {
        NalUnit unit;
        unit.offset = currentStart;
        unit.size = data.size() - currentStart;
        unit.type = data[currentStart] & 0x1F;
        units.push_back(unit);
    }

    return units;
}

inline void extractSpsPps(const std::vector<std::uint8_t>& packet,
                          std::vector<std::uint8_t>& sps,
                          std::vector<std::uint8_t>& pps) {
    for (const auto& unit : splitAnnexB(packet)) {
        if (unit.type == 7 && sps.empty()) {
            sps.assign(packet.begin() + static_cast<std::ptrdiff_t>(unit.offset),
                       packet.begin() + static_cast<std::ptrdiff_t>(unit.offset + unit.size));
        } else if (unit.type == 8 && pps.empty()) {
            pps.assign(packet.begin() + static_cast<std::ptrdiff_t>(unit.offset),
                       packet.begin() + static_cast<std::ptrdiff_t>(unit.offset + unit.size));
        }
    }
}

} // namespace argb::protocol
