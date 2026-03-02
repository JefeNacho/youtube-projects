# GUÍA DE PRUEBAS: RECEPCIÓN ESP32-C3 (FASE 2)
**Módulo:** `firmware_c3/src/main.cpp`
**Responsable:** @coder (Validation Engineer)

---

## 🧪 PRUEBA 1: Flasheo y Boot
Esta prueba verifica que el ESP32-C3 inicie correctamente su Kernel de FreeRTOS.

### **Pasos:**
1. Conecta tu ESP32-C3 al PC.
2. Abre el proyecto en **PlatformIO** (VS Code).
3. Compila y sube el firmware (`Build & Upload`).
4. Abre el **Monitor Serial** a **921600 baudios**.
5. **Resultado Esperado:** 
   - Deberías ver el mensaje: `[OK] ESP32-C3 RTOS Booted. Waiting for PC Bridge...`

---

## 🏎️ PRUEBA 2: Integración PC -> ESP32-C3
Esta prueba valida que el ESP32 reciba y valide los paquetes reales del simulador.

### **Pasos:**
1. Mantén el ESP32-C3 conectado y el Monitor Serial abierto.
2. Abre EuroTruck Simulator 2.
3. Ejecuta el script de Python:
   ```bash
   python pc_bridge/telemetry_bridge.py
   ```
4. **Resultado Esperado:** 
   - El script de Python dirá que el puerto COM3 está activo (asegúrate de cerrar el Monitor Serial de VS Code antes, para no colisionar).
   - Para verificar en el ESP32, puedes descomentar las líneas de `Serial.printf` en el archivo `main.cpp` para ver los valores de velocidad y marcha en tiempo real.

---

## 📝 NOTAS TÉCNICAS
- **UART:** El C3 usa el puerto USB-Serial nativo para esta prueba.
- **Checksum:** Si el ESP32 no imprime nada (y tienes el debug activo), revisa que el `struct` en C++ coincida exactamente con el `struct.pack` de Python.
- **Baudrate:** 921600 es una velocidad alta. Usa cables USB de buena calidad.
