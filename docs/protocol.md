# Protocolo ARGB

Protocolo binario propio para el transporte entre el servidor Windows y el
cliente Android.

## Transporte

- **TCP sobre ADB Reverse (USB).**
- Canal de vídeo: `127.0.0.1:5000` en el dispositivo (forward a `5000` en el PC).
- Canal de control: `127.0.0.1:5001` en el dispositivo (forward a `5001` en el PC).
- **Byte order:** little-endian.
- **Un mensaje = header (16 bytes) + payload.**

## Header

| Campo | Tipo | Offset | Descripción |
| --- | --- | --- | --- |
| `magic` | `uint32` | 0 | `0x41524742` (`"ARGB"`) |
| `version` | `uint16` | 4 | Versión del protocolo (`1`) |
| `type` | `uint16` | 6 | Tipo de mensaje |
| `length` | `uint32` | 8 | Tamaño del payload en bytes |
| `sequence` | `uint32` | 12 | Número secuencial por conexión |

- Payload máximo: **16 MB** (`16 * 1024 * 1024`).
- Un header con `magic`/`version` inválidos o `length` excesivo cierra la conexión.

## Tipos de mensaje

| Valor | Tipo | Dirección habitual | Uso |
| --- | --- | --- | --- |
| 1 | `HELLO` | Ambos | Handshake inicial (payload vacío) |
| 2 | `CONFIG` | PC → Android | Configuración del vídeo y CSD |
| 3 | `VIDEO` | PC → Android | Paquete H.264 (Annex-B) |
| 4 | `INPUT` | Android → PC | Eventos táctiles (Fase 06) |
| 5 | `STATS` | Ambos | Métricas |
| 6 | `PING` | Ambos | Eco para medir RTT |
| 7 | `ERROR` | Ambos | Error de protocolo/sesión |

## CONFIG

Payload:

| Campo | Tipo | Descripción |
| --- | --- | --- |
| `width` | `uint32` | Ancho en píxeles |
| `height` | `uint32` | Alto en píxeles |
| `fps` | `uint32` | FPS objetivo |
| `bitrateBps` | `uint32` | Bitrate en bits/s |
| `codec` | `uint32` + bytes | Longitud + `"H264"` |
| `sps` | `uint32` + bytes | SPS NAL (sin start code) |
| `pps` | `uint32` + bytes | PPS NAL (sin start code) |

El servidor envía `CONFIG` en cuanto el cliente se conecta al canal de vídeo,
seguido de un keyframe inicial.

## Vídeo

- El flujo H.264 viaja como **Annex-B** (start codes `00 00 01` / `00 00 00 01`).
- `SPS`/`PPS` se envían en `CONFIG` y también in-band en los keyframes.
- Cada mensaje `VIDEO` contiene los datos de un `AVPacket` del encoder NVENC.
- El cliente debe tolerar lecturas parciales y mensajes concatenados (framing
  explícito por `length`).

## ADB Reverse

```bash
adb devices
adb reverse tcp:5000 tcp:5000
adb reverse tcp:5001 tcp:5001
adb reverse --list
```

El cliente Android se conecta a `127.0.0.1:5000` (vídeo) y `127.0.0.1:5001`
(control).
