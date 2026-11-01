#include "core/log.hpp"
#include "encoders/ffmpeg_nvenc_encoder.hpp"
#include "encoders/frame_encoder.hpp"
#include "gpu/gpu_context.hpp"
#include "gpu/gpu_selector.hpp"
#include "input/input_processor.hpp"
#include "protocol/protocol.hpp"
#include "renderer/vulkan_renderer.hpp"
#include "transport/tcp_server.hpp"

#include <vulkan/vulkan.h>

#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    std::uint32_t fps = 30;
    std::uint32_t bitrateKbps = 8000;
    std::uint16_t videoPort = 5000;
    std::uint16_t controlPort = 5001;
    double durationSeconds = 60.0;
    bool forever = false;
    bool validate = false;
    std::string gpuName = "RTX 4060";
};

enum class ParseResult { Ok, Help, Error };

void printUsage() {
    argb::log::info("Uso: argb-server [opciones]");
    argb::log::info("  --width <n>           Ancho (defecto: 1280).");
    argb::log::info("  --height <n>          Alto (defecto: 720).");
    argb::log::info("  --fps <n>             FPS (defecto: 30).");
    argb::log::info("  --bitrate-kbps <n>    Bitrate en kbps (defecto: 8000).");
    argb::log::info("  --video-port <n>      Puerto del canal de video (defecto: 5000).");
    argb::log::info("  --control-port <n>    Puerto del canal de control (defecto: 5001).");
    argb::log::info("  --duration-seconds <s>  Duracion del servidor (defecto: 60).");
    argb::log::info("  --forever             Ejecuta hasta interrumpir (Ctrl+C).");
    argb::log::info("  --gpu <texto>         Subcadena del nombre de GPU (defecto: RTX 4060).");
    argb::log::info("  --validate            Habilita la capa de validacion si esta disponible.");
    argb::log::info("  --help                Muestra esta ayuda.");
}

