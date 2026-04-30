@echo off
:: reconnect_port.bat -- Reconecta el daemon al ESP32 tras un release_port.

python -c "import urllib.request,json; urllib.request.urlopen(urllib.request.Request('http://localhost:8080/api/port/reconnect', b'{}', {'Content-Type':'application/json'}, method='POST'))" 2>nul

if %errorlevel% neq 0 (
    echo [WARN] No se pudo contactar al daemon. Esta corriendo sentinel_daemon.py?
    pause
    exit /b 1
)
echo [OK] Reconectando HMI...
