# Investigación: redirección de GPU para apps Android de terceros

> **Estado:** Completada (investigación)
> **Dispositivo de prueba:** Xiaomi Redmi Note 13 4G (23129RA5FL), Android 15 (SDK 35)
> **Restricciones del proyecto:** sin root, build de usuario, sin emulador salvo último recurso
> **Fecha:** 31/10/2026

---

## 1. Resumen ejecutivo

La pregunta era: **¿se puede hacer que una app Android de terceros, sin root,
renderice en la GPU de la PC y reciba solo el vídeo en el teléfono?**

**Conclusión: no, no de forma transparente y sin root.** El modelo de seguridad
de Android impide interceptar el proceso de otra app y su pipeline gráfico.
Las únicas vías para inyectar capas gráficas (Vulkan/GLES) exigen una de estas
tres condiciones:

1. que la app sea **debuggable**,
2. que el sistema sea **userdebug/eng con root**, o
3. que la propia app declare el meta-data `com.android.graphics.injectLayers.enable`.

Ninguna se cumple para una app comercial arbitraria en un teléfono de usuario
normal. En el Redmi Note 13 lo verificamos: `ro.build.type=user`,
`ro.debuggable=0`, SELinux `Enforcing`, sin `su`, y los settings de depuración
de GPU (`enable_gpu_debug_layers`, `gpu_debug_app`, `gpu_debug_layers`) son
**rechazados por el sistema** con `SecurityException: WRITE_SECURE_SETTINGS`.

La POC de las fases 1–7 sigue siendo válida y útil: demuestra el pipeline de
remote rendering para **apps que cooperan** (la nuestra) y sirve de base para
las alternativas viables (VM/emulador Android en la PC o streaming oficial).

---

## 2. Pregunta de investigación

- ¿Qué parte del pipeline gráfico se puede observar o extender sin root?
- ¿Qué mecanismos requieren cooperación de la app?
- ¿Qué mecanismos requieren privilegios de sistema?
- ¿Qué limitaciones hay para apps comerciales?
- ¿Qué soluciones están documentadas y autorizadas?
- ¿Qué pruebas se pueden hacer con una app propia?
- ¿Qué no es viable bajo las restricciones del proyecto?

---

## 3. Cómo renderiza una app Android

```
App (Canvas / OpenGL ES / Vulkan)
        │
        ▼
EGL / Vulkan loader / driver de GPU (p. ej. vulkan.adreno.so)
        │
        ▼
Surface (productor de BufferQueue)
        │
        ▼
SurfaceFlinger (compositor del sistema)
        │
        ▼
Hardware Composer (HWC) + Gralloc
        │
        ▼
Pantalla
```

Puntos clave (documentación de AOSP, *Graphics*):

- Todo se renderiza **en la GPU del propio dispositivo** sobre un `Surface`.
- `SurfaceFlinger` es el **único** servicio que puede modificar el contenido de
  la pantalla; compone las surfaces usando OpenGL ES y el HWC.
- `BufferQueue` conecta productor (app) y consumidor (SurfaceFlinger) mediante
  Binder IPC.
- `Gralloc` asigna la memoria gráfica; el HWC es el punto central de la
  composición.

Para "redirigir la GPU" habría que interceptar **entre la app y el driver**
(capa/inyección), **reemplazar el driver/compositor** (sistema/vendor), o
**ejecutar la app en otro sitio** (VM/cloud). No hay una API pública para que
una app externa se apropie del render de otra.

---

## 4. Modelo de seguridad que lo impide

| Capa | Qué impide |
| --- | --- |
| Sandbox por UID | Cada app corre en su propio proceso/UID; no se puede leer/escribir su memoria. |
| SELinux (`Enforcing`) | Bloquea acceso a `/data/local/debug`, propiedades y sockets de otros dominios. |
| `ro.debuggable=0` / build `user` | Sin `run-as` en apps no debuggable; sin depuración nativa. |
| `WRITE_SECURE_SETTINGS` | `adb shell` no puede cambiar ajustes globales de GPU en un build `user`. |
| Firma/instalación | No se puede reemplazar una app de terceros sin su firma. |

Verificación en el dispositivo:

```text
ro.build.type      = user
ro.debuggable      = 0
ro.secure          = 1
getenforce         = Enforcing
id (adb shell)     = uid=2000(shell) ... context=u:r:shell:s0
which su           = (no existe)
```

---

## 5. Mecanismos evaluados

### 5.1 Capas de Vulkan (validation / implicit layers)

Android permite cargar capas externas (Android 9+) desde el APK, el directorio
de la app o un APK de capas. Pero la documentación oficial es explícita: para
cargar capas externas debe cumplirse **una** de estas condiciones:

- la app objetivo es **debuggable**;
- el sistema es **userdebug/eng con root**;
- la app declara:

