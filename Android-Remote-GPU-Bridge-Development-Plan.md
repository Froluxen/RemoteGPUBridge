# Android Remote GPU Bridge (ARGB)

> **Estado:** Propuesta de investigación y prueba de concepto (POC)\
> **Plataforma objetivo:** PC Windows con NVIDIA RTX 4060 Laptop GPU +
> Xiaomi Redmi Note 13 4G\
> **Conexión preferida:** USB mediante ADB Reverse\
> **Restricciones:** sin root, sin emulador (salvo último recurso), sin
> servicios cloud y con uso moderado de almacenamiento.

------------------------------------------------------------------------

## 1. Visión del proyecto

**Android Remote GPU Bridge (ARGB)** es un proyecto de investigación
para explorar cómo ejecutar el renderizado gráfico en una PC y
transmitir el resultado a un dispositivo Android, utilizando el teléfono
como pantalla táctil y como dispositivo de entrada.

La idea inicial es:

1.  La PC genera una escena 3D utilizando su GPU NVIDIA RTX 4060.
2.  La PC captura los frames renderizados.
3.  Los frames se codifican en H.264 mediante NVENC.
4.  El vídeo se transmite al teléfono por USB.
5.  Android decodifica el vídeo mediante `MediaCodec` y lo presenta en
    pantalla.
6.  Los eventos táctiles del teléfono se envían de regreso a la PC.
7.  La PC interpreta esos eventos y actualiza la escena.

### 1.1 Objetivo de la primera POC

Construir una aplicación propia de prueba que demuestre un flujo de
**remote rendering interactivo**:

-   Renderizado 3D en la PC.
-   Codificación de vídeo por hardware.
-   Transmisión por USB.
-   Decodificación por hardware en Android.
-   Retorno de controles táctiles.
-   Métricas de rendimiento y latencia.

### 1.2 Qué no promete esta POC

Es importante separar dos objetivos diferentes:

  -----------------------------------------------------------------------
  Objetivo                Alcance                 Viabilidad inicial
  ----------------------- ----------------------- -----------------------
  Remote rendering de una La aplicación           Viable como POC.
  aplicación propia       cliente-servidor se     
                          diseña para renderizar  
                          en PC y mostrar el      
                          resultado en Android.   

  Redirección             Una aplicación Android  Investigación abierta;
  transparente de la GPU  comercial se ejecuta    no debe darse por
  de cualquier app        normalmente en el       posible.
  Android                 teléfono, pero sus      
                          llamadas gráficas se    
                          desvían a la GPU de la  
                          PC.                     
  -----------------------------------------------------------------------

La POC no demostrará que una aplicación comercial arbitraria pueda
utilizar remotamente la GPU del PC. Para eso se necesitaría soporte
explícito del sistema, del desarrollador de la aplicación o una
integración técnica específica y autorizada.

------------------------------------------------------------------------

## 2. Hardware y restricciones

### 2.1 PC

-   CPU: Intel Core i7 de 12.ª generación (modelo exacto pendiente de
    confirmar).
-   GPU: NVIDIA RTX 4060 Laptop GPU.
-   RAM: 16 GB.
-   Sistema operativo previsto: Windows.
-   Espacio libre disponible: aproximadamente 10--20 GB.

### 2.2 Dispositivo Android

-   Modelo: Xiaomi Redmi Note 13 4G.
-   RAM anunciada: 12 GB; se debe verificar cuánta es memoria física y
    cuánta corresponde a expansión virtual.
-   Sin root.
-   Conexión preferida: USB.

### 2.3 Restricciones de diseño

-   No depender de root.
-   Evitar instalar emuladores, salvo que una investigación posterior
    justifique esa ruta.
-   No depender de servicios cloud.
-   Mantener el uso de disco reducido.
-   Priorizar USB sobre Wi-Fi.
-   Evitar modificaciones, inyección o evasión de protecciones de
    aplicaciones de terceros.
-   Mantener separada la POC propia de cualquier investigación sobre
    compatibilidad con aplicaciones comerciales.

------------------------------------------------------------------------

## 3. Arquitectura general

La solución se divide en dos componentes principales: un servidor en
Windows y un cliente Android.

