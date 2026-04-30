# Sentinel Monitor â€” Daemon principal
# Autor: Ignacio Aguilera

import serial
import serial.tools.list_ports
import struct
import time
import threading
import socket
import sys
import re
import os
import json
import csv
import hashlib
import base64
from collections import deque
from datetime import datetime, timezone
from http.server import HTTPServer, BaseHTTPRequestHandler
from data_fetchers import WeatherFetcher, SystemFetcher

# â”€â”€ Control de voz â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
# False = infraestructura de audio intacta (mic/speaker/paquetes 0xE5/0xD5)
# pero sin Whisper, Gemini ni ElevenLabs. Activar cuando haya servidor AI local.
VOICE_ENABLED = False

if VOICE_ENABLED:
    from sentinel_voice import VoiceEngine, MAGIC_MIC, MIC_CHUNK_BYTES
else:
    MAGIC_MIC      = 0xE5
    MIC_CHUNK_BYTES = 1024

# Importacion condicional de BLE â€” el daemon arranca sin BLE si bleak no esta instalado
try:
    from ble_channel import BLEChannel, BLEAK_AVAILABLE
except ImportError:
    BLEAK_AVAILABLE = False
    BLEChannel = None  # type: ignore

# --- CONFIGURACION ---
HMI_VID     = 0x1A86   # CH340 USB-Serial (CrowPanel)
HMI_PID     = 0x7522
BAUD_RATE   = 460800
IPC_PORT    = 9090      # Puerto TCP local para recibir logs de scripts externos
API_PORT    = 8080      # Puerto HTTP REST API
WS_PORT     = 8081      # Puerto WebSocket streaming
OWM_API_KEY = os.environ.get("SENTINEL_OWM_API_KEY", "")  # Reemplazar con tu clave de OpenWeatherMap o usar setx SENTINEL_OWM_API_KEY "tu_key"


# ---------------------------------------------------------------------------
# Filtrado de puertos COM â€” solo placas de desarrollo
# ---------------------------------------------------------------------------

# VIDs de fabricantes de chips USB-Serial usados en placas de desarrollo.
# Excluye dongles Bluetooth, adaptadores de audio y puertos virtuales sin VID.
_DEV_BOARD_VIDS: set[int] = {
    0x1A86,  # WCH CH340/CH341/CH342/CH343 (clones Arduino, ESP boards, CrowPanel)
    0x2341,  # Arduino LLC (Uno, Mega, Leonardo, Nano 33...)
    0x2A03,  # Arduino SA (placas europeas)
    0x303A,  # Espressif Systems (ESP32, ESP32-S3, ESP32-C3, ESP32-H2)
    0x10C4,  # Silicon Labs CP2102/CP2104/CP2109 (NodeMCU, WEMOS, muchos clones)
    0x0403,  # FTDI FT232R/FT2232/FT231X (placas clÃ¡sicas, analizadores)
    0x16C0,  # PJRC Teensy
    0x2E8A,  # Raspberry Pi Ltd (RP2040, RP2350, Pico)
    0x239A,  # Adafruit Industries
    0x1B4F,  # SparkFun Electronics
    0x04D8,  # Microchip Technology (PIC, SAM, curiosity boards)
    0x0483,  # STMicroelectronics (STM32, Nucleo, Discovery)
    0x1FC9,  # NXP Semiconductors (LPC, i.MX)
    0x0D28,  # ARM mbed / CMSIS-DAP (Nucleo, BBC micro:bit)
    0x04B4,  # Infineon / Cypress (PSoC)
    0x067B,  # Prolific PL2303 (muchos cables USB-Serial genÃ©ricos)
    0x6666,  # Prototype / vendor-specific (Microchip bootloaders)
}

# Palabras clave en descripcion/fabricante que indican NO es una placa de desarrollo
_BLUETOOTH_KEYWORDS: tuple[str, ...] = (
    "bluetooth", "bthenum", "bth", "btport",
    "wireless", "radio", "rfcomm",
    "modem", "audio", "soundcard", "voice",
)


def _is_dev_board(port_info) -> bool:
    """Retorna True si el puerto parece pertenecer a una placa de desarrollo.

    Criterios (en orden):
      1. Si el VID estÃ¡ en la whitelist â†’ sÃ­.
      2. Si la descripciÃ³n/fabricante contiene palabras Bluetooth/audio â†’ no.
      3. Si no tiene VID (puerto virtual puro o Bluetooth sin driver) â†’ no.
    """
    if port_info.vid is not None and port_info.vid in _DEV_BOARD_VIDS:
        return True

    combined = " ".join(filter(None, [
        port_info.description or "",
        port_info.manufacturer or "",
        port_info.hwid or "",
    ])).lower()

    if any(kw in combined for kw in _BLUETOOTH_KEYWORDS):
        return False

    # Sin VID reconocido â†’ excluir (puertos Bluetooth, COM virtuales, etc.)
    return False


# ---------------------------------------------------------------------------
# Deteccion automatica por VID:PID
# ---------------------------------------------------------------------------

def find_hmi_port() -> str | None:
    """Escanea los puertos COM y devuelve el dispositivo del CrowPanel por VID:PID.
    No requiere configurar el numero de COM manualmente."""
    for p in serial.tools.list_ports.comports():
        if p.vid == HMI_VID and p.pid == HMI_PID:
            return p.device
    return None


# ---------------------------------------------------------------------------
# SessionManager: gestiona hasta 4 terminales simultaneos
# ---------------------------------------------------------------------------

class SessionManager:
    """Gestiona hasta 4 terminales simultaneos con buffers independientes."""

    MAX_TERMINALS = 4
    BUFFER_SIZE = 10000  # lineas por terminal

    def __init__(self):
        self.terminals = [
            deque(maxlen=self.BUFFER_SIZE) for _ in range(self.MAX_TERMINALS)
        ]
        self.terminal_names = ["Python", "", "", ""]
        self.locks = [threading.Lock() for _ in range(self.MAX_TERMINALS)]
        self._listeners = []  # callables que reciben cada entry

    def add_listener(self, callback):
        """Registra un listener que sera llamado con cada entry nueva."""
        self._listeners.append(callback)

    def add_line(self, terminal: int, line: str, level: str = "NORMAL"):
        """Anade una linea al buffer del terminal."""
        terminal = max(0, min(terminal, self.MAX_TERMINALS - 1))
        timestamp = datetime.now(timezone.utc).isoformat(timespec="milliseconds")
        entry = {
            "ts": timestamp,
            "level": level,
            "msg": line,
            "terminal": terminal,
        }
        with self.locks[terminal]:
            self.terminals[terminal].append(entry)
        # Notificar listeners (FileLogger, TriggerManager, etc.)
        for cb in self._listeners:
            try:
                cb(entry)
            except Exception:
                pass

    def get_lines(self, terminal: int, n: int = 100, level: str | None = None) -> list:
        """Obtiene las ultimas n lineas de un terminal, opcionalmente filtradas por nivel."""
        terminal = max(0, min(terminal, self.MAX_TERMINALS - 1))
        with self.locks[terminal]:
            lines = list(self.terminals[terminal])
        if level:
            lines = [e for e in lines if e["level"] == level]
        return lines[-n:]

    def get_all_lines(self, n: int = 100, level: str | None = None) -> list:
        """Todas las lineas de todos los terminales, ordenadas por timestamp."""
        all_lines = []
        for t in range(self.MAX_TERMINALS):
            with self.locks[t]:
                all_lines.extend(self.terminals[t])
        if level:
            all_lines = [e for e in all_lines if e["level"] == level]
        return sorted(all_lines, key=lambda x: x["ts"])[-n:]


# ---------------------------------------------------------------------------
# FileLogger: guardado de logs a disco
# ---------------------------------------------------------------------------

