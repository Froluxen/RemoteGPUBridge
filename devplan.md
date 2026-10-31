# Plan de Desarrollo — Android Remote GPU Bridge (ARGB)

> **Fecha:** 31 de octubre de 2026
> **Estado:** Documentado; Fases 01–03 completadas, Fase 04 por iniciar
> **Repositorio:** https://github.com/Froluxen/RemoteGPUBridge
> **Fuente base:** `Android-Remote-GPU-Bridge-Development-Plan.md`
> (este documento consolida y actualiza ese plan con las decisiones acordadas)

---

## 1. Objetivo y alcance

**ARGB** es una prueba de concepto (POC) de **remote rendering interactivo**:

1. La PC renderiza una escena 3D con su GPU NVIDIA RTX 4060.
2. Captura los frames renderizados.
3. Los codifica en H.264 mediante NVENC.
4. Transmite el vídeo al teléfono por USB (ADB Reverse).
5. Android decodifica con `MediaCodec` y lo presenta en pantalla.
6. Los eventos táctiles regresan a la PC.
7. La PC actualiza la escena a partir de esos controles.

### Prioridad acordada

- **Objetivo principal:** completar la POC de remote rendering (**fases 1–7**).
- **Fuera del plazo base:** la redirección transparente de GPU de apps Android de
  terceros (**fase 8**) se mantiene como investigación separada y **no** condiciona
  el éxito de la POC.

### Qué NO promete esta POC

- No demuestra que una app Android comercial pueda usar remotamente la GPU del PC.
- No evita protecciones, no inyecta código en apps de terceros, no requiere root.

---

## 2. Hardware y restricciones

### PC
- CPU: Intel Core i7 de 12.ª gen (modelo exacto pendiente de confirmar).
- GPU: NVIDIA RTX 4060 Laptop GPU.
- RAM: 16 GB.
- SO: Windows.
- Espacio libre: ~10–20 GB.

### Android
- Xiaomi Redmi Note 13 4G.
- RAM: 12 GB (verificar cuánta es física vs. expansión virtual).
- Sin root. Conexión USB.

### Restricciones de diseño
- Sin root. Sin emulador (salvo último recurso). Sin servicios cloud.
- Uso moderado de almacenamiento. Priorizar USB sobre Wi-Fi.
- Mantener la POC propia separada de la investigación de apps de terceros.
- No añadir dependencias antes de que una fase las necesite.

---

## 3. Decisiones de ingeniería (actualizadas)

Decisiones tomadas del plan base:

- **Servidor (PC):** C++20 + CMake + Vulkan (render) + NVENC (codificación H.264).
- **Cliente (Android):** Kotlin + `MediaCodec` (decodificación).
- **Transporte:** TCP sobre ADB Reverse, dos canales: vídeo `:5000`, control `:5001`.
- **Protocolo:** binario, versionado, con framing explícito y header común
  (`magic`, `version`, `type`, `length`, `sequence`).
- **Renderizado:** destino offscreen primero (sin swapchain); ventana local opcional
  para depuración.
- **Captura:** readback GPU→CPU (staging buffer) para validar el pipeline; optimizar
  después con mediciones.

Decisiones nuevas acordadas (31/10/2026):

| Decisión | Elección | Justificación |
| --- | --- | --- |
| API de NVENC | **FFmpeg (`h264_nvenc`)**, encapsulado tras `FrameEncoder` | Vía más rápida a un H.264 válido usando el hardware NVENC real. La interfaz permite reemplazarlo por el Video Codec SDK puro en Fase 07 si las métricas lo piden. Se usa **FFmpeg 8.0 (BtbN shared)**: el build master requiere NVENC API 13.1 y el driver 581.80 solo soporta 13.0. |
| Paquetización H.264 | **NAL units con longitud explícita** + `SPS`/`PPS` por separado | Encaja con el framing ya previsto en el protocolo. En Android: `SPS`/`PPS` como `csd-0`/`csd-1` de `MediaCodec`. |
| Ritmo de trabajo | **Individual, sin fecha límite dura** | Priorizar cerrar bien cada fase antes de avanzar. |
| Estado del entorno | **Verificado e instalado** (31/10/2026): CMake 4.4.4, Ninja 1.13.2, MSVC 14.44 + Windows SDK 10.0.26100, Vulkan SDK 1.4.363; RTX 4060 visible por Vulkan. `adb`/Android SDK pendiente para Fase 04/05. | Paso previo a la Fase 01 completado. |

