# ble_channel.py — Canal BLE para Sentinel Monitor (Nordic UART Service)
# Autor: Ignacio Aguilera
#
# Gestiona la conexion BLE con la pantalla ESP32-S3 (Sentinel-HMI) como
# canal de comunicacion alternativo cuando USB no esta disponible.
#
# Dependencia: bleak>=0.21.0  (pip install bleak)
# Backend en Windows: WinRT (no requiere drivers adicionales en Win10/11)

import threading
import time
import asyncio
import logging
from typing import Callable, Optional

# Importacion condicional — el daemon arranca sin BLE si bleak no esta instalado
try:
    from bleak import BleakClient, BleakScanner
    from bleak.exc import BleakError, BleakDBusError
    BLEAK_AVAILABLE = True
except ImportError:
    BLEAK_AVAILABLE = False

# ---------------------------------------------------------------------------
# Constantes del Nordic UART Service (NUS)
# ---------------------------------------------------------------------------

NUS_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
NUS_TX_UUID      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  # ESP32 → PC (notify)
NUS_RX_UUID      = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  # PC → ESP32 (write)

DEVICE_NAME      = "Sentinel-HMI"
SCAN_TIMEOUT_S   = 8.0    # segundos buscando dispositivo BLE
MTU_REQUEST      = 247    # MTU grande para reducir fragmentacion (max BLE 5)
WRITE_CHUNK      = 20     # bytes por fragmento si el MTU negociado es pequeno

# Magics que NO se envian por BLE (audio PCM — demasiado ancho de banda)
_BLE_SKIP_MAGICS = {0xD5}

# ---------------------------------------------------------------------------
# BLEChannel
# ---------------------------------------------------------------------------