class FileLogger:
    """Gestiona guardado de logs a disco: recordings y CSV."""

    LOG_DIR = "sentinel_logs"

    def __init__(self, session_mgr: SessionManager):
        self.session_mgr = session_mgr
        self.is_recording = False
        self.recording_name = ""
        self._recording_txt = False   # grabando texto plano
        self._recording_csv = False   # grabando CSV de sesion
        self._files = {}              # {name: file_handle}
        self._csv_files = {}          # {filename: (file_handle, writer)}
        self._csv_schemas = {}        # {filename: [headers]} â€” esquemas registrados
        self._jsonl_files = {}        # {filename: file_handle}
        self._lock = threading.Lock()

        os.makedirs(self.LOG_DIR, exist_ok=True)
        os.makedirs(os.path.join(self.LOG_DIR, "sessions"), exist_ok=True)
        os.makedirs(os.path.join(self.LOG_DIR, "csv"), exist_ok=True)
        os.makedirs(os.path.join(self.LOG_DIR, "recordings"), exist_ok=True)
        os.makedirs(os.path.join(self.LOG_DIR, "json"), exist_ok=True)

        # Registrarse como listener del SessionManager
        self.session_mgr.add_listener(self.log_line)

    def start_recording(self, name: str = "", txt: bool = True, csv_log: bool = False):
        """Inicia grabacion de logs.

        txt=True  â†’ guarda cada linea en .txt (texto plano, default).
        csv_log=True â†’ ademas guarda un CSV de sesion con timestamp/level/message.
        Ambos son independientes y opt-in.
        """
        with self._lock:
            if self.is_recording:
                return
            ts = datetime.now().strftime("%Y-%m-%dT%H%M%S")
            self.recording_name = name or f"session_{ts}"
            base = os.path.join(self.LOG_DIR, "recordings")

            self._recording_txt = txt
            self._recording_csv = csv_log

            if txt:
                txt_path = os.path.join(base, f"{self.recording_name}.txt")
                self._files["recording_txt"] = open(txt_path, "a", encoding="utf-8")
                print(f"[FILE] Grabacion iniciada: {self.LOG_DIR}/recordings/{self.recording_name}.txt", flush=True)

            if csv_log:
                csv_path = os.path.join(base, f"{self.recording_name}.csv")
                is_new = not os.path.exists(csv_path) or os.path.getsize(csv_path) == 0
                csv_fh = open(csv_path, "a", newline="", encoding="utf-8")
                csv_writer = csv.writer(csv_fh)
                if is_new:
                    csv_writer.writerow(["timestamp", "level", "terminal", "message"])
                self._files["recording_csv_fh"] = csv_fh
                self._files["recording_csv_writer"] = csv_writer
                print(f"[FILE] CSV sesion iniciado: {self.LOG_DIR}/recordings/{self.recording_name}.csv", flush=True)

            self.is_recording = True

    def _stop_recording_locked(self):
        """Cierra archivos de grabacion. Llamar con self._lock ya adquirido."""
        for key in ("recording_txt", "recording_csv_fh"):
            if key in self._files:
                try:
                    self._files[key].close()
                except Exception:
                    pass
                del self._files[key]
        self._files.pop("recording_csv_writer", None)
        if self.is_recording:
            print(f"[FILE] Grabacion detenida: {self.recording_name}", flush=True)
        self.is_recording = False

    def stop_recording(self):
        """Detiene la grabacion actual."""
        with self._lock:
            self._stop_recording_locked()

    def log_line(self, entry: dict):
        """Llamado por SessionManager al recibir cada linea."""
        with self._lock:
            if not self.is_recording:
                return
            clean = re.sub(r"#[0-9A-Fa-f]{6}\s(.*?)#", r"\1", entry["msg"])
            if self._recording_txt and "recording_txt" in self._files:
                self._files["recording_txt"].write(clean + "\n")
                self._files["recording_txt"].flush()
            if self._recording_csv and "recording_csv_writer" in self._files:
                self._files["recording_csv_writer"].writerow(
                    [entry["ts"], entry["level"], entry.get("terminal", 0), clean]
                )
                self._files["recording_csv_fh"].flush()

    def write_csv(self, filename: str, headers: list[str], values: list):
        """Escribe una fila de CSV con headers explÃ­citos (API legacy)."""
        with self._lock:
            path = os.path.join(self.LOG_DIR, "csv", f"{filename}.csv")
            is_new = not os.path.exists(path)
            if filename not in self._csv_files:
                f = open(path, "a", newline="", encoding="utf-8")
                writer = csv.writer(f)
                if is_new:
                    writer.writerow(["timestamp"] + headers)
                self._csv_files[filename] = (f, writer)

            f, writer = self._csv_files[filename]
            ts = datetime.now(timezone.utc).isoformat(timespec="milliseconds")
            writer.writerow([ts] + [str(v) for v in values])
            f.flush()

    def register_csv_schema(self, filename: str, headers: list[str]):
        """Registra el esquema de columnas para un CSV.

        Crea el archivo con la cabecera si aun no existe. Llamar una vez
        antes de write_csv_values(); el microcontrolador no necesita reenviar
        los headers en cada fila.
        """
        with self._lock:
            self._csv_schemas[filename] = list(headers)
            path = os.path.join(self.LOG_DIR, "csv", f"{filename}.csv")
            is_new = not os.path.exists(path) or os.path.getsize(path) == 0
            if filename not in self._csv_files:
                f = open(path, "a", newline="", encoding="utf-8")
                writer = csv.writer(f)
                if is_new:
                    writer.writerow(["timestamp"] + list(headers))
                self._csv_files[filename] = (f, writer)
            print(f"[CSV] Esquema registrado: {filename} {headers}", flush=True)

    def write_csv_values(self, filename: str, values: list):
        """Escribe una fila usando el esquema ya registrado (sin repetir headers).

        Si el archivo no existe aun, lo crea. Si el esquema no esta registrado,
        las columnas seran solo los valores sin cabecera.
        Llamado desde el proxy COM cuando detecta 'CSV>filename:v1,v2,v3'.
        """
        with self._lock:
            path = os.path.join(self.LOG_DIR, "csv", f"{filename}.csv")
            if filename not in self._csv_files:
                is_new = not os.path.exists(path) or os.path.getsize(path) == 0
                f = open(path, "a", newline="", encoding="utf-8")
                writer = csv.writer(f)
                if is_new and filename in self._csv_schemas:
                    writer.writerow(["timestamp"] + self._csv_schemas[filename])
                self._csv_files[filename] = (f, writer)
            f, writer = self._csv_files[filename]
            ts = datetime.now(timezone.utc).isoformat(timespec="milliseconds")
            writer.writerow([ts] + [str(v) for v in values])
            f.flush()

    def close_csv(self, filename: str):
        """Cierra un CSV abierto. El MCU lo indica con 'CSV!filename'."""
        with self._lock:
            if filename in self._csv_files:
                fh, _ = self._csv_files.pop(filename)
                try:
                    fh.close()
                except Exception:
                    pass
                print(f"[CSV] Archivo cerrado: {filename}.csv", flush=True)

    # ---------- JSONL ----------

    def write_jsonl(self, filename: str, data: dict):
        """Escribe una entrada JSON Lines. Crea el archivo si no existe."""
        with self._lock:
            if filename not in self._jsonl_files:
                path = os.path.join(self.LOG_DIR, "json", f"{filename}.jsonl")
                self._jsonl_files[filename] = open(path, "a", encoding="utf-8")
            fh = self._jsonl_files[filename]
            entry = {"ts": datetime.now(timezone.utc).isoformat(timespec="milliseconds")}
            entry.update(data)
            fh.write(json.dumps(entry, ensure_ascii=False) + "\n")
            fh.flush()

    def close_jsonl(self, filename: str):
        """Cierra un archivo JSONL abierto."""
        with self._lock:
            if filename in self._jsonl_files:
                fh = self._jsonl_files.pop(filename)
                try:
                    fh.close()
                except Exception:
                    pass
                print(f"[JSON] Archivo cerrado: {filename}.jsonl", flush=True)

    # ---------- METRICAS ----------

    def log_metric(self, name: str, value: str, unit: str = ""):
        """Append una metrica al archivo metrics.jsonl."""
        with self._lock:
            path = os.path.join(self.LOG_DIR, "metrics.jsonl")
            ts = datetime.now(timezone.utc).isoformat(timespec="milliseconds")
            # Inferir tipo numerico
            try:
                typed_value = int(value)
            except ValueError:
                try:
                    typed_value = float(value)
                except ValueError:
                    typed_value = value
            entry = {"ts": ts, "metric": name, "value": typed_value, "unit": unit}
            with open(path, "a", encoding="utf-8") as f:
                f.write(json.dumps(entry) + "\n")

    def close_all(self):
        """Cierra todos los archivos abiertos."""
        with self._lock:
            for name, fh in self._files.items():
                if hasattr(fh, "close"):
                    try:
                        fh.close()
                    except Exception:
                        pass
            for filename, fh in self._jsonl_files.items():
                try:
                    fh.close()
                except Exception:
                    pass
            self._jsonl_files.clear()
            self._files.clear()
            for filename, (fh, _writer) in self._csv_files.items():
                try:
                    fh.close()
                except Exception:
                    pass
            self._csv_files.clear()
            self.is_recording = False


# ---------------------------------------------------------------------------
# TriggerManager: ejecuta acciones cuando aparecen patrones en los logs
# ---------------------------------------------------------------------------

class TriggerManager:
    """Sistema de triggers: ejecuta acciones cuando aparecen patrones en los logs."""

    def __init__(self, file_logger: FileLogger):
        self.triggers = []  # list of {pattern, action, terminal, id}
        self.file_logger = file_logger
        self._lock = threading.Lock()
        self._next_id = 1

    def add_trigger(
        self, pattern: str, action: str, terminal: int | None = None
    ) -> int:
        """Anade un trigger. Actions: 'record', 'buzzer', 'save_snapshot'.
        Retorna el ID del trigger."""
        with self._lock:
            trigger_id = self._next_id
            self._next_id += 1
            self.triggers.append(
                {
                    "id": trigger_id,
                    "pattern": re.compile(pattern, re.IGNORECASE),
                    "pattern_str": pattern,
                    "action": action,
                    "terminal": terminal,
                }
            )
            return trigger_id

    def remove_trigger(self, trigger_id: int) -> bool:
        """Elimina un trigger por ID."""
        with self._lock:
            before = len(self.triggers)
            self.triggers = [t for t in self.triggers if t["id"] != trigger_id]
            return len(self.triggers) < before

    def list_triggers(self) -> list[dict]:
        """Lista todos los triggers registrados."""
        with self._lock:
            return [
                {
                    "id": t["id"],
                    "pattern": t["pattern_str"],
                    "action": t["action"],
                    "terminal": t["terminal"],
                }
                for t in self.triggers
            ]

    def check(self, entry: dict):
        """Evalua la entrada contra todos los triggers."""
        with self._lock:
            triggers_copy = list(self.triggers)
        for t in triggers_copy:
            if t["terminal"] is not None and t["terminal"] != entry["terminal"]:
                continue
            if t["pattern"].search(entry["msg"]):
                self._execute(t["action"], entry)

    def _execute(self, action: str, entry: dict):
        if action == "record" and not self.file_logger.is_recording:
            self.file_logger.start_recording(
                f"trigger_{datetime.now().strftime('%H%M%S')}"
            )
        elif action == "buzzer":
            # El buzzer esta en el ESP32, se enviaria un comando I2C
            pass
        elif action == "save_snapshot":
            # Guardar snapshot de los ultimos N logs
            pass