```xml
<meta-data android:name="com.android.graphics.injectLayers.enable"
           android:value="true" />
```

Además, la activación per-app usa settings globales
(`enable_gpu_debug_layers`, `gpu_debug_app`, `gpu_debug_layers`).

Prueba en el Redmi Note 13:

```text
adb shell setprop debug.vulkan.layers VK_LAYER_KHRONOS_validation
  -> la propiedad se escribe, pero el loader sigue aplicando el candado
     (debuggable / root / injectLayers)

adb shell settings put global enable_gpu_debug_layers 1
  -> SecurityException: Permission denial, must have one of:
     [android.permission.WRITE_SECURE_SETTINGS]
```

Conclusión: **no se puede inyectar una capa en una app de terceros** en un
build `user` sin root y sin cooperación de la app. Una capa, además, **no
redirige** el render por sí sola: tendría que reimplementar la GPU o enviar las
llamadas a la PC, lo cual es inviable para una app comercial arbitraria.

### 5.2 Capas de OpenGL ES

Mismo modelo que Vulkan (Android 10+), con `debug.gles.layers` y los settings
`gpu_debug_layers_gles`. Requisitos idénticos: app debuggable, userdebug+root o
`injectLayers.enable`. Las capas GLES interceptan `eglGetProcAddress`, pero no
permiten redirigir el render de una app no cooperativa.

### 5.3 `LD_PRELOAD` / hooks de bibliotecas

En Android, `LD_PRELOAD` y la sustitución de `libEGL`/`libvulkan` requieren
control del entorno del proceso (root, app debuggable o un wrapper propio).
No es aplicable a apps de terceros instaladas normalmente.

### 5.4 Graphics HAL (Gralloc / HWC) y SurfaceFlinger

Reemplazar Gralloc, el HWC o interceptar SurfaceFlinger permitiría, en teoría,
capturar o redirigir la composición. Pero son componentes **de sistema/vendor**:
requieren root, una imagen de sistema modificada o soporte del OEM. Quedan
fuera de las restricciones del proyecto.

### 5.5 MediaProjection (captura de pantalla)

`MediaProjection` (Android 5+) permite capturar la pantalla o una ventana de
app en un `Surface`/`VirtualDisplay`, con **consentimiento del usuario**. Es la
base de apps como scrcpy o grabadores.

Limitación clave: **la app sigue renderizando en la GPU del teléfono**. Se
captura el resultado ya compuesto; no se offloada el render a la PC. Es una
vía autorizada para "ver/controlar el teléfono desde la PC", no para "usar la
GPU de la PC".

### 5.6 Companion app streaming (Android 13+)

Android 13 introduce el rol `COMPANION_DEVICE_APP_STREAMING`: una app
**preinstalada como priv-app** puede crear un `VirtualDisplay`, lanzar una app
en él y transmitir el vídeo a otro dispositivo, además de inyectar input
remoto. Es la arquitectura oficial de "app streaming".

Limitaciones:

- Solo **priv-apps preinstaladas** pueden tener el rol (OEM/Google).
- La app se ejecuta en el **dispositivo local** (el teléfono); se transmite su
  pantalla. No usa la GPU del PC.

### 5.7 AVF / Microdroid

El Android Virtualization Framework (pKVM + Microdroid) ofrece VMs aisladas
para cargas de trabajo de seguridad. No está pensado para ejecutar apps
arbitrarias con la GPU del PC. En el Redmi Note 13, el servicio
`virtualization` **no existe** (`cmd: Can't find service: virtualization`).

### 5.8 Depuración/instrumentación (JDWP, Frida, UIAutomator)

- JDWP requiere app debuggable.
- Frida requiere root o app debuggable/repackageada.
- UIAutomator/Accesibility puede observar/interactuar con la UI, pero no
  interceptar el render 3D.

No sirven para redirigir gráficos de una app comercial.

### 5.9 Emulador/VM de Android en la PC

Ejecutar Android en la PC (Cuttlefish, Android Studio Emulator, Waydroid, etc.)
permite que las apps usen la GPU de la PC mediante virtualización gráfica
(gfxstream/virgl/ANGLE). Es la vía **más cercana** al objetivo: la app corre en
un Android alojado en la PC y se muestra en el teléfono/PC.

Limitaciones: requiere emulador/VM (el plan lo permitía solo como último
recurso), compatibilidad variable, DRM/Play Integrity, rendimiento y
mantenimiento. No es "redirección transparente en el teléfono".

### 5.10 Cloud app streaming

La app corre en un servidor (Android virtualizado) y se transmite al cliente.
Es la arquitectura oficial de servicios de app streaming/cloud gaming. Usa la
GPU del servidor, no la del PC del usuario, y depende de un servicio externo.

