"""
install_autostart.py — Registra/desregistra SidecarDaemon.exe en el
Programador de Tareas de Windows para que inicie automáticamente con el sistema.

Uso:
    python install_autostart.py --install      # Registrar
    python install_autostart.py --uninstall    # Desregistrar
    python install_autostart.py --status       # Ver estado actual

Requisitos:
    - Ejecutar como Administrador (el Task Scheduler requiere privilegios elevados)
    - SidecarDaemon.exe debe existir en dist\SidecarDaemon.exe
      (o la ruta configurada en EXE_PATH abajo)

Por qué Task Scheduler y no el Registro de Windows:
    - Permite reinicio automático si el proceso crashea (RestartCount=3)
    - Permite ejecutar con privilegios elevados sin UAC
    - Es reversible y auditable desde la GUI de Windows
    - No requiere sesión de usuario activa para arrancar (AtLogon funciona antes del desktop)
"""

import os
import sys
import subprocess
import argparse
from pathlib import Path

TASK_NAME  = "AntigravitySidecarDaemon"
TASK_DESC  = "Antigravity Sidecar HMI — daemon de telemetría para panel ESP32-S3"

# Ruta al exe — se resuelve relativa a este script
EXE_PATH   = Path(__file__).parent / "dist" / "SidecarDaemon.exe"
LOG_DIR    = Path(__file__).parent / "logs"


def check_admin() -> bool:
    """Verifica si el script corre con privilegios de administrador."""
    try:
        import ctypes
        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except Exception:
        return False


def run_schtasks(*args: str) -> tuple[int, str]:
    """Ejecuta schtasks.exe y devuelve (código de salida, stdout+stderr)."""
    cmd = ["schtasks"] + list(args)
    result = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="ignore")
    output = result.stdout + result.stderr
    return result.returncode, output.strip()


def install():
    if not check_admin():
        print("[ERROR] Este script requiere privilegios de Administrador.")
        print("        Ejecuta la terminal como Administrador e intenta de nuevo.")
        sys.exit(1)

    if not EXE_PATH.exists():
        print(f"[ERROR] No se encontró el ejecutable en: {EXE_PATH}")
        print("        Ejecuta build.bat primero para generar el .exe")
        sys.exit(1)

    # Crear directorio de logs
    LOG_DIR.mkdir(exist_ok=True)
    log_stdout = LOG_DIR / "sidecar_out.txt"
    log_stderr = LOG_DIR / "sidecar_err.txt"

    exe = str(EXE_PATH.resolve())

    # Crear la tarea con schtasks.exe
    # /SC ONLOGON  → arranca cuando cualquier usuario inicia sesión
    # /RL HIGHEST  → corre con privilegios elevados (necesario para acceso a COM)
    # /F           → sobreescribir si ya existe
    code, out = run_schtasks(
        "/Create",
        "/TN", TASK_NAME,
        "/TR", f'"{exe}"',
        "/SC", "ONLOGON",
        "/RL", "HIGHEST",
        "/IT",           # Solo cuando el usuario está logueado (necesario para COM)
        "/DELAY", "0001:00",  # Delay de 1 minuto tras login para que los drivers COM carguen
        "/SD", "01/01/2025",
        "/F",
        "/D", "MON,TUE,WED,THU,FRI,SAT,SUN",
    )

    if code != 0:
        print(f"[ERROR] No se pudo crear la tarea:\n{out}")
        sys.exit(1)

    # Configurar reinicio automático si crashea (requiere XML o PowerShell)
    _configure_restart_policy()

    print(f"[OK] Tarea '{TASK_NAME}' registrada exitosamente.")
    print(f"     Ejecutable: {exe}")
    print(f"     Logs en:    {LOG_DIR}")
    print()
    print("La pantalla HMI será detectada automáticamente al conectarla por USB.")
    print("El daemon iniciará con Windows en el próximo reinicio.")
    print()
    print("Para iniciar ahora sin reiniciar:")
    print(f'    schtasks /Run /TN "{TASK_NAME}"')


def _configure_restart_policy():
    """Configura la política de reinicio automático via PowerShell."""
    ps_script = f"""
$task = Get-ScheduledTask -TaskName "{TASK_NAME}" -ErrorAction SilentlyContinue
if ($task) {{
    $settings = New-ScheduledTaskSettingsSet `
        -RestartCount 5 `
        -RestartInterval (New-TimeSpan -Minutes 2) `
        -ExecutionTimeLimit ([TimeSpan]::Zero) `
        -MultipleInstances IgnoreNew
    Set-ScheduledTask -TaskName "{TASK_NAME}" -Settings $settings | Out-Null
    Write-Host "[OK] Política de reinicio configurada (5 reintentos, cada 2 min)"
}}
"""
    result = subprocess.run(
        ["powershell", "-NoProfile", "-NonInteractive", "-Command", ps_script],
        capture_output=True, text=True
    )
    if result.returncode == 0:
        print(result.stdout.strip())
    else:
        print(f"[WARN] No se pudo configurar la política de reinicio: {result.stderr.strip()}")


def uninstall():
    if not check_admin():
        print("[ERROR] Este script requiere privilegios de Administrador.")
        sys.exit(1)

    code, out = run_schtasks("/Delete", "/TN", TASK_NAME, "/F")
    if code == 0:
        print(f"[OK] Tarea '{TASK_NAME}' eliminada.")
    elif "no existe" in out.lower() or "not found" in out.lower() or "does not exist" in out.lower():
        print(f"[INFO] La tarea '{TASK_NAME}' no estaba registrada.")
    else:
        print(f"[ERROR] No se pudo eliminar la tarea:\n{out}")
        sys.exit(1)


def status():
    code, out = run_schtasks("/Query", "/TN", TASK_NAME, "/FO", "LIST", "/V")
    if code == 0:
        print(f"[STATUS] Tarea '{TASK_NAME}' encontrada:\n")
        # Mostrar solo las líneas relevantes
        relevant_keys = ("Nombre", "Estado", "Pr", "Resultado", "Próxima", "Última", "Task Name", "Status", "Next Run", "Last Run", "Last Result")
        for line in out.splitlines():
            if any(line.strip().startswith(k) for k in relevant_keys):
                print(f"  {line.strip()}")
    else:
        print(f"[INFO] La tarea '{TASK_NAME}' no está registrada en el sistema.")
        print("       Ejecuta: python install_autostart.py --install")


if __name__ == "__main__":
    if sys.platform != "win32":
        print("[ERROR] Este script es solo para Windows.")
        sys.exit(1)

    parser = argparse.ArgumentParser(
        description="Gestiona el arranque automático de SidecarDaemon en Windows"
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--install",   action="store_true", help="Registrar en Task Scheduler")
    group.add_argument("--uninstall", action="store_true", help="Desregistrar del Task Scheduler")
    group.add_argument("--status",    action="store_true", help="Ver estado actual")

    args = parser.parse_args()

    if args.install:
        install()
    elif args.uninstall:
        uninstall()
    elif args.status:
        status()