# ---------------------------------------------------------------------------
# WebSocketServer: broadcast de logs en tiempo real via WS :8081
# ---------------------------------------------------------------------------

class WebSocketServer(threading.Thread):
    """Servidor WebSocket minimalista (RFC 6455) para streaming de logs.

    Cada cliente conectado a ws://localhost:8081/stream recibe todos los
    log entries en JSON en tiempo real.
    """

    WS_MAGIC = "258EAFA5-E914-47DA-95CA-5AB9A11AD8FE"

    def __init__(self, session_mgr: SessionManager, host: str = "127.0.0.1", port: int = WS_PORT):
        super().__init__(daemon=True)
        self.session_mgr = session_mgr
        self.host = host
        self.port = port
        self._clients: list[socket.socket] = []
        self._clients_lock = threading.Lock()
        self._running = True

        # Registrar como listener del SessionManager
        self.session_mgr.add_listener(self._broadcast_entry)

    def run(self):
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((self.host, self.port))
        srv.listen(8)
        srv.settimeout(1.0)
        print(f"[WS] WebSocket server en ws://{self.host}:{self.port}/stream", flush=True)
        while self._running:
            try:
                conn, addr = srv.accept()
                threading.Thread(target=self._handshake, args=(conn,), daemon=True).start()
            except socket.timeout:
                continue
            except OSError:
                break
        srv.close()

    def _handshake(self, conn: socket.socket):
        """Realiza el handshake HTTP -> WebSocket (RFC 6455)."""
        try:
            conn.settimeout(5.0)
            data = conn.recv(4096).decode("utf-8", errors="ignore")
            if "Upgrade: websocket" not in data and "upgrade: websocket" not in data:
                conn.close()
                return
            # Extraer Sec-WebSocket-Key
            key = ""
            for line in data.split("\r\n"):
                if line.lower().startswith("sec-websocket-key:"):
                    key = line.split(":", 1)[1].strip()
                    break
            if not key:
                conn.close()
                return
            # Generar accept key
            accept = base64.b64encode(
                hashlib.sha1((key + self.WS_MAGIC).encode()).digest()
            ).decode()
            response = (
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                f"Sec-WebSocket-Accept: {accept}\r\n"
                "\r\n"
            )
            conn.sendall(response.encode())
            conn.settimeout(None)
            with self._clients_lock:
                self._clients.append(conn)
            # Mantener viva la conexion leyendo pings/close frames
            self._read_loop(conn)
        except Exception:
            pass
        finally:
            with self._clients_lock:
                if conn in self._clients:
                    self._clients.remove(conn)
            try:
                conn.close()
            except Exception:
                pass

    def _read_loop(self, conn: socket.socket):
        """Lee frames del cliente para detectar close/ping."""
        try:
            while self._running:
                head = conn.recv(2)
                if not head or len(head) < 2:
                    break
                opcode = head[0] & 0x0F
                if opcode == 0x8:  # close
                    break
                if opcode == 0x9:  # ping -> pong
                    mask_len = head[1] & 0x7F
                    payload_len = mask_len
                    if payload_len == 126:
                        ext = conn.recv(2)
                        payload_len = int.from_bytes(ext, "big")
                    elif payload_len == 127:
                        ext = conn.recv(8)
                        payload_len = int.from_bytes(ext, "big")
                    mask = conn.recv(4) if head[1] & 0x80 else b""
                    payload = conn.recv(payload_len) if payload_len else b""
                    if mask:
                        payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
                    pong = bytearray([0x8A, len(payload)]) + payload
                    conn.sendall(pong)
                    continue
                # Otros frames: consumir payload y descartar
                mask_bit = head[1] & 0x80
                payload_len = head[1] & 0x7F
                if payload_len == 126:
                    conn.recv(2)
                elif payload_len == 127:
                    conn.recv(8)
                if mask_bit:
                    conn.recv(4)
                if payload_len > 0 and payload_len < 126:
                    conn.recv(payload_len)
        except Exception:
            pass

    def _ws_frame(self, data: bytes) -> bytes:
        """Construye un frame WebSocket de texto."""
        frame = bytearray([0x81])  # FIN + TEXT opcode
        length = len(data)
        if length < 126:
            frame.append(length)
        elif length < 65536:
            frame.append(126)
            frame.extend(length.to_bytes(2, "big"))
        else:
            frame.append(127)
            frame.extend(length.to_bytes(8, "big"))
        frame.extend(data)
        return bytes(frame)

    def _broadcast_entry(self, entry: dict):
        """Envia un entry JSON a todos los clientes WebSocket conectados."""
        if not self._clients:
            return
        payload = json.dumps(entry).encode("utf-8")
        frame = self._ws_frame(payload)
        dead = []
        with self._clients_lock:
            for conn in self._clients:
                try:
                    conn.sendall(frame)
                except Exception:
                    dead.append(conn)
            for conn in dead:
                self._clients.remove(conn)
                try:
                    conn.close()
                except Exception:
                    pass

    def shutdown(self):
        self._running = False
        with self._clients_lock:
            for conn in self._clients:
                try:
                    conn.close()
                except Exception:
                    pass
            self._clients.clear()


# ---------------------------------------------------------------------------
# SentinelAPI: servidor HTTP REST para control externo
# ---------------------------------------------------------------------------

