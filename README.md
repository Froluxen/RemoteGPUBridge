# Remote GPU Bridge (ARGB)

Prueba de concepto de **remote rendering interactivo**: una PC Windows con GPU
NVIDIA renderiza una escena 3D con Vulkan, la codifica en H.264 mediante NVENC y
la transmite por USB (ADB Reverse) a un dispositivo Android, que la decodifica
con `MediaCodec` y devuelve los eventos táctiles a la PC.

## Estado

- **Fase 01 — GPU Discovery & Baseline:** completada (`gpu-info.exe`).
- Entorno de desarrollo verificado.

## Documentación

- [`devplan.md`](devplan.md) — plan de desarrollo consolidado y actualizado.
- [`Android-Remote-GPU-Bridge-Development-Plan.md`](Android-Remote-GPU-Bridge-Development-Plan.md)
  — plan de investigación original.

## Entorno verificado

- Windows + NVIDIA RTX 4060 Laptop GPU (driver 581.80).
- CMake 4.4.4 y Ninja 1.13.2.
- MSVC 14.44 (Visual Studio 2022 Build Tools) + Windows SDK 10.0.26100.
- Vulkan SDK 1.4.363 (incluye `glslc` y `glslangValidator`).
- Pendiente para la fase Android: Android SDK / `adb`.

## Compilación

Abrir el **x64 Native Tools Command Prompt for VS 2022** (o ejecutar
`vcvars64.bat`) y después:

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

## Roadmap

1. GPU Discovery & Baseline
2. Vulkan Rendering Engine
3. Frame Capture & Hardware Encoding
4. USB Transport Layer
5. Android Video Client
6. Touch Input Bridge
7. Performance Optimization
8. GPU Redirection Research (investigación separada)

## Restricciones

Sin root, sin emuladores (salvo último recurso), sin servicios cloud, prioridad
USB y uso moderado de disco.