bool parseUnsigned(std::string_view text, std::uint32_t& value) {
    try {
        std::size_t position = 0;
        const unsigned long parsed = std::stoul(std::string(text), &position);
        if (position != text.size()) return false;
        value = static_cast<std::uint32_t>(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parsePositiveDouble(std::string_view text, double& value) {
    try {
        std::size_t position = 0;
        const double parsed = std::stod(std::string(text), &position);
        if (position != text.size() || !(parsed > 0.0)) return false;
        value = parsed;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

ParseResult parseOptions(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];

        const auto requireValue = [&](std::string_view& out) {
            if (i + 1 >= argc) return false;
            out = argv[++i];
            return true;
        };

        if (arg == "--help" || arg == "-h") {
            printUsage();
            return ParseResult::Help;
        }
        if (arg == "--validate") {
            options.validate = true;
            continue;
        }
        if (arg == "--forever") {
            options.forever = true;
            continue;
        }

        if (arg == "--width" || arg == "--height" || arg == "--fps" ||
            arg == "--bitrate-kbps" || arg == "--video-port" || arg == "--control-port" ||
            arg == "--duration-seconds" || arg == "--gpu") {
            std::string_view value;
            if (!requireValue(value)) {
                argb::log::error(std::format("{} requiere un valor.", arg));
                return ParseResult::Error;
            }

            std::uint32_t parsed = 0;
            if (arg == "--width") {
                if (!parseUnsigned(value, parsed) || parsed == 0) {
                    argb::log::error("Valor invalido para --width.");
                    return ParseResult::Error;
                }
                options.width = parsed;
            } else if (arg == "--height") {
                if (!parseUnsigned(value, parsed) || parsed == 0) {
                    argb::log::error("Valor invalido para --height.");
                    return ParseResult::Error;
                }
                options.height = parsed;
            } else if (arg == "--fps") {
                if (!parseUnsigned(value, parsed) || parsed == 0) {
                    argb::log::error("Valor invalido para --fps.");
                    return ParseResult::Error;
                }
                options.fps = parsed;
            } else if (arg == "--bitrate-kbps") {
                if (!parseUnsigned(value, parsed) || parsed == 0) {
                    argb::log::error("Valor invalido para --bitrate-kbps.");
                    return ParseResult::Error;
                }
                options.bitrateKbps = parsed;
            } else if (arg == "--video-port") {
                if (!parseUnsigned(value, parsed) || parsed == 0 || parsed > 65535) {
                    argb::log::error("Valor invalido para --video-port.");
                    return ParseResult::Error;
                }
                options.videoPort = static_cast<std::uint16_t>(parsed);
            } else if (arg == "--control-port") {
                if (!parseUnsigned(value, parsed) || parsed == 0 || parsed > 65535) {
                    argb::log::error("Valor invalido para --control-port.");
                    return ParseResult::Error;
                }
                options.controlPort = static_cast<std::uint16_t>(parsed);
            } else if (arg == "--duration-seconds") {
                if (!parsePositiveDouble(value, options.durationSeconds)) {
                    argb::log::error("Valor invalido para --duration-seconds.");
                    return ParseResult::Error;
                }
            } else if (arg == "--gpu") {
                options.gpuName = std::string(value);
            }
            continue;
        }

        argb::log::error(std::format("Opcion desconocida: {}", arg));
        printUsage();
        return ParseResult::Error;
    }
    return ParseResult::Ok;
}

std::filesystem::path shaderDirectory(const char* argv0) {
    if (const char* overrideDir = std::getenv("ARGB_SHADER_DIR")) {
        return std::filesystem::path(overrideDir);
    }
    std::error_code error;
    const std::filesystem::path executable = std::filesystem::weakly_canonical(
        std::filesystem::path(argv0), error);
    if (error || executable.empty()) {
        return std::filesystem::current_path() / "shaders";
    }
    return executable.parent_path() / "shaders";
}

argb::encode::PixelFormat pixelFormatFromVk(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            return argb::encode::PixelFormat::RGBA8;
        default:
            return argb::encode::PixelFormat::BGRA8;
    }
}

double millisecondsSince(const std::chrono::steady_clock::time_point& start) {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
}

} // namespace

int main(int argc, char** argv) {
    using namespace argb;

    Options options;
    const ParseResult parsed = parseOptions(argc, argv, options);
    if (parsed == ParseResult::Help) return EXIT_SUCCESS;
    if (parsed == ParseResult::Error) return EXIT_FAILURE;

    try {
        log::info("ARGB argb-server - Fase 04 (USB Transport Layer)");

        gpu::GpuContext context;
        if (!context.createInstance(options.validate)) {
            log::error("No se pudo inicializar Vulkan.");
            return EXIT_FAILURE;
        }

        const auto devices = gpu::enumeratePhysicalDevices(context.instance());
        if (devices.empty()) {
            log::error("No se encontro ningun dispositivo Vulkan.");
            context.shutdown();
            return EXIT_FAILURE;
        }

        gpu::SelectionCriteria criteria;
        criteria.nameContains = options.gpuName;
        const auto selectedIndex = gpu::selectPhysicalDevice(devices, criteria);
        if (!selectedIndex) {
            log::error(std::format("No se encontro un dispositivo que coincida con '{}'.", options.gpuName));
            context.shutdown();
            return EXIT_FAILURE;
        }

        const gpu::PhysicalDeviceInfo& selected = devices[*selectedIndex];
        const auto graphicsFamily = gpu::findGraphicsQueueFamily(selected);
        if (!graphicsFamily) {
            log::error("El dispositivo seleccionado no tiene queue family grafica.");
            context.shutdown();
            return EXIT_FAILURE;
        }

        log::info(std::format("GPU seleccionada: {}", selected.properties.deviceName));
        if (!context.createLogicalDevice(selected, *graphicsFamily)) {
            log::error("No se pudo crear el logical device.");
            context.shutdown();
            return EXIT_FAILURE;
        }

        renderer::RendererConfig renderConfig;
        renderConfig.width = options.width;
        renderConfig.height = options.height;
        renderConfig.shaderDir = shaderDirectory(argv[0]);

        renderer::VulkanRenderer renderer;
        if (!renderer.initialize(context, selected, *graphicsFamily, renderConfig)) {
            log::error("No se pudo inicializar el renderer.");
            renderer.shutdown();
            context.shutdown();
            return EXIT_FAILURE;
        }

        encode::EncoderConfig encoderConfig;
        encoderConfig.width = options.width;
        encoderConfig.height = options.height;
        encoderConfig.fps = options.fps;
        encoderConfig.bitrateBps = options.bitrateKbps * 1000;
        encoderConfig.inputFormat = pixelFormatFromVk(renderer.colorFormat());

        encode::FfmpegNvencEncoder encoder;
        if (!encoder.initialize(encoderConfig)) {
            log::error("No se pudo inicializar el encoder NVENC.");
            renderer.shutdown();
            context.shutdown();
            return EXIT_FAILURE;
        }

        renderer.camera().fovDegrees = 60.0f;
        input::InputProcessor inputProcessor;

        std::vector<std::uint8_t> pixels;
        std::vector<encode::EncodedPacket> packets;

        encode::FrameView view;
        view.width = options.width;
        view.height = options.height;
        view.stride = options.width * 4;
        view.format = encoderConfig.inputFormat;

        protocol::VideoConfig videoConfig;
        videoConfig.width = options.width;
        videoConfig.height = options.height;
        videoConfig.fps = options.fps;
        videoConfig.bitrateBps = options.bitrateKbps * 1000;
        videoConfig.codec = "H264";

        // NVENC puede bufferizar el primer frame. Codificamos unos pocos hasta
        // obtener el keyframe que contiene SPS/PPS.
        std::vector<encode::EncodedPacket> warmupPackets;
        std::vector<encode::EncodedPacket> framePackets;
        int warmupFrames = 0;

        for (int i = 0; i < 3 && videoConfig.sps.empty(); ++i) {
            renderer::FrameStats warmupStats;
            if (!renderer.renderFrame(static_cast<float>(i) / static_cast<float>(options.fps), warmupStats) ||
                !renderer.captureFrame(pixels)) {
                log::error("Fallo el frame de calentamiento.");
                encoder.shutdown();
                renderer.shutdown();
                context.shutdown();
                return EXIT_FAILURE;
            }

            view.data = pixels.data();
            framePackets.clear();
            if (!encoder.encode(view, framePackets)) {
                log::error("Fallo la codificacion del frame de calentamiento.");
                encoder.shutdown();
                renderer.shutdown();
                context.shutdown();
                return EXIT_FAILURE;
            }

            for (const auto& packet : framePackets) {
                if (packet.keyframe && videoConfig.sps.empty()) {
                    protocol::extractSpsPps(packet.data, videoConfig.sps, videoConfig.pps);
                }
                warmupPackets.push_back(packet);
            }
            ++warmupFrames;
        }

        log::info(std::format("SPS: {} bytes | PPS: {} bytes",
                              videoConfig.sps.size(), videoConfig.pps.size()));

        transport::TcpServer videoServer;
        transport::TcpServer controlServer;

        if (!controlServer.start(options.controlPort, [&](const protocol::Message& message) {
                switch (message.type) {
                    case protocol::MessageType::Hello:
                        log::info("HELLO recibido (control).");
                        controlServer.send(protocol::MessageType::Hello, {});
                        break;
                    case protocol::MessageType::Ping:
                        controlServer.send(protocol::MessageType::Ping, message.payload);
                        break;
                    case protocol::MessageType::Input: {
                        input::TouchEvent touch;
                        std::size_t offset = 0;
                        std::uint16_t action = 0;
                        std::uint64_t timestamp = 0;
                        if (protocol::readU32(message.payload, offset, touch.pointerId) &&
                            protocol::readU16(message.payload, offset, action) &&
                            protocol::readF32(message.payload, offset, touch.x) &&
                            protocol::readF32(message.payload, offset, touch.y) &&
                            protocol::readU64(message.payload, offset, timestamp)) {
                            touch.action = static_cast<input::TouchAction>(action);
                            touch.timestampMs = timestamp;
                            inputProcessor.handleEvent(touch);
                            log::info(std::format("INPUT pointer={} action={} x={:.3f} y={:.3f}",
                                                  touch.pointerId,
                                                  static_cast<int>(touch.action),
                                                  touch.x,
                                                  touch.y));
                        }
                        break;
                    }
                    case protocol::MessageType::Stats:
                        break;
                    default:
                        log::warn(std::format("Mensaje de control inesperado: {}",
                                              protocol::messageTypeName(message.type)));
                        break;
                }
            })) {
            log::error("No se pudo iniciar el servidor de control.");
            encoder.shutdown();
            renderer.shutdown();
            context.shutdown();
            return EXIT_FAILURE;
        }

        if (!videoServer.start(options.videoPort, [&](const protocol::Message& message) {
                if (message.type == protocol::MessageType::Ping) {
                    videoServer.send(protocol::MessageType::Ping, message.payload);
                }
            }, [&] {
                videoServer.send(protocol::MessageType::Config, protocol::encodeVideoConfig(videoConfig));
                log::info(std::format("CONFIG enviado: {}x{} @ {} fps, SPS {} B, PPS {} B",
                                      videoConfig.width,
                                      videoConfig.height,
                                      videoConfig.fps,
                                      videoConfig.sps.size(),
                                      videoConfig.pps.size()));
                for (const auto& packet : warmupPackets) {
                    videoServer.send(protocol::MessageType::Video, packet.data);
                }
            })) {
            log::error("No se pudo iniciar el servidor de video.");
            controlServer.stop();
            encoder.shutdown();
            renderer.shutdown();
            context.shutdown();
            return EXIT_FAILURE;
        }

        const auto benchmarkStart = std::chrono::steady_clock::now();
        auto lastStatsTime = benchmarkStart;

        double renderTotalMs = 0.0;
        double readbackTotalMs = 0.0;
        double encodeTotalMs = 0.0;
        int frameIndex = warmupFrames;
        std::uint64_t videoPackets = 0;

        while (options.forever ||
               millisecondsSince(benchmarkStart) < options.durationSeconds * 1000.0) {
            const float timeSeconds =
                static_cast<float>(frameIndex) / static_cast<float>(options.fps);

            inputProcessor.applyToCamera(renderer.camera());

            renderer::FrameStats frameStats;
            if (!renderer.renderFrame(timeSeconds, frameStats)) {
                log::error(std::format("Fallo el render del frame {}.", frameIndex));
                break;
            }
            renderTotalMs += frameStats.cpuFrameMs;
            inputProcessor.markFrameRendered();

            const auto captureStart = std::chrono::steady_clock::now();
            if (!renderer.captureFrame(pixels)) {
                log::error(std::format("Fallo la captura del frame {}.", frameIndex));
                break;
            }
            readbackTotalMs += millisecondsSince(captureStart);

            view.data = pixels.data();
            packets.clear();

            const auto encodeStart = std::chrono::steady_clock::now();
            if (!encoder.encode(view, packets)) {
                log::error(std::format("Fallo la codificacion del frame {}.", frameIndex));
                break;
            }
            encodeTotalMs += millisecondsSince(encodeStart);

            for (const auto& packet : packets) {
                if (videoServer.send(protocol::MessageType::Video, packet.data)) {
                    ++videoPackets;
                }
            }

            ++frameIndex;

            if (millisecondsSince(lastStatsTime) >= 1000.0) {
                lastStatsTime = std::chrono::steady_clock::now();
                std::vector<std::uint8_t> statsPayload;
                protocol::writeU32(statsPayload, static_cast<std::uint32_t>(frameIndex));
                protocol::writeU32(statsPayload, static_cast<std::uint32_t>(videoPackets));
                protocol::writeU32(statsPayload, static_cast<std::uint32_t>(videoServer.droppedMessages()));
                protocol::writeF32(statsPayload, static_cast<float>(inputProcessor.lastInputToFrameMs()));
                controlServer.send(protocol::MessageType::Stats, statsPayload);
                log::info(std::format("Stats: frames={} video={} dropped={} inputToFrame={:.2f} ms",
                                      frameIndex,
                                      videoPackets,
                                      videoServer.droppedMessages(),
                                      inputProcessor.lastInputToFrameMs()));
            }
        }

        packets.clear();
        if (encoder.flush(packets)) {
            for (const auto& packet : packets) {
                if (videoServer.send(protocol::MessageType::Video, packet.data)) {
                    ++videoPackets;
                }
            }
        }

        const int renderedFrames = frameIndex - 1;
        const double averageRenderMs =
            renderedFrames > 0 ? renderTotalMs / static_cast<double>(renderedFrames) : 0.0;
        const double averageReadbackMs =
            renderedFrames > 0 ? readbackTotalMs / static_cast<double>(renderedFrames) : 0.0;
        const double averageEncodeMs =
            renderedFrames > 0 ? encodeTotalMs / static_cast<double>(renderedFrames) : 0.0;

        log::info(std::format("Frames: {} | paquetes de video enviados: {} | descartados: {}",
                              renderedFrames, videoPackets, videoServer.droppedMessages()));
        log::info(std::format("Promedios: render {:.3f} ms | readback {:.3f} ms | encode {:.3f} ms",
                              averageRenderMs, averageReadbackMs, averageEncodeMs));

        videoServer.stop();
        controlServer.stop();
        encoder.shutdown();
        renderer.shutdown();
        context.shutdown();
        log::info("Cierre limpio completado.");
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        log::error(std::format("Excepcion no controlada: {}", exception.what()));
        return EXIT_FAILURE;
    }
}
