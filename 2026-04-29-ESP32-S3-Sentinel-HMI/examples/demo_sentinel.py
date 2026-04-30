"""
demo_sentinel.py — Ejemplo completo de la librería Sentinel para Python
Autor: Ignacio Aguilera

Requisitos:
    1. sentinel_daemon.py corriendo en otra terminal (o SidecarDaemon.exe)
    2. Pantalla CrowPanel conectada por USB
    3. pip install .   (desde la raíz del proyecto)

Ejecutar:
    python examples/demo_sentinel.py
"""

import time
import random
from sentinel import logger

# ─────────────────────────────────────────────────────────────────────────────
# 1. LOGGING BÁSICO
#    Los mensajes aparecen en el terminal T0 de la pantalla con colores.
# ─────────────────────────────────────────────────────────────────────────────

logger.set_terminal(0)
logger.separator("=", 30)
logger.info("Sentinel Demo — Python")
logger.separator()

logger.print("Mensaje genérico (cyan)")
logger.info("Mensaje informativo (azul)")
logger.warn("Advertencia (ámbar) — voltaje bajo: 3.1 V")
logger.error("Error (rojo) — sensor sin respuesta")
logger.separator()

time.sleep(1)

# ─────────────────────────────────────────────────────────────────────────────
# 2. MÉTRICAS EN TIEMPO REAL
#    Aparecen en la pantalla y se guardan en sentinel_logs/metrics.jsonl
# ─────────────────────────────────────────────────────────────────────────────

logger.info("Enviando métricas...")

for i in range(5):
    temp    = 22.0 + random.uniform(-2, 8)
    cpu     = random.uniform(10, 90)
    latency = random.randint(5, 40)

    logger.metric("temp",    round(temp, 1), "°C")
    logger.metric("cpu",     round(cpu, 1),  "%")
    logger.metric("latency", latency,         "ms")
    time.sleep(0.5)

logger.separator()
time.sleep(0.5)

# ─────────────────────────────────────────────────────────────────────────────
# 3. ARCHIVO CSV
#    Abre un archivo, escribe filas y lo cierra. El daemon lo guarda en
#    sentinel_logs/csv/sensores.csv listo para leer con pandas.
# ─────────────────────────────────────────────────────────────────────────────

logger.info("Grabando CSV de sensores...")

N = 20
with logger.open("sensores.csv", ["temp_c", "hum_pct", "presion_hpa"]) as f:
    for i in range(N):
        temp     = round(22.0 + random.gauss(0, 2), 2)
        hum      = round(55.0 + random.gauss(0, 5), 1)
        presion  = round(1013.0 + random.gauss(0, 3), 1)

        f.write([temp, hum, presion])

        logger.progress(i + 1, N, "Captura")
        time.sleep(0.1)

logger.info("CSV cerrado: sentinel_logs/csv/sensores.csv")
logger.separator()
time.sleep(0.5)

# ─────────────────────────────────────────────────────────────────────────────
# 4. ARCHIVO JSONL
#    Cada llamada a write() agrega una línea JSON con timestamp automático.
#    Leer después: pd.read_json("sentinel_logs/json/entrenamiento.jsonl", lines=True)
# ─────────────────────────────────────────────────────────────────────────────

logger.info("Simulando pipeline de entrenamiento (JSONL)...")

EPOCHS = 10
run = logger.open("entrenamiento.jsonl")

for epoch in range(1, EPOCHS + 1):
    loss = round(1.0 / epoch + random.uniform(0, 0.05), 4)
    acc  = round(min(0.5 + epoch * 0.04 + random.uniform(0, 0.02), 0.99), 4)
    lat  = random.randint(80, 200)

    run.write({
        "epoch":   epoch,
        "loss":    loss,
        "acc":     acc,
        "lat_ms":  lat,
        "ok":      loss < 0.5,
    })

    logger.metric("loss", loss)
    logger.metric("acc",  acc)
    logger.progress(epoch, EPOCHS, "Entrenando")
    time.sleep(0.3)

run.close()
logger.info("JSONL cerrado: sentinel_logs/json/entrenamiento.jsonl")
logger.separator()
time.sleep(0.5)

# ─────────────────────────────────────────────────────────────────────────────
# 5. TABLA ASCII
#    Muestra una tabla formateada en el terminal de la pantalla.
# ─────────────────────────────────────────────────────────────────────────────

logger.info("Resumen de modelos:")
logger.table(
    ["Modelo",    "Acc",   "Latencia"],
    [
        ["YOLOv8n",  "87.2%", "12 ms"],
        ["YOLOv8s",  "91.0%", "28 ms"],
        ["YOLOv8m",  "94.3%", "55 ms"],
    ]
)

logger.separator("=", 30)
logger.info("Demo completado.")

# ─────────────────────────────────────────────────────────────────────────────
# 6. SESIONES (terminales separados)
#    Cada sesión escribe en un terminal independiente de la pantalla (T1/T2/T3).
#    Útil para monitorear varias partes de tu sistema al mismo tiempo.
# ─────────────────────────────────────────────────────────────────────────────

# Descomentar para probar con múltiples terminales en pantalla:
#
# entrenamiento = logger.session("Training")
# inferencia    = logger.session("Inference")
#
# entrenamiento.info("Epoch 1/10 iniciado")
# inferencia.info("Modelo cargado en GPU")
# entrenamiento.metric("loss", 0.342)
# inferencia.metric("fps", 47)
