"""
Sentinel Monitor — Cliente de logging Python
Autor: Ignacio Aguilera

Envia mensajes de texto al Sentinel Daemon via socket TCP local (127.0.0.1:9090).
El daemon los reenvía en tiempo real a la pantalla ESP32-S3.

Uso basico:
    from sentinel import logger
    logger.print("Entrenamiento epoch 1/10 -- loss: 0.342")

Uso avanzado (instancia propia con terminal y prefijo):
    from sentinel.logger import SentinelLogger
    log = SentinelLogger(prefix="[YOLOv8]", terminal=1)
    log.print("Frame 42 procesado")

CSV logging:
    logger.define_csv("training", ["epoch", "loss", "acc"])
    logger.save_to_csv("training", [1, 0.342, 0.87])
    logger.close_csv("training")

JSONL logging:
    logger.save_to_jsonl("run", {"epoch": 1, "loss": 0.342, "ok": True})
    logger.close_jsonl("run")

Metricas:
    logger.metric("accuracy", 0.91, "%")

Tabla (solo visual en terminal):
    logger.table(["Modelo", "Acc", "ms"], [["YOLOv8n", 0.87, 12], ["YOLOv8s", 0.91, 28]])

Progreso (solo visual en terminal):
    logger.progress(45, 100, "Entrenando")

Recording:
    logger.start_recording("mi_sesion")              # solo .txt (default)
    logger.start_recording("run", csv_log=True)      # .txt + CSV de sesion
    # ... logs ...
    logger.stop_recording()

Desde microcontrolador (sin librerias extra):
    Serial.println("CSV>sensors:25.3,60.1");   // fila CSV
    Serial.println("CSV!sensors");              // cerrar CSV
    Serial.println("KV>run:epoch=1,loss=0.34"); // JSONL via key-value
    Serial.println("MET>temp:72.3:C");          // metrica

Sessions (multi-terminal):
    s1 = logger.session("Training")
    s2 = logger.session("Inference")
    s1.print("Epoch 1/10")
    s2.print("Modelo cargado")

Triggers (client-side pattern matching):
    logger.add_trigger(pattern="OOM", callback=lambda msg: print(f"ALERTA: {msg}"))
    logger.error("OOM killer activado")  # dispara el callback
"""

import os
import re
import socket
from datetime import datetime, timezone
from typing import Callable, List, Optional


_DEFAULT_HOST = "127.0.0.1"


# ---------------------------------------------------------------------------
# SentinelFile — objeto retornado por logger.open()
# ---------------------------------------------------------------------------

class SentinelFile:
    """Representa un archivo de datos abierto en el daemon (CSV o JSONL).

    No instanciar directamente — usar logger.open().

    Uso:
        # CSV
        with logger.open("datos.csv", headers=["temp", "hum"]) as f:
            f.write([25.3, 60.1])
            f.write([25.5, 59.8])

        # JSONL
        run = logger.open("run.jsonl")
        run.write({"epoch": 1, "loss": 0.342, "ok": True})
        run.close()

        # Leer resultado:
        pd.read_csv("sentinel_logs/csv/datos.csv")
        pd.read_json("sentinel_logs/json/run.jsonl", lines=True)
    """

    def __init__(self, logger: "SentinelLogger", filename: str,
                 headers: Optional[List] = None):
        self._logger  = logger
        self._base, self._ext = os.path.splitext(filename)
        self._ext = self._ext.lower() or ".jsonl"
        self._closed  = False

        # CSV: registrar schema al abrir
        if self._ext == ".csv" and headers:
            logger.define_csv(self._base, headers)

    def write(self, data):
        """Escribe una entrada en el archivo.

        CSV — lista o tupla de valores:
            f.write([25.3, 60.1, 1013.0])

        JSONL — diccionario:
            f.write({"epoch": 1, "loss": 0.342, "ok": True})
        """
        if self._closed:
            return
        if self._ext == ".csv":
            values = list(data.values()) if isinstance(data, dict) else list(data)
            self._logger.save_to_csv(self._base, values)
        else:
            if isinstance(data, dict):
                self._logger.save_to_jsonl(self._base, data)
            else:
                self._logger.save_to_jsonl(self._base, {"value": str(data)})

    def close(self):
        """Cierra el archivo. El daemon libera el file handle."""
        if self._closed:
            return
        self._closed = True
        if self._ext == ".csv":
            self._logger.close_csv(self._base)
        else:
            self._logger.close_jsonl(self._base)

    # Context manager — permite usar `with logger.open(...) as f:`
    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


_DEFAULT_PORT = 9090

# Registro global de triggers (compartido entre todas las instancias)
_triggers: list[dict] = []