class BLEChannel:
    """
    Canal BLE sobre Nordic UART Service para comunicarse con Sentinel-HMI.

    El daemon (sentinel_daemon.py) es sync/threading. bleak es async.
    Esta clase gestiona un asyncio event loop en un hilo daemon dedicado
    y expone una API completamente sincrona hacia el exterior.

    Uso tipico:
        ch = BLEChannel(on_data_callback)
        ch.start()                     # inicia hilo BLE
        ch.write(packet_bytes)         # thread-safe
        connected = ch.is_connected()
        ch.stop()
    """

    def __init__(self, data_callback: Callable[[bytes], None],
                 connect_callback: Optional[Callable[[], None]] = None):
        """
        data_callback:   funcion llamada con los bytes crudos recibidos del ESP32.
        connect_callback: funcion opcional llamada cada vez que BLE conecta.
                          Util para reenviar paquetes iniciales (ej. AtmosPacket)
                          que pudieron haberse descartado antes de que conectara.
        """
        if not BLEAK_AVAILABLE:
            raise RuntimeError(
                "[BLE] bleak no esta instalado. "
                "Ejecutar: pip install bleak>=0.21.0"
            )

        self._data_callback = data_callback
        self._connect_callback = connect_callback
        self._client: Optional[BleakClient] = None
        self._loop: Optional[asyncio.AbstractEventLoop] = None
        self._thread: Optional[threading.Thread] = None
        self._connected = False
        self._stop_event = threading.Event()
        self._write_lock = threading.Lock()   # protege writes concurrentes
        self._ble_addr: Optional[str] = None  # direccion MAC/UUID del dispositivo
        self._mtu: int = WRITE_CHUNK          # MTU efectivo (actualizado post-conexion)

    # ------------------------------------------------------------------
    # API publica — thread-safe, sincrona
    # ------------------------------------------------------------------

    def start(self):
        """Arranca el hilo del event loop BLE y comienza a buscar el dispositivo."""
        if self._thread and self._thread.is_alive():
            return
        self._stop_event.clear()
        self._thread = threading.Thread(
            target=self._run_loop, name="BLE-EventLoop", daemon=True
        )
        self._thread.start()

    def stop(self):
        """Detiene el canal BLE de forma limpia."""
        self._stop_event.set()
        if self._loop and self._loop.is_running():
            asyncio.run_coroutine_threadsafe(self._async_disconnect(), self._loop)
        if self._thread:
            self._thread.join(timeout=5)
        self._connected = False
        print("[BLE] Canal detenido.", flush=True)

    def write(self, data: bytes) -> bool:
        """
        Envia bytes al ESP32 por la caracteristica NUS RX.
        - Retorna True si se envio, False si no hay conexion o fue bloqueado.
        - Descarta silenciosamente paquetes cuyo primer byte esta en _BLE_SKIP_MAGICS.
        - Thread-safe.
        """
        if not data:
            return False
        # Filtrar paquetes de audio (0xD5) — BLE no tiene ancho de banda suficiente
        if data[0] in _BLE_SKIP_MAGICS:
            return False
        if not self._connected or not self._loop:
            return False

        # Fire-and-forget: no bloquear el hilo llamante esperando el ACK.
        # El event loop BLE procesa las escrituras en secuencia por si solo.
        # Errores de write se loguean desde _async_write via callback.
        asyncio.run_coroutine_threadsafe(self._async_write(bytes(data)), self._loop)
        return True

    def is_connected(self) -> bool:
        """Retorna True si hay una conexion BLE activa y estable."""
        return self._connected and self._client is not None and self._client.is_connected

    # ------------------------------------------------------------------
    # Hilo del event loop asyncio
    # ------------------------------------------------------------------

    def _run_loop(self):
        """Punto de entrada del hilo BLE. Gestiona el event loop asyncio."""
        self._loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self._loop)
        try:
            self._loop.run_until_complete(self._main_async())
        except Exception as e:
            print(f"[BLE] Error fatal en event loop: {e}", flush=True)
        finally:
            try:
                self._loop.close()
            except Exception:
                pass
            self._connected = False

    # ------------------------------------------------------------------
    # Logica async principal
    # ------------------------------------------------------------------

    async def _main_async(self):
        """Bucle principal: conectar → operar → reconectar si se pierde."""
        while not self._stop_event.is_set():
            try:
                connected = await self._scan_and_connect()
                if connected:
                    # Mantener la conexion viva; la desconexion la detecta el callback
                    await self._keep_alive()
                else:
                    # No se encontro el dispositivo — esperar antes de reintentar
                    print(
                        f"[BLE] {DEVICE_NAME} no encontrado. Reintentando en 10s...",
                        flush=True,
                    )
                    await asyncio.sleep(10)
            except Exception as e:
                print(f"[BLE] Error en conexion: {e}. Reintentando en 5s...", flush=True)
                self._connected = False
                await asyncio.sleep(5)

    async def _scan_and_connect(self) -> bool:
        """Escanea BLE, localiza Sentinel-HMI y establece conexion NUS."""
        print(f"[BLE] Escaneando BLE ({SCAN_TIMEOUT_S}s)...", flush=True)

        # Buscar por UUID del servicio NUS en lugar de por nombre.
        # El paquete de advertising primario (31 bytes) se llena con el UUID
        # de 128 bits del NUS, dejando espacio insuficiente para el nombre
        # completo "Sentinel-HMI". El nombre va en el scan response (SCAN_RSP),
        # pero algunos backends no lo exponen antes de conectar. Filtrar por
        # UUID del servicio es mas robusto y no depende del nombre truncado.
        device = await BleakScanner.find_device_by_filter(
            lambda d, ad: NUS_SERVICE_UUID.lower() in [
                str(u).lower() for u in (ad.service_uuids or [])
            ],
            timeout=SCAN_TIMEOUT_S,
        )
        if device is None:
            # Fallback: intentar por nombre en caso de que el UUID no aparezca
            # (p.ej. si el scan response aun no fue recibido)
            device = await BleakScanner.find_device_by_name(
                DEVICE_NAME, timeout=SCAN_TIMEOUT_S
            )
        if device is None:
            return False

        self._ble_addr = device.address
        found_name = device.name or DEVICE_NAME
        print(f"[BLE] Encontrado {found_name} en {self._ble_addr}", flush=True)

        # Conectar con handler de desconexion
        self._client = BleakClient(
            device,
            disconnected_callback=self._on_disconnected,
        )

        print(f"[BLE] Conectando a {self._ble_addr}...", flush=True)
        await self._client.connect()
        print("[BLE] Link establecido — iniciando setup GATT", flush=True)

        # Leer MTU negociado — en bleak >= 0.20 es una propiedad, no corrutina.
        # El backend WinRT de Windows negocia MTU automaticamente al conectar.
        try:
            negotiated = self._client.mtu_size   # propiedad sincrona
            self._mtu = max(20, negotiated - 3)
            print(f"[BLE] MTU: {negotiated} (payload={self._mtu}B)", flush=True)
        except Exception as e:
            self._mtu = WRITE_CHUNK
            print(f"[BLE] MTU fallback={self._mtu}B ({e})", flush=True)

        # Suscribirse a notificaciones del TX del ESP32 (ESP32 → PC)
        print("[BLE] Enviando start_notify (CCCD write)...", flush=True)
        try:
            await self._client.start_notify(NUS_TX_UUID, self._on_notify)
            print("[BLE] start_notify OK — notificaciones activas", flush=True)
        except Exception as e:
            print(f"[BLE] start_notify FAILED: {e}", flush=True)
            raise

        self._connected = True
        print(
            f"[BLE] Conectado a {DEVICE_NAME}  MTU={self._mtu + 3}  "
            f"(payload={self._mtu}B)",
            flush=True,
        )
        if self._connect_callback:
            try:
                self._connect_callback()
            except Exception as e:
                print(f"[BLE] Error en connect_callback: {e}", flush=True)
        return True

    async def _keep_alive(self):
        """Espera activa mientras la conexion esta viva. Sale al desconectarse."""
        while self._connected and not self._stop_event.is_set():
            await asyncio.sleep(0.5)

    async def _async_disconnect(self):
        """Desconecta limpiamente si hay un cliente activo."""
        if self._client and self._client.is_connected:
            try:
                await self._client.stop_notify(NUS_TX_UUID)
            except Exception:
                pass
            try:
                await self._client.disconnect()
            except Exception:
                pass
        self._connected = False

    async def _async_write(self, data: bytes):
        """Envia bytes al RX del ESP32. Fragmenta si excede el MTU negociado."""
        if not self._client or not self._client.is_connected:
            return  # conexion perdida — descartar silenciosamente

        chunk_size = self._mtu
        try:
            for offset in range(0, len(data), chunk_size):
                chunk = data[offset: offset + chunk_size]
                # response=False (Write Without Response) — sin round-trip ATT,
                # mucho menor latencia. El ESP32 soporta WRITE_NR en la RX char.
                await self._client.write_gatt_char(
                    NUS_RX_UUID, chunk, response=False
                )
        except Exception as e:
            if self._connected:
                print(f"[BLE] Error en write ({type(e).__name__}): {e}", flush=True)

    # ------------------------------------------------------------------
    # Callbacks async (llamados desde el event loop BLE)
    # ------------------------------------------------------------------

    def _on_notify(self, _sender, data: bytearray):
        """Llamado cuando el ESP32 envia datos por notificacion NUS TX."""
        try:
            self._data_callback(bytes(data))
        except Exception as e:
            print(f"[BLE] Error en data_callback: {e}", flush=True)

    def _on_disconnected(self, _client: BleakClient):
        """Llamado por bleak cuando la conexion BLE se pierde."""
        self._connected = False
        print(
            f"[BLE] Desconectado de {DEVICE_NAME}. "
            "Reconexion automatica activa...",
            flush=True,
        )
        # _main_async detectara _connected=False y reiniciara el ciclo de conexion