class SentinelAPI(threading.Thread):
    """Servidor HTTP REST para control externo del Sentinel."""

    def __init__(self, daemon: "SentinelDaemon", host: str = "127.0.0.1", port: int = API_PORT):
        super().__init__(daemon=True)
        self.daemon_ref = daemon
        self.host = host
        self.port = port
        self._server = None

    def run(self):
        daemon_ref = self.daemon_ref

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, format, *args):
                # Silenciar logs del servidor HTTP
                pass

            def _set_headers(self, status: int = 200, content_type: str = "application/json"):
                self.send_response(status)
                self.send_header("Content-Type", content_type)
                self.send_header("Access-Control-Allow-Origin", "*")
                self.send_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS")
                self.send_header("Access-Control-Allow-Headers", "Content-Type")
                self.end_headers()

            def _json_response(self, data, status: int = 200):
                self._set_headers(status)
                self.wfile.write(json.dumps(data).encode("utf-8"))

            def _read_body(self) -> dict:
                length = int(self.headers.get("Content-Length", 0))
                if length == 0:
                    return {}
                raw = self.rfile.read(length)
                try:
                    return json.loads(raw.decode("utf-8"))
                except (json.JSONDecodeError, UnicodeDecodeError):
                    return {}

            def do_OPTIONS(self):
                self._set_headers(204)

            def do_GET(self):
                path = self.path.split("?")[0]
                params = {}
                if "?" in self.path:
                    qs = self.path.split("?")[1]
                    for part in qs.split("&"):
                        if "=" in part:
                            k, v = part.split("=", 1)
                            params[k] = v

                # GET /api/status
                if path == "/api/status":
                    ble_connected = (
                        daemon_ref._ble.is_connected()
                        if daemon_ref._ble
                        else False
                    )
                    self._json_response(
                        {
                            "status": "running",
                            "channel": daemon_ref._channel,
                            "hmi_connected": (
                                daemon_ref._channel == "usb"
                                and daemon_ref.ser is not None
                                and daemon_ref.ser.is_open
                            ) or (
                                daemon_ref._channel == "ble"
                                and ble_connected
                            ),
                            "hmi_port": daemon_ref.ser.port
                            if daemon_ref.ser
                            else None,
                            "ble_connected": ble_connected,
                            "proxy_port": daemon_ref.target_port,
                            "recording": daemon_ref.file_logger.is_recording,
                            "recording_name": daemon_ref.file_logger.recording_name,
                            "terminals": daemon_ref.session_mgr.terminal_names,
                        }
                    )

                # GET /api/terminals
                elif path == "/api/terminals":
                    terminals = []
                    for i in range(SessionManager.MAX_TERMINALS):
                        with daemon_ref.session_mgr.locks[i]:
                            count = len(daemon_ref.session_mgr.terminals[i])
                        terminals.append(
                            {
                                "id": i,
                                "name": daemon_ref.session_mgr.terminal_names[i],
                                "line_count": count,
                            }
                        )
                    self._json_response({"terminals": terminals})

                # GET /api/terminals/<id>/logs
                elif path.startswith("/api/terminals/") and path.endswith("/logs"):
                    try:
                        tid = int(path.split("/")[3])
                    except (IndexError, ValueError):
                        self._json_response({"error": "invalid terminal id"}, 400)
                        return
                    n = int(params.get("n", "100"))
                    level = params.get("level", None)
                    lines = daemon_ref.session_mgr.get_lines(tid, n, level)
                    self._json_response({"terminal": tid, "lines": lines})

                # GET /api/logs
                elif path == "/api/logs":
                    n = int(params.get("n", "100"))
                    level = params.get("level", None)
                    lines = daemon_ref.session_mgr.get_all_lines(n, level)
                    self._json_response({"lines": lines})

                # GET /api/recording/status
                elif path == "/api/recording/status":
                    self._json_response(
                        {
                            "is_recording": daemon_ref.file_logger.is_recording,
                            "recording_name": daemon_ref.file_logger.recording_name,
                        }
                    )

                # GET /api/triggers
                elif path == "/api/triggers":
                    self._json_response(
                        {"triggers": daemon_ref.trigger_mgr.list_triggers()}
                    )

                # GET /api/sessions
                elif path == "/api/sessions":
                    names = [
                        n for n in daemon_ref.session_mgr.terminal_names if n
                    ]
                    self._json_response(
                        {"sessions": names, "count": len(names)}
                    )

                # GET /api/ai/analyze
                elif path == "/api/ai/analyze":
                    self._json_response(
                        {
                            "status": "not_implemented",
                            "message": "AI endpoint ready for future integration",
                        },
                        501,
                    )

                # GET /api/ai/schema
                elif path == "/api/ai/schema":
                    self._json_response(
                        {
                            "endpoint": "/api/ai/analyze",
                            "method": "POST",
                            "description": "Analyze log patterns using AI",
                            "request_schema": {
                                "type": "object",
                                "properties": {
                                    "terminal": {
                                        "type": "integer",
                                        "description": "Terminal ID to analyze (0-3)",
                                    },
                                    "n": {
                                        "type": "integer",
                                        "description": "Number of recent lines to analyze",
                                        "default": 100,
                                    },
                                    "prompt": {
                                        "type": "string",
                                        "description": "Analysis prompt or question",
                                    },
                                },
                                "required": ["prompt"],
                            },
                            "response_schema": {
                                "type": "object",
                                "properties": {
                                    "analysis": {"type": "string"},
                                    "patterns": {
                                        "type": "array",
                                        "items": {"type": "string"},
                                    },
                                    "suggestions": {
                                        "type": "array",
                                        "items": {"type": "string"},
                                    },
                                },
                            },
                        }
                    )

                else:
                    self._json_response({"error": "not found"}, 404)

            def do_POST(self):
                path = self.path.split("?")[0]
                body = self._read_body()

                # POST /api/recording/start
                if path == "/api/recording/start":
                    name = body.get("name", "")
                    daemon_ref.file_logger.start_recording(name)
                    self._json_response(
                        {
                            "ok": True,
                            "recording_name": daemon_ref.file_logger.recording_name,
                        }
                    )

                # POST /api/recording/stop
                elif path == "/api/recording/stop":
                    daemon_ref.file_logger.stop_recording()
                    self._json_response({"ok": True})

                # POST /api/triggers
                elif path == "/api/triggers":
                    pattern = body.get("pattern", "")
                    action = body.get("action", "record")
                    terminal = body.get("terminal", None)
                    if not pattern:
                        self._json_response(
                            {"error": "pattern is required"}, 400
                        )
                        return
                    tid = daemon_ref.trigger_mgr.add_trigger(
                        pattern, action, terminal
                    )
                    self._json_response({"ok": True, "trigger_id": tid})

                # POST /api/ai/analyze
                elif path == "/api/ai/analyze":
                    self._json_response(
                        {
                            "status": "not_implemented",
                            "message": "AI endpoint ready for future integration",
                        },
                        501,
                    )

                # POST /api/send
                elif path == "/api/send":
                    terminal = body.get("terminal", 0)
                    message = body.get("message", "")
                    if not message:
                        self._json_response(
                            {"error": "message is required"}, 400
                        )
                        return
                    # Registrar en session y reenviar al ESP32
                    daemon_ref.session_mgr.add_line(
                        terminal, message, "NORMAL"
                    )
                    daemon_ref._safe_write(
                        f"{message}\n".encode("ascii", errors="ignore")
                    )
                    self._json_response({"ok": True})

                # POST /api/port/release
                elif path == "/api/port/release":
                    daemon_ref.release_port()
                    self._json_response({"ok": True, "message": "Puerto liberado"})

                # POST /api/port/reconnect
                elif path == "/api/port/reconnect":
                    daemon_ref.reconnect_port()
                    self._json_response({"ok": True, "message": "Reconectando..."})

                else:
                    self._json_response({"error": "not found"}, 404)

            def do_DELETE(self):
                path = self.path.split("?")[0]

                # DELETE /api/triggers/<id>
                if path.startswith("/api/triggers/"):
                    try:
                        tid = int(path.split("/")[3])
                    except (IndexError, ValueError):
                        self._json_response(
                            {"error": "invalid trigger id"}, 400
                        )
                        return
                    removed = daemon_ref.trigger_mgr.remove_trigger(tid)
                    self._json_response({"ok": removed})
                else:
                    self._json_response({"error": "not found"}, 404)

        self._server = HTTPServer((self.host, self.port), Handler)
        self._server.timeout = 1.0
        print(f"[API] REST server en http://{self.host}:{self.port}", flush=True)
        while daemon_ref.running:
            self._server.handle_request()

    def shutdown(self):
        if self._server:
            self._server.server_close()


def _parse_kv_string(kv_str: str) -> dict:
    """Parsea 'k=v,k2=v2' a dict infiriendo tipos (int, float, bool, str)."""
    result = {}
    for pair in kv_str.split(","):
        if "=" not in pair:
            continue
        k, _, v = pair.partition("=")
        k, v = k.strip(), v.strip()
        if v.lower() == "true":
            result[k] = True
        elif v.lower() == "false":
            result[k] = False
        else:
            try:
                result[k] = int(v)
            except ValueError:
                try:
                    result[k] = float(v)
                except ValueError:
                    result[k] = v
    return result


# ---------------------------------------------------------------------------
# IPCServer: recibe mensajes de scripts externos via TCP 127.0.0.1:9090
# ---------------------------------------------------------------------------

