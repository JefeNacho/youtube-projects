"""
audio_stream.py — Transmite audio del sistema a la pantalla CrowPanel ESP32-S3
Autor: Ignacio Aguilera

Captura el audio que se reproduce en Windows (WASAPI loopback) y lo envía
a la pantalla via USB Serial. El firmware lo reproduce en tiempo real por
el DAC I2S + amplificador integrado.

Requisitos:
    pip install sounddevice numpy scipy pyserial

IMPORTANTE: Cerrar sentinel_daemon.py antes de usar este script
            (ambos necesitan el mismo puerto COM de la pantalla).

Uso:
    python audio_stream.py                    # auto-detecta pantalla y usa audio por defecto
    python audio_stream.py --port COM5        # puerto COM específico
    python audio_stream.py --list-devices     # ver todos los dispositivos de audio
    python audio_stream.py --device 3         # forzar dispositivo de audio índice 3
"""

import sys
import time
import struct
import threading
import argparse
from collections import deque

import os
import serial
import serial.tools.list_ports
import numpy as np
import sounddevice as sd
import soundfile as sf
from scipy.signal import resample_poly
from math import gcd

# ── Protocolo (debe coincidir con audio_synth.h en el firmware) ───────────────
MAGIC_AUDIO       = 0xD5
AUDIO_CHUNK_SAMP  = 512       # muestras mono por paquete
AUDIO_SAMPLE_RATE = 16000     # Hz — igual que en firmware

HMI_VID = 0x1A86
HMI_PID = 0x7522


# ─────────────────────────────────────────────────────────────────────────────
# Utilidades
# ─────────────────────────────────────────────────────────────────────────────

def find_hmi_port() -> str | None:
    for p in serial.tools.list_ports.comports():
        if p.vid == HMI_VID and p.pid == HMI_PID:
            return p.device
    return None


def list_audio_devices():
    print("\nDispositivos de audio disponibles:\n")
    print(f"  {'Idx':>3}  {'Nombre':<50}  {'In':>3}  {'Out':>3}  {'Hz':>6}")
    print(f"  {'-'*3}  {'-'*50}  {'-'*3}  {'-'*3}  {'-'*6}")
    for i, d in enumerate(sd.query_devices()):
        marker = " ←" if i == sd.default.device[1] else ""
        print(f"  {i:>3}  {d['name']:<50}  {d['max_input_channels']:>3}  "
              f"{d['max_output_channels']:>3}  {int(d['default_samplerate']):>6}{marker}")
    print("\n  ← = salida por defecto (loopback de esta si no especificas --device)")
    print()


def find_loopback_device() -> int | None:
    """Intenta encontrar el device WASAPI loopback del output por defecto."""
    try:
        hostapis = sd.query_hostapis()
        wasapi_idx = next(
            (i for i, h in enumerate(hostapis) if "wasapi" in h["name"].lower()), None
        )
        if wasapi_idx is None:
            return None

        default_out_name = sd.query_devices(sd.default.device[1])["name"]
        for i, d in enumerate(sd.query_devices()):
            if d["hostapi"] == wasapi_idx and d["max_input_channels"] > 0:
                if default_out_name in d["name"] or d["name"] in default_out_name:
                    return i
    except Exception:
        pass
    return None


# ─────────────────────────────────────────────────────────────────────────────
# Streamer
# ─────────────────────────────────────────────────────────────────────────────

