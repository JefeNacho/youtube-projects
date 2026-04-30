@echo off
:: release_port.bat -- Libera el puerto COM del daemon para flashear el ESP32.
:: Correr de nuevo para reconectar (o presionar USB en la pantalla).

python -c "import urllib.request,json; urllib.request.urlopen(urllib.request.Request('http://localhost:8080/api/port/release', b'{}', {'Content-Type':'application/json'}, method='POST'))" 2>nul

if %errorlevel% neq 0 (
    echo [WARN] No se pudo contactar al daemon. Esta corriendo sentinel_daemon.py?
    pause
    exit /b 1
)
echo [OK] Puerto liberado. Flashea cuando quieras.
echo      Corre reconnect_port.bat o presiona USB en la pantalla para reconectar.
