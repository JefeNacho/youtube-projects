"""
Verifica la grabacion REAL del ESP32-S3: pide al firmware que capture audio
directo de los DOS microfonos I2S (canal L y canal R) SIN NINGUN filtro ni
seleccion de canal (ver src/main.cpp::runAudioDump, que llama a
readStereoChunk() directamente) y lo trae por el puerto serie para guardarlo
como .wav estereo en herramientas/grabaciones_esp32/.

El canal L y el canal R quedan tanto en pistas separadas del mismo .wav como
en archivos mono independientes para escucharlos con facilidad.

El script solo graba y guarda; abre el .wav tu mismo con el reproductor que
prefieras para escucharlo.

Requiere haber subido el firmware de este proyecto y tener la placa conectada
por USB. La captura de 20 s usa el comando 'D' y transmite a 460800 baudios.

Uso:
    python dump_audio_esp32.py                 # usa COM6 a 115200 baudios
    python dump_audio_esp32.py --duracion-segundos 20
    python dump_audio_esp32.py --puerto COM7
    python dump_audio_esp32.py --repetir        # vuelve a volcar tras cada toma
"""

import argparse
import datetime
import sys
import time
from pathlib import Path

import numpy as np
import serial
from scipy.io import wavfile

PUERTO_DEFECTO = "COM6"
BAUDIOS_DEFECTO = 115200
BARRA_ANCHO = 40
ESCALA_RMS = 5000.0
VENTANA_RMS_MUESTRAS = 512
PASO_RMS_MUESTRAS = 256

MARCADORES_LISTO = ("Comandos:", "Sistema listo", "No hay plantilla")
TIMEOUT_ARRANQUE_S = 20  # incluye los 10s de calibracion de ruido ambiente al arrancar
TIMEOUT_CABECERA_S = 6
TIMEOUT_BINARIO_S = 5


def calcular_rms(muestras: np.ndarray) -> float:
    datos = muestras.astype(np.float64)
    if not datos.size:
        return 0.0
    if datos.size < VENTANA_RMS_MUESTRAS:
        datos -= np.mean(datos)
        return float(np.sqrt(np.mean(datos * datos)))

    rms_ventanas = []
    for inicio in range(0, datos.size - VENTANA_RMS_MUESTRAS + 1, PASO_RMS_MUESTRAS):
        ventana = datos[inicio:inicio + VENTANA_RMS_MUESTRAS]
        centrada = ventana - np.mean(ventana)
        rms_ventanas.append(np.mean(centrada * centrada))
    return float(np.sqrt(np.mean(rms_ventanas)))


def rms_ventana_maximo(muestras: np.ndarray) -> float:
    datos = muestras.astype(np.float64)
    if not datos.size:
        return 0.0
    if datos.size < VENTANA_RMS_MUESTRAS:
        datos -= np.mean(datos)
        return float(np.sqrt(np.mean(datos * datos)))

    maximo = 0.0
    for inicio in range(0, datos.size - VENTANA_RMS_MUESTRAS + 1, PASO_RMS_MUESTRAS):
        ventana = datos[inicio:inicio + VENTANA_RMS_MUESTRAS]
        centrada = ventana - np.mean(ventana)
        rms = float(np.sqrt(np.mean(centrada * centrada)))
        maximo = max(maximo, rms)
    return maximo


def barra_audio(etiqueta: str, rms: float, escala: float = ESCALA_RMS) -> str:
    llenado = int(np.clip(rms * BARRA_ANCHO / escala, 0, BARRA_ANCHO))
    return f"{etiqueta} [{'#' * llenado}{'.' * (BARRA_ANCHO - llenado)}] RMS={rms:6.0f}"


def esperar_arranque(ser: serial.Serial, verboso: bool) -> None:
    print("Esperando a que el ESP32 termine el arranque y la calibracion de ruido (10s)...")
    fin = time.time() + TIMEOUT_ARRANQUE_S
    while time.time() < fin:
        linea = ser.readline().decode(errors="replace").strip()
        if not linea:
            continue
        if verboso:
            print(f"  [ESP32] {linea}")
        if any(marca in linea for marca in MARCADORES_LISTO):
            print("ESP32 listo.")
            return
    print("AVISO: no se vio el mensaje de 'listo'; se intenta igualmente (puede que ya estuviera arrancado).")