class AudioStreamer:
    def __init__(self, port: str, device_idx: int | None = None):
        self.port       = port
        self.device     = device_idx
        self.running    = False
        self._ser       = None
        self._ser_lock  = threading.Lock()

        # Stats
        self._chunks_sent    = 0
        self._chunks_dropped = 0
        self._bytes_sent     = 0
        self._t0             = 0.0

        # Buffer inter-hilo: captura → envío
        # Usamos deque limitado para que si el envío va lento, descartamos por el frente
        self._pcm_queue: deque[bytes] = deque(maxlen=16)

    # ── Conexión ──────────────────────────────────────────────────────────────

    def connect(self):
        # CRÍTICO: configurar DTR=False ANTES de abrir el puerto.
        # Si se abre primero y luego se baja DTR, el CH340 ya generó el pulso
        # de reset y el ESP32 arranca de cero → pantalla negra.
        self._ser           = serial.Serial()
        self._ser.port      = self.port
        self._ser.baudrate  = 460800
        self._ser.timeout   = 1.0
        self._ser.dtr       = False
        self._ser.rts       = False
        self._ser.open()
        time.sleep(0.3)
        print(f"  [OK] Puerto {self.port} abierto (sin reset al ESP32)")

    def close(self):
        self.running = False
        if self._ser and self._ser.is_open:
            self._ser.close()

    # ── Hilo de envío ─────────────────────────────────────────────────────────

    def _send_loop(self):
        """Hilo dedicado al envío — consume la cola de chunks PCM y los envía."""
        while self.running:
            if self._pcm_queue:
                chunk_bytes = self._pcm_queue.popleft()
                packet = bytes([MAGIC_AUDIO]) + chunk_bytes
                try:
                    with self._ser_lock:
                        self._ser.write(packet)
                    self._chunks_sent += 1
                    self._bytes_sent  += len(packet)
                except serial.SerialException as e:
                    print(f"\n[ERROR Serial] {e}")
                    self.running = False
                    return
            else:
                time.sleep(0.001)  # yield sin datos

    # ── Captura ───────────────────────────────────────────────────────────────

    def _find_device_and_rate(self) -> tuple:
        """Resuelve dispositivo de audio y tasa de muestreo.

        Estrategia:
          1. Si --device fue especificado, usarlo directamente.
          2. Buscar un dispositivo WASAPI de entrada (micrófono o Stereo Mix).
          3. Fallback al dispositivo de entrada por defecto del sistema.

        Para capturar audio del sistema (lo que suena en los parlantes), habilitar
        "Stereo Mix" en: Panel de Control → Sonido → Grabación → click derecho
        → Mostrar dispositivos deshabilitados → habilitar Stereo Mix.
        """
        if self.device is not None:
            dev  = self.device
            info = sd.query_devices(dev)
            src_rate = int(info["default_samplerate"])
            return dev, src_rate, None

        # Buscar "Stereo Mix" o "Mezcla estéreo" en WASAPI para loopback
        hostapis = sd.query_hostapis()
        wasapi_idx = next(
            (i for i, h in enumerate(hostapis) if "wasapi" in h["name"].lower()), None
        )
        if wasapi_idx is not None:
            for i, d in enumerate(sd.query_devices()):
                if d["hostapi"] == wasapi_idx and d["max_input_channels"] > 0:
                    name_lower = d["name"].lower()
                    if "stereo mix" in name_lower or "mezcla" in name_lower or "loopback" in name_lower:
                        src_rate = int(d["default_samplerate"])
                        print(f"  [AUTO] Detectado loopback: {d['name']} (device {i})")
                        return i, src_rate, None

        # Fallback: micrófono WASAPI o default del sistema
        if wasapi_idx is not None:
            for i, d in enumerate(sd.query_devices()):
                if d["hostapi"] == wasapi_idx and d["max_input_channels"] > 0:
                    src_rate = int(d["default_samplerate"])
                    print(f"  [AUTO] Usando micrófono WASAPI: {d['name']} (device {i})")
                    print(f"  [NOTA] Para capturar audio del sistema, habilita 'Stereo Mix'")
                    print(f"         en Panel de Control > Sonido > Grabacion")
                    return i, src_rate, None

        # Ultimo fallback: input por defecto
        input_idx  = sd.default.device[0]
        input_info = sd.query_devices(input_idx)
        src_rate   = int(input_info["default_samplerate"])
        print(f"  [AUTO] Usando input por defecto: {input_info['name']} (device {input_idx})")
        return input_idx, src_rate, None

    def stream(self):
        """Captura audio y llena la cola de envío. Bloquea hasta Ctrl+C."""
        dev, src_rate, wasapi_extra = self._find_device_and_rate()

        info = sd.query_devices(dev)
        channels = min(2, max(1, info["max_input_channels"]))
        name_lower = info["name"].lower()
        if "stereo mix" in name_lower or "mezcla" in name_lower or "loopback" in name_lower:
            mode_str = "loopback (audio del sistema)"
        else:
            mode_str = "input directo (micrófono)"
        print(f"  [OK] Audio: {info['name']}")
        print(f"       Modo: {mode_str}")
        print(f"       {src_rate} Hz {channels}ch → remuestreo a {AUDIO_SAMPLE_RATE} Hz mono")

        # Ratio de remuestreo
        g           = gcd(AUDIO_SAMPLE_RATE, src_rate)
        up, down    = AUDIO_SAMPLE_RATE // g, src_rate // g
        # Tamaño de bloque de entrada que produce exactamente AUDIO_CHUNK_SAMP muestras de salida
        block_in    = AUDIO_CHUNK_SAMP * down // up
        need_resamp = (src_rate != AUDIO_SAMPLE_RATE)

        print(f"       Chunk entrada: {block_in} frames  |  salida: {AUDIO_CHUNK_SAMP} samples  |  {AUDIO_CHUNK_SAMP * 1000 // AUDIO_SAMPLE_RATE} ms\n")
        print("  Transmitiendo... (Ctrl+C para detener)\n")

        self.running = True
        self._t0 = time.time()

        # Hilo de envío
        send_thread = threading.Thread(target=self._send_loop, daemon=True)
        send_thread.start()

        try:
            stream_kwargs = dict(
                device=dev,
                samplerate=src_rate,
                blocksize=block_in,
                dtype="float32",
                channels=channels,
            )

            with sd.InputStream(**stream_kwargs) as stream_in:
                while self.running:
                    raw_f32, overflowed = stream_in.read(block_in)

                    # Mezclar a mono
                    if raw_f32.shape[1] > 1:
                        mono_f32 = raw_f32.mean(axis=1)
                    else:
                        mono_f32 = raw_f32[:, 0]

                    # Remuestrear si necesario
                    if need_resamp:
                        mono_f32 = resample_poly(mono_f32, up, down).astype(np.float32)

                    # Asegurar exactamente AUDIO_CHUNK_SAMP muestras
                    if len(mono_f32) < AUDIO_CHUNK_SAMP:
                        mono_f32 = np.pad(mono_f32, (0, AUDIO_CHUNK_SAMP - len(mono_f32)))
                    elif len(mono_f32) > AUDIO_CHUNK_SAMP:
                        mono_f32 = mono_f32[:AUDIO_CHUNK_SAMP]

                    # Convertir a int16
                    np.clip(mono_f32, -1.0, 1.0, out=mono_f32)
                    pcm16 = (mono_f32 * 32767).astype(np.int16)

                    # Encolar (deque descarta automáticamente si está lleno)
                    self._pcm_queue.append(pcm16.tobytes())

                    # Mostrar stats
                    self._print_stats()

        except KeyboardInterrupt:
            print("\n\n  Deteniendo...")
        except Exception as e:
            print(f"\n  [ERROR] {e}")
        finally:
            self.running = False
            send_thread.join(timeout=1.0)
            self._print_final()

    # ── Stats ─────────────────────────────────────────────────────────────────

    def _print_stats(self):
        elapsed = time.time() - self._t0
        if elapsed < 0.5:
            return
        kbps = self._bytes_sent / elapsed / 1024
        q    = len(self._pcm_queue)
        print(f"\r  Chunks: {self._chunks_sent:6d}  |  {kbps:5.1f} KB/s  |  Cola: {q:2d}  |  {elapsed:.0f}s",
              end="", flush=True)

    def _print_final(self):
        elapsed = time.time() - self._t0
        kbps    = self._bytes_sent / max(elapsed, 0.001) / 1024
        print(f"\n\n  Resumen: {self._chunks_sent} chunks enviados  |  "
              f"{self._bytes_sent / 1024:.1f} KB  |  {kbps:.1f} KB/s promedio")


