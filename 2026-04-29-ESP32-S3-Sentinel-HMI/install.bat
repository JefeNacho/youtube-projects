@echo off
setlocal EnableDelayedExpansion
title Sentinel Monitor â€” Instalador
chcp 65001 >nul 2>&1

echo.
echo  â•”â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•—
echo  â•‘       Sentinel Monitor â€” Instalador          â•‘
echo  â•‘       Panel HMI ESP32-S3 + Daemon PC         â•‘
echo  â•šâ•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
echo.

:: â”€â”€ 1. Verificar Python â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

python --version >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Python no encontrado.
    echo         Descargalo desde https://python.org y vuelve a ejecutar este instalador.
    echo.
    pause
    exit /b 1
)

for /f "tokens=*" %%V in ('python --version 2^>^&1') do set PY_VER=%%V
echo [OK] %PY_VER% detectado
echo.

:: â”€â”€ 2. Instalar dependencias del daemon â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

echo [1/4] Instalando dependencias Python...
pip install -r requirements.txt --quiet
if %errorlevel% neq 0 (
    echo [ERROR] Fallo al instalar dependencias. Revisa tu conexion a internet.
    pause
    exit /b 1
)
echo [OK] pyserial, psutil, requests instalados
echo.

:: â”€â”€ 3. Instalar libreria sentinel (cliente Python) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

echo [2/4] Instalando libreria sentinel...
pip install . --quiet
if %errorlevel% neq 0 (
    echo [WARN] No se pudo instalar la libreria sentinel en modo paquete.
    echo        Puedes usarla igual copiando src/sentinel/ a tu proyecto.
) else (
    echo [OK] Libreria sentinel instalada ^(from sentinel import logger^)
)
echo.

:: â”€â”€ 4. Construir ejecutable standalone â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

echo [3/4] Construyendo SidecarDaemon.exe...
echo       ^(esto puede tardar 30-60 segundos^)
echo.

where pyinstaller >nul 2>&1
if %errorlevel% neq 0 (
    echo       PyInstaller no encontrado, instalando...
    pip install pyinstaller --quiet
)

if exist tools\SidecarDaemon.spec (
    pyinstaller tools\SidecarDaemon.spec --clean --noconfirm >nul 2>&1
) else (
    pyinstaller sentinel_daemon.py --onefile --name SidecarDaemon --noconfirm --clean >nul 2>&1
)

if %errorlevel% neq 0 (
    echo [WARN] No se pudo generar el .exe. El daemon puede correrse con:
    echo        python sentinel_daemon.py
) else (
    for %%F in (dist\SidecarDaemon.exe) do set EXE_SIZE=%%~zF
    set /a EXE_MB=!EXE_SIZE! / 1048576
    echo [OK] dist\SidecarDaemon.exe generado ^(!EXE_MB! MB^)
)
echo.

:: â”€â”€ 5. Autostart opcional â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

echo [4/4] Arranque automatico con Windows
echo.
echo       Registrar el daemon para que inicie automaticamente con Windows?
echo       ^(requiere permisos de Administrador â€” se pedira confirmacion UAC^)
echo.
set /p AUTOSTART=       [s/N]:

if /i "!AUTOSTART!"=="s" (
    echo.
    echo       Solicitando permisos de Administrador...
    :: Auto-elevacion via PowerShell
    powershell -Command "Start-Process python -ArgumentList 'tools\install_autostart.py --install' -Verb RunAs -Wait"
    if %errorlevel% neq 0 (
        echo [WARN] No se pudo registrar el autostart. Puedes hacerlo manualmente:
        echo        ^(Abrir terminal como Administrador^)
        echo        python tools\install_autostart.py --install
    )
) else (
    echo.
    echo [INFO] Autostart omitido. Para activarlo despues ^(como Administrador^):
    echo        python tools\install_autostart.py --install
    echo        -- o --
    echo        dist\SidecarDaemon.exe  ^(ejecutar manualmente cuando necesites^)
)

echo.
echo  â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
echo.
echo  Instalacion completada. Proximos pasos:
echo.
echo  1. Conecta la pantalla CrowPanel por USB
echo  2. Flashea el firmware desde Arduino IDE
echo  3. Ejecuta el daemon:
echo       dist\SidecarDaemon.exe
echo       -- o --
echo       python sentinel_daemon.py
echo.
echo  En tu script Python:
echo       from sentinel import logger
echo       logger.info("Hola desde Python")
echo.
echo  Para la libreria Arduino: ejecuta tools\make_arduino_zip.bat
echo  y en Arduino IDE: Sketch ^> Include Library ^> Add .ZIP Library
echo.
pause
endlocal

