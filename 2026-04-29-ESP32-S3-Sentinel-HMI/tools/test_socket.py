"""
Script de prueba para el servidor IPC via socket crudo.
Ejecutar con: python test_socket.py
Requiere que sidecar_daemon.py este corriendo.
"""
import socket
import time

with socket.create_connection(("127.0.0.1", 9090), timeout=3) as s:
    for i in range(5):
        s.sendall(f"Linea {i+1} de prueba\n".encode())
        time.sleep(0.2)

print("Enviado OK")