``` text
┌──────────────────────────────── PC Windows ────────────────────────────────┐
│                                                                            │
│  ┌──────────────────┐       ┌──────────────────┐                           │
│  │ Vulkan Renderer  │──────▶│ Frame Capture    │                           │
│  │ RTX 4060         │       │ Offscreen Target │                           │
│  └──────────────────┘       └────────┬─────────┘                           │
│                                      │                                     │
│                                      ▼                                     │
│                            ┌──────────────────┐                            │
│                            │ NVENC H.264      │                            │
│                            │ Video Encoder    │                            │
│                            └────────┬─────────┘                            │
│                                     │                                      │
│                                     ▼                                      │
│                            ┌──────────────────┐                            │
│                            │ Video Transport  │                            │
│                            │ TCP :5000        │                            │
│                            └────────┬─────────┘                            │
│                                     │                                      │
│                            ADB Reverse / USB                               │
└─────────────────────────────────────┼──────────────────────────────────────┘
                                      │
                                      ▼
┌────────────────────────────── Android ────────────────────────────────────┐
│                            ┌──────────────────┐                            │
│                            │ Video Receiver   │                            │
│                            └────────┬─────────┘                            │
│                                     ▼                                      │
│                            ┌──────────────────┐                            │
│                            │ MediaCodec       │                            │
│                            │ H.264 Decoder    │                            │
│                            └────────┬─────────┘                            │
│                                     ▼                                      │
│                            ┌──────────────────┐                            │
│                            │ Surface / UI     │                            │
│                            └──────────────────┘                            │
│                                                                            │
│  Touch Events ──▶ Input Client ──▶ TCP :5001 ──▶ PC Input Processor        │
└────────────────────────────────────────────────────────────────────────────┘
```

### 3.1 PC Server

Responsabilidades:

-   Detectar e inicializar la GPU NVIDIA.
-   Renderizar una escena 3D mediante Vulkan.
-   Renderizar inicialmente a un destino offscreen.
-   Capturar los frames.
-   Codificar los frames con NVENC.
-   Enviar paquetes de vídeo al cliente Android.
-   Recibir eventos táctiles.
-   Actualizar la escena a partir de los controles.
-   Registrar métricas de rendimiento.

Módulos principales:

-   `GpuContext`
-   `GpuSelector`
-   `VulkanRenderer`
-   `Scene`
-   `Camera`
-   `FrameCapture`
-   `FrameEncoder`
-   `NvencEncoder`
-   `TcpServer`
-   `InputProcessor`
-   `Telemetry`

### 3.2 Android Client

Responsabilidades:

-   Conectarse al servidor del PC.
-   Recibir el flujo de vídeo.
-   Decodificar H.264 mediante `MediaCodec`.
-   Presentar el vídeo en una `Surface`.
-   Capturar eventos táctiles.
-   Enviar los eventos de entrada al PC.
-   Mostrar métricas básicas de conexión y reproducción.
-   Gestionar desconexiones y reconexiones.

Módulos principales:

-   `NetworkClient`
-   `VideoReceiver`
-   `H264Parser`
-   `MediaCodecDecoder`
-   `TouchController`
-   `InputSender`
-   `StatsOverlay`

------------------------------------------------------------------------

## 4. Comunicación PC--Android

### 4.1 Transporte USB mediante ADB Reverse

La primera implementación utilizará TCP sobre el túnel USB de ADB.

Se proponen dos canales independientes:

  ------------------------------------------------------------------------
  Canal                                       Puerto Uso
  --------------------- ---------------------------- ---------------------
  Vídeo                                       `5000` Frames H.264 del PC
                                                     al Android.

  Control                                     `5001` Eventos táctiles,
                                                     configuración, ping y
                                                     telemetría.
  ------------------------------------------------------------------------

Comandos de ejemplo desde el PC:

``` bash
adb devices
adb reverse tcp:5000 tcp:5000
adb reverse tcp:5001 tcp:5001
```

El cliente Android se conectará a:

``` text
127.0.0.1:5000
127.0.0.1:5001
```

La disponibilidad del dispositivo y el funcionamiento de ADB Reverse
deben verificarse durante la fase de transporte.

### 4.2 Por qué dos conexiones

Separar vídeo y control evita que el envío de grandes cantidades de
datos de vídeo bloquee los mensajes de entrada.

TCP simplifica la primera implementación, pero tiene limitaciones:

-   TCP es un flujo de bytes; no conserva los límites de los mensajes.
-   Se necesita implementar framing.
-   La retransmisión puede introducir bloqueos de cabecera (head-of-line
    blocking).
-   Una conexión TCP no garantiza por sí sola baja latencia.

