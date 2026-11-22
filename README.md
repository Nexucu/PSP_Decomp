# PSP Flower — Port 2: Android con Vulkan

Port a Android (Vulkan 1.0+, `NativeActivity`, sin Java/Kotlin) del demo
**"Moppi PSP Flower"** de Moppi Productions (2005), un demo de la demoscene
para PSP, libremente redistribuible según PPSSPP.

Fuente: `~/workspace/decompiler/test/flower/flower.elf`, decompilado en
`flower_decompilado_v03.c` (62.368 instrucciones, 706 funciones, 44 imports
PSP resueltos por NID). Este port **no re-analiza el binario**: reproduce el
análisis fiel que ya existía de `main` (`func_0x89006b4`).

## Qué reproduce del original

- **Pantalla PSP 480×272** (draw buffer de 512 de ancho en VRAM), viewport
  `(2048, 2048, 480, 272)`, scissor `(0, 0, 480, 272)`, offset `(1808, 1912)`,
  matrices orto. En Vulkan la escena se dibuja en coordenadas PSP 480×272
  y una matriz orto en push constants la lleva a NDC (la ventana real puede
  ser mayor; la escena se escala a pantalla completa).
- **Carga de texturas en VRAM desde `0x040cc000`, en orden**: `flower`,
  `tron`, `overlay`, `bg`, `logo` (mismo orden de `data/*.tga` aquí, mismo
  loader TGA: type 2 sin comprimir, 24/32 bpp, origen inferior/superior).
- **Fase**: `fase += 0.01` por frame, envuelve en `1.0` → ciclo de 100 frames.
  La flor gira una vuelta completa por ciclo.
- **Filtros**: `TexFilter` linear/linear (sampler Vulkan `VK_FILTER_LINEAR`).
- **Blend**: `ADD, SRC_ALPHA, ONE_MINUS_SRC_ALPHA` (alpha clásico, sin
  blending aditivo) — un solo pipeline Vulkan con ese blend.
- **Orden de dibujo por frame** (idéntico):
  1. `bg` — sprite a pantalla completa (`GU_SPRITES`, 2 vértices).
  2. `tron` — triángulos generados con `sin`/`cos`.
  3. `flower` — triángulos, misma generación, rotada por la fase.
  4. `overlay` — sprite a pantalla completa.
  5. `logo` — sprite con el texto "PSP flower moppi productions 2005".
- **Hilo `update_thread`**: en el original solo registra el callback de
  salida; aquí no hace falta hilo extra.
- **Vblank**: el original hace `Finish`/`Sync`, doble buffer y
  `WaitVblankStart`; aquí el `present` FIFO de la swapchain es el vsync.
- **Controles**: el original lee `sceCtrlReadBufferPositive` (SQUARE
  `0x8000`, CROSS `0x4000`, stick analógico). El efecto exacto de cada botón
  no se resolvió, así que en este port:
  - **tocar la pantalla** mueve la flor un poco hacia el toque (±40 px PSP,
    como haría el stick);
  - **botón atrás** sale del demo (como `sceKernelExitGame`).

## Qué es aproximado (declarado)

- **La malla de pétalos/tronco es una reconstrucción del efecto, no vértice
  a vértice del binario.** El análisis no resolvió cuántos triángulos, qué
  pétalos ni qué UV del atlas usa el original. La reconstrucción
  (`app/src/main/cpp/flower_scene.cpp`): flor de **12 pétalos** (2 triángulos
  cada uno, radio 95 px, centro en 240,136) + disco central, sobre un tronco
  de 2 segmentos con **6 ramas**; cada pétalo/rama toma una celda de un atlas
  8×2 del `.tga` 256×128. Si se compara con el original en PPSSPP, la flor
  se verá parecida (gira, misma textura, mismo fondo) pero no idéntica.
- **Posición del logo**: el análisis no resolvió sus coordenadas exactas.
  Se coloca centrado en X, 12 px sobre el borde inferior (y 196–260).
- **`TexEnvColor 0x00FFFF00`**: está documentado en el código
  (`flower_scene.h`, `kTexEnvColor`), pero el shader usa tint blanco: en el
  modo de textura del original ese color no modula el color final de forma
  que se pueda afirmar sin el binario, y teñir toda la escena de cian/amarillo
  sería inventar. Si alguien confirma su efecto en PPSSPP, es un cambio de
  una línea en `renderer_vulkan.cpp` (`pc.tint`).
- **Alfa del overlay (`kOverlayAlpha = 0.5`)**: `overlay.tga` es RGB 24bpp
  sin canal alfa y se dibuja a pantalla completa *después* de la flor. Con
  alfa 1 la flor quedaría tapada por completo (comprobado en el harness: el
  frame no cambiaba con la fase). En el demo original la flor se ve a través
  del velo de luz, así que el port dibuja el overlay al 50%. El valor exacto
  del original (color de vértice o factor de blend de ese draw) no se resolvió;
  0.5 es una elección, no una medida.