# ─────────────────────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────────────────────

def _load_audio_file(path: str) -> np.ndarray:
    """Carga un archivo de audio, lo convierte a mono float32 a AUDIO_SAMPLE_RATE."""
    print(f"  [LOAD] Leyendo: {path}")
    data, sr = sf.read(path, dtype="float32")
    print(f"         Original: {sr} Hz, {data.shape}, {len(data)/sr:.1f}s")

    # Stereo → mono
    if data.ndim == 2:
        data = data.mean(axis=1)
        print(f"         Stereo → mono")

    # Remuestrear a AUDIO_SAMPLE_RATE si es necesario
    if sr != AUDIO_SAMPLE_RATE:
        g = gcd(AUDIO_SAMPLE_RATE, sr)
        up, down = AUDIO_SAMPLE_RATE // g, sr // g
        print(f"         Remuestreando: {sr} → {AUDIO_SAMPLE_RATE} Hz (up={up}, down={down})")
        data = resample_poly(data, up, down).astype(np.float32)

    print(f"         Final: {AUDIO_SAMPLE_RATE} Hz, {len(data)} muestras, {len(data)/AUDIO_SAMPLE_RATE:.1f}s")
    return data


def _audio_to_chunks(data: np.ndarray) -> list[bytes]:
    """Divide audio float32 mono en chunks PCM int16 de AUDIO_CHUNK_SAMP muestras."""
    np.clip(data, -1.0, 1.0, out=data)
    pcm16 = (data * 32767).astype(np.int16)

    chunks = []
    for i in range(0, len(pcm16), AUDIO_CHUNK_SAMP):
        block = pcm16[i : i + AUDIO_CHUNK_SAMP]
        if len(block) < AUDIO_CHUNK_SAMP:
            block = np.pad(block, (0, AUDIO_CHUNK_SAMP - len(block)))
        chunks.append(block.tobytes())

    print(f"         {len(chunks)} chunks de {AUDIO_CHUNK_SAMP} muestras ({AUDIO_CHUNK_SAMP*1000//AUDIO_SAMPLE_RATE}ms c/u)")
    return chunks


