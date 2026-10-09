@echo off
setlocal
if "%~1"=="" (
    echo Uso: crt-play.bat "pelicula.mkv" [opciones de mpv]
    echo Selecciona antes 119,88 Hz para 23,976 fps o 100 Hz para 25 fps en Windows.
    exit /b 1
)
"%~dp0..\mpv.com" --no-config "--include=%~dp0crt-sdr.conf" %*
exit /b %errorlevel%
