# sentinel_voice.py — Motor de voz para Sentinel HMI
# Autor: Ignacio Aguilera
#
# Pipeline completo:
#   INMP441 mic → 0xE5 chunks → Whisper STT → intent router
#   → Gemini CLI (con log context) → ElevenLabs TTS → 0xD5 chunks → speaker
#
# Configurar las tres API keys antes de arrancar el daemon.

import threading
import time
import wave
import io
import re
import subprocess
import statistics
from collections import deque

# ── Configuración de API keys ─────────────────────────────────────────────────
OPENAI_API_KEY     = ""                           # Whisper STT
ELEVENLABS_KEY     = ""                           # TTS
ELEVENLABS_VOICE   = "21m00Tcm4TlvDq8ikWAM"      # Rachel — neutral, buen español
GEMINI_MODEL       = "gemini-2.0-flash"           # Modelo de Gemini CLI

# ── Constantes de protocolo ───────────────────────────────────────────────────
MAGIC_MIC          = 0xE5
MAGIC_AUDIO        = 0xD5
MAGIC_VOICE_STATE  = 0xF5

VOICE_IDLE         = 0
VOICE_LISTENING    = 1
VOICE_THINKING     = 2
VOICE_SPEAKING     = 3

MIC_SAMPLE_RATE    = 8000
MIC_CHUNK_SAMPLES  = 512
MIC_CHUNK_BYTES    = MIC_CHUNK_SAMPLES * 2   # 1024 bytes int16 mono

AUDIO_SAMPLE_RATE  = 16000
AUDIO_CHUNK_SAMPLES = 512
AUDIO_CHUNK_BYTES  = AUDIO_CHUNK_SAMPLES * 2  # 1024 bytes int16 mono

WAKE_WORD          = "sentinel"


# ─────────────────────────────────────────────────────────────────────────────
# AlertRule
# ─────────────────────────────────────────────────────────────────────────────

class AlertRule:
    """Regla de alerta creada por voz. Evaluada en cada sys_loop."""

    def __init__(self, metric: str, direction: str, threshold: float, description: str):
        self.metric      = metric       # "cpu" | "ram" | "temp"
        self.direction   = direction    # "above" | "below"
        self.threshold   = threshold
        self.description = description  # texto para confirmación TTS
        self.active      = True
        self.triggered   = False        # evitar repetición de alertas


# ─────────────────────────────────────────────────────────────────────────────
# VoiceEngine
# ─────────────────────────────────────────────────────────────────────────────