# Ruta por defecto del audio de prueba (junto a este script)
_DEFAULT_TEST_AUDIO = os.path.join(os.path.dirname(os.path.abspath(__file__)), "audio_prueba.mp3")


def run_test_tone(port: str, audio_path: str = None):
    """Reproduce un audio de prueba: primero en la laptop, luego en la pantalla."""
    audio_path = audio_path or _DEFAULT_TEST_AUDIO
    print()
    print("  === MODO TEST AUDIO ===")
    print(f"  Archivo:     {audio_path}")
    print(f"  Sample rate: {AUDIO_SAMPLE_RATE} Hz (destino)")
    print(f"  Chunk:       {AUDIO_CHUNK_SAMP} muestras = {AUDIO_CHUNK_SAMP * 1000 // AUDIO_SAMPLE_RATE} ms")
    print()

    # ── PASO 1: Cargar y preparar audio ──────────────────────────────────────
    if not os.path.exists(audio_path):
        print(f"  [ERROR] Archivo no encontrado: {audio_path}")
        return

    print("  [1/3] Cargando audio...")
    audio_data = _load_audio_file(audio_path)
    chunks = _audio_to_chunks(audio_data)
    duration = len(audio_data) / AUDIO_SAMPLE_RATE
    print()

    # ── PASO 2: Abrir puerto serial ──────────────────────────────────────────
    print(f"  [2/3] Abriendo puerto {port}...")
    ser = serial.Serial()
    ser.port     = port
    ser.baudrate = 460800
    ser.timeout  = 1.0
    ser.dtr      = False
    ser.rts      = False
    ser.open()
    time.sleep(0.3)
    print(f"  [OK]  Puerto {port} abierto (sin reset al ESP32)")
    print(f"         Header:  0xD5  |  Payload: {AUDIO_CHUNK_SAMP * 2} bytes  |  Chunks: {len(chunks)} ({duration:.1f}s)")
    print()

    # ── PASO 3: Enviar a la pantalla ─────────────────────────────────────────
    print(f"  [3/3] Enviando audio a la pantalla... (Ctrl+C para detener)")
    print()

    chunks_sent = 0
    bytes_sent = 0
    total_chunks = len(chunks)
    interval = AUDIO_CHUNK_SAMP / AUDIO_SAMPLE_RATE  # 32ms por chunk
    t0 = time.time()

    try:
        for chunk_bytes in chunks:
            # Pacing basado en tiempo de pared — evita drift acumulado
            target = t0 + chunks_sent * interval
            now = time.time()
            if target > now:
                time.sleep(target - now)

            packet = bytes([MAGIC_AUDIO]) + chunk_bytes
            ser.write(packet)
            chunks_sent += 1
            bytes_sent += len(packet)

            pct = 100 * chunks_sent / total_chunks
            elapsed = time.time() - t0
            kbps = bytes_sent / max(elapsed, 0.001) / 1024
            print(f"\r  TX → Pantalla  |  {chunks_sent}/{total_chunks} ({pct:.0f}%)  |  {kbps:5.1f} KB/s  |  {elapsed:.1f}s",
                  end="", flush=True)

        print(f"\n\n  [OK] Audio completo enviado!")

    except KeyboardInterrupt:
        print("\n\n  Detenido por usuario.")
    except serial.SerialException as e:
        print(f"\n\n  [ERROR Serial] {e}")
    finally:
        elapsed = time.time() - t0
        kbps = bytes_sent / max(elapsed, 0.001) / 1024
        print(f"  Resumen: {chunks_sent}/{total_chunks} chunks  |  {bytes_sent / 1024:.1f} KB  |  {kbps:.1f} KB/s")
        ser.close()
        print(f"  Puerto {port} cerrado.")