Por eso, TCP se utilizará primero para validar el sistema. Más adelante
se podrá evaluar UDP, WebRTC u otro transporte apropiado si las
mediciones justifican el cambio.

------------------------------------------------------------------------

## 5. Protocolo binario

Se utilizará un protocolo propio, versionado y con framing explícito.

### 5.1 Header común propuesto

  Campo        Tipo       Descripción
  ------------ ---------- --------------------------------
  `magic`      `uint32`   Identificador del protocolo.
  `version`    `uint16`   Versión del protocolo.
  `type`       `uint16`   Tipo de mensaje.
  `length`     `uint32`   Tamaño del payload.
  `sequence`   `uint32`   Número secuencial del mensaje.

La implementación debe definir explícitamente:

-   Byte order (endianness).
-   Tamaño máximo permitido para un payload.
-   Validación del `magic` y de la versión.
-   Comportamiento ante mensajes incompletos o inválidos.
-   Tratamiento de secuencias duplicadas o fuera de orden, cuando
    corresponda.
-   Gestión de desconexiones.
-   Límites de memoria para evitar asignaciones descontroladas.

### 5.2 Tipos de mensaje

  -----------------------------------------------------------------------
  Tipo                    Dirección habitual      Propósito
  ----------------------- ----------------------- -----------------------
  `HELLO`                 Ambos sentidos          Negociación inicial y
                                                  capacidades.

  `CONFIG`                PC → Android            Resolución, FPS y
                                                  configuración de vídeo.

  `VIDEO`                 PC → Android            Datos de vídeo
                                                  codificados.

  `INPUT`                 Android → PC            Eventos táctiles y
                                                  controles.

  `STATS`                 Ambos sentidos          Métricas de
                                                  rendimiento.

  `PING`                  Ambos sentidos          Medición de
                                                  conectividad y tiempos.

  `ERROR`                 Ambos sentidos          Reporte de errores del
                                                  protocolo o sesión.
  -----------------------------------------------------------------------

### 5.3 Paquetización de vídeo

El vídeo H.264 debe transmitirse con framing explícito. No se debe
asumir que una llamada de lectura TCP equivale a un frame o a una unidad
NAL.

El protocolo debe contemplar:

-   Configuración del decoder, incluyendo SPS/PPS cuando corresponda.
-   Delimitación de unidades de datos.
-   Tamaño de cada unidad transmitida.
-   Identificación o secuencia de frames.
-   Manejo de frames perdidos o inválidos.
-   Reinicialización del decoder cuando cambie la configuración del
    flujo.

La implementación concreta deberá acordar un formato de paquetización
entre el encoder del PC y el parser del cliente Android.

### 5.4 Eventos táctiles

Cada evento debe incluir, como mínimo:

-   Identificador del puntero (`touch_id`).
-   Tipo de evento.
-   Coordenada X normalizada entre `0.0` y `1.0`.
-   Coordenada Y normalizada entre `0.0` y `1.0`.
-   Timestamp o secuencia.
-   Información adicional necesaria para distinguir gestos o botones.

Eventos previstos:

-   `ACTION_DOWN`
-   `ACTION_MOVE`
-   `ACTION_UP`
-   `ACTION_CANCEL`

Debe contemplarse multitouch y evitar que un evento `MOVE` perdido deje
un puntero en un estado incorrecto.

------------------------------------------------------------------------

## 6. Renderizado y codificación

### 6.1 Vulkan

El renderer utilizará Vulkan para generar la escena en la GPU del PC.

Se recomienda empezar con un destino de renderizado **offscreen**, en
lugar de depender de una ventana visible o de una swapchain. Una ventana
local opcional puede servir para depuración.

La escena inicial debe evolucionar de forma incremental:

1.  Triángulo.
2.  Cubo rotatorio.
3.  Escena 3D sencilla.
4.  Cámara controlable.
5.  Interacción táctil.

Tecnologías previstas:

-   C++20.
-   CMake.
-   Vulkan.
-   GLFW (si se necesita una ventana de depuración).
-   GLM.
-   GLSL y compilación de shaders.

### 6.2 Captura de frames

La primera versión puede usar:

``` text
Vulkan Offscreen Image
        │
        ▼
GPU → CPU Readback (staging buffer)
        │
        ▼
Frame Buffer
        │
        ▼
H.264 Encoder
```

Este enfoque es más sencillo para validar el flujo completo, pero puede
introducir copias y latencia. No debe considerarse la arquitectura final
de máximo rendimiento.

