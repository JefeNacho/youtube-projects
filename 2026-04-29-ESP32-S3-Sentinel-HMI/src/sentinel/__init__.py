"""Sentinel — Ultimate Serial Monitor & Logger"""
__version__ = "2.0.0"

from sentinel.logger import SentinelLogger

# Instancia global de conveniencia (terminal 0)
logger = SentinelLogger()

__all__ = ["logger", "SentinelLogger"]