- **Audio: pendiente.** El archivo `data/sumo_lounge_-_one_note_cripple.wav`
  está incluido en `assets/data/` (en realidad es MP3 mono 44.100 Hz dentro
  de un WAV; el original lo reproduce con 4 canales
  `sceAudioChReserve(-1, 1024, 0)` y un callback MP3 en `0x89072f4`). Este
  port **no lo reproduce**: no hay decodificador MP3 integrado. Pendiente:
  decodificarlo a PCM (p. ej. con miniaudio o FFmpeg) y reproducirlo con
  OpenSL ES / AAudio.
- **Controles exactos** (SQUARE/CROSS): pendientes de resolver en el binario;
  ver "Controles" arriba.

## Estructura

```
flower_port_android_vulkan/
├── README.md
├── build.gradle / settings.gradle / gradle.properties
├── gradle/wrapper/gradle-wrapper.properties   (Gradle 8.7)
├── local.properties.example                  (copiar a local.properties)
├── app/
│   ├── build.gradle                          (AGP 8.5.2, NDK 26.1, CMake 3.22.1,
│   │                                          minSdk 26, targetSdk 34)
│   └── src/main/
│       ├── AndroidManifest.xml               (NativeActivity, Vulkan requerido)
│       ├── cpp/
│       │   ├── main.cpp                      (NativeActivity, loop, toque/atrás)
│       │   ├── renderer_vulkan.{h,cpp}       (Vulkan 1.0: instancia, swapchain,
│       │   │                                  pipeline, 5 texturas, vertex buffer)
│       │   ├── flower_scene.{h,cpp}          (fase, draws, malla sin/cos)
│       │   ├── tga.{h,cpp}                   (loader TGA type 2/10, 24/32 bpp)
│       │   ├── CMakeLists.txt
│       │   └── shaders/textured.{vert,frag}  (GLSL 450)
│       ├── assets/
│       │   ├── data/{bg,flower,tron,overlay,logo}.tga
│       │   ├── data/sumo_lounge_-_one_note_cripple.wav   (MP3 en WAV, sin uso aún)
│       │   └── shaders/textured.{vert,frag}.spv          (SPIR-V compilado)
│       └── res/values/ (vacío; la app no usa recursos Android)
└── tests/
    ├── harness.cpp        (validación en Linux: TGA, escena, render offscreen)
    └── CMakeLists.txt
```

## Cómo compilar el APK

Requisitos: JDK 17, Android SDK con `platforms;android-34`,
`build-tools;34.0.0`, `ndk;26.1.10909125`, `cmake;3.22.1`.

```sh
cd flower_port_android_vulkan
cp local.properties.example local.properties   # y ajusta sdk.dir
./gradlew assembleDebug
# APK en: app/build/outputs/apk/debug/app-debug.apk
```

En una máquina sin acceso a los repositorios de Gradle (o si el build de
Gradle falla por red), el mismo APK se construye sin Gradle con el script
verificado en esta VM (usa el mismo SDK/NDK/CMake; no descarga nada):

```sh
./build-manual/build-apk.sh --abi all
# APK en: build-manual/flower-vulkan-debug-signed.apk
```

Instalar en un dispositivo Android con Vulkan (API 26+):

```sh
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Para regenerar los SPIR-V tras tocar los shaders:

```sh
glslangValidator -V app/src/main/cpp/shaders/textured.vert \
    -o app/src/main/assets/shaders/textured.vert.spv
glslangValidator -V app/src/main/cpp/shaders/textured.frag \
    -o app/src/main/assets/shaders/textured.frag.spv
