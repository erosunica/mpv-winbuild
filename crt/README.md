# CRT Electron Beam Simulator para mpv-winbuild

Implementación nativa del algoritmo temporal de **Mark Rejhon / Blur Busters y Timothy Lottes**, con licencia MIT. Conserva el núcleo de intervalos luminosos y las **tres edades de imagen** del [repositorio oficial](https://github.com/blurbusters/crt-beam-simulator/tree/734786a6c48f954af11cb390e38a9e06107ffdd9). Genera cada exposición durante la presentación de mpv mediante `gpu-next` y libplacebo.

El efecto está desactivado por defecto. Este proyecto añade únicamente CRT Electron Beam Simulator. Los modos de uno y dos barridos repiten la imagen original; no generan posiciones de movimiento nuevas. La configuración mantiene `speed=1`, `video-sync=display-vdrop` e `interpolation=no`.

## Configuración para NUC11PHKi7C + Sony XR-42A90K

La elección inicial es **dos barridos por fotograma**, SDR y NVIDIA RTX 2060. Selecciona primero la frecuencia correspondiente en Windows; el perfil no cambia el modo de pantalla.

| Vídeo | Frecuencia de pantalla preferida | Barridos | CRT simulado | Refrescos por barrido | Ganancia automática |
| --- | --- | --- | --- | --- | --- |
| 24000/1001 fps (23,976) | 120000/1001 Hz (119,88) | 2 | 48000/1001 Hz (47,952) | 2,5 | 0,4 |
| 24000/1001 fps (23,976) | 120000/1001 Hz (119,88) | 1 | 24000/1001 Hz (23,976) | 5 | 0,2 |
| 25 fps | 100 Hz | 2 | 50 Hz | 2 | 0,5 |
| 25 fps | 100 Hz | 1 | 25 Hz | 4 | 0,25 |

Un cociente **2,5** es válido: no se redondea a 2 ni a 3. Se integra la parte de cada barrido que cae dentro de cada refresco. A 25 fps, 100 Hz evita la cadencia 4/5 de 120 Hz. Para 23,976 fps, 119,88 Hz evita la pequeña discrepancia de 120 Hz. Si utilizas otras combinaciones, el algoritmo sigue admitiendo cocientes fraccionarios; la cadencia de la fuente y la del barrido pueden tener distribuciones distintas.

Un barrido ofrece menos persistencia y menor brillo, con parpadeo de aproximadamente 24/25 Hz. Dos barridos elevan la frecuencia a aproximadamente 48/50 Hz y repiten la misma imagen en movimiento, lo que puede producir imagen doble. Es el resultado de repetir barridos sin interpolar. El mínimo nativo:CRT es 2:1; Blur Busters recomienda 4:1 o más. Aumentar la ganancia aumenta brillo y persistencia de los píxeles claros.

Desde el directorio del paquete, en PowerShell:

```powershell
.\mpv.com --no-config --include=crt/crt-sdr.conf "pelicula.mkv"
.\mpv.com --no-config --include=crt/crt-sdr.conf --profile=crt-single "pelicula.mkv"
```

Puedes alternar con `--profile=crt-double` o desactivar el efecto con `--profile=crt-off`. Para consultar adaptadores: `mpv.com --no-config --gpu-api=d3d11 --d3d11-adapter=help`. El perfil selecciona `NVIDIA GeForce RTX 2060`; si tu controlador anuncia otro nombre, ajusta esa línea.

También puedes arrastrar un vídeo sobre `crt/crt-play.bat`; carga la misma configuración de dos barridos. Selecciona previamente el refresco de la TV en Windows.

El NUC tiene un **Core i7-1165G7, 4 núcleos/8 hilos, Iris Xe de 96 EU y RTX 2060 de 6 GB GDDR6**. El procesador admite x86-64-v3. Según el [Technical Product Specification de Intel, revisión 1.4](https://www.asus.com/supportonly/nuc11phki7c/helpdesk_manual/), el miniDisplayPort y HDMI físicos proceden de NVIDIA; las salidas Thunderbolt/USB-C proceden de Intel. Tu conexión miniDisplayPort→HDMI es, por tanto, una ruta adecuada para renderizar y decodificar en la RTX. `hwdec=d3d11va` permite decodificación compatible con esa GPU; RTX 2060 no proporciona decodificación AV1 por hardware. AV1 requiere decodificación por CPU o una ruta Intel que debe medirse por separado.

Configura Windows en **SDR**, RGB y rango completo. El perfil solicita una superficie de salida de 10 bits; eso **no acredita** la profundidad física del enlace o la entrada de la TV. Comprueba en Windows/controlador/TV la resolución, frecuencia, profundidad y rango realmente activos. Los modos 100/119,88/120 Hz y RGB 8–12 bits son datos confirmados por el usuario; no se han medido desde esta nube.

En la Sony, usa una entrada y un modo de imagen que admitan la frecuencia elegida, con interpolación de movimiento, inserción de negro/claridad y ahorro energético que altere el brillo desactivados. Empieza con gamma 2,2; si la respuesta SDR elegida o medida es 2,4, añade `--profile=crt-gamma24`. El valor 2,2 de TestUFO no mide la gamma de la TV. La respuesta del panel, ABL y otros ajustes pueden afectar las bandas y la luminosidad. La implementación no modifica protecciones de servicio del OLED.

## Opciones y funcionamiento

| Opción | Valor por defecto | Función |
| --- | --- | --- |
| `crt-beam` | `no` | Activa el efecto en `gpu-next`. |
| `crt-beam-scans` | `2` | Uno o dos ciclos CRT por fotograma del vídeo. |
| `crt-beam-gain` | `0` | Cero calcula `CRT Hz / display Hz`, como Auto Calc de TestUFO. Un valor positivo fija la ganancia lineal entre 0 y 1. |
| `crt-beam-debug` | `no` | Registra índice de presentación, ciclo, fuente enclavada, fase, cociente y ganancia. |

El historial utiliza tres texturas independientes RGBA de coma flotante, propiedad del efecto. Se captura la imagen procesada al entrar en un nuevo ciclo CRT; esa imagen queda enclavada durante el ciclo. No se conservan referencias a texturas temporales del renderer. A 3840×2160, tres texturas RGBA16F ocupan aproximadamente **190 MiB**, además de la memoria habitual de mpv/libplacebo. Se captura una textura por ciclo después de la inicialización; cada refresco lee las tres edades.

El reloj usa fase acotada en doble precisión en la CPU. Los contadores de 64 bits no se convierten en un contador flotante creciente en GLSL. La fase avanza tras enviar y presentar un refresco. El registro acredita renderizado/envío; por sí solo no prueba que el panel haya mostrado cada exposición.

El barrido es de arriba abajo y se refiere a la altura física de salida, también con bandas negras. Se ejecuta en `OUTPUT`, después del procesado de color y escala y antes del dithering. Las transferencias SDR de libplacebo sustituyen explícitamente la pareja sRGB modificada del ejemplo GLSL original. El presupuesto temporal permanece intacto; se normaliza la transferencia a negro cero y blanco uno para no introducir un nivel de negro supuesto en cada exposición. La forma de emisión se calcula por canal RGB.

La captura adjunta de TestUFO representa **120 Hz nativos / 60 Hz CRT**, dos refrescos por barrido, Auto Calc, gamma 2,2 y LCD Saver apagado para OLED. No fija 60 Hz CRT para cine. La versión de TestUFO servida durante el estudio usa dos edades y una fase distinta; este port mantiene las tres edades del repositorio canónico. Véase [el estudio y las fuentes](docs/estudio-integracion-crt-mpv.md).

Pausa, seek, cambio de tamaño, transferencia o modo reinician el historial. Durante pausa se muestra una imagen completa. HDR, interpolación, ausencia de display-sync o un cociente inferior a 2:1 desactivan el efecto. Se admite una tolerancia de reloj del 0,1 % junto al límite 2:1 para modos nominales de 100 Hz medidos ligeramente por debajo; no se ajusta la velocidad del vídeo. Un fallo de GPU/presentación o una pérdida de refrescos comunicada por DXGI lo deja inactivo hasta reset/seek/resume o desactivación/reactivación.

DXGI puede no facilitar estadísticas válidas, especialmente según compositor, controlador y modo de ventana. La ausencia de un aviso de pérdida no demuestra que no haya pérdidas. El algoritmo necesita una exposición por refresco con VSync fijo; no debe evaluarse con VRR ni `d3d11-sync-interval=0`.

## Compilación y validación

`sources.lock.json` fija mpv, libplacebo y las recetas. `apply.py` valida la revisión, aplica `integration.patch` y copia `src/`; falla ante conflictos o fuentes CRT modificadas. `prepare-build.py` integra el paso de parche y el empaquetado en las recetas de CMake. `build.sh` lo ejecuta automáticamente y reconstruye libplacebo y mpv. El workflow existente `MPV`, objetivo **64bit-v3**, conserva las características habituales de la compilación completa y añade los archivos CRT al paquete. Esta integración no depende del mecanismo opcional de parches de PR; combinar PR ajenas exige revalidar la base.

El paquete de referencia compilado en esta nube utiliza LLVM-MinGW/UCRT, FFmpeg, libplacebo Direct3D 11, libass y LuaJIT. Incluye reproducción de archivos, audio WASAPI, OSC y estadísticas. Las bibliotecas opcionales de la compilación completa, como JavaScript/MuJS, libarchive, VapourSynth, libbluray, DVD y otros backends gráficos, no se incluyen en este paquete de referencia. La implementación CRT no depende de ellas.

`reference-build.lock.json` registra sus fuentes y hashes. `compile-reference.py` recompila mpv y los ejecutables de prueba a partir de un prefijo Windows ya preparado, valida las versiones principales y copia las DLL. Por ejemplo, en el contenedor de desarrollo:

```sh
python3 crt/compile-reference.py \
  --source /workspace/scratch/crt-research/mpv \
  --toolchain /workspace/.setup/mpv-winbuild/llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64 \
  --prefix /workspace/.build/crt/windows/prefix \
  --work /workspace/.build/crt/windows --jobs 3
```

Este helper requiere las dependencias instaladas; la ruta de compilación completa desde fuentes es el workflow `MPV`/`build.sh` con las recetas fijadas.

Las pruebas comparan el shader ejecutado con una referencia independiente que suma intervalos luminosos en tiempo absoluto: cocientes 2, 2,5, 4, 5, 2,4 y 4,8; uno/dos barridos; ganancias automática, 0,7 y 1; transferencias lineal, gamma 2,2/2,4 y sRGB; imagen con bandas negras; pausa y suspensión tras pérdida de refresco. Se ejecutan con Vulkan de software en Linux y el **binario Windows Direct3D 11** bajo Wine. Los intermediarios FP16 admiten un error absoluto de energía de 0,0035; no es una medición de gamma ni de calidad visual de la Sony.

Para la prueba matemática portátil y la aplicación del parche:

```sh
cc -Icrt/src crt/tests/math.c -lm -o /tmp/crt-math
/tmp/crt-math
python3 crt/tests/build.py /ruta/a/mpv /ruta/a/mpv-winbuild-cmake
```

La validación definitiva en tu NUC debe comprobar **4K a 100 y 119,88 Hz**, estabilidad durante reproducción prolongada, contadores de descartes/retrasos y una grabación de alta velocidad del barrido. No está certificado el rendimiento 4K/120 Hz de la RTX 2060 ni la cadencia física de la TV desde esta nube. Empieza con los dos presets preferidos, estadísticas de mpv (`i`/`I`) y:

```powershell
.\mpv.com --no-config --include=crt/crt-sdr.conf --crt-beam-debug=yes --log-file=crt-validation.log "pelicula.mkv"
```

Para mediciones de rendimiento, repite con `crt-beam-debug=no`: el registro por refresco añade carga. El historial y el barrido añaden retardo de imagen del orden de uno a dos periodos CRT según la fila; comprueba el sincronismo labial antes de aplicar una corrección de audio.

El efecto contiene parpadeo, también en dos barridos. Usa el modo que te resulte tolerable y desactívalo si produce molestias.

## Crédito y licencia

Algoritmo CRT: **Copyright 2024 Mark Rejhon (@BlurBusters) y Timothy Lottes (@NOTimothyLottes)**, [licencia MIT](LICENSE-BlurBusters.txt). Adaptación y reloj del host: contribuyentes de mpv-winbuild, MIT. mpv y sus bibliotecas conservan sus respectivas licencias. Fuentes de referencia adicionales: [TestUFO CRT](https://testufo.com/crt), [ShaderBeam](https://github.com/mausimus/ShaderBeam), [RetroArch](https://github.com/libretro/slang-shaders/blob/master/subframe-bfi/shaders/crt-beam-simulator.slang), [Vint](https://store.steampowered.com/app/3448910/Vint_Video_Player/), [manual de mpv](https://mpv.io/manual/master/).
