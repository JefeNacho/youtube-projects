@echo off
setlocal
chcp 65001 >nul 2>&1

echo.
echo  Sentinel Arduino Library -- Generador de ZIP
echo  =============================================
echo.

if not exist sentinel_arduino (
    echo [ERROR] No se encontro la carpeta sentinel_arduino\
    echo         Ejecuta este script desde la raiz del proyecto.
    pause
    exit /b 1
)

if exist Sentinel.zip del /f Sentinel.zip

echo [1/2] Generando Sentinel.zip...
powershell -NoProfile -Command "Compress-Archive -Path 'sentinel_arduino' -DestinationPath 'Sentinel.zip' -Force"

if %errorlevel% neq 0 (
    echo [ERROR] No se pudo generar el ZIP. Requiere Windows 10 o superior.
    pause
    exit /b 1
)

for %%F in (Sentinel.zip) do set ZIP_SIZE=%%~zF
set /a ZIP_KB=%ZIP_SIZE% / 1024
echo [OK] Sentinel.zip generado (%ZIP_KB% KB)
echo.
echo [2/2] Instrucciones de instalacion en Arduino IDE:
echo.
echo    1. Abre Arduino IDE
echo    2. Sketch ^> Include Library ^> Add .ZIP Library...
echo    3. Selecciona Sentinel.zip (en esta misma carpeta)
echo    4. Listo -- #include ^<Sentinel.h^> ya disponible
echo.
pause
endlocal
