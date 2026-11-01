# Benchmarks

## Metodología

- **Offline:** `argb-encode.exe --frames 300 --fps 30 --out bench.h264`
  (render → readback → NVENC, sin red).
- **Streaming:** `argb-server.exe --duration-seconds 20` + app Android por
  ADB Reverse.
- **Equipo:** RTX 4060 Laptop (driver 581.80), Intel Core i7 12.ª gen, 16 GB.
- Las métricas son promedios por frame medidas en el servidor.

## Antes / después (Fase 07)

| Etapa | Antes | Después |
| --- | --- | --- |
| Render (Vulkan) | 0,35 ms | 0,19 ms |
| **Readback GPU→CPU** | **19,86 ms** | **0,75 ms** (offline) / ~2,2 ms (streaming) |
| Encode (NVENC) | 1,92 ms | 1,05–1,42 ms |
| Pipeline | 45 FPS | 452 FPS offline · 30 FPS con pacing |
| Frames descartados | 0 | 0 |

## Causa del cuello de botella

1. Se llamaba a `vkMapMemory` / `vkUnmapMemory` **en cada frame**.
2. La memoria de readback no era cacheada para la CPU, así que el `memcpy`
   desde el buffer host era lento.

## Corrección

- Buffer de readback **mapeado persistentemente** durante toda la vida del
  renderer.
- Se prioriza memoria `HOST_VISIBLE | HOST_COHERENT | HOST_CACHED`, con
  fallback a `HOST_VISIBLE | HOST_COHERENT`.
- Se mantiene la ruta de copia GPU (imagen → buffer) y se evita el mapeo por frame.

## Pacing

- `argb-server` limita el pipeline al FPS objetivo (30).
- Se activa `timeBeginPeriod(1)` en Windows para que `sleep_for` tenga
  resolución de 1 ms (sin esto, Windows redondea a ~15,6 ms y baja a ~23 FPS).

## Etapas en streaming (720p30)

| Etapa | Medición |
| --- | --- |
| Render | ~0,9 ms |
| Readback | ~2,2 ms |
| Encode NVENC | ~1,4 ms |
| Transporte | 30 paquetes/s, 8 Mbps, 0 drops |
| Decode Android | `c2.qti.avc.decoder`, 30 FPS, 0 drops, ~33 ms/frame |
| Entrada → frame | 2–35 ms (picos ~50 ms) |

## Colas y latencia

- `TcpServer` (vídeo): cola máxima de 32 mensajes; descarta el más antiguo.
- Cola táctil (Android): máximo 64 eventos; descarta el más antiguo.
- El pacing evita acumular frames y mantiene la latencia acotada.