### 6.3 NVENC

La codificación H.264 se realizará mediante NVIDIA NVENC.

Se recomienda encapsular la codificación detrás de una interfaz
independiente del renderer:

``` cpp
class FrameEncoder {
public:
    virtual ~FrameEncoder() = default;

    virtual bool initialize(const EncoderConfig& config) = 0;
    virtual bool encode(const FrameView& frame) = 0;
    virtual void shutdown() = 0;
};
```

`VulkanRenderer` no debería depender directamente de los detalles
internos de NVENC.

La primera implementación puede recibir frames desde memoria del
sistema. Después se puede investigar una ruta de menor cantidad de
copias.

### 6.4 Interoperabilidad Vulkan--NVENC

Esta es una de las partes de mayor riesgo técnico.

No se debe asumir que NVENC acepta directamente un `VkImage`, ni que
Vulkan y NVENC comparten memoria automáticamente.

Una optimización posterior podría explorar:

-   Memoria externa de Vulkan.
-   Interoperabilidad con Direct3D 11.
-   Interoperabilidad con CUDA.
-   Recursos compartidos de Windows.
-   Sincronización entre APIs y dispositivos.

La ruta concreta dependerá de las capacidades y restricciones de las
APIs y del hardware. Primero debe existir una ruta funcional con
readback; luego se mide si la optimización es necesaria.

------------------------------------------------------------------------

## 7. Cliente Android

### 7.1 Recepción y decodificación

El cliente se implementará en Kotlin.

Pipeline esperado:

``` text
TCP Socket
    │
    ▼
Video Framing / Parser
    │
    ▼
H.264 Access Units
    │
    ▼
MediaCodec Decoder
    │
    ▼
Surface
    │
    ▼
Android Display
```

Se debe comprobar que el dispositivo disponga de un decoder H.264 por
hardware compatible con la resolución y configuración elegidas.

### 7.2 Presentación

El vídeo decodificado se presentará en una `Surface`. Se evitará
convertir cada frame a un bitmap para mostrarlo, porque esa ruta puede
añadir copias y consumo de memoria innecesarios.

### 7.3 Entrada táctil

El cliente capturará los eventos de pantalla y los convertirá a
coordenadas normalizadas. El servidor interpretará los eventos para
controlar la cámara y la escena de prueba.

La POC manejará los controles de su propia aplicación. No enviará
eventos para controlar o modificar otra aplicación Android.

### 7.4 Overlay de estadísticas

El cliente puede mostrar:

-   FPS recibidos o presentados.
-   Resolución.
-   Estado de conexión.
-   Frames descartados.
-   Estado del decoder.
-   Latencia estimada, si se dispone de timestamps sincronizados o de
    una técnica de medición válida.

------------------------------------------------------------------------

## 8. Roadmap de desarrollo

Estimación orientativa para una persona trabajando de forma individual:
**3--5 semanas**. Es una estimación de planificación, no una garantía.
La investigación de interoperabilidad GPU y la redirección de
aplicaciones arbitrarias quedan fuera de ese plazo.

### Fase 01 --- GPU Discovery & Baseline

**Estimación:** 1--2 días.

**Objetivo:** detectar e inicializar correctamente la GPU NVIDIA
mediante Vulkan.

Tareas:

1.  Crear la estructura inicial del repositorio.
2.  Configurar CMake y el estándar C++20.
3.  Crear una instancia Vulkan.
4.  Enumerar los physical devices.
5.  Registrar nombre, vendor ID y device ID.
6.  Consultar las propiedades y características de cada dispositivo.
7.  Consultar las queue families.
8.  Consultar extensiones disponibles.
9.  Seleccionar explícitamente la NVIDIA RTX 4060.
10. Crear el logical device.
11. Implementar logs de inicialización y errores.
12. Ejecutar una prueba de inicialización y cierre limpio.

Entregable:

``` text
gpu-info.exe
```

El programa debe mostrar los dispositivos disponibles y confirmar cuál
fue seleccionado.

**Criterios de aceptación:**

-   [ ] CMake configura y compila el proyecto.
-   [ ] Vulkan inicializa correctamente.
-   [ ] Se enumeran los physical devices.
-   [ ] La RTX 4060 se identifica correctamente.
-   [ ] Se selecciona la GPU esperada, sin depender del orden de
    enumeración.
-   [ ] Se crea el logical device.
-   [ ] Se registran errores y capacidades relevantes.
-   [ ] El programa termina limpiamente.

