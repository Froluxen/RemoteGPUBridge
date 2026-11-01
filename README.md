# Remote GPU Bridge (ARGB)

Prueba de concepto de **remote rendering interactivo**: una PC Windows con GPU
NVIDIA renderiza una escena 3D con Vulkan, la codifica en H.264 mediante NVENC y
la transmite por USB (ADB Reverse) a un dispositivo Android, que la decodifica
con `MediaCodec` y devuelve los eventos táctiles a la PC.

## Estado

- **Fase 01 — GPU Discovery & Baseline:** completada (`gpu-info.exe`).
- **Fase 02 — Vulkan Rendering Engine:** completada (`argb-render.exe`).
- **Fase 03 — Frame Capture & Hardware Encoding:** completada (`argb-encode.exe`).
- **Fase 04 — USB Transport Layer:** completada (`argb-server.exe` + `docs/protocol.md`).
- **Fase 05 — Android Video Client:** completada (`android-client/`, decoder hardware 30 FPS).
- **Fase 06 — Touch Input Bridge:** completada (orbit + pinch, `InputProcessor`).
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

O simplemente ejecutar `build.bat` (configura el entorno y compila todo).

## Ejecución

```bat
build\pc-server\gpu-info.exe
build\pc-server\argb-render.exe --frames 120 --snapshot-every 30
build\pc-server\argb-encode.exe --frames 300 --fps 30 --out out.h264
build\pc-server\argb-server.exe --forever
```

### Servidor de streaming + ADB Reverse

```bat
adb reverse tcp:5000 tcp:5000
adb reverse tcp:5001 tcp:5001
adb reverse --list
build\pc-server\argb-server.exe --forever
```

El cliente Android se conecta a `127.0.0.1:5000` (vídeo) y `127.0.0.1:5001`
(control). El protocolo está documentado en [`docs/protocol.md`](docs/protocol.md).

### Cliente Android

Requisitos: JDK 17 y Android SDK (platform 35 + build-tools 35.0.0).

```bat
build-android.bat
adb install -r android-client\app\build\outputs\apk\debug\app-debug.apk
adb shell am start -n com.argb.client/.MainActivity
```

El cliente usa `MediaCodec` con el decoder H.264 por hardware y presenta el
vídeo en una `Surface`, con overlay de estado/RTT.

`argb-render` renderiza offscreen y guarda snapshots PPM en `snapshots/`.
`argb-encode` renderiza, captura y codifica con NVENC a un flujo H.264.

Opciones de `argb-render`: `--width`, `--height`, `--frames`,
`--duration-seconds`, `--snapshot-every`, `--out`, `--gpu`, `--validate`.
Opciones de `argb-encode`: `--width`, `--height`, `--fps`, `--bitrate-kbps`,
`--frames`, `--duration-seconds`, `--out`, `--gpu`, `--validate`.

### Requisito de FFmpeg para `argb-encode`

Se usa **FFmpeg 8.0 shared (BtbN)** porque su NVENC API es compatible con el
driver 581.80 (NVENC API 13.0). Los builds master requieren NVENC API 13.1.
`build.bat` detecta automáticamente el paquete instalado en
`%LOCALAPPDATA%\Microsoft\WinGet\Packages\BtbN.FFmpeg.GPL.Shared.8.0_*`.

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
