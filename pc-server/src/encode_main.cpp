#include "core/log.hpp"
#include "encoders/ffmpeg_nvenc_encoder.hpp"
#include "encoders/frame_encoder.hpp"
#include "encoders/video_packetizer.hpp"
#include "gpu/gpu_context.hpp"
#include "gpu/gpu_selector.hpp"
#include "renderer/vulkan_renderer.hpp"

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
    int frames = 120;
    double durationSeconds = 0.0;
    std::string output = "out.h264";
    bool validate = false;
    std::string gpuName = "RTX 4060";
};

enum class ParseResult { Ok, Help, Error };

void printUsage() {
    argb::log::info("Uso: argb-encode [opciones]");
    argb::log::info("  --width <n>           Ancho (defecto: 1280).");
    argb::log::info("  --height <n>          Alto (defecto: 720).");
    argb::log::info("  --fps <n>             FPS objetivo (defecto: 30).");
    argb::log::info("  --bitrate-kbps <n>    Bitrate en kbps (defecto: 8000).");
    argb::log::info("  --frames <n>          Frames a codificar (defecto: 120).");
    argb::log::info("  --duration-seconds <s>  Codifica durante s segundos.");
    argb::log::info("  --out <archivo>       Salida H.264 (defecto: out.h264).");
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

bool parsePositiveInt(std::string_view text, int& value) {
    try {
        std::size_t position = 0;
        const int parsed = std::stoi(std::string(text), &position);
        if (position != text.size() || parsed <= 0) return false;
        value = parsed;
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

        if (arg == "--width" || arg == "--height" || arg == "--fps" ||
            arg == "--bitrate-kbps" || arg == "--frames" || arg == "--duration-seconds" ||
            arg == "--out" || arg == "--gpu") {
            std::string_view value;
            if (!requireValue(value)) {
                argb::log::error(std::format("{} requiere un valor.", arg));
                return ParseResult::Error;
            }

            if (arg == "--width") {
                if (!parseUnsigned(value, options.width) || options.width == 0) {
                    argb::log::error("Valor invalido para --width.");
                    return ParseResult::Error;
                }
            } else if (arg == "--height") {
                if (!parseUnsigned(value, options.height) || options.height == 0) {
                    argb::log::error("Valor invalido para --height.");
                    return ParseResult::Error;
                }
            } else if (arg == "--fps") {
                if (!parseUnsigned(value, options.fps) || options.fps == 0) {
                    argb::log::error("Valor invalido para --fps.");
                    return ParseResult::Error;
                }
            } else if (arg == "--bitrate-kbps") {
                if (!parseUnsigned(value, options.bitrateKbps) || options.bitrateKbps == 0) {
                    argb::log::error("Valor invalido para --bitrate-kbps.");
                    return ParseResult::Error;
                }
            } else if (arg == "--frames") {
                if (!parsePositiveInt(value, options.frames)) {
                    argb::log::error("Valor invalido para --frames.");
                    return ParseResult::Error;
                }
            } else if (arg == "--duration-seconds") {
                if (!parsePositiveDouble(value, options.durationSeconds)) {
                    argb::log::error("Valor invalido para --duration-seconds.");
                    return ParseResult::Error;
                }
            } else if (arg == "--out") {
                options.output = std::string(value);
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
        log::info("ARGB argb-encode - Fase 03 (Frame Capture & Hardware Encoding)");

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

        encode::VideoPacketizer packetizer;
        if (!packetizer.open(options.output)) {
            log::error(std::format("No se pudo abrir '{}' para escritura.", options.output));
            encoder.shutdown();
            renderer.shutdown();
            context.shutdown();
            return EXIT_FAILURE;
        }

        renderer.camera().position = {2.4f, 1.8f, 3.2f};
        renderer.camera().target = {0.0f, 0.0f, 0.0f};
        renderer.camera().fovDegrees = 60.0f;

        std::vector<std::uint8_t> pixels;
        std::vector<encode::EncodedPacket> packets;

        double renderTotalMs = 0.0;
        double readbackTotalMs = 0.0;
        double encodeTotalMs = 0.0;
        int renderedFrames = 0;
        int encodedPackets = 0;

        const auto benchmarkStart = std::chrono::steady_clock::now();

        while (true) {
            if (options.durationSeconds > 0.0) {
                if (millisecondsSince(benchmarkStart) >= options.durationSeconds * 1000.0) {
                    break;
                }
            } else if (renderedFrames >= options.frames) {
                break;
            }

            const float timeSeconds =
                static_cast<float>(renderedFrames) / static_cast<float>(options.fps);

            renderer::FrameStats renderStats;
            if (!renderer.renderFrame(timeSeconds, renderStats)) {
                log::error(std::format("Fallo el render del frame {}.", renderedFrames));
                packetizer.close();
                encoder.shutdown();
                renderer.shutdown();
                context.shutdown();
                return EXIT_FAILURE;
            }
            renderTotalMs += renderStats.cpuFrameMs;

            const auto captureStart = std::chrono::steady_clock::now();
            if (!renderer.captureFrame(pixels)) {
                log::error(std::format("Fallo la captura del frame {}.", renderedFrames));
                packetizer.close();
                encoder.shutdown();
                renderer.shutdown();
                context.shutdown();
                return EXIT_FAILURE;
            }
            readbackTotalMs += millisecondsSince(captureStart);

            encode::FrameView view;
            view.data = pixels.data();
            view.width = options.width;
            view.height = options.height;
            view.stride = options.width * 4;
            view.format = encoderConfig.inputFormat;

            packets.clear();
            const auto encodeStart = std::chrono::steady_clock::now();
            if (!encoder.encode(view, packets)) {
                log::error(std::format("Fallo la codificacion del frame {}.", renderedFrames));
                packetizer.close();
                encoder.shutdown();
                renderer.shutdown();
                context.shutdown();
                return EXIT_FAILURE;
            }
            encodeTotalMs += millisecondsSince(encodeStart);

            for (const auto& packet : packets) {
                if (!packetizer.writePacket(packet)) {
                    log::error("Fallo la escritura del paquete H.264.");
                    packetizer.close();
                    encoder.shutdown();
                    renderer.shutdown();
                    context.shutdown();
                    return EXIT_FAILURE;
                }
                ++encodedPackets;
            }

            ++renderedFrames;
        }

        packets.clear();
        if (!encoder.flush(packets)) {
            log::error("Fallo el flush del encoder.");
        }
        for (const auto& packet : packets) {
            if (packetizer.writePacket(packet)) {
                ++encodedPackets;
            }
        }
        packetizer.close();

        const double elapsedSeconds = millisecondsSince(benchmarkStart) / 1000.0;
        const double averageRenderMs =
            renderedFrames > 0 ? renderTotalMs / static_cast<double>(renderedFrames) : 0.0;
        const double averageReadbackMs =
            renderedFrames > 0 ? readbackTotalMs / static_cast<double>(renderedFrames) : 0.0;
        const double averageEncodeMs =
            renderedFrames > 0 ? encodeTotalMs / static_cast<double>(renderedFrames) : 0.0;
        const double averageFps =
            elapsedSeconds > 0.0 ? static_cast<double>(renderedFrames) / elapsedSeconds : 0.0;
        const double encodedDurationSeconds =
            options.fps > 0 ? static_cast<double>(renderedFrames) / static_cast<double>(options.fps) : 0.0;
        const double bitrateMbps =
            encodedDurationSeconds > 0.0
                ? (static_cast<double>(packetizer.totalBytes()) * 8.0 / encodedDurationSeconds) / 1.0e6
                : 0.0;

        log::info(std::format("Frames: {} | video {:.2f} s | pipeline {:.1f} FPS ({:.2f} s)",
                              renderedFrames, encodedDurationSeconds, averageFps, elapsedSeconds));
        log::info(std::format("Promedios: render {:.3f} ms | readback {:.3f} ms | encode {:.3f} ms",
                              averageRenderMs, averageReadbackMs, averageEncodeMs));
        log::info(std::format("Paquetes: {} ({} keyframes) | {} bytes | {:.2f} Mbps",
                              encodedPackets,
                              packetizer.keyframeCount(),
                              packetizer.totalBytes(),
                              bitrateMbps));
        log::info(std::format("Salida H.264: {}", options.output));

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
