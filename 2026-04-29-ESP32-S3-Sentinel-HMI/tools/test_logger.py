"""
Script de prueba para la libreria sentinel.
Ejecutar con: python test_logger.py
Requiere que sentinel_daemon.py este corriendo y la pantalla en canal Python.
"""
from sentinel import logger
from sentinel.logger import SentinelLogger
import time

# --- Prueba basica de colores ---
logger.separator()
logger.info("Iniciando prueba de colores")
logger.print("Mensaje normal en cyan")
logger.info("Mensaje info en azul")
logger.warn("Mensaje warn en ambar")
logger.error("Mensaje error en rojo")
logger.separator()

# --- Prueba de rafaga ---
logger.info("Prueba de rafaga de mensajes:")
for i in range(8):
    logger.print(f"  Mensaje rapido {i + 1}/8")

logger.separator()

# --- Prueba de terminal especifico ---
log_t1 = SentinelLogger(terminal=1, prefix="[T1]")
log_t1.print("Mensaje en terminal 1")
log_t1.info("Info en terminal 1")

# --- Prueba de CSV ---
logger.info("Prueba de CSV logging:")
logger.csv("test_training", ["epoch", "loss", "accuracy"], [1, 0.542, 0.73])
logger.csv("test_training", ["epoch", "loss", "accuracy"], [2, 0.342, 0.85])
logger.csv("test_training", ["epoch", "loss", "accuracy"], [3, 0.198, 0.91])
logger.info("3 filas CSV enviadas a test_training.csv")

# --- Prueba de recording ---
logger.info("Prueba de recording:")
logger.start_recording("test_session")
time.sleep(0.5)
logger.print("Esta linea deberia quedar grabada")
logger.warn("Esta advertencia tambien")
time.sleep(0.5)
logger.stop_recording()
logger.info("Recording detenido")

logger.separator()
logger.info("Prueba completada")
print("Enviado OK -- revisa la pantalla y sentinel_logs/")