**Regla de avance:** no empezar el renderer completo, el streaming ni
Android hasta completar esta fase.

### Fase 02 --- Vulkan Rendering Engine

**Estimación:** 3--5 días.

**Objetivo:** renderizar una escena 3D estable en la GPU del PC.

Tareas:

1.  Implementar el contexto Vulkan.
2.  Crear las estructuras de recursos y gestión de memoria.
3.  Crear el render target offscreen.
4.  Configurar formatos de color y profundidad.
5.  Crear shaders y pipelines.
6.  Crear vertex buffers e index buffers.
7.  Implementar una cámara.
8.  Renderizar un triángulo.
9.  Renderizar un cubo rotatorio.
10. Construir una escena 3D sencilla.
11. Añadir controles locales para depurar la cámara.
12. Medir FPS y tiempo de renderizado.
13. Mantener la aplicación estable durante una prueba prolongada.

Tecnologías previstas:

-   Vulkan.
-   C++20.
-   GLM.
-   GLSL.
-   GLFW, solo si se necesita ventana de depuración.

**Criterios de aceptación:**

-   [ ] Se renderiza un triángulo.
-   [ ] Se renderiza un cubo rotatorio.
-   [ ] La escena utiliza la GPU seleccionada.
-   [ ] Existe un render target offscreen.
-   [ ] La cámara puede actualizarse.
-   [ ] La aplicación se mantiene estable durante 10 minutos.
-   [ ] Se registran FPS y tiempo de renderizado.

**Objetivo inicial de rendimiento:** 1280×720 y 60 FPS locales, sujeto a
medición.

### Fase 03 --- Frame Capture & Hardware Encoding

**Estimación:** 3--5 días.

**Objetivo:** capturar los frames de Vulkan y codificarlos mediante
NVENC.

Tareas:

1.  Diseñar la interfaz `FrameCapture`.
2.  Leer el render target mediante staging buffer.
3.  Validar el formato de píxel y el layout de memoria.
4.  Crear la interfaz abstracta `FrameEncoder`.
5.  Integrar NVENC.
6.  Configurar H.264.
7.  Configurar resolución, bitrate y FPS.
8.  Obtener unidades codificadas.
9.  Implementar `VideoPacketizer`.
10. Validar la salida con un reproductor o herramienta de análisis
    apropiada.
11. Medir tiempo de readback y de codificación.
12. Registrar errores y pérdidas de frames.

Entregables:

-   `FrameCapture`
-   `NvencEncoder`
-   `VideoPacketizer`

**Criterios de aceptación:**

-   [ ] Se capturan frames válidos del render target.
-   [ ] NVENC está realmente activo.
-   [ ] Se genera un flujo H.264 reproducible.
-   [ ] Se transmiten correctamente los datos de configuración del
    decoder.
-   [ ] Se miden readback y encode time.
-   [ ] Se mantiene una salida estable a 720p30.

**Optimización posterior:** investigar interoperabilidad
Vulkan--D3D11/CUDA y memoria compartida. No bloquear la POC si esa
optimización requiere más investigación.

### Fase 04 --- USB Transport Layer

**Estimación:** 2--4 días.

**Objetivo:** transmitir los datos del PC al Android mediante USB y ADB
Reverse.

Tareas:

1.  Implementar un servidor TCP en Windows.
2.  Crear los canales de vídeo y control.
3.  Implementar el header binario.
4.  Implementar serialización y deserialización.
5.  Validar tamaños y versiones.
6.  Implementar `HELLO` y `CONFIG`.
7.  Transmitir datos H.264 de prueba.
8.  Implementar mensajes `PING`.
9.  Manejar desconexiones.
10. Implementar reconexión.
11. Probar sesiones prolongadas.
12. Registrar throughput y tiempos de transporte.

**Criterios de aceptación:**

-   [ ] El dispositivo aparece mediante `adb devices`.
-   [ ] ADB Reverse funciona para ambos puertos.
-   [ ] Android puede conectarse a los sockets del PC.
-   [ ] El framing funciona con lecturas parciales y múltiples mensajes.
-   [ ] Los mensajes inválidos se rechazan de forma segura.
-   [ ] La reconexión funciona.
-   [ ] Una prueba de 30 minutos no produce bloqueos ni crecimiento
    descontrolado de memoria.

### Fase 05 --- Android Video Client

**Estimación:** 3--5 días.