```

## Validación en Linux (harness)

El código Vulkan nativo se valida en Linux contra `libvulkan-dev`, sin
necesidad de un dispositivo Android:

```sh
# Requisitos: libvulkan-dev, cmake, g++
cmake -S tests -B tests/build && cmake --build tests/build
cd tests/build && FLOWER_ASSET_ROOT=../../app/src/main/assets ./harness
```

El harness comprueba: las 5 TGA cargan con sus dimensiones/bpp; los 5 draws
salen en orden con vértices múltiplo de 3; la fase envuelve tras 100 frames;
la flor gira entre fases; el renderer crea instancia/dispositivo, sube las
5 texturas, crea el pipeline y renderiza 3 frames offscreen a 480×272, que
no son negros y cambian con la fase. Escribe `frame_0.ppm` y `frame_1.ppm`
para inspección visual.

> **Estado de la verificación en esta VM:** ver la sección siguiente
> (se actualiza con el resultado real del build en esta máquina).

## Estado del build (en esta VM, 2026-11-22)

**APK: SÍ existe** — pero construido con el script manual, no con Gradle:

- `build-manual/flower-vulkan-debug-signed.apk` — **2.971.129 bytes**,
  `com.moppi.flower.vulkan`, versionCode 1 / versionName 1.0, minSdk 26,
  targetSdk 34, compilado con NDK r26b (clang 17), ABIs `arm64-v8a`
  (libflower_vulkan.so 101.816 bytes) y `armeabi-v7a` (65.200 bytes),
  con `libc++_shared.so` empaquetado en ambos ABI. `apksigner verify`:
  **Verifies, v2 y v3 true, 1 firmante** (v1 false: apksigner 34 lo omite
  en este APK; irrelevante con minSdk 26, donde v2/v3 es lo que se usa).
  `aapt2 dump badging` confirma `NativeActivity` lanzable y
  `uses-feature android.hardware.vulkan.version`.
- Comando: `./build-manual/build-apk.sh --abi all` (ver ese script).
- **No probado en un dispositivo Android real**: en esta VM no hay
  emulador ni móvil; no se ha instalado ni ejecutado el APK.

**Gradle (`./gradlew assembleDebug`) NO completó aquí** — el proyecto
Gradle está listo para Android Studio / una máquina con red normal, pero en
esta VM el build falla por red, no por el código:
- El cliente Gradle no lograba comunicarse con su daemon
  ("Could not receive a message from the daemon"): se resolvió con
  `GRADLE_OPTS=-Djava.net.preferIPv4Stack=true` (localhost resolvía a
  `::1` antes que a `127.0.0.1`).
- Después, el HttpClient de Gradle no descarga el plugin AGP 8.5.2:
  `Connection reset` al tunelizar TLS por el proxy de egress. `curl` y
  `HttpsURLConnection` de Java llegan sin problema por el mismo proxy (y
  por un relay local en `127.0.0.1:18080` que añade la auth), e incluso un
  `curl` ejecutado *por* Gradle llega (200) — el fallo es específico del
  HttpClient de Gradle en esta VM. Por eso el APK se construyó a mano
  con NDK + aapt2 + zipalign + apksigner (mismo SDK/NDK/CMake que usa
  Gradle). En una máquina con red normal, `./gradlew assembleDebug`
  debería funcionar tal cual.

**Harness Linux: TODO OK** — `tests/build/harness` (compilado contra
`libvulkan-dev` 1.3.275, GPU `llvmpipe` lavapipe):
`HARNESS: TODO OK` — 5 TGA con dimensiones/bpp correctos, 5 draws en
orden (bg 6, tron 48, flower 78, overlay 6, logo 6 vértices), fase que
envuelve en el frame 101 (float32, igual que en PSP), flor que gira entre
fases, renderer que crea instancia/dispositivo, sube las 5 texturas,
crea el pipeline con los SPIR-V y renderiza 3 frames offscreen 480×272
no negros y distintos entre fases. Escribe `frame_0.ppm`/`frame_1.ppm`
(verificados visualmente: flor blanca girando, tronco, velo de luz,
logo "PSP Flower / moppi productions 2005" legible abajo).

**Bugs encontrados y corregidos gracias a estas validaciones:**
- `recordDrawCommands` enlazaba la textura por índice de draw en vez de
  por `TexId` (el quad de overlay mostraba el bg, etc.).
- La matriz orto invertía Y (NDC Vulkan: y=-1 es arriba); el logo salía
  boca abajo y arriba.
- Faltaba `#include <vulkan/vulkan_android.h>` (+ `VK_USE_PLATFORM_ANDROID_KHR`)
  para `vkCreateAndroidSurfaceKHR` (el harness Linux no lo cubría).
- `android_native_app_glue.c` debe compilarse como C, no como C++.
- El APK manual debe empaquetar `libc++_shared.so` por ABI (si no, la
  app no cargaría en el móvil).

## Pendiente

- Audio MP3 (ver arriba).
- Controles exactos SQUARE/CROSS del original.
- Verificar la malla de pétalos contra el original corriendo en PPSSPP y
  ajustar número de pétalos/UV si difiere.
- Probar en un dispositivo Android real (el APK no se ha probado en hardware
  en esta VM, que no tiene emulador ni dispositivo).

## GitHub Codespaces (2026-11-22)

El proyecto ya trae `gradlew` + `gradle/wrapper/gradle-wrapper.jar`
(Gradle 8.7, antes faltaban) y `.devcontainer/` para Codespaces.

Al abrirlo en un Codespace, `setup-android.sh` instala solo el SDK
(cmdline-tools, platform-tools, platforms;android-34,
build-tools;34.0.0, ndk;26.1.10909125, cmake;3.22.1) y escribe
`local.properties`. Luego:

```sh
./gradlew assembleDebug
# APK en: app/build/outputs/apk/debug/app-debug.apk
```

El build con Gradle empaqueta los `.so` sin comprimir y alineados,
y firma v1+v2+v3, a diferencia del APK manual anterior (v1 false,
`.so` comprimidos), que en un teléfono marcaba "archivo dañado".
