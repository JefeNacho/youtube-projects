@echo off
setlocal

echo.
echo ========================================
echo   Antigravity Sidecar - Build System
echo ========================================
echo.

:: Verificar que pyinstaller está instalado
where pyinstaller >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] PyInstaller no encontrado. Instalando...
    pip install pyinstaller
)

:: Limpiar builds anteriores
echo [BUILD] Limpiando builds anteriores...
if exist dist\SidecarDaemon.exe del /f dist\SidecarDaemon.exe

:: Construir exe
echo [BUILD] Construyendo SidecarDaemon.exe...
pyinstaller SidecarDaemon.spec --clean --noconfirm

if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Build fallido. Revisa los errores arriba.
    exit /b 1
)

echo.
echo [OK] Ejecutable generado en: dist\SidecarDaemon.exe
echo.

:: Mostrar tamaño del exe
for %%F in (dist\SidecarDaemon.exe) do echo [INFO] Tamaño: %%~zF bytes

echo.
echo Para instalar el arranque automático en Windows:
echo   python install_autostart.py --install
echo.
echo Para desinstalar el arranque automático:
echo   python install_autostart.py --uninstall
echo.

endlocal
