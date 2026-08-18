# Aplicación Flutter/Dart FFI

`app/flutter/` es la aplicación principal de ODPAR Territorial Domain. Flutter
presenta el juego, resuelve el tacto y administra el ciclo de vida; toda regla,
simulación y rasterización permanece en el mismo núcleo C11 de `engine/`.

No hay WebView, traducción de reglas a Dart ni una segunda implementación del
juego. La previsualización web y Flutter son dos hosts del mismo motor.

## Ruta de datos

```text
multitouch Flutter -> Q15 odg_set_input -> odg_tick_us (reloj fijo 120 Hz)
                  C11 odg_render_frame -> copia RGBA8 -> ui.Image -> RawImage
                  C11 odg_copy_stats  -> POD estable -> HUD Flutter
```

El host consulta `odg_ffi_abi_query()` antes de inicializar y rechaza una
biblioteca incompatible. El contrato esperado es:

| Contrato | Valor |
| --- | ---: |
| Engine API | 14 |
| FFI ABI | 1 |
| `odg_ffi_abi_info` | 64 bytes |
| `odg_game_stats` | 192 bytes |
| `odg_leader_entry` | 24 bytes |
| Pixel | RGBA8, 4 bytes |
| Simulación | 120 Hz fijo |

Flutter usa las funciones de copia del ABI. Nunca conserva un puntero mutable
al framebuffer C durante la decodificación asíncrona de la imagen.

## Actualizaciones del motor sin rehacer Flutter

`app/flutter/CMakeLists.txt` compila directamente `engine/src/render.c`,
`sim.c`, `game.c` y las demás fuentes del mismo núcleo. Por tanto, una
corrección interna de render, mapa, bots o mecánicas entra en la siguiente
compilación de Flutter sin copiar código ni modificar la UI Dart.

Solo hace falta actualizar bindings cuando cambia deliberadamente el contrato
público —firmas exportadas o structs FFI—. Un cambio interno que conserve API 14,
FFI ABI 1 y RGBA8 no requiere tocar Dart, Gradle ni controles. `make host-check`
protege esta frontera en cada ejecución de CI.

## Presentación y controles

- Portrait y landscape comparten una sola pantalla con `SafeArea` real.
- Un dedo posee el joystick; otro puede controlar free-look al mismo tiempo.
- `ACTION` y `DROP` tienen ownership independiente y pulsos de dos muestras.
- Una pausa, pérdida de foco o cambio de ciclo de vida neutraliza todo input.
- El HUD solo lee snapshots: dominio, jugadores activos, líderes, carga y estado
  de torretas. No calcula resultados de juego.
- La UI usa una paleta oscura y sobria, tipografía espaciada, paneles discretos
  y controles translúcidos sin gráficos infantiles.

La calidad `EQUILIBRADA` es el valor inicial. Un gobernador con histéresis baja
o sube únicamente la resolución del raster según el costo medido. No cambia la
frecuencia de simulación, el mapa, los bots ni la precisión del motor. `ULTRA`
puede solicitar todo el límite anunciado por el ABI; el dispositivo no define
un techo artificial del motor.

## Estructura

```text
app/flutter/
  CMakeLists.txt                  mismo C11 -> biblioteca dinámica
  lib/src/native/                ABI 14, structs y validación
  lib/src/engine/                reloj, snapshots y copia de framebuffer
  lib/src/input/                 multitouch y Q15
  lib/src/render/                presupuesto raster adaptativo
  lib/src/ui/                    pantalla, HUD y tema
  test/                          ABI, input y raster adaptativo
  android/                       empaquetado mínimo de Flutter
```

## Desarrollo Flutter

El build reproducible usa Flutter 3.47.0 con Dart 3.8 o posterior. Las pruebas que no abren
la biblioteca nativa pueden ejecutarse en cualquier estación Flutter:

```sh
cd app/flutter
flutter pub get
dart format --output=none lib test
flutter analyze
flutter test
```

Para probar contra una biblioteca Linux, compila el núcleo desde la raíz y
expone `build/` en la ruta de carga del sistema antes de iniciar un runner
Flutter Linux. La aplicación comprueba API y tamaños en el arranque.

## Empaquetado APK

Android solo aporta el contenedor de distribución Flutter. `MainActivity` no
incluye lógica: extiende `FlutterActivity`. Gradle llama a
`app/flutter/CMakeLists.txt`; CMake compila directamente las seis fuentes C11 y
genera `libodpar_territorial_domain.so` con:

- `-fstack-protector-strong`;
- RELRO, NOW y pila no ejecutable;
- segmentos compatibles con páginas de memoria de 16 KiB;
- `--no-undefined`;
- el version-script `engine/odpar_territorial_domain.exports.map`, si existe.

Los ABI empacados son `arm64-v8a` (principal), `armeabi-v7a` y `x86_64`. Con
Flutter, Android SDK/NDK y CMake instalados:

```sh
cd app/flutter
flutter pub get
gradle -p android wrapper --gradle-version 8.14 --distribution-type bin
flutter build apk --release \
  --target-platform android-arm,android-arm64,android-x64
flutter build apk --release --split-per-abi \
  --target-platform android-arm,android-arm64,android-x64
```

`android/local.properties` no se versiona. Debe contener `flutter.sdk` y
`sdk.dir`; Android Studio lo crea o puede escribirse antes del build. El APK de
CI usa la clave debug para ser instalable. Una publicación en tienda debe
reemplazar `signingConfigs.debug` por una upload key protegida mediante secrets.
Los flags `android.builtInKotlin=false` y `android.newDsl=false` fijan de forma
explícita la ruta AGP 8/KGP compatible mientras Flutter completa su transición
a AGP 9; así también funcionan invocaciones directas de Gradle anteriores al
migrador de Flutter.

## Integración continua

`.github/workflows/android.yml` usa runners gratuitos de GitHub Actions y tiene
dos puertas independientes:

1. ejecuta pruebas C/FFI, soak, sanitizers y hardening; exige formato Dart,
   `flutter analyze` y tests; construye APK universal y splits, y comprueba la
   biblioteca C11 dentro de cada ABI;
2. instala clang/lld, ejecuta `make wasm`, el smoke test Node y `make bundle`,
   y publica HTML, WASM y el HTML autocontenido.

Los artifacts resultantes son `odpar-flutter-apk-api14` y
`odpar-wasm-preview-api14`. Esta última puerta es la autoridad para regenerar
los binarios web API 14; los archivos preexistentes pueden pertenecer a una
compilación anterior.

## Validación disponible en este entorno

La prueba nativa FFI API 14 existente confirma portrait `720x1280`, 921 600
píxeles y snapshots compatibles. Este entorno no contiene Flutter, Dart,
CMake, NDK ni Android SDK, por lo que la construcción del APK queda validada
por GitHub Actions y no se afirma como compilada localmente.
