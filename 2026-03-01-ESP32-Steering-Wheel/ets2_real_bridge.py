import serial
import json
import time
import pyvjoy
import struct
import unicodedata
import truck_telemetry.truck_telemetry as telemetry
import sys

SERIAL_PORT = 'COM10' 
BAUD_RATE   = 115200
UPDATE_HZ   = 50 

# --- PARÁMETROS DE FILTRADO Y CONTROL ---
STEER_SMOOTHING = 0.25
KP = 4500.0
KI = 150.0
KD = 800.0

class CruiseControl:
    def __init__(self):
        self.active = False
        self.target_speed = 0.0
        self.integral = 0.0
        self.last_error = 0.0
        self.last_time = time.time()

    def update(self, current_speed):
        if not self.active: return 1
        now = time.time()
        dt = now - self.last_time
        if dt <= 0: dt = 0.02
        error = self.target_speed - current_speed
        self.integral += error * dt
        self.integral = max(-10, min(10, self.integral))
        derivative = (error - self.last_error) / dt
        output = (KP * error) + (KI * self.integral) + (KD * derivative)
        self.last_error = error
        self.last_time = now
        vjoy_val = int(5000 + output)
        return max(1, min(32768, vjoy_val))

    def reset(self):
        self.active = False
        self.integral = 0.0
        self.last_error = 0.0

def to_ascii(s, length):
    s = ''.join(c for c in unicodedata.normalize('NFD', s) if unicodedata.category(c) != 'Mn')
    return s.encode('ascii', 'ignore')[:length].ljust(length, b'\0')

def get_raw_steer(ser):
    start_time = time.time()
    while time.time() - start_time < 0.5:
        if ser.in_waiting > 0:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line.startswith("RAW:"):
                try:
                    return int(line.replace("RAW:", "").split(",")[0])
                except: pass
    return None

def calibrate_steering(ser):
    print("\n" + "="*40)
    print("   SISTEMA DE CALIBRACIÓN + FILTRO ANTI-VIBRACIÓN")
    print("="*40)
    input("\n[1/3] Pon el volante en el CENTRO y presiona ENTER...")
    center = 0
    for _ in range(20):
        val = get_raw_steer(ser); center += (val if val is not None else 0)
        time.sleep(0.02)
    center //= 20
    print(f" -> Centro: {center}")
    print("\n[2/3] Gira al máximo IZQUIERDA y presiona ENTER...")
    min_val = center
    while True:
        val = get_raw_steer(ser)
        if val is not None: print(f" RAW IZQ: {val}      ", end='\r'); min_val = val
        import msvcrt
        if msvcrt.kbhit() and msvcrt.getch() == b'\r': break
    print(f"\n -> Mínimo: {min_val}")
    print("\n[3/3] Gira al máximo DERECHA y presiona ENTER...")
    max_val = center
    while True:
        val = get_raw_steer(ser)
        if val is not None: print(f" RAW DER: {val}      ", end='\r'); max_val = val
        import msvcrt
        if msvcrt.kbhit() and msvcrt.getch() == b'\r': break
    print(f"\n -> Máximo: {max_val}")
    return min_val, center, max_val

def map_steer(val, min_v, center_v, max_v):
    if val <= center_v:
        if (center_v - min_v) == 0: return 16384
        return int(1 + (val - min_v) * (16383) / (center_v - min_v))
    else:
        if (max_v - center_v) == 0: return 16384
        return int(16384 + (val - center_v) * (16384) / (max_v - center_v))

