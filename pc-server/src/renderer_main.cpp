#include "core/log.hpp"
#include "gpu/gpu_context.hpp"
#include "gpu/gpu_selector.hpp"
#include "renderer/camera.hpp"
#include "renderer/vulkan_renderer.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>

namespace {

struct Options {
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    int frames = 120;
    double durationSeconds = 0.0;
    int snapshotEvery = 0;
    std::string outDir = "snapshots";
    bool validate = false;
    std::string gpuName = "RTX 4060";
};

enum class ParseResult { Ok, Help, Error };

void printUsage() {
    argb::log::info("Uso: argb-render [opciones]");
    argb::log::info("  --width <n>          Ancho del render target (defecto: 1280).");
    argb::log::info("  --height <n>         Alto del render target (defecto: 720).");
    argb::log::info("  --frames <n>         Numero de frames a renderizar (defecto: 120).");
    argb::log::info("  --duration-seconds <s>  Renderiza durante s segundos (alternativa a --frames).");
    argb::log::info("  --snapshot-every <n> Guarda un snapshot cada n frames (defecto: solo el ultimo).");
    argb::log::info("  --out <dir>          Carpeta de snapshots (defecto: snapshots).");
    argb::log::info("  --gpu <texto>        Subcadena del nombre de GPU (defecto: RTX 4060).");
    argb::log::info("  --validate           Habilita la capa de validacion si esta disponible.");
    argb::log::info("  --help               Muestra esta ayuda.");
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
        if (arg == "--width" || arg == "--height" || arg == "--frames" ||
            arg == "--duration-seconds" || arg == "--snapshot-every" || arg == "--out" ||
            arg == "--gpu") {
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
            } else if (arg == "--snapshot-every") {
                if (!parsePositiveInt(value, options.snapshotEvery)) {
                    argb::log::error("Valor invalido para --snapshot-every.");
                    return ParseResult::Error;
                }
            } else if (arg == "--out") {
                options.outDir = std::string(value);
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

} // namespace

int main(int argc, char** argv) {
    using namespace argb;

    Options options;
    const ParseResult parsed = parseOptions(argc, argv, options);
    if (parsed == ParseResult::Help) return EXIT_SUCCESS;
    if (parsed == ParseResult::Error) return EXIT_FAILURE;

    try {
        log::info("ARGB argb-render - Fase 02 (Vulkan Rendering Engine)");

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

        renderer::RendererConfig config;
        config.width = options.width;
        config.height = options.height;
        config.shaderDir = shaderDirectory(argv[0]);

        renderer::VulkanRenderer renderer;
        if (!renderer.initialize(context, selected, *graphicsFamily, config)) {
            log::error("No se pudo inicializar el renderer.");
            renderer.shutdown();
            context.shutdown();
            return EXIT_FAILURE;
        }

        renderer.camera().position = {2.4f, 1.8f, 3.2f};
        renderer.camera().target = {0.0f, 0.0f, 0.0f};
        renderer.camera().fovDegrees = 60.0f;

        std::error_code directoryError;
        std::filesystem::create_directories(options.outDir, directoryError);

        double totalMs = 0.0;
        double minMs = 1.0e30;
        double maxMs = 0.0;
        int renderedFrames = 0;
        int snapshots = 0;

        const auto benchmarkStart = std::chrono::steady_clock::now();

        while (true) {
            if (options.durationSeconds > 0.0) {
                const double elapsed = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - benchmarkStart).count();
                if (elapsed >= options.durationSeconds) break;
            } else if (renderedFrames >= options.frames) {
                break;
            }

            const float timeSeconds = static_cast<float>(renderedFrames) / 60.0f;

            renderer::FrameStats stats;
            if (!renderer.renderFrame(timeSeconds, stats)) {
                log::error(std::format("Fallo el render del frame {}.", renderedFrames));
                renderer.shutdown();
                context.shutdown();
                return EXIT_FAILURE;
            }

            totalMs += stats.cpuFrameMs;
            minMs = std::min(minMs, stats.cpuFrameMs);
            maxMs = std::max(maxMs, stats.cpuFrameMs);

            const bool onSchedule =
                options.snapshotEvery > 0 && renderedFrames % options.snapshotEvery == 0;

            const int currentFrame = renderedFrames;
            ++renderedFrames;

            bool isLastFrame = false;
            if (options.durationSeconds > 0.0) {
                const double elapsed = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - benchmarkStart).count();
                isLastFrame = elapsed >= options.durationSeconds;
            } else {
                isLastFrame = renderedFrames >= options.frames;
            }

            if (onSchedule || isLastFrame) {
                const std::filesystem::path path =
                    std::filesystem::path(options.outDir) /
                    std::format("frame_{:05d}.ppm", currentFrame);
                if (renderer.saveSnapshot(path)) {
                    ++snapshots;
                } else {
                    log::warn(std::format("No se pudo guardar '{}'.", path.string()));
                }
            }

            if (isLastFrame) break;
        }

        const double elapsedSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - benchmarkStart).count();
        const double averageMs = renderedFrames > 0 ? totalMs / static_cast<double>(renderedFrames) : 0.0;
        const double averageFps = averageMs > 0.0 ? 1000.0 / averageMs : 0.0;

        log::info(std::format("Frames: {} | {:.2f} s | promedio {:.3f} ms ({:.1f} FPS) | min {:.3f} ms | max {:.3f} ms",
                              renderedFrames, elapsedSeconds, averageMs, averageFps, minMs, maxMs));
        log::info(std::format("Snapshots guardados: {}", snapshots));

        renderer.shutdown();
        context.shutdown();
        log::info("Cierre limpio completado.");
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        log::error(std::format("Excepcion no controlada: {}", exception.what()));
        return EXIT_FAILURE;
    }
}