def volcar_audio(
    ser: serial.Serial, verboso: bool, baudios: int, duracion_segundos: float
) -> tuple[int, np.ndarray]:
    ser.reset_input_buffer()
    if duracion_segundos == 20:
        print("La captura inicia ahora y dura 20 s; habla para probar ambos microfonos.", flush=True)
    ser.write(b"D" if duracion_segundos == 20 else b"d")

    # 1. Leer lineas de texto hasta encontrar la cabecera "AUDIODUMP <sr> <n_por_canal> <canales>"
    sample_rate = None
    n_muestras = None
    canales = 1
    fin = time.time() + max(TIMEOUT_CABECERA_S, duracion_segundos + 10)
    while time.time() < fin:
        linea = ser.readline().decode(errors="replace").strip()
        if not linea:
            continue
        if linea.startswith("AUDIODUMP_ERROR,"):
            raise IOError(f"El ESP32 no pudo iniciar la captura: {linea}")
        if linea.startswith("AUDIODUMP "):
            partes = linea.split()
            sample_rate = int(partes[1])
            n_muestras = int(partes[2])
            canales = int(partes[3]) if len(partes) > 3 else 1
            velocidad_binaria = int(partes[4]) if len(partes) > 4 else baudios
            break
        if verboso:
            print(f"  [ESP32] {linea}")

    if sample_rate is None or n_muestras is None:
        raise TimeoutError("No llego la cabecera AUDIODUMP del ESP32 (revisa el firmware y el puerto).")

    if velocidad_binaria != baudios:
        ser.write(b"A")
        respuesta = ser.readline().decode(errors="replace").strip()
        if respuesta != "AUDIODUMP_FAST":
            raise IOError(f"Respuesta inesperada al cambiar velocidad serie: {respuesta!r}")
        ser.baudrate = velocidad_binaria

    print(f"Recibiendo {n_muestras} muestras/canal a {sample_rate} Hz ({canales} canal(es), crudo sin filtrar)...")

    # 2. Leer exactamente el binario crudo (int16 little-endian, entrelazado L,R,L,R,... si hay 2 canales)
    # El timeout debe cubrir tanto la duracion de captura como la transferencia.
    bytes_esperados = n_muestras * canales * 2
    tiempo_transferencia_s = bytes_esperados / (velocidad_binaria / 10.0)
    tiempo_captura_s = n_muestras / sample_rate
    ser.timeout = max(
        TIMEOUT_BINARIO_S,
        tiempo_transferencia_s * 1.5 + 2,
        tiempo_captura_s * 1.5 + 5,
    )
    crudo = ser.read(bytes_esperados)
    if velocidad_binaria != baudios:
        ser.baudrate = baudios
    if len(crudo) != bytes_esperados:
        faltantes = bytes_esperados - len(crudo)
        if not 0 < faltantes <= 512:
            raise IOError(f"Se esperaban {bytes_esperados} bytes y llegaron {len(crudo)} (timeout o desconexion).")
        bytes_por_frame = canales * 2
        crudo = crudo[:len(crudo) - len(crudo) % bytes_por_frame]
        faltantes = bytes_esperados - len(crudo)
        crudo += bytes(faltantes)
        print(
            f"AVISO: faltaron {faltantes} bytes al final; se completo menos de 4 ms con silencio.",
            flush=True,
        )

    audio = np.frombuffer(crudo, dtype="<i2").copy()
    if canales > 1:
        audio = audio.reshape(-1, canales)

    # 3. Consumir el pie "AUDIODUMP_END" (informativo, no critico)
    fin = time.time() + 2
    while time.time() < fin:
        linea = ser.readline().decode(errors="replace").strip()
        if linea == "AUDIODUMP_END":
            break
        if linea and verboso:
            print(f"  [ESP32] {linea}")

    return sample_rate, audio


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--puerto", default=PUERTO_DEFECTO, help=f"Puerto serie del ESP32 (por defecto {PUERTO_DEFECTO})")
    parser.add_argument("--baudios", type=int, default=BAUDIOS_DEFECTO, help=f"Velocidad serie (por defecto {BAUDIOS_DEFECTO}, igual que monitor_speed)")
    parser.add_argument(
        "--duracion-segundos",
        type=float,
        choices=(1.5, 20.0),
        default=1.5,
        help="Duracion de la captura (20 s requiere el firmware actualizado)",
    )
    parser.add_argument("--repetir", action="store_true", help="Vuelve a pedir un volcado tras cada toma (Ctrl+C para salir)")
    parser.add_argument("--verboso", action="store_true", help="Muestra tambien los mensajes de log del ESP32")
    args = parser.parse_args()

    salida_dir = Path(__file__).parent / "grabaciones_esp32"
    salida_dir.mkdir(exist_ok=True)

    try:
        ser = serial.Serial(args.puerto, args.baudios, timeout=1)
    except serial.SerialException as error:
        print(f"No se pudo abrir {args.puerto}: {error}")
        sys.exit(1)

    try:
        esperar_arranque(ser, args.verboso)
        while True:
            sample_rate, audio = volcar_audio(
                ser, args.verboso, args.baudios, args.duracion_segundos
            )

            canales = audio.shape[1] if audio.ndim > 1 else 1
            etiquetas = ["Mic L", "Mic R"] if canales == 2 else ["Mic"]
            for indice, etiqueta in enumerate(etiquetas):
                canal = audio[:, indice] if canales > 1 else audio
                rms = calcular_rms(canal)
                rms_maximo = rms_ventana_maximo(canal)
                media = float(np.mean(canal)) if canal.size else 0.0
                pico = int(np.max(np.abs(canal.astype(np.int32)))) if canal.size else 0
                print(barra_audio(etiqueta, rms))
                print(f"  RMS AC (ventanas de 32ms): {rms:.1f}; maximo por ventana: {rms_maximo:.1f}")
                print(f"  Nivel DC (media): {media:.1f}")
                print(f"  Pico absoluto: {pico} (rango int16: -32768..32767)")
                if pico >= 32000:
                    print(f"  AVISO: {etiqueta} muy cerca del limite; posible recorte (clipping) en el I2S.")
                if rms < 80:
                    print(f"  AVISO: {etiqueta} con nivel muy bajo; revisa el cableado de ese microfono.")

            marca = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
            ruta = salida_dir / f"esp32_{marca}.wav"
            wavfile.write(ruta, sample_rate, audio)
            print(f"Guardado en: {ruta}")
            if canales == 2:
                ruta_izquierda = salida_dir / f"esp32_{marca}_L.wav"
                ruta_derecha = salida_dir / f"esp32_{marca}_R.wav"
                wavfile.write(ruta_izquierda, sample_rate, audio[:, 0])
                wavfile.write(ruta_derecha, sample_rate, audio[:, 1])
                print(f"Canal izquierdo: {ruta_izquierda}")
                print(f"Canal derecho: {ruta_derecha}")

            if not args.repetir:
                break
            input("\nPulsa Enter y habla para volcar otra vez (Ctrl+C para salir)...")
    except KeyboardInterrupt:
        print("\nInterrumpido por el usuario. Saliendo.")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
