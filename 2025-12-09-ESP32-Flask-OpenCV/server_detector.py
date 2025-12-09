from flask import Flask, request, jsonify, Response
import cv2
import numpy as np
import threading
import time

# Variables globales para compartir la imagen entre el servidor y el visualizador
latest_frame = None
frame_lock = threading.Lock()

app = Flask(__name__)


# Cargar el clasificador pre-entrenado de Haar Cascade para detección de rostros
face_cascade = cv2.CascadeClassifier(cv2.data.haarcascades + 'haarcascade_frontalface_default.xml')

if face_cascade.empty():
    print(" No se pudo cargar el clasificador Haar Cascade.")
    exit()

print(" Clasificador de rostros cargado exitosamente.")

@app.route('/upload', methods=['POST'])
def upload_image():
    global latest_frame

    # 1. Recibir los datos crudos de la imagen
    image_bytes = request.data
        
    if not image_bytes:
        return jsonify({"status": "error", "message": "No se recibieron datos de imagen."}), 400

    try:
        # 2. Convertir bytes a imagen de OpenCV
        nparr = np.frombuffer(image_bytes, np.uint8)
        frame = cv2.imdecode(nparr, cv2.IMREAD_COLOR)
        
        # Rotar la imagen si es necesario (depende de la orientación de la cámara)
        frame = cv2.rotate(frame, cv2.ROTATE_90_COUNTERCLOCKWISE)

        if frame is None:
            return jsonify({"status": "error", "message": "Datos de imagen corruptos."}), 400

        # 3. Detección de rostros
        if face_cascade:
            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY) # Convertir a escala de grises
            faces = face_cascade.detectMultiScale(gray, scaleFactor=1.1, minNeighbors=5, minSize=(30, 30))

            faces_list = []
            for (x, y, w, h) in faces:
                # Dibujar rectángulo alrededor del rostro
                cv2.rectangle(frame, (x, y), (x + w, y + h), (0, 255, 0), 2)
                faces_list.append({
                    "x": int(x),
                    "y": int(y),
                    "w": int(w),
                    "h": int(h)
                })

            # Mostrar contador en la imagen
            cv2.putText(frame, f"Rostros: {len(faces_list)}", (10, 30), 
                        cv2.FONT_HERSHEY_SIMPLEX, 1, (255, 0, 0), 2)

            # Actualizar el frame global para el visualizador
            with frame_lock:
                latest_frame = frame.copy()

            # 4. Preparar respuesta JSON
            response_data = {
                "status": "success",
                "faces_detected": len(faces_list),
                "positions": faces_list
            }

            return jsonify(response_data), 200
                
    except Exception as e:
        return jsonify({"status": "error", "message": f"Error procesando imagen: {str(e)}"}), 500

@app.route('/')
def index():
    return Response("Servidor de Detección ESP32 ACTIVO. Esperando POST a /upload", mimetype='text/plain')

def start_opencv_viewer():
    """Función que corre en un hilo separado para mostrar la ventana de video"""
    print("→ Iniciando visualizador de OpenCV...")
    cv2.namedWindow("Deteccion en Tiempo Real (Server Side)", cv2.WINDOW_AUTOSIZE)
    while True:
        # Obtener el último frame de manera segura
        with frame_lock:
            current_frame = latest_frame
        
        # Mostrar si hay imagen disponible
        if current_frame is not None:
            cv2.imshow("Deteccion en Tiempo Real (Server Side)", current_frame)
        
        # Salir si se presiona 'q'
        if cv2.waitKey(1) & 0xFF == ord('q'):
            print("→ Usuario presionó 'q', cerrando visualizador...")
            break
        time.sleep(0.01)
    cv2.destroyAllWindows()
    print("Visualizador de OpenCV cerrado.")

if __name__ == '__main__':
    print("*" * 45)
    print("  SERVIDOR DE DETECCIÓN DE ROSTROS ESP32")
    print("*" * 45)
    
    # Iniciar el visualizador en un hilo paralelo
    viewer_thread = threading.Thread(target=start_opencv_viewer)
    viewer_thread.daemon = True
    viewer_thread.start()
    print("→ Iniciando servidor Flask en http://0.0.0.0:5000")
    print("→ Presiona Ctrl+C para detener el servidor")
    print("→ Presiona 'q' en la ventana de OpenCV para cerrar el visualizador")
    print("*" * 45)
    
    # Iniciar la aplicación Flask
    app.run(host='0.0.0.0', port=5000)