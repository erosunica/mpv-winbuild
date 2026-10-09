# Validación de la implementación CRT — 9 de octubre de 2026

Compilación de referencia Windows UCRT x86-64-v3: mpv b2c255c13, libplacebo 7.374.0, FFmpeg 8.0.1, LLVM-MinGW 20261006 / Clang 23.1.3, libass 0.17.4 y LuaJIT 2.1. La reconstrucción desde un directorio de build nuevo con compile-reference.py completó 256 pasos y produjo mpv.exe, mpv.com, libmpv-2.dll y las pruebas Windows. La carga de las DLL y --version se comprobaron bajo Wine 11.19.

Resultados comprobados:

- Núcleo temporal: **221.184 comparaciones**, error máximo 4,15×10⁻¹³. Ejecución Linux y Windows; contador grande y fase acotada.
- Vulkan/Linux: **17 escenarios, 1.543.680 comparaciones de canal**, error absoluto máximo de energía **0,00289823**.
- Windows Direct3D 11/Wine: **17 escenarios, 1.543.680 comparaciones de canal**, error absoluto máximo de energía **0,0017107**. La prueba se volvió a ejecutar con el binario producido por la recompilación limpia.
- Casos gráficos: R=2/2,5/4/5/2,4/4,8, uno/dos barridos, ganancia automática/0,7/1, lineal/gamma 2,2/gamma 2,4/sRGB, bandas negras, pausa completa y suspensión persistente tras pérdida de refresco comunicada.
- Reproductor real: pausa/reanudación, seek, cambio de barridos y ganancia, off/on, resize, speed=1 e interpolation=no. Linux con fuente 24000/1001 y cociente 2,5; Windows Direct3D 11 con fuente 25 y cociente 2. Los registros verifican reinicio de época y fase acotada; un render ya en curso puede terminar mientras el hilo de Lua solicita el cambio.
- Aplicación del parche: base correcta, repetición idempotente, rechazo de revisión/fuentes modificadas y empaquetado de configuración.
- Suite existente de mpv: 36/37 pruebas pasaron inicialmente. `paths` falló al ejecutar desde `/` porque su fixture esperaba `//foo`; pasó al repetir esa prueba desde `/workspace`. Se comprobaron las 37 pruebas, sin cambiar el código de rutas.
- Recetas completas: CMake generó 17 proyectos de toolchain y 106 paquetes con los pins y el paso de parche CRT. **No se ejecutó la matriz completa de CI** ni se construyeron GCC/LLVM desde fuentes.
- Perfil SDR: reproducción y configuración aceptadas bajo Windows/Wine, reemplazando únicamente el adaptador NVIDIA y la decodificación por hardware por la GPU de software disponible.

Límites de estas comprobaciones:

Xvfb informa 0 Hz y no impone una cadencia física. Los overrides de frecuencia de las pruebas del player verifican el scheduling lógico, no un enlace de vídeo ni la velocidad física de reproducción. Wine implementa parcialmente DXGI: CheckColorSpaceSupport y SetColorSpace1 emiten E_NOTIMPL en el smoke test. La comparación de píxeles Direct3D usa un render target fuera de pantalla y lectura staging para evitar esa limitación de la swapchain.

**No se han medido en el NUC/Sony** el rendimiento 3840×2160 a 100/119,88/120 Hz, los refrescos físicamente mostrados, la gamma del panel, ABL, profundidad/rango efectivos del enlace ni el sincronismo labial. Las pruebas de software no certifican esos puntos. La guía propone las pruebas locales necesarias.

El paquete de referencia incluye Lua/OSC/estadísticas, audio WASAPI y las dependencias necesarias para CRT. Omite las características opcionales indicadas en README. El workflow normal conserva el conjunto habitual de mpv-winbuild con la integración CRT.