**Objetivo:** recibir y presentar el flujo H.264 en el teléfono.

Tareas:

1.  Crear el cliente TCP.
2.  Implementar el parser del protocolo.
3.  Implementar `VideoReceiver`.
4.  Implementar la gestión de SPS/PPS y unidades H.264.
5.  Inicializar `MediaCodec`.
6.  Configurar el decoder con una `Surface`.
7.  Enviar las unidades codificadas al decoder.
8.  Gestionar cambios de resolución o configuración.
9.  Manejar errores del decoder.
10. Implementar reconexión.
11. Añadir overlay de estadísticas.
12. Medir FPS recibidos y presentados.

**Criterios de aceptación:**

-   [ ] Android se conecta al servidor.
-   [ ] Recibe datos H.264 correctamente.
-   [ ] `MediaCodec` decodifica el flujo.
-   [ ] El vídeo se presenta en la pantalla.
-   [ ] Se confirma el uso del decoder por hardware cuando esté
    disponible.
-   [ ] La aplicación soporta desconexión y reconexión.
-   [ ] Se mantiene una reproducción estable a 720p30.

### Fase 06 --- Touch Input Bridge

**Estimación:** 2--4 días.

**Objetivo:** controlar la escena del PC desde la pantalla táctil del
teléfono.

Tareas:

1.  Capturar `ACTION_DOWN`.
2.  Capturar `ACTION_MOVE`.
3.  Capturar `ACTION_UP`.
4.  Capturar `ACTION_CANCEL`.
5.  Añadir soporte multitouch.
6.  Normalizar coordenadas.
7.  Serializar eventos `INPUT`.
8.  Recibir eventos en el PC.
9.  Implementar `InputProcessor`.
10. Controlar la cámara con gestos.
11. Añadir zoom.
12. Añadir botones virtuales.
13. Medir latencia de entrada.

**Criterios de aceptación:**

-   [ ] El movimiento táctil modifica la cámara de la escena.
-   [ ] Se reconocen los eventos de inicio y fin del toque.
-   [ ] Multitouch no deja punteros bloqueados.
-   [ ] Las coordenadas se interpretan correctamente.
-   [ ] La interacción funciona durante una sesión prolongada.

### Fase 07 --- Performance Optimization

**Estimación:** 3--5 días.

**Objetivo:** reducir latencia y mejorar estabilidad sin optimizar a
ciegas.

Tareas:

1.  Medir render time.
2.  Medir readback time.
3.  Medir encode time.
4.  Medir transport time.
5.  Medir decode time.
6.  Medir tiempo hasta presentación.
7.  Medir latencia de entrada.
8.  Medir FPS y frames descartados.
9.  Reutilizar buffers.
10. Evitar asignaciones frecuentes.
11. Mantener colas acotadas.
12. Descartar frames antiguos cuando sea preferible a acumular latencia.
13. Revisar sincronización.
14. Investigar una ruta de menor cantidad de copias.
15. Comparar resultados antes y después de cada optimización.

**Criterios de aceptación:**

-   [ ] Existe un informe de métricas por etapa.
-   [ ] Se identifican los principales cuellos de botella.
-   [ ] Las colas tienen límites.
-   [ ] No se acumula latencia indefinidamente.
-   [ ] Los cambios de rendimiento se validan mediante mediciones.

### Fase 08 --- GPU Redirection Research

**Estimación:** abierta; no forma parte del plazo base de la POC.

**Objetivo:** estudiar si existe una ruta técnicamente viable y
autorizada para redirigir el renderizado de aplicaciones Android de
terceros.

Líneas de investigación:

-   Vulkan Layers.
-   OpenGL ES y mecanismos de interceptación.
-   ANGLE.
-   Android Graphics HAL.
-   Modelo de procesos y aislamiento de Android.
-   Restricciones de privilegios y permisos.
-   Compatibilidad con aplicaciones de producción.
-   Requisitos de soporte del desarrollador.
-   Alternativas oficiales de ejecución remota.

Preguntas que debe responder el informe:

1.  ¿Qué parte del pipeline gráfico puede observarse o extenderse sin
    root?
2.  ¿Qué mecanismos requieren que la aplicación coopere?
3.  ¿Qué mecanismos requieren privilegios del sistema?
4.  ¿Qué limitaciones existen para aplicaciones comerciales?
5.  ¿Qué soluciones están documentadas y autorizadas?
6.  ¿Qué pruebas pueden realizarse con una aplicación propia?
7.  ¿Qué aspectos no son viables bajo las restricciones del proyecto?