Pendientes de definir más adelante (no bloquean la Fase 01):
- Byte order (endianness) y tamaño máximo de payload del protocolo.
- Formato concreto de conversión NAL ↔ MediaCodec (detalle de Fase 04/05).

---

## 4. Arquitectura (resumen)

```
PC Windows                                  Android
Vulkan Renderer → Frame Capture  ──TCP:5000──▶  Video Receiver → MediaCodec → Surface
   (RTX 4060)       │                                    ▲
                    ▼                                    │
             NVENC H.264  ───────────────────────────────┘
Input Processor ◀──TCP:5001──  Input Client ◀── Touch Events
```

Módulos clave del servidor: `GpuContext`, `GpuSelector`, `VulkanRenderer`,
`Scene`, `Camera`, `FrameCapture`, `FrameEncoder`, `NvencEncoder`, `TcpServer`,
`InputProcessor`, `Telemetry`.

Módulos clave del cliente: `NetworkClient`, `VideoReceiver`, `H264Parser`,
`MediaCodecDecoder`, `TouchController`, `InputSender`, `StatsOverlay`.

---

## 5. Roadmap por fases

Estimación orientativa total (individual): **3–5 semanas** (no es garantía).

### Fase 01 — GPU Discovery & Baseline (1–2 días) — ✅ Completada
Detectar e inicializar la RTX 4060 por Vulkan.
- Estructura del repo, CMake + C++20, instancia Vulkan, enumerar physical devices,
  seleccionar la RTX 4060 explícitamente, crear logical device, logs y cierre limpio.
- **Entregable:** `gpu-info.exe`.
- **Regla de avance:** no empezar render/streaming/Android hasta completarla.

### Fase 02 — Vulkan Rendering Engine (3–5 días) — ✅ Completada
- Contexto Vulkan, render target offscreen, shaders/pipelines, buffers, cámara.
- Triángulo → cubo rotatorio → escena 3D sencilla. FPS y tiempo de render.
- **Meta local:** 1280×720 a 60 FPS, estable 10 minutos.
- **Entregable:** `argb-render.exe` (offscreen 1280×720, cubo rotatorio con
  depth test, snapshots PPM y métricas de frame time/FPS).

### Fase 03 — Frame Capture & Hardware Encoding (3–5 días) — ✅ Completada
- `FrameCapture` (staging buffer), interfaz `FrameEncoder`, integración NVENC vía
  FFmpeg, config H.264 (resolución/bitrate/FPS), `VideoPacketizer`.
- **Meta:** flujo H.264 reproducible y estable a 720p30.
- **Entregable:** `argb-encode.exe` (render → readback BGRA → `h264_nvenc` → `.h264`).
- **Resultado:** 1800 frames a 720p30 (60 s de vídeo), 8 Mbps, decodificación
  completa sin errores; pipeline ~51 FPS (readback ~17 ms, encode ~1,6 ms).

### Fase 04 — USB Transport Layer (2–4 días)
- Servidor TCP Windows, canales vídeo/control, header binario, `HELLO`/`CONFIG`,
  `PING`, manejo de desconexión/reconexión. Framing con lecturas parciales.
- Prueba de 30 min sin bloqueos ni crecimiento de memoria.

### Fase 05 — Android Video Client (3–5 días)
- Cliente TCP, parser, `VideoReceiver`, gestión SPS/PPS, `MediaCodec` + `Surface`,
  reconexión, overlay de estadísticas.
- **Meta:** reproducción estable a 720p30 con decoder por hardware.

### Fase 06 — Touch Input Bridge (2–4 días)
- `ACTION_DOWN/MOVE/UP/CANCEL`, multitouch, coordenadas normalizadas, `InputProcessor`,
  control de cámara, zoom, botones virtuales.

### Fase 07 — Performance Optimization (3–5 días)
- Medir cada etapa (render/readback/encode/transport/decode/present/input),
  reutilizar buffers, colas acotadas, descarte de frames viejos, comparar antes/después.