class VoiceEngine:
    """
    Pipeline de voz completo para Sentinel HMI.

    Integración con el daemon:
      - Llamar on_mic_chunk(data) por cada paquete 0xE5 recibido del ESP32.
      - Llamar evaluate_alerts(sys_data) en cada sys_loop.
      - Llamar start_watchdog() una vez desde daemon.run().
    """

    def __init__(self, daemon):
        self.daemon       = daemon          # referencia a SentinelDaemon
        self.state        = VOICE_IDLE
        self.silent_mode  = False           # True → sin TTS, solo visual
        self.guardian_mode = False

        # Buffer de audio del mic
        self._mic_buf       = bytearray()
        self._mic_lock      = threading.Lock()
        self._last_mic_time = 0.0
        self._silence_fired = False         # para no disparar on_mic_silence dos veces

        # Alertas
        self.alert_rules: list[AlertRule] = []
        self._alert_lock  = threading.Lock()

        # Historial para modo guardián (últimos 60 valores)
        self._cpu_hist = deque(maxlen=60)
        self._ram_hist = deque(maxlen=60)
        self._guardian_cpu_alerted = False
        self._guardian_ram_alerted = False

        # Sesión de debug activa
        self._session_name: str | None = None

        # Evitar hablar mientras ya hay un TTS en curso
        self._speaking_lock = threading.Lock()

    # ── Estado y feedback visual (0xF5) ──────────────────────────────────────

    def _set_state(self, state: int):
        self.state = state
        self.daemon._safe_write(bytes([MAGIC_VOICE_STATE, state]))

    # ── Recepción de chunks del mic (llamado por _hmi_reader_loop del daemon) ─

    def on_mic_chunk(self, pcm_bytes: bytes):
        """Acumula un chunk PCM de 1024 bytes. Solo acepta cuando no estamos hablando."""
        if self.state == VOICE_SPEAKING:
            return
        with self._mic_lock:
            self._mic_buf.extend(pcm_bytes)
            self._last_mic_time = time.time()
            self._silence_fired = False
        if self.state != VOICE_LISTENING:
            self._set_state(VOICE_LISTENING)

    # ── Watchdog de silencio (hilo independiente) ─────────────────────────────

    def start_watchdog(self):
        """Lanzar una vez desde daemon.run(). Detecta fin de utterance por ausencia de chunks."""
        threading.Thread(target=self._silence_watchdog, daemon=True).start()

    def _silence_watchdog(self):
        while True:
            time.sleep(0.3)
            if self.state != VOICE_LISTENING:
                continue
            with self._mic_lock:
                elapsed   = time.time() - self._last_mic_time
                has_audio = len(self._mic_buf) >= MIC_CHUNK_BYTES * 2
                fired     = self._silence_fired
            if elapsed > 0.8 and has_audio and not fired:
                with self._mic_lock:
                    self._silence_fired = True
                    audio = bytes(self._mic_buf)
                    self._mic_buf.clear()
                threading.Thread(
                    target=self._process_utterance, args=(audio,), daemon=True
                ).start()

    # ── STT: Whisper ──────────────────────────────────────────────────────────

    def _transcribe(self, pcm_bytes: bytes) -> str:
        from openai import OpenAI
        # Empaquetar PCM crudo como WAV en memoria
        wav_buf = io.BytesIO()
        with wave.open(wav_buf, "wb") as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)
            wf.setframerate(MIC_SAMPLE_RATE)
            wf.writeframes(pcm_bytes)
        wav_buf.seek(0)
        client = OpenAI(api_key=OPENAI_API_KEY)
        result = client.audio.transcriptions.create(
            model="whisper-1",
            file=("audio.wav", wav_buf, "audio/wav"),
            language="es",
        )
        return result.text.strip()

    # ── Procesamiento de utterance completo ───────────────────────────────────

    def _process_utterance(self, audio: bytes):
        self._set_state(VOICE_THINKING)
        print(f"[VOICE] Transcribiendo {len(audio)//2} muestras de audio...", flush=True)
        try:
            text = self._transcribe(audio)
        except Exception as e:
            print(f"[VOICE] Error Whisper: {e}", flush=True)
            self._set_state(VOICE_IDLE)
            return

        print(f"[VOICE] Transcripción: '{text}'", flush=True)
        if not text:
            self._set_state(VOICE_IDLE)
            return

        # Verificar wake word
        t_lower = text.lower()
        pos = t_lower.find(WAKE_WORD)
        if pos == -1:
            print("[VOICE] Sin wake word — ignorado", flush=True)
            self._set_state(VOICE_IDLE)
            return

        # Extraer comando posterior al wake word
        command = text[pos + len(WAKE_WORD):].strip().lstrip(".,: ").strip()
        if not command:
            self._speak("¿Sí? ¿En qué puedo ayudarte?")
            return

        self._route(command)

    # ── Intent router ─────────────────────────────────────────────────────────

    def _route(self, text: str):
        t = text.lower().strip()
        print(f"[VOICE] Comando: '{t}'", flush=True)

        # ── Grabación ──────────────────────────────────────────────────────────
        if re.search(r"empie(za|sa).*(logg|graba)", t) or "inicia el loggeo" in t:
            self.daemon.file_logger.start_recording()
            self.daemon._safe_write(b"S:REC\n")
            self._speak("Grabación iniciada.")
            return

        if re.search(r"(detén|detiene|para|pausa).*(logg|grabac)", t):
            self.daemon.file_logger.stop_recording()
            self.daemon._safe_write(b"S:STOP\n")
            self._speak("Grabación detenida.")
            return

        # ── Sesión de debug ────────────────────────────────────────────────────
        m = re.search(r"abre (sesión|sesion)[:\s]+(.+)", t)
        if m:
            name = m.group(2).strip()
            self._session_name = name
            self.daemon.file_logger.start_recording(name)
            self.daemon._safe_write(b"S:REC\n")
            self._speak(f"Sesión {name} abierta.")
            return

        if re.search(r"(cierra|termina|finaliza).*(sesión|sesion)", t):
            name = self._session_name or "sesión actual"
            self.daemon.file_logger.stop_recording()
            self.daemon._safe_write(b"S:STOP\n")
            self._speak(f"Cerrando {name}. Generando resumen.")
            threading.Thread(target=self._summarize_session, args=(name,), daemon=True).start()
            self._session_name = None
            return

        # ── Modo guardián ──────────────────────────────────────────────────────
        if re.search(r"(activa|pon).*(guardián|guardian)", t):
            self.guardian_mode = True
            self._speak("Modo guardián activado. Te aviso si veo anomalías.")
            return

        if re.search(r"(desactiva|apaga).*(guardián|guardian)", t):
            self.guardian_mode = False
            self._speak("Modo guardián desactivado.")
            return

        # ── Alertas dinámicas ──────────────────────────────────────────────────
        rule = self._parse_alert(t)
        if rule:
            with self._alert_lock:
                self.alert_rules.append(rule)
            self._speak(f"Entendido. Te aviso si {rule.description}.")
            return

        if re.search(r"cancela.*(alerta|alertas)", t):
            with self._alert_lock:
                count = len(self.alert_rules)
                self.alert_rules.clear()
            self._speak(f"{count} alerta{'s' if count != 1 else ''} cancelada{'s' if count != 1 else ''}.")
            return

        # ── Modo silencio ──────────────────────────────────────────────────────
        if re.search(r"modo silenci", t):
            self.silent_mode = True
            self._speak("Modo silencio activado.")
            return

        if re.search(r"(desactiva|sal).*(silenci)", t):
            self.silent_mode = False
            self._speak("Modo silencio desactivado.")
            return

        # ── Estado actual ──────────────────────────────────────────────────────
        if re.search(r"qué estás (monitoreando|viendo|vigilando)", t):
            with self._alert_lock:
                rules = list(self.alert_rules)
            guardian_txt = "sí" if self.guardian_mode else "no"
            resp = f"Guardián: {guardian_txt}. Alertas activas: {len(rules)}."
            for r in rules:
                dir_txt = "supere" if r.direction == "above" else "baje de"
                resp += f" {r.metric.upper()} {dir_txt} {r.threshold}."
            self._speak(resp)
            return

        # ── Pregunta general → Gemini ──────────────────────────────────────────
        threading.Thread(target=self._ask_gemini, args=(text,), daemon=True).start()

    # ── Parser de alertas por voz ─────────────────────────────────────────────

    def _parse_alert(self, text: str) -> AlertRule | None:
        """Parsea: 'avísame si el CPU supera 90', 'alerta si la RAM baja de 500'."""
        m = re.search(
            r"(avísame|alértame|notifícame|dime).*?"
            r"(cpu|ram|temperatura|temp).*?"
            r"(supera|sube (de|por encima)|excede|baja de|cae (a|bajo))[\s]+(\d+\.?\d*)",
            text,
            re.IGNORECASE,
        )
        if not m:
            return None
        raw_metric = m.group(2).lower()
        metric = "cpu" if "cpu" in raw_metric else ("temp" if "temp" in raw_metric else "ram")
        direction = "above" if any(k in m.group(3).lower() for k in ("supera", "sube", "excede")) else "below"
        threshold = float(m.group(6))
        dir_txt = "supere" if direction == "above" else "baje de"
        desc = f"el {metric.upper()} {dir_txt} {threshold}"
        return AlertRule(metric, direction, threshold, desc)

    # ── Alert engine (llamado en sys_loop del daemon) ─────────────────────────

    def evaluate_alerts(self, sys_data: dict):
        """
        Evaluar reglas de alerta y modo guardián contra métricas actuales.
        sys_data: {"cpu": float, "ram": float, "temp": float}
        Llamar en cada sys_loop (cada 1s).
        """
        cpu = float(sys_data.get("cpu", 0))
        ram = float(sys_data.get("ram", 0))
        temp = float(sys_data.get("temp", 0))
        metrics = {"cpu": cpu, "ram": ram, "temp": temp}

        # ── Reglas explícitas ──────────────────────────────────────────────
        with self._alert_lock:
            rules = list(self.alert_rules)

        for rule in rules:
            val = metrics.get(rule.metric, 0)
            fired = (
                (rule.direction == "above" and val >= rule.threshold) or
                (rule.direction == "below" and val <= rule.threshold)
            )
            if fired and not rule.triggered:
                rule.triggered = True
                dir_txt = "superó" if rule.direction == "above" else "bajó de"
                msg = (f"Alerta: {rule.metric.upper()} {dir_txt} {rule.threshold}. "
                       f"Valor actual: {val:.1f}.")
                print(f"[ALERT] {msg}", flush=True)
                threading.Thread(target=self._speak, args=(msg,), daemon=True).start()
            elif not fired:
                rule.triggered = False

        # ── Modo guardián: detección de spikes estadísticos ───────────────
        if self.guardian_mode:
            self._cpu_hist.append(cpu)
            self._ram_hist.append(ram)
            self._guardian_check(cpu, ram)

    def _guardian_check(self, cpu: float, ram: float):
        if len(self._cpu_hist) < 10:
            return
        cpu_mean  = statistics.mean(self._cpu_hist)
        cpu_stdev = statistics.stdev(self._cpu_hist) if len(self._cpu_hist) > 1 else 0
        if cpu_stdev > 2 and (cpu - cpu_mean) > 2.5 * cpu_stdev:
            if not self._guardian_cpu_alerted:
                self._guardian_cpu_alerted = True
                msg = (f"Atención: spike de CPU. Actual: {cpu:.0f}%, "
                       f"media: {cpu_mean:.0f}%.")
                threading.Thread(target=self._speak, args=(msg,), daemon=True).start()
        else:
            self._guardian_cpu_alerted = False

        ram_mean  = statistics.mean(self._ram_hist)
        ram_stdev = statistics.stdev(self._ram_hist) if len(self._ram_hist) > 1 else 0
        if ram_stdev > 5 and (ram - ram_mean) > 2.5 * ram_stdev:
            if not self._guardian_ram_alerted:
                self._guardian_ram_alerted = True
                msg = f"Atención: spike de RAM. Actual: {ram:.0f} MB, media: {ram_mean:.0f} MB."
                threading.Thread(target=self._speak, args=(msg,), daemon=True).start()
        else:
            self._guardian_ram_alerted = False

    # ── Gemini ────────────────────────────────────────────────────────────────

    def _ask_gemini(self, question: str):
        """Envía pregunta a Gemini con log context y responde por bocina."""
        log_lines = self.daemon.session_mgr.get_all_lines(n=120)
        log_text  = "\n".join(f"[{e['ts']}] {e['msg']}" for e in log_lines)
        prompt = (
            "Eres Sentinel, asistente de debugging para desarrolladores embebidos. "
            "Tienes acceso a los logs en tiempo real del sistema a continuación.\n\n"
            f"--- LOGS ---\n{log_text}\n--- FIN LOGS ---\n\n"
            f"Pregunta: {question}\n\n"
            "Responde de forma breve y directa (máximo 2-3 oraciones). "
            "Responde en español. Si no hay información suficiente en los logs, dilo."
        )
        answer = self._gemini_cli(prompt)
        self._speak(answer)

    def _summarize_session(self, name: str):
        """Resumen de sesión de debug via Gemini."""
        log_lines = self.daemon.session_mgr.get_all_lines(n=200)
        log_text  = "\n".join(f"[{e['ts']}] {e['msg']}" for e in log_lines)
        prompt = (
            f"Eres Sentinel. Resume la sesión de debug '{name}':\n\n"
            f"{log_text}\n\n"
            "Da un resumen de: qué se monitoreó, anomalías encontradas, y conclusión. "
            "Máximo 3 oraciones. En español."
        )
        answer = self._gemini_cli(prompt)
        self._speak(f"Resumen de {name}: {answer}")

    def _gemini_cli(self, prompt: str) -> str:
        """Llama gemini CLI. Retorna texto de respuesta o mensaje de error."""
        try:
            result = subprocess.run(
                ["gemini", "--model", GEMINI_MODEL, "--prompt", prompt],
                capture_output=True,
                text=True,
                timeout=30,
                encoding="utf-8",
                errors="replace",
            )
            if result.returncode == 0 and result.stdout.strip():
                return result.stdout.strip()
            print(f"[VOICE] Gemini stderr: {result.stderr[:300]}", flush=True)
            return "No pude obtener respuesta de Gemini."
        except subprocess.TimeoutExpired:
            return "Gemini tardó demasiado. Intenta de nuevo."
        except FileNotFoundError:
            return "El CLI de Gemini no está instalado."
        except Exception as e:
            return f"Error al consultar Gemini: {e}"

    # ── ElevenLabs TTS ────────────────────────────────────────────────────────

    def _tts_to_pcm(self, text: str) -> bytes | None:
        """Convierte texto a PCM int16 mono 16kHz usando ElevenLabs."""
        try:
            import requests
            from pydub import AudioSegment

            url = f"https://api.elevenlabs.io/v1/text-to-speech/{ELEVENLABS_VOICE}"
            headers = {
                "xi-api-key": ELEVENLABS_KEY,
                "Content-Type": "application/json",
                "Accept": "audio/mpeg",
            }
            body = {
                "text": text,
                "model_id": "eleven_multilingual_v2",
                "voice_settings": {"stability": 0.5, "similarity_boost": 0.75},
            }
            r = requests.post(url, json=body, headers=headers, timeout=15)
            r.raise_for_status()

            audio = AudioSegment.from_mp3(io.BytesIO(r.content))
            audio = (
                audio
                .set_frame_rate(AUDIO_SAMPLE_RATE)
                .set_channels(1)
                .set_sample_width(2)
            )
            return audio.raw_data
        except Exception as e:
            print(f"[VOICE] TTS error: {e}", flush=True)
            return None

    # ── Envío de audio al speaker (0xD5 con pacing) ───────────────────────────

    def _speak(self, text: str):
        """No bloqueante. Lanza TTS en hilo separado."""
        if self.silent_mode:
            print(f"[VOICE][SILENCIO] {text}", flush=True)
            self._set_state(VOICE_IDLE)
            return
        print(f"[VOICE] Hablando: {text}", flush=True)
        threading.Thread(target=self._speak_blocking, args=(text,), daemon=True).start()

    def _speak_blocking(self, text: str):
        with self._speaking_lock:
            self._set_state(VOICE_SPEAKING)
            pcm = self._tts_to_pcm(text)
            if not pcm:
                self._set_state(VOICE_IDLE)
                return

            interval = AUDIO_CHUNK_SAMPLES / AUDIO_SAMPLE_RATE  # s/chunk = 32ms
            pkt = bytearray(1 + AUDIO_CHUNK_BYTES)
            pkt[0] = MAGIC_AUDIO
            offset = 0
            chunks_sent = 0
            t0 = time.time()

            while offset < len(pcm):
                chunk = pcm[offset:offset + AUDIO_CHUNK_BYTES]
                if len(chunk) < AUDIO_CHUNK_BYTES:
                    chunk = chunk + bytes(AUDIO_CHUNK_BYTES - len(chunk))
                pkt[1:] = chunk
                self.daemon._safe_write(bytes(pkt))
                chunks_sent += 1
                offset += AUDIO_CHUNK_BYTES

                # Pacing por tiempo de pared — evita slow-motion
                target = t0 + chunks_sent * interval
                remaining = target - time.time()
                if remaining > 0:
                    time.sleep(remaining)

            self._set_state(VOICE_IDLE)