**Límites:** no evadir protecciones, no inyectar código en aplicaciones
comerciales y no asumir que una capa Vulkan u OpenGL puede instalarse de
forma universal en procesos de producción.

**Entregable:** `docs/research/android-gpu-redirection.md`, con
evidencia, limitaciones, experimentos reproducibles y una conclusión de
viabilidad.

------------------------------------------------------------------------

## 9. Métricas y objetivos iniciales

Los valores siguientes son metas preliminares para orientar las pruebas,
no garantías.

  Métrica                                  Objetivo inicial
  ------------------------- -------------------------------
  Resolución de streaming                          1280×720
  FPS de streaming                                   30 FPS
  FPS de render local                                60 FPS
  Codec                                               H.264
  Transporte                              USB + ADB Reverse
  Latencia end-to-end         Menos de 100 ms, aspiracional
  Transporte                   Menos de 10 ms, aspiracional
  Decodificación                                 MediaCodec
  Codificación                                 NVIDIA NVENC

### 9.1 Instrumentación obligatoria

Registrar al menos:

-   `render_time_ms`
-   `readback_time_ms`
-   `encode_time_ms`
-   `transport_time_ms`
-   `decode_time_ms`
-   `presentation_time_ms`, si puede medirse correctamente
-   `input_latency_ms`
-   `fps`
-   `dropped_frames`
-   `queue_depth`
-   `bitrate`
-   `connection_state`

La latencia end-to-end debe medirse con un método definido. No se deben
sumar timestamps de relojes diferentes sin sincronización ni presentar
estimaciones como mediciones exactas.

------------------------------------------------------------------------

## 10. Estructura del repositorio

``` text
Android-Remote-GPU-Bridge/
├── README.md
├── LICENSE
├── CMakeLists.txt
├── .gitignore
│
├── docs/
│   ├── architecture.md
│   ├── protocol.md
│   ├── development-plan.md
│   ├── benchmarks.md
│   └── research/
│       └── android-gpu-redirection.md
│
├── shared/
│   ├── protocol/
│   │   ├── message.hpp
│   │   ├── header.hpp
│   │   └── serializer.hpp
│   └── config/
│
├── pc-server/
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── main.cpp
│   │   ├── gpu/
│   │   │   ├── gpu_context.cpp
│   │   │   └── gpu_selector.cpp
│   │   ├── renderer/
│   │   │   ├── vulkan_renderer.cpp
│   │   │   ├── swapchain.cpp
│   │   │   ├── camera.cpp
│   │   │   └── scene.cpp
│   │   ├── capture/
│   │   │   └── frame_capture.cpp
│   │   ├── encoder/
│   │   │   └── nvenc_encoder.cpp
│   │   ├── transport/
│   │   │   └── tcp_server.cpp
│   │   └── input/
│   │       └── input_processor.cpp
│   └── shaders/
│
├── android-client/
│   ├── app/
│   └── core/
│       ├── network/
│       ├── decoder/
│       ├── input/
│       ├── renderer/
│       └── telemetry/
│
└── tests/
    ├── gpu/
    ├── protocol/
    ├── encoder/
    ├── transport/
    └── integration/
```

La estructura es una guía inicial. Se puede ajustar al implementar,
manteniendo separados los componentes de GPU, captura, codificación,
transporte, entrada y cliente Android.

------------------------------------------------------------------------

## 11. Estrategia de pruebas

Las pruebas deben avanzar por capas. No integrar todo de una sola vez.

  Nivel   Prueba               Resultado esperado
  ------- -------------------- -----------------------------------------------
  1       Enumeración Vulkan   Se identifica la RTX 4060.
  2       Renderizado          La escena se dibuja correctamente.
  3       Captura              Se obtiene un frame válido del render target.
  4       Codificación         NVENC produce H.264 válido.
  5       Protocolo            Los mensajes se reconstruyen correctamente.
  6       Transporte           Los datos llegan al Android por USB.
  7       Decodificación       MediaCodec decodifica el flujo.
  8       Presentación         El vídeo aparece en pantalla.
  9       Entrada              Los toques controlan la escena.
  10      Integración          La sesión se mantiene estable y medible.

Cada capa debe tener pruebas independientes y logs suficientes para
localizar fallos.

------------------------------------------------------------------------