# Contador global de terminales asignados por session()
_next_session_terminal: int = 1


class SentinelLogger:
    """Logger que envia lineas al Sentinel Daemon via TCP.

    Falla silenciosamente si el daemon no esta corriendo,
    para no romper el script principal.
    """

    # Paleta de colores -- formato LVGL recolor: #RRGGBB texto#
    COLOR_NORMAL    = "00FFCC"  # Cyan
    COLOR_INFO      = "5DADE2"  # Azul claro
    COLOR_WARN      = "FFAA00"  # Ambar
    COLOR_ERROR     = "FF4444"  # Rojo
    COLOR_SEPARATOR = "555555"  # Gris

    def __init__(
        self,
        host: str = _DEFAULT_HOST,
        port: int = _DEFAULT_PORT,
        prefix: Optional[str] = None,
        terminal: int = 0,
        timeout: float = 2.0,
    ):
        self.host = host
        self.port = port
        self.prefix = prefix
        self.terminal = terminal
        self.timeout = timeout

    def _color(self, hex_color: str, message: str) -> str:
        return f"#{hex_color} {message}#"

    def _send(self, message: str):
        """Envia con prefijo de terminal: T0:mensaje, T1:mensaje, etc.

        Abre una conexion TCP, envia el mensaje y la cierra.
        Usa conexiones cortas para no bloquear el daemon con clientes colgados.
        """
        line = f"{self.prefix} {message}" if self.prefix else message
        # Truncar a 250 chars para no desbordar el buffer del terminal LVGL
        line = line[:250]
        # Prefijo de terminal
        prefix = f"T{self.terminal}:" if self.terminal > 0 else ""
        payload = f"{prefix}{line}\n"
        try:
            with socket.create_connection(
                (self.host, self.port), timeout=self.timeout
            ) as sock:
                sock.sendall(payload.encode("ascii", errors="ignore"))
        except (ConnectionRefusedError, OSError):
            # El daemon no esta corriendo -- ignorar silenciosamente
            pass

    def _send_raw(self, raw: str):
        """Envia string sin formato LVGL (comandos internos)."""
        try:
            with socket.create_connection(
                (self.host, self.port), timeout=self.timeout
            ) as sock:
                sock.sendall(raw.encode("ascii", errors="ignore"))
        except (ConnectionRefusedError, OSError):
            pass

    def _check_triggers(self, message: str):
        """Evalua el mensaje contra todos los triggers registrados."""
        for t in _triggers:
            if t["compiled"].search(message):
                try:
                    t["callback"](message)
                except Exception:
                    pass

    def _format_timestamp(self) -> str:
        """Genera timestamp ISO 8601 UTC."""
        return datetime.now(timezone.utc).strftime("[%Y-%m-%dT%H:%M:%SZ]")

    def print(self, message: str, timestamp: bool = False):
        """Mensaje generico en cyan."""
        msg = str(message)
        self._check_triggers(msg)
        if timestamp:
            msg = f"{self._format_timestamp()} {msg}"
        self._send(self._color(self.COLOR_NORMAL, msg))

    def info(self, message: str, timestamp: bool = False):
        """Mensaje informativo en azul."""
        msg = str(message)
        self._check_triggers(msg)
        tag = f"{self._format_timestamp()} [INFO] {msg}" if timestamp else f"[INFO] {msg}"
        self._send(self._color(self.COLOR_INFO, tag))

    def warn(self, message: str, timestamp: bool = False):
        """Advertencia en ambar."""
        msg = str(message)
        self._check_triggers(msg)
        tag = f"{self._format_timestamp()} [WARN] {msg}" if timestamp else f"[WARN] {msg}"
        self._send(self._color(self.COLOR_WARN, tag))

    def error(self, message: str, timestamp: bool = False):
        """Error en rojo."""
        msg = str(message)
        self._check_triggers(msg)
        tag = f"{self._format_timestamp()} [ERR ] {msg}" if timestamp else f"[ERR ] {msg}"
        self._send(self._color(self.COLOR_ERROR, tag))

    def separator(self, char: str = "-", length: int = 30):
        """Separador visual en gris."""
        self._send(self._color(self.COLOR_SEPARATOR, char * min(length, 30)))

    def define_csv(self, filename: str, headers: list):
        """Registra el esquema de columnas de un CSV en el daemon.

        Llamar una vez antes de save_to_csv(). El archivo se crea con la
        cabecera en ese momento; las llamadas posteriores solo escriben filas.

        Uso:
            logger.define_csv("training", ["epoch", "loss", "acc"])
            logger.save_to_csv("training", [1, 0.342, 0.87])
            logger.save_to_csv("training", [2, 0.261, 0.91])
        """
        h = ",".join(str(hdr) for hdr in headers)
        self._send_raw(f"CMD:CSV_SCHEMA:{filename}:{h}\n")

    def save_to_csv(self, filename: str, values: list, headers: list = None):
        """Escribe una fila en un CSV sin repetir headers en cada llamada.

        Si se proporcionan headers, los registra primero (equivale a llamar
        define_csv() antes). En llamadas siguientes headers puede omitirse.

        Uso:
            # Primera llamada — define esquema y escribe primera fila:
            logger.save_to_csv("sensors", [25.3, 60.1], headers=["temp", "hum"])
            # Siguientes — sin headers:
            logger.save_to_csv("sensors", [25.5, 59.8])

        Protocolo enviado al daemon: CSV>filename:v1,v2,v3
        """
        if headers is not None:
            self.define_csv(filename, headers)
        v = ",".join(str(val) for val in values)
        self._send_raw(f"CSV>{filename}:{v}\n")

    def csv(self, filename: str, headers: list, values: list):
        """[Legacy] Envia CSV con headers en cada llamada.

        Preferir save_to_csv() + define_csv() para uso nuevo.
        """
        h = ",".join(str(hdr) for hdr in headers)
        v = ",".join(str(val) for val in values)
        self._send_raw(f"CSV:{filename}:{h}:{v}\n")

    def start_recording(self, name: str = "", txt: bool = True, csv_log: bool = False):
        """Pide al daemon iniciar grabacion de logs.

        txt=True   → guarda cada linea en .txt (default).
        csv_log=True → ademas guarda CSV de sesion con timestamp/level/terminal/message.

        Uso:
            logger.start_recording("mi_experimento")          # solo .txt
            logger.start_recording("run_01", csv_log=True)    # .txt + .csv de sesion
            logger.start_recording("run_02", txt=False, csv_log=True)  # solo .csv
        """
        flags = f"txt={'1' if txt else '0'},csv={'1' if csv_log else '0'}"
        self._send_raw(f"CMD:REC_START:{name}:{flags}\n")

    def stop_recording(self):
        """Pide al daemon detener grabacion."""
        self._send_raw("CMD:REC_STOP\n")

    def close_csv(self, filename: str):
        """Indica al daemon que cierre un CSV abierto con save_to_csv/define_csv.

        Uso:
            logger.define_csv("run", ["x", "y"])
            logger.save_to_csv("run", [1, 2])
            logger.close_csv("run")  # archivo cerrado y listo para leer
        """
        self._send_raw(f"CSV!{filename}\n")

    # ------------------------------------------------------------------ JSONL

    def save_to_jsonl(self, filename: str, data: dict):
        """Escribe una entrada en un archivo JSON Lines (.jsonl).

        Cada llamada escribe una linea JSON con timestamp automatico.
        Ideal para logs estructurados que luego se analizan con pandas/jq.

        Uso:
            logger.save_to_jsonl("training", {"epoch": 1, "loss": 0.342, "acc": 0.87})
            logger.save_to_jsonl("training", {"epoch": 2, "loss": 0.261, "acc": 0.91})
            logger.close_jsonl("training")

        El archivo se guarda en sentinel_logs/json/training.jsonl
        Leer con pandas: pd.read_json("sentinel_logs/json/training.jsonl", lines=True)
        """
        # Serializar el dict a formato kv: k=v,k2=v2
        # El daemon reconstruye el JSON con inferencia de tipos
        pairs = []
        for k, v in data.items():
            if isinstance(v, bool):
                pairs.append(f"{k}={'true' if v else 'false'}")
            elif isinstance(v, float):
                pairs.append(f"{k}={v:.6g}")
            else:
                pairs.append(f"{k}={v}")
        self._send_raw(f"KV>{filename}:{','.join(pairs)}\n")

    def close_jsonl(self, filename: str):
        """Cierra un archivo JSONL abierto.

        Uso:
            logger.save_to_jsonl("run", {"x": 1})
            logger.close_jsonl("run")
        """
        self._send_raw(f"JSON!{filename}\n")

    def open(self, filename: str, headers: list = None) -> "SentinelFile":
        """Abre un archivo de datos. El formato se determina por la extension.

        Retorna un objeto SentinelFile con metodos write() y close().
        Soporta uso como context manager (with).

        CSV — definir columnas al abrir:
            with logger.open("datos.csv", ["temp", "hum", "presion"]) as f:
                f.write([25.3, 60.1, 1013.0])
                f.write([25.5, 59.8, 1012.5])
            # archivo listo: sentinel_logs/csv/datos.csv

        JSONL — sin schema:
            run = logger.open("run.jsonl")
            run.write({"epoch": 1, "loss": 0.342, "ok": True})
            run.close()
            # archivo listo: sentinel_logs/json/run.jsonl

        Leer con pandas:
            pd.read_csv("sentinel_logs/csv/datos.csv")
            pd.read_json("sentinel_logs/json/run.jsonl", lines=True)
        """
        return SentinelFile(self, filename, headers)

    # ---------------------------------------------------------------- METRICAS

    def metric(self, name: str, value, unit: str = ""):
        """Registra una metrica nombrada en metrics.jsonl y la muestra en la pantalla.

        Uso:
            logger.metric("loss", 0.342)
            logger.metric("cpu_temp", 72.3, "C")
            logger.metric("accuracy", 91.4, "%")
        """
        self._send_raw(f"MET>{name}:{value}:{unit}\n")

    # ---------------------------------------------------------- VISUALIZACION

    def table(self, headers: list, rows: list):
        """Muestra una tabla ASCII en el terminal de la pantalla.

        Uso:
            logger.table(
                ["Modelo", "Acc", "ms"],
                [["YOLOv8n", 0.87, 12], ["YOLOv8s", 0.91, 28]]
            )
        """
        # Calcular ancho de cada columna
        cols = len(headers)
        widths = [len(str(h)) for h in headers]
        for row in rows:
            for i, cell in enumerate(row[:cols]):
                widths[i] = max(widths[i], len(str(cell)))

        def _row_line(cells):
            return "| " + " | ".join(str(c).ljust(widths[i]) for i, c in enumerate(cells)) + " |"

        sep = "+" + "+".join("-" * (w + 2) for w in widths) + "+"
        lines = [sep, _row_line(headers), sep]
        for row in rows:
            lines.append(_row_line(row))
        lines.append(sep)

        for line in lines:
            self._send(self._color(self.COLOR_NORMAL, line))

    def progress(self, current: int, total: int, label: str = ""):
        """Muestra una barra de progreso en el terminal.

        Uso:
            for i, batch in enumerate(data):
                logger.progress(i + 1, len(data), "Training")
        """
        if total <= 0:
            return
        pct = int(100 * current / total)
        bar_w = 20
        filled = int(bar_w * current / total)
        bar = "=" * filled + ">" + " " * (bar_w - filled - 1) if filled < bar_w else "=" * bar_w
        prefix = f"{label} " if label else ""
        line = f"{prefix}[{bar}] {pct}% ({current}/{total})"
        self._send(self._color(self.COLOR_INFO, line))

    def set_terminal(self, terminal: int) -> "SentinelLogger":
        """Cambia el terminal activo de esta instancia (0-3).

        Todos los mensajes enviados despues de este llamado van al terminal indicado.
        Retorna self para permitir encadenamiento.

        Uso:
            from sentinel import logger
            logger.set_terminal(2)
            logger.info("Va al T2 de la pantalla")

            # Encadenado:
            logger.set_terminal(1).warn("Warning directo en T1")

            # Restaurar T0:
            logger.set_terminal(0)
        """
        self.terminal = max(0, min(int(terminal), 3))
        return self

    def session(self, name: str) -> "SentinelLogger":
        """Crea una sesion nombrada en un terminal independiente.

        Retorna una nueva instancia de SentinelLogger vinculada a un terminal
        dedicado. El daemon registra el nombre de la sesion para la UI.

        Uso:
            s1 = logger.session("Training")
            s2 = logger.session("Inference")
            s1.print("Epoch 1")
            s2.print("Modelo cargado")
        """
        global _next_session_terminal
        terminal = _next_session_terminal
        if _next_session_terminal < 3:
            _next_session_terminal += 1

        # Notificar al daemon el nombre de esta sesion
        self._send_raw(f"CMD:SESSION:{terminal}:{name}\n")

        return SentinelLogger(
            host=self.host,
            port=self.port,
            prefix=self.prefix,
            terminal=terminal,
            timeout=self.timeout,
        )

    @staticmethod
    def add_trigger(pattern: str, callback: Callable[[str], None]):
        """Registra un trigger client-side que ejecuta callback al hacer match.

        El callback se invoca localmente cada vez que un mensaje enviado
        por CUALQUIER instancia de SentinelLogger coincide con el patron.

        Uso:
            logger.add_trigger(pattern="OOM", callback=lambda msg: print(f"ALERTA: {msg}"))
            logger.error("OOM killer activado")  # -> imprime "ALERTA: OOM killer activado"
        """
        _triggers.append({
            "pattern": pattern,
            "compiled": re.compile(pattern, re.IGNORECASE),
            "callback": callback,
        })

    @staticmethod
    def remove_triggers():
        """Elimina todos los triggers registrados."""
        _triggers.clear()