def main():
    parser = argparse.ArgumentParser(
        description="Sentinel Audio Stream — Transmite audio del PC a la pantalla CrowPanel ESP32-S3",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Ejemplos:
  python audio_stream.py                    # auto-detectar todo
  python audio_stream.py --port COM5        # puerto específico
  python audio_stream.py --list-devices     # ver dispositivos de audio
  python audio_stream.py --device 3         # forzar dispositivo índice 3
        """
    )
    parser.add_argument("--port",         help="Puerto COM de la pantalla (auto-detecta por VID:PID si se omite)")
    parser.add_argument("--device", type=int, help="Índice del dispositivo de audio (ver --list-devices)")
    parser.add_argument("--list-devices",  action="store_true", help="Listar dispositivos de audio y salir")
    parser.add_argument("--test-tone",     action="store_true", help="Reproduce audio de prueba: primero en laptop, luego en la pantalla")
    parser.add_argument("--audio",          help="Archivo de audio para --test-tone (default: audio_prueba.mp3)")
    args = parser.parse_args()

    if args.list_devices:
        list_audio_devices()
        sys.exit(0)

    print()
    print("  Sentinel Audio Stream")
    print("  ══════════════════════════════════")
    print(f"  Formato: {AUDIO_SAMPLE_RATE} Hz · 16-bit · mono")
    print(f"  Chunk:   {AUDIO_CHUNK_SAMP} muestras = {AUDIO_CHUNK_SAMP * 1000 // AUDIO_SAMPLE_RATE} ms por paquete")
    print()

    # Detectar puerto
    port = args.port or find_hmi_port()
    if not port:
        print("  [ERROR] No se encontró la pantalla CrowPanel.")
        print("          Conecta la pantalla por USB o especifica el puerto con --port COMx")
        sys.exit(1)

    if args.test_tone:
        run_test_tone(port, args.audio)
    else:
        streamer = AudioStreamer(port, args.device)
        try:
            streamer.connect()
            streamer.stream()
        except serial.SerialException as e:
            print(f"\n  [ERROR] No se pudo abrir {port}: {e}")
            print("          ¿Está corriendo sentinel_daemon.py? Ciérralo primero.")
            sys.exit(1)
        finally:
            streamer.close()


if __name__ == "__main__":
    main()
