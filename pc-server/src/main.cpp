#include <vulkan/vulkan.h>

#include <cstdlib>
#include <exception>
#include <format>
#include <string>
#include <string_view>

#include "core/log.hpp"
#include "core/vk_util.hpp"
#include "gpu/gpu_context.hpp"
#include "gpu/gpu_selector.hpp"

namespace {

struct Options {
    bool listOnly = false;
    bool validate = false;
    bool verbose = false;
    std::string gpuName = "RTX 4060";
};

enum class ParseResult { Ok, Help, Error };

void printUsage() {
    argb::log::info("Uso: gpu-info [opciones]");
    argb::log::info("  --list              Enumera los dispositivos y termina.");
    argb::log::info("  --gpu <texto>       Subcadena del nombre a seleccionar (defecto: RTX 4060).");
    argb::log::info("  --validate          Habilita la capa de validacion si esta disponible.");
    argb::log::info("  --verbose           Muestra las extensiones de cada dispositivo.");
    argb::log::info("  --help              Muestra esta ayuda.");
}

ParseResult parseOptions(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return ParseResult::Help;
        }
        if (arg == "--list") {
            options.listOnly = true;
            continue;
        }
        if (arg == "--validate") {
            options.validate = true;
            continue;
        }
        if (arg == "--verbose") {
            options.verbose = true;
            continue;
        }
        if (arg == "--gpu") {
            if (i + 1 >= argc) {
                argb::log::error("--gpu requiere un valor.");
                return ParseResult::Error;
            }
            options.gpuName = argv[++i];
            continue;
        }
        argb::log::error(std::format("Opcion desconocida: {}", arg));
        printUsage();
        return ParseResult::Error;
    }
    return ParseResult::Ok;
}

void printFeatures(const argb::gpu::PhysicalDeviceInfo& info) {
    const VkPhysicalDeviceFeatures& features = info.features;
    argb::log::info(std::format(
        "    features     : geometryShader={} tessellationShader={} fillModeNonSolid={} samplerAnisotropy={}",
        features.geometryShader ? "si" : "no",
        features.tessellationShader ? "si" : "no",
        features.fillModeNonSolid ? "si" : "no",
        features.samplerAnisotropy ? "si" : "no"));
}

void printDevice(std::size_t index, const argb::gpu::PhysicalDeviceInfo& info, bool verbose) {
    const VkPhysicalDeviceProperties& properties = info.properties;
    argb::log::info(std::format("[{}] {}", index, properties.deviceName));
    argb::log::info(std::format("    vendorID     : 0x{:04x} ({})",
                                properties.vendorID,
                                argb::vk::isNvidiaVendor(properties.vendorID) ? "NVIDIA" : "otro"));
    argb::log::info(std::format("    deviceID     : 0x{:04x}", properties.deviceID));
    argb::log::info(std::format("    tipo         : {}", argb::vk::deviceTypeString(properties.deviceType)));
    argb::log::info(std::format("    apiVersion   : {}", argb::vk::versionString(properties.apiVersion)));
    argb::log::info(std::format("    driverVersion: {}",
                                argb::vk::driverVersionString(properties.vendorID, properties.driverVersion)));
    argb::log::info(std::format("    queueFamilies: {}", info.queueFamilies.size()));

    for (const auto& family : info.queueFamilies) {
        argb::log::info(std::format("      family {}: {} ({} {}, timestampValidBits={})",
                                    family.index,
                                    argb::vk::queueFlagsString(family.properties.queueFlags),
                                    family.properties.queueCount,
                                    family.properties.queueCount == 1 ? "cola" : "colas",
                                    family.properties.timestampValidBits));
    }

    argb::log::info(std::format("    extensiones  : {}", info.extensions.size()));
    if (verbose) {
        for (const auto& extension : info.extensions) {
            argb::log::info(std::format("      - {}", extension));
        }
    }
    printFeatures(info);
}

} // namespace

int main(int argc, char** argv) {
    using namespace argb;

    Options options;
    const ParseResult parsed = parseOptions(argc, argv, options);
    if (parsed == ParseResult::Help) return EXIT_SUCCESS;
    if (parsed == ParseResult::Error) return EXIT_FAILURE;

    try {
        log::info("ARGB gpu-info - Fase 01 (GPU Discovery & Baseline)");

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

        log::info(std::format("Dispositivos fisicos encontrados: {}", devices.size()));
        for (std::size_t i = 0; i < devices.size(); ++i) {
            printDevice(i, devices[i], options.verbose);
        }

        if (options.listOnly) {
            log::info("Modo --list: finalizando sin crear logical device.");
            context.shutdown();
            return EXIT_SUCCESS;
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
        log::info(std::format("Dispositivo seleccionado: [{}] {}",
                              *selectedIndex,
                              selected.properties.deviceName));

        const auto graphicsFamily = gpu::findGraphicsQueueFamily(selected);
        if (!graphicsFamily) {
            log::error("El dispositivo seleccionado no tiene una queue family con soporte grafico.");
            context.shutdown();
            return EXIT_FAILURE;
        }

        if (!context.createLogicalDevice(selected, *graphicsFamily)) {
            log::error("No se pudo crear el logical device.");
            context.shutdown();
            return EXIT_FAILURE;
        }

        log::info("Fase 01 completada: instancia, seleccion de GPU y logical device OK.");
        context.shutdown();
        log::info("Cierre limpio completado.");
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        log::error(std::format("Excepcion no controlada: {}", exception.what()));
        return EXIT_FAILURE;
    }
}