class IPCServer:
    """Servidor TCP local que acepta conexiones de cualquier script Python.
    Cada linea recibida se clasifica y reenvÃ­a a la pantalla HMI via Serial.

    Protocolo:
        Linea normal:       texto\\n          -> terminal 0, reenvio al ESP32
        Con prefijo:        T1:texto\\n       -> terminal 1, reenvio al ESP32
        Comando CSV:        CSV:file:h1,h2:v1,v2\\n -> FileLogger.write_csv()
        Comando REC start:  CMD:REC_START:name\\n    -> FileLogger.start_recording()
        Comando REC stop:   CMD:REC_STOP\\n          -> FileLogger.stop_recording()
    """

    def __init__(self, daemon: "SentinelDaemon"):
        self.daemon = daemon
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.bind(("127.0.0.1", IPC_PORT))
        self._sock.listen(5)
        print(f"[IPC] Servidor escuchando en 127.0.0.1:{IPC_PORT}", flush=True)

    def serve(self):
        self._sock.settimeout(1.0)
        while self.daemon.running:
            try:
                conn, _ = self._sock.accept()
                threading.Thread(
                    target=self._handle_client, args=(conn,), daemon=True
                ).start()
            except socket.timeout:
                continue
            except OSError:
                break

    def _handle_client(self, conn: socket.socket):
        with conn:
            try:
                for line in conn.makefile("r", encoding="utf-8", errors="ignore"):
                    raw = line.strip()
                    if not raw:
                        continue

                    # --- Comando CSV con headers explÃ­citos (legacy) ---
                    if raw.startswith("CSV:"):
                        self._handle_csv(raw)
                        continue

                    # --- Comando CSV solo valores (esquema pre-registrado) ---
                    # Formato: CSV>filename:v1,v2,v3
                    if raw.startswith("CSV>"):
                        self._handle_csv_values(raw)
                        continue

                    # --- KV â†’ JSONL ---
                    # Formato: KV>filename:k=v,k2=v2
                    if raw.startswith("KV>"):
                        rest = raw[3:]
                        colon = rest.find(":")
                        if colon >= 0:
                            fname = rest[:colon].strip()
                            data = _parse_kv_string(rest[colon + 1:])
                            if fname and data:
                                self.daemon.file_logger.write_jsonl(fname, data)
                        continue

                    # --- Metrica ---
                    # Formato: MET>name:value:unit
                    if raw.startswith("MET>"):
                        parts = raw[4:].split(":", 2)
                        if len(parts) >= 2:
                            unit = parts[2] if len(parts) > 2 else ""
                            self.daemon.file_logger.log_metric(parts[0].strip(), parts[1].strip(), unit.strip())
                            # Tambien mostrar en terminal como texto
                            unit_str = f" {unit.strip()}" if unit.strip() else ""
                            display = f"[MET] {parts[0].strip()} = {parts[1].strip()}{unit_str}"
                            self.daemon.session_mgr.add_line(0, display, "INFO")
                            self.daemon._safe_write(f"{display}\n".encode("ascii", errors="ignore"))
                        continue

                    # --- Cerrar JSONL ---
                    if raw.startswith("JSON!"):
                        fname = raw[5:].strip()
                        if fname:
                            self.daemon.file_logger.close_jsonl(fname)
                        continue

                    # --- Registrar esquema CSV ---
                    # Formato: CMD:CSV_SCHEMA:filename:h1,h2,h3
                    if raw.startswith("CMD:CSV_SCHEMA:"):
                        parts = raw.split(":", 3)
                        if len(parts) == 4:
                            self.daemon.file_logger.register_csv_schema(
                                parts[2], parts[3].split(",")
                            )
                        continue

                    # --- Comando REC ---
                    if raw.startswith("CMD:REC_START"):
                        # Formato: CMD:REC_START:name:txt=1,csv=0
                        parts = raw.split(":", 3)
                        name = parts[2] if len(parts) > 2 else ""
                        txt, csv_log = True, False
                        if len(parts) > 3:
                            for flag in parts[3].split(","):
                                k, _, v = flag.partition("=")
                                if k == "txt":   txt     = v == "1"
                                if k == "csv":   csv_log = v == "1"
                        self.daemon.file_logger.start_recording(name, txt=txt, csv_log=csv_log)
                        continue
                    if raw == "CMD:REC_STOP":
                        self.daemon.file_logger.stop_recording()
                        continue

                    # --- Comando SESSION (nombrar terminal) ---
                    if raw.startswith("CMD:SESSION:"):
                        parts = raw.split(":", 3)
                        if len(parts) >= 4:
                            try:
                                tid = int(parts[2])
                                sname = parts[3]
                                if 0 <= tid < SessionManager.MAX_TERMINALS:
                                    self.daemon.session_mgr.terminal_names[tid] = sname
                                    print(f"[SESSION] Terminal {tid} = '{sname}'", flush=True)
                            except (ValueError, IndexError):
                                pass
                        continue

                    # --- Mensaje con prefijo de terminal ---
                    terminal = 0
                    msg = raw
                    t_match = re.match(r"^T(\d):", raw)
                    if t_match:
                        terminal = int(t_match.group(1))
                        msg = raw[3:]  # quitar "Tx:"

                    # Detectar nivel del mensaje
                    level = "NORMAL"
                    if "[ERR" in msg:
                        level = "ERROR"
                    elif "[WARN" in msg:
                        level = "WARN"
                    elif "[INFO" in msg:
                        level = "INFO"

                    # Registrar en SessionManager
                    self.daemon.session_mgr.add_line(terminal, msg, level)

                    # Reenviar al ESP32 con prefijo de terminal para routing
                    if terminal > 0:
                        self.daemon._safe_write(
                            f"T{terminal}:{msg}\n".encode("ascii", errors="ignore")
                        )
                    else:
                        self.daemon._safe_write(
                            f"{msg}\n".encode("ascii", errors="ignore")
                        )
            except Exception:
                pass

    def _handle_csv(self, raw: str):
        """Procesa: CSV:filename:h1,h2,h3:v1,v2,v3  (API legacy con headers)"""
        parts = raw.split(":", 3)
        if len(parts) < 4:
            return
        filename = parts[1]
        headers = parts[2].split(",")
        values = parts[3].split(",")
        self.daemon.file_logger.write_csv(filename, headers, values)

    def _handle_csv_values(self, raw: str):
        """Procesa: CSV>filename:v1,v2,v3  (solo valores, esquema pre-registrado)"""
        # Quitar prefijo "CSV>"
        rest = raw[4:]
        colon = rest.find(":")
        if colon < 0:
            return
        filename = rest[:colon]
        values = rest[colon + 1:].split(",")
        self.daemon.file_logger.write_csv_values(filename, values)

    def close(self):
        try:
            self._sock.close()
        except OSError:
            pass


# ---------------------------------------------------------------------------
# SentinelDaemon: daemon principal
# ---------------------------------------------------------------------------