## 12. Riesgos técnicos

  -----------------------------------------------------------------------
  Riesgo                  Impacto                 Estrategia
  ----------------------- ----------------------- -----------------------
  Selección incorrecta de Alto                    Identificar por
  GPU                                             propiedades y
                                                  seleccionar
                                                  explícitamente el
                                                  dispositivo.

  Complejidad de Vulkan   Medio/alto              Construir el renderer
                                                  incrementalmente.

  Readback GPU→CPU        Alto                    Usarlo primero para
  costoso                                         validar; optimizar
                                                  después con mediciones.

  Integración NVENC       Alto                    Encapsular el encoder y
                                                  validar un flujo mínimo
                                                  independiente.

  Interoperabilidad       Alto                    Tratarla como
  Vulkan--NVENC                                   investigación separada;
                                                  no bloquear la POC
                                                  inicial.

  Framing TCP incorrecto  Alto                    Implementar parser
                                                  incremental y pruebas
                                                  con lecturas parciales.

  Latencia acumulada      Alto                    Colas acotadas y
                                                  descarte controlado de
                                                  frames antiguos.

  Incompatibilidad        Medio                   Consultar capacidades
  MediaCodec                                      del dispositivo y
                                                  probar configuraciones
                                                  progresivamente.

  Reconexión defectuosa   Medio                   Diseñar estados de
                                                  conexión y pruebas de
                                                  desconexión.

  Redirección de apps     Muy alto                Mantenerla como
  comerciales no viable                           investigación separada
                                                  y no como requisito de
                                                  la POC.
  -----------------------------------------------------------------------

------------------------------------------------------------------------

## 13. Decisiones de ingeniería

-   Usar C++20 y CMake para el servidor.
-   Usar Kotlin para el cliente Android.
-   Usar Vulkan para el renderizado del PC.
-   Usar NVENC para codificación H.264.
-   Usar MediaCodec para decodificación Android.
-   Usar TCP sobre ADB Reverse para la primera versión.
-   Separar vídeo y control en dos conexiones.
-   Diseñar un protocolo binario versionado.
-   Empezar con readback a CPU para validar el pipeline.
-   Optimizar la transferencia GPU después de obtener mediciones.
-   Mantener la aplicación de prueba propia separada de la investigación
    de redirección de terceros.
-   Usar Git desde el primer día y commits pequeños.
-   Evitar Docker o máquinas virtuales si no aportan una necesidad
    concreta al desarrollo.
-   No añadir dependencias o componentes antes de que una fase los
    necesite.

------------------------------------------------------------------------

## 14. Primer objetivo inmediato: completar la Fase 01

El primer entregable debe ser `gpu-info.exe`.

No se debe comenzar por el streaming, Android o NVENC. Primero hay que
demostrar que el programa puede inicializar Vulkan, enumerar
dispositivos y seleccionar la RTX 4060 de forma fiable.

### Definition of Done --- Fase 01

-   [ ] Repositorio creado.
-   [ ] Estructura inicial organizada.
-   [ ] CMake configura correctamente.
-   [ ] Proyecto compila con C++20.
-   [ ] Vulkan se inicializa.
-   [ ] Se enumeran todos los physical devices.
-   [ ] Se registran nombre, vendor ID y device ID.
-   [ ] Se consulta la información relevante de queues y extensiones.
-   [ ] La RTX 4060 se identifica correctamente.
-   [ ] Se selecciona la RTX 4060 explícitamente.
-   [ ] Se crea el logical device.
-   [ ] Se registran errores y resultados.
-   [ ] La aplicación finaliza limpiamente.

**Criterio para avanzar:** todos los puntos esenciales de esta fase
deben estar comprobados antes de implementar el renderer.

------------------------------------------------------------------------

## 15. Resultado esperado del proyecto

Al completar las fases 1--7, el proyecto debería contar con una POC
funcional en la que:

1.  El PC renderiza una escena 3D utilizando la RTX 4060.
2.  El PC captura y codifica el resultado mediante NVENC.
3.  El vídeo viaja al teléfono por USB.
4.  Android lo decodifica con MediaCodec y lo presenta.
5.  Los controles táctiles regresan al PC.
6.  La escena responde a esos controles.
7.  El sistema registra métricas para evaluar latencia, estabilidad y
    rendimiento.

La fase 8 producirá una evaluación independiente sobre la viabilidad de
la redirección gráfica de aplicaciones Android de terceros. Esa
investigación no debe confundirse con el éxito de la POC de remote
rendering.