### Fase 08 — GPU Redirection Research (abierta; fuera del plazo base)
- Investigación documentada de viabilidad (Vulkan Layers, ANGLE, Graphics HAL, etc.).
- **Entregable:** `docs/research/android-gpu-redirection.md`.

---

## 6. Métricas objetivo (preliminares)

| Métrica | Objetivo inicial |
| --- | --- |
| Resolución de streaming | 1280×720 |
| FPS de streaming | 30 FPS |
| FPS de render local | 60 FPS |
| Codec | H.264 |
| Transporte | USB + ADB Reverse |
| Latencia end-to-end | < 100 ms (aspiracional) |
| Codificación | NVIDIA NVENC |
| Decodificación | MediaCodec |

Instrumentación obligatoria: `render_time_ms`, `readback_time_ms`,
`encode_time_ms`, `transport_time_ms`, `decode_time_ms`, `input_latency_ms`,
`fps`, `dropped_frames`, `queue_depth`, `bitrate`, `connection_state`.

---

## 7. Riesgos principales

| Riesgo | Estrategia |
| --- | --- |
| Selección incorrecta de GPU | Seleccionar por propiedades, explícitamente. |
| Readback GPU→CPU costoso | Validar primero; optimizar después con mediciones. |
| Integración NVENC | Encapsular en `FrameEncoder`; flujo mínimo independiente. |
| Interoperabilidad Vulkan–NVENC | Investigación separada; no bloquear la POC. |
| Framing TCP incorrecto | Parser incremental + pruebas con lecturas parciales. |
| Latencia acumulada | Colas acotadas + descarte controlado de frames viejos. |
| Incompatibilidad MediaCodec | Consultar capacidades del dispositivo y probar configs. |
| Redirección de apps comerciales | Investigación separada; no es requisito de la POC. |

---

## 8. Estado actual y próximos pasos

**Estado:** Fases 01, 02 y 03 completadas. Repositorio conectado a
`https://github.com/Froluxen/RemoteGPUBridge.git` (rama `main`).

Resultado de la Fase 01:
- `gpu-info.exe` compila con CMake + Ninja + MSVC (C++20).
- Enumera 2 dispositivos: NVIDIA RTX 4060 Laptop (discreta) e Intel Iris Xe.
- Selecciona la RTX 4060 por nombre + vendor, independiente del orden.
- Crea el logical device y cierra limpiamente (exit code 0).

Resultado de la Fase 02:
- `argb-render.exe` renderiza un cubo 3D con cámara, depth test y rotación.
- Render target offscreen 1280×720 (color B8G8R8A8_UNORM, depth D32_SFLOAT).
- Shaders GLSL compilados a SPIR-V con `glslc` desde CMake.
- Snapshots PPM para verificación visual.
- Métricas de frame time y FPS en consola.
- Prueba de estabilidad de 10 min: 4.623.774 frames, 0,129 ms promedio
  (~7727 FPS), cierre limpio.

Resultado de la Fase 03:
- `argb-encode.exe`: render Vulkan → readback BGRA → `h264_nvenc` → `.h264`.
- 720p30, 8 Mbps; 1 minuto de vídeo (1800 frames) decodificado sin errores.
- Métricas: render ~0,7 ms, readback ~17 ms, encode ~1,6 ms; pipeline ~51 FPS.
- Módulos: `FrameEncoder` (interfaz), `FfmpegNvencEncoder`, `VideoPacketizer`.

Próximos pasos:
1. ~~Verificar el entorno~~ ✔ completado (31/10/2026).
2. ~~Fase 01: `gpu-info.exe`~~ ✔ completado (31/10/2026).
3. ~~Fase 02: renderer offscreen + cubo rotatorio~~ ✔ completado (31/10/2026).
4. ~~Fase 03: captura de frames + NVENC~~ ✔ completado (31/10/2026).
5. Iniciar la Fase 04: transporte USB (TCP + ADB Reverse) y protocolo binario.

**Definition of Done — Fase 01** (resumida):
- [x] Repo y estructura creados; CMake configura y compila con C++20.
- [x] Vulkan inicializa; se enumeran los physical devices.
- [x] Se identifica y selecciona la RTX 4060 explícitamente.
- [x] Se crea el logical device; logs de errores y capacidades.
- [x] La app finaliza limpiamente.