# Registro de decisiones

## 2026-10-31 — Cierre del proyecto

**Decisión:** dar por cerrado el proyecto ARGB en este punto.

### Contexto

- **Objetivo original:** usar la GPU de la PC para renderizar apps Android de
  terceros y enviar solo el vídeo al teléfono.
- Se completó la POC de remote rendering (fases 1–7): Vulkan → NVENC → USB →
  MediaCodec → touch, a 30 FPS, 0 drops y con readback optimizado.
- La Fase 08 concluyó que la **redirección transparente de apps de terceros no
  es viable** sin root o cooperación de la app (sandbox/SELinux + candados de
  capas gráficas). Ver `docs/research/android-gpu-redirection.md`.

### Qué se entrega

- POC funcional:
  - `gpu-info` (descubrimiento de GPU)
  - `argb-render` (render Vulkan offscreen)
  - `argb-encode` (captura + NVENC)
  - `argb-server` (streaming por USB + protocolo binario)
  - `android-client` (MediaCodec + Surface + touch)
- Documentación:
  - `devplan.md` (plan y estado)
  - `docs/protocol.md`
  - `docs/benchmarks.md`
  - `docs/research/android-gpu-redirection.md`
- Repositorio: https://github.com/Froluxen/RemoteGPUBridge

### Qué no se hace

- No se implementa root, inyección en apps de terceros ni evasión de
  protecciones.
- No se continúa, por ahora, con la alternativa de emulador/VM Android en PC.

### Cómo retomar (si se decide más adelante)

1. Evaluar un emulador Android con aceleración por GPU en la PC y streaming al
   teléfono (la alternativa más alineada con el objetivo).
2. O integrar remote rendering en apps propias/cooperativas mediante un SDK.

### Motivo del cierre

El objetivo original no es alcanzable bajo las restricciones del proyecto
(sin root, build `user`, apps de terceros sin cooperación). La POC y la
investigación quedan como base reproducible por si se retoma la alternativa
de VM/emulador.