---

## 6. Matriz de viabilidad

| Mecanismo | ¿Root? | ¿Coopera la app? | ¿Usa GPU del PC? | Veredicto |
| --- | --- | --- | --- | --- |
| Capas Vulkan | No si debuggable/opt-in; sí en `user` sin cooperación | Sí | No (intercepta, no redirige) | **No viable** para apps arbitrarias |
| Capas GLES | Igual | Sí | No | **No viable** |
| `LD_PRELOAD` / hooks | Sí (o debuggable) | Sí | No | **No viable** |
| HAL gráfico / SurfaceFlinger | Sí (sistema/vendor) | No | No | **No viable** sin root/ROM |
| MediaProjection | No | No | **No** (render local) | Viable, pero **no cumple** el objetivo |
| Companion app streaming | No (priv-app OEM) | No | **No** (render local) | Viable solo para OEM |
| AVF / Microdroid | No | No | No | No aplica |
| Emulador/VM Android en PC | No | No | **Sí** (gfxstream/ANGLE) | **Alternativa viable** |
| Cloud app streaming | No | No | No (GPU del cloud) | Alternativa viable (no local) |

---

## 7. Por qué la POC sí funcionó

La POC (fases 1–7) **no** redirige una app de terceros: es una app propia que
coopera. Nosotros controlamos ambos extremos:

- El servidor renderiza la escena con Vulkan y la codifica con NVENC.
- El cliente Android decodifica y presenta el vídeo, y envía el input.

Eso demuestra el pipeline de **remote rendering** y sirve como base para:

1. apps propias o apps que integren un SDK de streaming,
2. un emulador/VM Android en la PC que use su GPU,
3. servicios de app streaming.

No demuestra que una app comercial arbitraria pueda usar la GPU del PC.

---

## 8. Recomendación

Bajo las restricciones actuales (sin root, build `user`), **la redirección
transparente de GPU para apps de terceros no es viable**. Las rutas realistas
son:

1. **Emulador/VM Android en la PC con GPU acelerada** (Cuttlefish/AVD):
   la app corre en la PC y usa su GPU. Es la opción más alineada con el
   objetivo, a costa de renunciar al "sin emulador" y asumir limitaciones de
   compatibilidad/DRM.
2. **Screen mirroring/control** (MediaProjection/scrcpy): útil para usar el
   teléfono desde la PC, pero el render sigue en el teléfono.
3. **App cooperation**: si la app integra remote rendering (nuestro caso) o
   declara `injectLayers.enable` (solo para depuración).
4. **Cloud app streaming**: oficial, pero no usa la GPU del PC.

No se recomienda (y el proyecto no lo hará) root, inyección en apps
comerciales, evasión de protecciones ni modificación de binarios de terceros.

---

## 9. Reproducibilidad

Comandos usados en el dispositivo:

```bash
adb shell getprop ro.build.type          # user
adb shell getprop ro.debuggable          # 0
adb shell getprop ro.secure              # 1
adb shell getenforce                     # Enforcing
adb shell "id; which su"                 # uid=2000(shell); sin su
adb shell setprop debug.vulkan.layers VK_LAYER_KHRONOS_validation
adb shell settings put global enable_gpu_debug_layers 1   # SecurityException
adb shell run-as com.argb.client id       # funciona (app debuggable)
adb shell run-as com.android.settings id  # falla (no debuggable)
adb shell dumpsys SurfaceFlinger --list   # compositor visible (solo lectura)
adb shell cmd virtualization              # servicio inexistente
```

---

## 10. Referencias

- Android NDK — *Vulkan validation layers on Android*:
  https://developer.android.com/ndk/guides/graphics/validation-layer
- Android NDK — *GLES layers*:
  https://developer.android.com/ndk/guides/rootless-debug-gles
- AOSP — *Graphics* (surfaces, BufferQueue, SurfaceFlinger, HWC):
  https://source.android.com/docs/core/graphics
- AOSP — *Companion app streaming*:
  https://source.android.com/docs/core/permissions/app-streaming
- Android — *Media projection*:
  https://developer.android.com/media/grow/media-projection
- AOSP — *Android Virtualization Framework (AVF)*:
  https://source.android.com/docs/core/virtualization
- AOSP — *BufferQueue and Gralloc*:
  https://source.android.com/docs/core/graphics/arch-bq-gralloc
- AOSP — *Hardware Composer HAL*:
  https://source.android.com/docs/core/graphics/hwc

---

## 11. Decisión del proyecto (31/10/2026)

El proyecto se cierra en este punto. No se implementa ninguna vía de
inyección/root ni la alternativa de emulador/VM por ahora. Esta investigación
queda como el resultado de la Fase 08. Detalle: [`docs/decision-log.md`](../decision-log.md).