def start_real_bridge():
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)
        j = pyvjoy.VJoyDevice(1)
        min_s, center_s, max_s = calibrate_steering(ser)
        telemetry.init()
        cc = CruiseControl()
        input_buffer = ""
        last_logi_update = 0
        phys_gas_pressed = False
        current_speed_mph = 0.0
        smoothed_s = float(center_s)
        cur_s_raw = center_s

        while True:
            t_now = time.time()
            game_data = telemetry.get_data()
            if game_data:
                current_speed_mph = abs(game_data.get('speed', 0) * 2.23694)

            if ser.in_waiting > 0:
                try:
                    data = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
                    input_buffer += data
                    if "\n" in input_buffer:
                        lines = input_buffer.split("\n"); input_buffer = lines.pop()
                        for line in lines:
                            line = line.strip()
                            if not line: continue
                            if '{"' in line:
                                try:
                                    msg = json.loads(line)
                                    cmd = msg.get("cmd")
                                    if cmd:
                                        btn = {"gear_R":6, "gear_N":7, "gear_D":8, "lights":2, "highbeam":3, "wipers":4, "hazards":5}.get(cmd)
                                        if btn: j.set_button(btn, 1); time.sleep(0.02); j.set_button(btn, 0)
                                except Exception: pass
                            elif line.startswith("RAW:"):
                                try:
                                    vals = line.replace("RAW:", "").split(",")
                                    if len(vals) == 4:
                                        s_raw, g_raw, b_raw, h_raw = map(int, vals); cur_s_raw = s_raw
                                        smoothed_s = (STEER_SMOOTHING * s_raw) + (1.0 - STEER_SMOOTHING) * smoothed_s
                                        j.set_axis(pyvjoy.HID_USAGE_X, max(1, min(32768, map_steer(smoothed_s, min_s, center_s, max_s))))
                                        new_gas, new_brk = (g_raw == 0), (b_raw == 0)
                                        if new_brk: cc.reset(); j.set_axis(pyvjoy.HID_USAGE_Y, 1); j.set_axis(pyvjoy.HID_USAGE_Z, 32768)
                                        elif new_gas: cc.reset(); j.set_axis(pyvjoy.HID_USAGE_Y, 32768); j.set_axis(pyvjoy.HID_USAGE_Z, 1)
                                        else:
                                            if phys_gas_pressed and current_speed_mph > 15.0:
                                                cc.active = True; cc.target_speed = current_speed_mph; cc.last_time = time.time()
                                            if cc.active: j.set_axis(pyvjoy.HID_USAGE_Y, cc.update(current_speed_mph)); j.set_axis(pyvjoy.HID_USAGE_Z, 1)
                                            else: j.set_axis(pyvjoy.HID_USAGE_Y, 1); j.set_axis(pyvjoy.HID_USAGE_Z, 1)
                                        phys_gas_pressed = new_gas; j.set_button(1, h_raw)
                                except Exception: pass
                except Exception: pass

            if game_data:
                pkt = bytearray(32); pkt[0] = 0xA5
                struct.pack_into('<H', pkt, 1, min(65535, int(current_speed_mph * 10)))
                struct.pack_into('<H', pkt, 3, min(65535, int(game_data.get('engineRpm', 0))))
                struct.pack_into('<b', pkt, 5, int(game_data.get('gearDashboard', 0)))
                pkt[6] = int(max(0, min(1.0, game_data.get('fuel', 0) / (game_data.get('fuelCapacity', 1) or 1))) * 100)
                struct.pack_into('<I', pkt, 7, int(max(0, (game_data.get('routeDistance', 0) or 0) / 1609.34)))
                
                # --- DATOS DE DESGASTE Y DAÑOS (BYTES 11-16) ---
                pkt[11] = int(max(0, min(1.0, game_data.get('wearEngine', 0))) * 100)
                pkt[12] = int(max(0, min(1.0, game_data.get('wearTransmission', 0))) * 100)
                pkt[13] = int(max(0, min(1.0, game_data.get('wearCabin', 0))) * 100)
                pkt[14] = int(max(0, min(1.0, game_data.get('wearChassis', 0))) * 100)
                pkt[15] = int(max(0, min(1.0, game_data.get('wearWheels', 0))) * 100)
                pkt[16] = int(max(0, min(1.0, game_data.get('cargoDamage', 0))) * 100)
                
                pkt[17] = 1 if game_data.get('lightsBeamLow') else 0
                pkt[18] = 1 if game_data.get('lightsBeamHigh') else 0
                pkt[19] = 1 if game_data.get('wipers') else 0
                pkt[20] = 1 if game_data.get('lightsHazards') else 0
                struct.pack_into('<I', pkt, 21, int(max(0, game_data.get('jobIncome', 0))))
                cs = 0xA5
                for i in range(1, 31): cs ^= pkt[i]
                pkt[31] = cs; ser.write(pkt)

                if t_now - last_logi_update > 1.0:
                    lpkt = bytearray(128); lpkt[0] = 0xB5
                    lpkt[1:65] = to_ascii(game_data.get('cargo', "---"), 64)
                    lpkt[65:127] = to_ascii(game_data.get('cityDst', "---"), 62)
                    cs_l = 0xB5
                    for i in range(1, 127): cs_l ^= lpkt[i]
                    lpkt[127] = cs_l; ser.write(lpkt)
                    last_logi_update = t_now
                
                cc_status = f"CC:{cc.target_speed:.1f}" if cc.active else "OFF    "
                print(f" VEL: {current_speed_mph:4.1f} | {cc_status} | RAW_ST: {cur_s_raw:4} | GAS: {'Y' if phys_gas_pressed else 'N'}      ", end='\r')

            time.sleep(max(0, (1.0 / UPDATE_HZ) - (time.time() - t_now)))
    except Exception as e: print(f"\n[ERR]: {e}")
    finally: ser.close()

if __name__ == "__main__":
    start_real_bridge()