class SentinelDaemon:
    # Reintentos maximos de conexion USB antes de intentar BLE
    _USB_RETRIES = 5

    def __init__(self):
        self.ser: serial.Serial | None = None
        self.target_ser: serial.Serial | None = None
        self.target_port: str | None = None
        self.proxy_sers: dict[int, serial.Serial] = {}   # T1-T3: {idx: Serial}
        self.proxy_ports: dict[int, str] = {}             # T1-T3: {idx: port_name}
        self.running = True
        self.serial_lock = threading.Lock()
        self._reconnecting = False
        self._released_proxy_ports: dict[int, str] = {}  # {idx: port} guardado al hacer release
        self._proxy_release_requested = False             # flag para que proxy_loop cierre el puerto el mismo

        # Canal activo: "usb" | "ble" | "none"
        self._channel: str = "none"

        # Buffer acumulativo para el parser BLE (notificaciones pueden llegar fragmentadas)
        self._ble_rx_buf: bytearray = bytearray()

        # Buffer acumulativo para el proxy T0 (bytes crudos pueden llegar sin \n completo)
        self._t0_line_buf: bytearray = bytearray()

        # Canal BLE (None si bleak no esta disponible)
        self._ble: BLEChannel | None = None
        if BLEAK_AVAILABLE and BLEChannel is not None:
            self._ble = BLEChannel(self._on_ble_data, connect_callback=self._on_ble_connected)
        else:
            print(
                "[BLE] bleak no disponible. Solo canal USB activo. "
                "(pip install bleak>=0.21.0 para habilitar BLE)",
                flush=True,
            )

        # Subsistemas
        self.session_mgr = SessionManager()
        self.file_logger = FileLogger(self.session_mgr)
        self.trigger_mgr = TriggerManager(self.file_logger)

        # Registrar TriggerManager como listener del SessionManager
        self.session_mgr.add_listener(self.trigger_mgr.check)

        self._connect()

        self.weather = WeatherFetcher(OWM_API_KEY)
        self.system = SystemFetcher()

        # Motor de voz (deshabilitado â€” VOICE_ENABLED = False)
        self.voice = VoiceEngine(self) if VOICE_ENABLED else None

    # ------------------------------------------------------------------
    # Conexion y reconexion automatica
    # ------------------------------------------------------------------

    def _connect(self):
        """Detecta el CrowPanel por VID:PID y abre el puerto USB.
        Si USB no esta disponible, intenta BLE como fallback pero sigue buscando USB.
        USB tiene prioridad absoluta sobre BLE.
        """
        usb_attempts = 0
        last_search_msg = 0

        while self.running:
            # â”€â”€ 1. Intentar USB (Prioridad) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
            port = find_hmi_port()
            if port:
                try:
                    time.sleep(0.5)
                    ser = serial.Serial()
                    ser.port         = port
                    ser.baudrate     = BAUD_RATE
                    ser.timeout      = 0.1
                    ser.write_timeout = 1
                    ser.dtr          = False
                    ser.rts          = False
                    ser.open()
                    
                    # Handshake DTR/RTS
                    time.sleep(0.1)
                    ser.dtr = True
                    ser.rts = True
                    
                    self.ser = ser
                    self._reconnecting = False
                    usb_attempts = 0

                    # Si BLE estaba activo, desactivarlo
                    if self._channel == "ble" and self._ble:
                        print("\n[DAEMON] USB detectado â€” desactivando Bluetooth...", flush=True)
                        self._ble.stop()

                    self._channel = "usb"
                    print(f"\n[OK] Pantalla conectada por USB en {port}.", flush=True)
                    return
                except Exception as e:
                    pass

            # â”€â”€ 2. Fallback BLE â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
            usb_attempts += 1
            if (
                usb_attempts >= self._USB_RETRIES
                and self._ble is not None
                and self._channel != "ble"
            ):
                print("\n[DAEMON] USB no encontrado. Activando bÃºsqueda por Bluetooth...", flush=True)
                self._channel = "ble"
                self._ble.start()
                # No hacemos 'return' aquÃ­: seguimos en el bucle buscando USB por si acaso

            # â”€â”€ 3. Mensaje de estado â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
            now = time.time()
            if now - last_search_msg > 10:
                if self._channel == "ble":
                    print("[INFO] Operando por Bluetooth. USB listo para reconexiÃ³n caliente...", end="\r", flush=True)
                else:
                    print("[BUSCANDO] No se detecta la pantalla. Verifica el cable USB o activa Bluetooth...", end="\r", flush=True)
                last_search_msg = now

            time.sleep(2)

    def release_port(self):
        """Pide al proxy_loop que cierre el puerto desde su propio thread.
        Cerrar desde aqui causaria que Windows retenga el handle hasta que
        proxy_loop termine su llamada activa a in_waiting.
        La conexion HMI (self.ser, COM6) permanece intacta.
        """
        if not self.target_ser and not self.proxy_sers:
            print("[DAEMON] release_port: no habia puerto proxy activo.", flush=True)
            return

        # Guardar los puertos ANTES de pedir el cierre, para poder reconectar despues
        self._released_proxy_ports.clear()
        if self.target_ser and self.target_port:
            self._released_proxy_ports[0] = self.target_port
        for idx, port in self.proxy_ports.items():
            self._released_proxy_ports[idx] = port

        # SeÃ±alar al proxy_loop que cierre el puerto el mismo
        self._proxy_release_requested = True
        print("[DAEMON] Solicitando liberacion de puerto proxy al proxy_loop...", flush=True)

    def reconnect_port(self):
        """Reconecta automaticamente los puertos proxy que se liberaron con release_port()."""
        if not self._released_proxy_ports:
            print("[DAEMON] reconnect_port: no hay puertos guardados para reconectar.", flush=True)
            return

        for idx, port in self._released_proxy_ports.items():
            print(f"[DAEMON] Reconectando T{idx} â†’ {port}...", flush=True)
            if idx == 0:
                self.start_proxy(port)
            else:
                self.start_proxy_terminal(idx, port)

        self._released_proxy_ports.clear()

    def _trigger_reconnect(self):
        """Lanza reconexion en background sin bloquear los hilos activos.
        Cierra USB, establece canal='none', y vuelve a intentar _connect()
        (que puede derivar a BLE si USB no reaparece).
        """
        if self._reconnecting:
            return
        self._reconnecting = True
        self._channel = "none"
        # Cerrar el puerto USB antes de soltar la referencia.
        # En Windows es obligatorio para que el proximo serial.Serial()
        # no reciba "Access Denied".
        old_ser = self.ser
        self.ser = None
        if old_ser:
            try:
                old_ser.close()
            except Exception:
                pass
        print("[DAEMON] Pantalla desconectada. Buscando reconexion...", flush=True)
        threading.Thread(target=self._connect, daemon=True).start()

    def _safe_write(self, data: bytes):
        """Enruta la escritura al canal activo (USB o BLE).

        - Canal USB: usa serial_lock como siempre. Dispara reconexion si falla.
        - Canal BLE: delega a BLEChannel.write(); paquetes 0xD5 son descartados
          internamente por BLEChannel (sin ancho de banda para audio).
        - Canal 'none': descarte silencioso.
        """
        if not data:
            return

        if self._channel == "usb":
            if not self.ser or not self.ser.is_open:
                return
            try:
                with self.serial_lock:
                    self.ser.write(data)
            except (serial.SerialException, serial.SerialTimeoutException, OSError) as e:
                print(f"[SERIAL] Error de escritura: {e}", flush=True)
                self._trigger_reconnect()

        elif self._channel == "ble":
            if self._ble:
                self._ble.write(data)
            # Si BLE no esta conectado todavia, descarte silencioso

        # canal "none": descarte silencioso

    # ------------------------------------------------------------------
    # Envio de paquetes binarios
    # ------------------------------------------------------------------

    def send_atmos_packet(self):
        data = self.weather.get_atmos_data()
        if not data:
            return
        aqi_val = data["aqi"] * 100
        w_id = data["weather_id"]
        w_code = 0
        if 200 <= w_id <= 531:
            w_code = 2  # Rain
        elif 801 <= w_id <= 804:
            w_code = 1  # Clouds
        packet = struct.pack(
            "<BffibB",
            0xA5,
            data["temp"],
            float(data["hum"]),
            aqi_val,
            w_code,
            0x00,
        )
        self._safe_write(packet)
        print(f"[ATMOS] Sent: {data['temp']}C, AQI: {aqi_val}", flush=True)

    def send_sys_packet(self):
        data = self.system.get_metrics()
        if not data:
            return
        status_str = f"CPU:{data['cpu']}% RAM:{data['ram']}%".encode("ascii")[:32]
        status_fixed = status_str.ljust(32, b"\0")
        packet = struct.pack(
            "<BBB H 32s B",
            0xB5,
            int(data["cpu"]),
            0,  # GPU placeholder
            int(data["ram"]),
            status_fixed,
            0x00,
        )
        self._safe_write(packet)

    def send_com_list_packet(self):
        ports = serial.tools.list_ports.comports()
        hmi_port = self.ser.port if self.ser else None
        # Solo mostrar puertos de placas de desarrollo, excluir Bluetooth y virtuales
        real_ports = [
            p.device for p in ports
            if p.device != hmi_port and _is_dev_board(p)
        ]
        # Canales Python virtuales siempre primero en la lista
        available = ["PY CH0", "PY CH1", "PY CH2", "PY CH3"] + real_ports
        ports_str = ",".join(available).encode("ascii")[:255]
        ports_fixed = ports_str.ljust(255, b"\0")
        packet = struct.pack(
            "<BBB 255s B",
            0xC5,
            len(available),
            0,
            ports_fixed,
            0x00,
        )
        self._safe_write(packet)

    # ------------------------------------------------------------------
    # Lectura de respuestas de la pantalla
    # ------------------------------------------------------------------

    def _handle_text_command(self, data: str):
        """Procesa una lÃ­nea ASCII recibida del ESP32 (lÃ³gica extraÃ­da de read_from_sidecar)."""
        if not data:
            return
        if data.startswith("SENTINEL:"):
            cmd = data.split(":")[1].strip()
            if cmd == "REC_START":
                self.file_logger.start_recording()
                self._safe_write(b"S:REC\n")
            elif cmd == "REC_STOP":
                self.file_logger.stop_recording()
                self._safe_write(b"S:STOP\n")
            elif cmd == "RELEASE_PORT":
                self.release_port()
            elif cmd == "RECONNECT_PORT":
                self.reconnect_port()
            return
        if data.startswith("MONITOR:"):
            parts = data.split(":")
            if (len(parts) >= 3 and len(parts[1]) >= 2
                    and parts[1][0] == "T" and parts[1][1:].isdigit()):
                idx = int(parts[1][1:])
                port_name = ":".join(parts[2:]).strip()
                if port_name == "NONE":
                    self.stop_proxy_terminal(idx)
                elif port_name == "Python":
                    self.start_virtual_python_console_terminal(idx)
                else:
                    self.start_proxy_terminal(idx, port_name)
            else:
                port_to_open = parts[1].strip()
                if port_to_open == "Python":
                    self.start_virtual_python_console()
                else:
                    self.start_proxy(port_to_open)
            return

        # --- Protocolo librerÃ­a Arduino Sentinel ---

        # CMD:CSV_SCHEMA:filename:h1,h2,h3
        if data.startswith("CMD:CSV_SCHEMA:"):
            parts = data.split(":", 3)
            if len(parts) == 4:
                self.file_logger.register_csv_schema(parts[2], parts[3].split(","))
            return

        # CSV>filename:v1,v2,v3  o  CSV!filename
        if data.startswith("CSV>") or data.startswith("CSV!"):
            self._handle_csv_proxy(data)
            return

        # KV>filename:k=v,k2=v2
        if data.startswith("KV>"):
            self._handle_kv_proxy(data)
            return

        # JSON!filename
        if data.startswith("JSON!"):
            fname = data[5:].strip()
            if fname:
                self.file_logger.close_jsonl(fname)
            return

        # MET>name:value:unit
        if data.startswith("MET>"):
            self._handle_met_proxy(data)
            return

        # Linea de texto normal: detectar terminal y registrar en sesion
        terminal = 0
        msg = data
        t_match = re.match(r"^T(\d):", data)
        if t_match:
            terminal = int(t_match.group(1))
            msg = data[3:]

        level = "NORMAL"
        if "[ERR" in msg:
            level = "ERROR"
        elif "[WARN" in msg:
            level = "WARN"
        elif "[INFO" in msg:
            level = "INFO"

        self.session_mgr.add_line(terminal, msg, level)

    # ------------------------------------------------------------------
    # Recepcion BLE â€” callback llamado desde el hilo del event loop BLE
    # ------------------------------------------------------------------

    def _on_ble_data(self, raw: bytes):
        """
        Callback invocado por BLEChannel cuando llegan bytes del ESP32 por BLE.
        Implementa el mismo parser que _hmi_reader_loop pero opera sobre un
        buffer acumulativo porque las notificaciones BLE pueden llegar en
        fragmentos de cualquier tamano.
        """
        self._ble_rx_buf.extend(raw)

        # Procesar el buffer mientras haya datos completos
        while self._ble_rx_buf:
            b = self._ble_rx_buf[0]

            if b == MAGIC_MIC:
                # Paquete binario de mic: esperar MIC_CHUNK_BYTES bytes completos
                if len(self._ble_rx_buf) < 1 + MIC_CHUNK_BYTES:
                    break  # fragmentado, esperar mas datos
                mic_data = bytes(self._ble_rx_buf[1: 1 + MIC_CHUNK_BYTES])
                self._ble_rx_buf = self._ble_rx_buf[1 + MIC_CHUNK_BYTES:]
                if VOICE_ENABLED and self.voice:
                    self.voice.on_mic_chunk(mic_data)

            else:
                # Linea de texto ASCII: buscar \n
                newline_pos = self._ble_rx_buf.find(ord("\n"))
                if newline_pos == -1:
                    break  # linea incompleta, esperar mas datos
                line = self._ble_rx_buf[:newline_pos]
                self._ble_rx_buf = self._ble_rx_buf[newline_pos + 1:]
                text = line.decode("ascii", errors="ignore").strip()
                if text:
                    self._handle_text_command(text)

    def _on_ble_connected(self):
        """Callback invocado por BLEChannel cuando la conexion BLE queda establecida.
        ReenvÃ­a AtmosPacket inmediatamente para que la pantalla no espere hasta
        el prÃ³ximo ciclo de 600 s del weather_loop."""
        threading.Thread(target=self.send_atmos_packet, daemon=True).start()

    # ------------------------------------------------------------------
    # Lector HMI USB
    # ------------------------------------------------------------------

    def _hmi_reader_loop(self):
        """
        Hilo dedicado que lee continuamente del serial USB del ESP32.
        Despacha paquetes binarios 0xE5 (mic audio) al VoiceEngine
        y lineas ASCII al handler de comandos.
        Cuando el canal activo es BLE, este hilo queda suspendido en polling
        liviano â€” los datos llegan por _on_ble_data().
        """
        while self.running:
            # Si el canal activo es BLE, no hay nada que leer por USB
            if self._channel != "usb":
                time.sleep(0.5)
                continue

            if not self.ser or not self.ser.is_open:
                time.sleep(0.5)
                continue
            try:
                if self.ser.in_waiting == 0:
                    time.sleep(0.005)
                    continue

                first = self.ser.read(1)
                if not first:
                    continue
                b = first[0]

                if b == MAGIC_MIC:
                    # Paquete binario de mic: leer exactamente MIC_CHUNK_BYTES
                    data = b""
                    while len(data) < MIC_CHUNK_BYTES:
                        chunk = self.ser.read(MIC_CHUNK_BYTES - len(data))
                        if not chunk:
                            break
                        data += chunk
                    if len(data) == MIC_CHUNK_BYTES and VOICE_ENABLED and self.voice:
                        self.voice.on_mic_chunk(data)
                else:
                    # Linea de texto ASCII: acumular hasta \n
                    line = bytearray([b])
                    while True:
                        c = self.ser.read(1)
                        if not c or c[0] == ord("\n"):
                            break
                        line.append(c[0])
                    text = line.decode("ascii", errors="ignore").strip()
                    self._handle_text_command(text)

            except (serial.SerialException, OSError) as e:
                if not self._reconnecting:
                    # Error genuino de desconexion â€” intentar reconexion normal
                    print(f"[HMI-R] Error: {e}", flush=True)
                    self._trigger_reconnect()
                time.sleep(1)
            except Exception:
                pass

    def read_from_sidecar(self):
        if not self.ser or not self.ser.is_open:
            return
        try:
            if self.ser.in_waiting > 0:
                data = (
                    self.ser.read_until(b"\n")
                    .decode("ascii", errors="ignore")
                    .strip()
                )

                # Comandos SENTINEL: desde la pantalla
                if data.startswith("SENTINEL:"):
                    cmd = data.split(":")[1].strip()
                    if cmd == "REC_START":
                        self.file_logger.start_recording()
                        self._safe_write(b"S:REC\n")
                    elif cmd == "REC_STOP":
                        self.file_logger.stop_recording()
                        self._safe_write(b"S:STOP\n")
                    elif cmd == "RELEASE_PORT":
                        self.release_port()
                    elif cmd == "RECONNECT_PORT":
                        self.reconnect_port()
                    elif cmd.startswith("FILTER:"):
                        pass
                    return

                # Comandos MONITOR:
                if data.startswith("MONITOR:"):
                    parts = data.split(":")
                    # Multi-terminal: MONITOR:T{idx}:{port} o MONITOR:T{idx}:NONE
                    if len(parts) >= 3 and len(parts[1]) >= 2 and parts[1][0] == "T" and parts[1][1:].isdigit():
                        idx = int(parts[1][1:])
                        port_name = ":".join(parts[2:]).strip()
                        if port_name == "NONE":
                            self.stop_proxy_terminal(idx)
                        elif port_name == "Python":
                            self.start_virtual_python_console_terminal(idx)
                        else:
                            self.start_proxy_terminal(idx, port_name)
                    else:
                        # Legacy T0: MONITOR:{port}
                        port_to_open = parts[1].strip()
                        if port_to_open == "Python":
                            self.start_virtual_python_console()
                        else:
                            self.start_proxy(port_to_open)
        except (serial.SerialException, OSError) as e:
            print(f"[SERIAL] Error de lectura: {e}", flush=True)
            self._trigger_reconnect()
        except Exception:
            pass

    # ------------------------------------------------------------------
    # Proxy serial y consola virtual
    # ------------------------------------------------------------------

    def start_virtual_python_console(self):
        if self.target_ser and self.target_ser.is_open:
            self.target_ser.close()
        self.target_ser = None
        self.target_port = "Python"
        print(
            "[PROXY] Consola Python lista. Usa sentinel.logger o socket 9090.",
            flush=True,
        )
        self._safe_write(
            b"> [SENTINEL] Listo. Esperando mensajes por socket 9090...\n"
        )

    def start_proxy(self, port_desc: str):
        # Canal Python virtual (T0)
        if port_desc.startswith("PY CH"):
            if self.target_ser and self.target_ser.is_open:
                self.target_ser.close()
            self.target_ser = None
            self.target_port = port_desc
            self.session_mgr.terminal_names[0] = port_desc
            print(f"[PY] T0: {port_desc} activo", flush=True)
            return
        if self.target_ser and self.target_ser.is_open:
            self.target_ser.close()
        port = port_desc
        match = re.search(r"\((COM\d+)\)", port_desc)
        if match:
            port = match.group(1)
        try:
            self.target_ser = serial.Serial(port, 115200, timeout=0.1)
            self.target_port = port
            print(f"[PROXY] Iniciando monitoreo de {port}", flush=True)
            time.sleep(0.5)
            self._safe_write(f"> [PROXY] Conectado a {port}\n".encode("ascii"))
        except Exception as e:
            print(f"[PROXYERR] No se pudo abrir {port}: {e}", flush=True)
            self._safe_write(f"> [PROXYERR] Error en {port}\n".encode("ascii"))

    def start_proxy_terminal(self, idx: int, port_desc: str):
        """Abre un proxy serial para el terminal T{idx} (1-3). Cada linea se envia con prefijo T{idx}:."""
        # Canal Python virtual (T1-T3)
        if port_desc.startswith("PY CH"):
            self.stop_proxy_terminal(idx)  # cerrar proxy serial si habia
            if idx < len(self.session_mgr.terminal_names):
                self.session_mgr.terminal_names[idx] = port_desc
            print(f"[PY] T{idx}: {port_desc} activo", flush=True)
            return
        self.stop_proxy_terminal(idx)
        port = port_desc
        match = re.search(r"\((COM\d+)\)", port_desc)
        if match:
            port = match.group(1)
        try:
            ser = serial.Serial(port, 115200, timeout=0.1)
            self.proxy_sers[idx] = ser
            self.proxy_ports[idx] = port
            if idx < len(self.session_mgr.terminal_names):
                self.session_mgr.terminal_names[idx] = port
            print(f"[PROXY] T{idx}: Iniciando monitoreo de {port}", flush=True)
            time.sleep(0.5)
            self._safe_write(f"T{idx}:> [PROXY] Conectado a {port}\n".encode("ascii"))
        except Exception as e:
            print(f"[PROXYERR] T{idx}: No se pudo abrir {port}: {e}", flush=True)
            self._safe_write(f"T{idx}:> [PROXYERR] Error en {port}\n".encode("ascii"))

    def stop_proxy_terminal(self, idx: int):
        """Cierra el proxy del terminal T{idx} y notifica al ESP32."""
        ser = self.proxy_sers.pop(idx, None)
        port = self.proxy_ports.pop(idx, "?")
        if ser and ser.is_open:
            ser.close()
        if port != "?":
            print(f"[PROXY] T{idx}: Desconectado de {port}", flush=True)
            self._safe_write(f"T{idx}:> [PROXY] Desconectado de {port}\n".encode("ascii"))
        if idx < len(self.session_mgr.terminal_names):
            self.session_mgr.terminal_names[idx] = ""

    def start_virtual_python_console_terminal(self, idx: int):
        """Modo consola Python para terminal T{idx}."""
        self.stop_proxy_terminal(idx)
        if idx < len(self.session_mgr.terminal_names):
            self.session_mgr.terminal_names[idx] = "Python"
        print(f"[PROXY] T{idx}: Consola Python lista.", flush=True)
        self._safe_write(f"T{idx}:> [SENTINEL] Listo en T{idx}. Esperando mensajes...\n".encode("ascii"))

    def _handle_csv_proxy(self, line: str):
        """Parsea lineas CSV del microcontrolador:
          CSV>archivo:v1,v2,v3  â†’ escribe fila
          CSV!archivo            â†’ cierra archivo
        """
        if line.startswith("CSV!"):
            filename = line[4:].strip()
            if filename:
                self.file_logger.close_csv(filename)
            return
        # CSV>archivo:v1,v2,v3
        rest = line[4:]
        colon = rest.find(":")
        if colon < 0:
            return
        filename = rest[:colon].strip()
        values = rest[colon + 1:].split(",")
        self.file_logger.write_csv_values(filename, values)

    def _handle_kv_proxy(self, line: str):
        """Parsea 'KV>archivo:k=v,k2=v2' de un puerto COM proxy."""
        rest = line[3:]
        colon = rest.find(":")
        if colon < 0:
            return
        filename = rest[:colon].strip()
        data = _parse_kv_string(rest[colon + 1:])
        if filename and data:
            self.file_logger.write_jsonl(filename, data)

    def _handle_met_proxy(self, line: str, terminal: int = 0):
        """Parsea 'MET>name:value:unit' de un puerto COM proxy."""
        parts = line[4:].split(":", 2)
        if len(parts) >= 2:
            unit = parts[2] if len(parts) > 2 else ""
            name = parts[0].strip()
            value = parts[1].strip()
            unit = unit.strip()
            self.file_logger.log_metric(name, value, unit)
            unit_str = f" {unit}" if unit else ""
            display = f"[MET] {name} = {value}{unit_str}"
            self.session_mgr.add_line(terminal, display, "INFO")

    def proxy_loop(self):
        while self.running:
            # Chequear si release_port() pidio cerrar el puerto â€” hacerlo desde este hilo
            # garantiza que Windows libere el handle antes de que esptool intente abrirlo.
            if self._proxy_release_requested:
                self._proxy_release_requested = False
                if self.target_ser:
                    try:
                        self.target_ser.dtr = False
                        self.target_ser.rts = False
                    except Exception:
                        pass
                    try:
                        self.target_ser.close()
                    except Exception:
                        pass
                    del self.target_ser
                    self.target_ser = None
                for idx in list(self.proxy_sers.keys()):
                    ser = self.proxy_sers.pop(idx, None)
                    self.proxy_ports.pop(idx, None)
                    if ser:
                        try:
                            ser.dtr = False
                            ser.rts = False
                            ser.close()
                        except Exception:
                            pass
                        del ser
                ports_str = ", ".join(self._released_proxy_ports.values())
                print(f"[DAEMON] Puerto proxy '{ports_str}' liberado correctamente.", flush=True)
                self._safe_write(f"> [USB] {ports_str} liberado. Flashea cuando quieras.\n".encode("ascii"))
                time.sleep(0.1)
                continue

            # --- T0 legacy proxy (bytes crudos, sin prefijo) ---
            if self.target_ser and self.target_ser.is_open:
                try:
                    if self.target_ser.in_waiting > 0:
                        incoming = self.target_ser.read(self.target_ser.in_waiting)
                        # Reenviar al HMI tal cual (comportamiento original)
                        self._safe_write(incoming)
                        # Parsear lineas para session_mgr (grabacion y WebSocket)
                        self._t0_line_buf.extend(incoming)
                        while b"\n" in self._t0_line_buf:
                            nl = self._t0_line_buf.index(ord("\n"))
                            raw_line = self._t0_line_buf[:nl].decode("utf-8", errors="replace").strip()
                            self._t0_line_buf = self._t0_line_buf[nl + 1:]
                            if not raw_line:
                                continue
                            # Quitar prefijo T{n}: si lo hay (ej. T1:MET>...)
                            t_match = re.match(r"^T(\d):", raw_line)
                            terminal_idx = int(t_match.group(1)) if t_match else 0
                            cmd_line = raw_line[3:] if t_match else raw_line
                            # Comandos estructurados â€” no reenviar, ya se enviaron los bytes crudos
                            if cmd_line.startswith("CMD:CSV_SCHEMA:"):
                                parts = cmd_line.split(":", 3)
                                if len(parts) == 4:
                                    self.file_logger.register_csv_schema(parts[2], parts[3].split(","))
                                continue
                            if cmd_line.startswith("CMD:"):
                                continue  # otros CMD: ignorados (grabacion solo desde pantalla)
                            if cmd_line.startswith("CSV>") or cmd_line.startswith("CSV!"):
                                self._handle_csv_proxy(cmd_line)
                                continue
                            if cmd_line.startswith("KV>"):
                                self._handle_kv_proxy(cmd_line)
                                continue
                            if cmd_line.startswith("MET>"):
                                self._handle_met_proxy(cmd_line, terminal=terminal_idx)
                                continue
                            if cmd_line.startswith("JSON!"):
                                self.file_logger.close_jsonl(cmd_line[5:].strip())
                                continue
                            # Linea de texto â€” registrar en sesion (activa grabacion)
                            level = "NORMAL"
                            if "[ERR" in cmd_line:  level = "ERROR"
                            elif "[WARN" in cmd_line: level = "WARN"
                            elif "[INFO" in cmd_line: level = "INFO"
                            self.session_mgr.add_line(terminal_idx, cmd_line, level)
                except Exception:
                    print(f"[PROXYERR] Se perdio la conexion con {self.target_port}", flush=True)
                    self.target_ser.close()
                    self.target_ser = None
                    self._t0_line_buf.clear()

            # --- T1-T3 proxies (una linea por vez con prefijo T{idx}:) ---
            for idx in list(self.proxy_sers.keys()):
                ser = self.proxy_sers.get(idx)
                if not ser or not ser.is_open:
                    continue
                try:
                    if ser.in_waiting > 0:
                        raw = ser.read(ser.in_waiting)
                        text = raw.decode("utf-8", errors="replace")
                        for line in text.splitlines():
                            if not line:
                                continue
                            # Quitar prefijo T{n}: si lo hay
                            t_match = re.match(r"^T(\d):", line)
                            terminal_idx = int(t_match.group(1)) if t_match else idx
                            cmd_line = line[3:] if t_match else line
                            # Comandos estructurados â€” procesar pero NO reenviar a la pantalla
                            if cmd_line.startswith("CMD:CSV_SCHEMA:"):
                                parts = cmd_line.split(":", 3)
                                if len(parts) == 4:
                                    self.file_logger.register_csv_schema(parts[2], parts[3].split(","))
                                continue
                            if cmd_line.startswith("CMD:"):
                                continue  # otros CMD: ignorados (grabacion solo desde pantalla)
                            if cmd_line.startswith("CSV>") or cmd_line.startswith("CSV!"):
                                self._handle_csv_proxy(cmd_line)
                                continue
                            if cmd_line.startswith("KV>"):
                                self._handle_kv_proxy(cmd_line)
                                continue
                            if cmd_line.startswith("MET>"):
                                self._handle_met_proxy(cmd_line, terminal=terminal_idx)
                                continue
                            if cmd_line.startswith("JSON!"):
                                self.file_logger.close_jsonl(cmd_line[5:].strip())
                                continue
                            # Linea de texto â€” registrar en sesion y reenviar a pantalla
                            level = "NORMAL"
                            if "[ERR" in cmd_line:  level = "ERROR"
                            elif "[WARN" in cmd_line: level = "WARN"
                            elif "[INFO" in cmd_line: level = "INFO"
                            self.session_mgr.add_line(terminal_idx, cmd_line, level)
                            self._safe_write(f"T{terminal_idx}:{cmd_line}\n".encode("ascii", errors="replace"))
                except Exception:
                    port = self.proxy_ports.get(idx, "?")
                    print(f"[PROXYERR] T{idx}: Se perdio la conexion con {port}", flush=True)
                    try:
                        ser.close()
                    except Exception:
                        pass
                    self.proxy_sers.pop(idx, None)
                    self.proxy_ports.pop(idx, None)
                    self._safe_write(f"T{idx}:> [PROXYERR] Conexion perdida con {port}\n".encode("ascii"))

            time.sleep(0.01)

    # ------------------------------------------------------------------
    # Bucles de hilos
    # ------------------------------------------------------------------

    def run(self):
        # --- Servidor IPC en su propio hilo ---
        ipc = IPCServer(self)
        threading.Thread(target=ipc.serve, daemon=True).start()

        # --- REST API en su propio hilo ---
        api = SentinelAPI(self)
        api.start()

        # --- WebSocket streaming server ---
        ws = WebSocketServer(self.session_mgr)
        ws.start()

        def weather_loop():
            while self.running:
                self.send_atmos_packet()
                time.sleep(600)

        def sys_loop():
            while self.running:
                data = self.system.get_metrics()
                self.send_sys_packet()
                if data and VOICE_ENABLED and self.voice:
                    self.voice.evaluate_alerts({
                        "cpu":  float(data.get("cpu", 0)),
                        "ram":  float(data.get("ram", 0)),
                        "temp": float(data.get("temp", 0)),
                    })
                time.sleep(1)

        def com_loop():
            while self.running:
                self.send_com_list_packet()
                time.sleep(3)

        threading.Thread(target=weather_loop, daemon=True).start()
        threading.Thread(target=sys_loop, daemon=True).start()
        threading.Thread(target=com_loop, daemon=True).start()
        threading.Thread(target=self.proxy_loop, daemon=True).start()

        threading.Thread(target=self._hmi_reader_loop, daemon=True).start()
        if VOICE_ENABLED and self.voice:
            self.voice.start_watchdog()

        try:
            while self.running:
                time.sleep(1)
        except KeyboardInterrupt:
            self.running = False
            ipc.close()
            api.shutdown()
            ws.shutdown()
            self.file_logger.close_all()
            if self._ble:
                self._ble.stop()
            print("[DAEMON] Apagando...", flush=True)


if __name__ == "__main__":
    SentinelDaemon().run()

