import paho.mqtt.client as mqtt
import json
import threading

# ==============================
# CONFIGURACIÓN MQTT
# ==============================
BROKER = "192.168.0.16"
PORT = 1883
TENANT = "UNRaf"
DEVICE_MAC = "D8BC38E423D0"  # MAC del ESP32
BASE_TOPIC = f"{TENANT}/{DEVICE_MAC}"

# Tópicos estandarizados
STATUS_TOPIC = f"{BASE_TOPIC}/status"   # Online / Offline (LWT)
ACK_TOPIC = f"{BASE_TOPIC}/ack"
CMD_TOPIC = f"{BASE_TOPIC}/cmd"

# Tópicos de estado individuales
LEVEL_TOPIC = f"{BASE_TOPIC}/nivel"
BOMBA_TOPIC = f"{BASE_TOPIC}/bomba"
ALARMA_TOPIC = f"{BASE_TOPIC}/alarma"
DATETIME_TOPIC = f"{BASE_TOPIC}/datetime"

# Estado consolidado del dispositivo
estado_actual = {
    'estado_conexion': 'DESCONOCIDO',
    'nivel': 'N/A',
    'bomba': 'N/A',
    'alarma': 'N/A',
    'fecha': 'N/A'
}

# ==============================
# CALLBACK: Conexión
# ==============================
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("✅ Conectado al broker MQTT")
        client.subscribe(f"{BASE_TOPIC}/#", qos=1)
        print(f"📡 Suscrito a: {BASE_TOPIC}/# (QoS 1)")
    else:
        print(f"❌ Error al conectar. Código: {rc}")

# ==============================
# CALLBACK: Mensajes recibidos
# ==============================
def on_message(client, userdata, msg):
    topic = msg.topic
    payload = msg.payload.decode("utf-8")
    
    print(f"\n📩 [{topic}] → {payload}")

    # Manejo del estado de conexión (LWT)
    if topic == STATUS_TOPIC:
        estado_actual['estado_conexion'] = payload
        print("🟢 DISPOSITIVO CONECTADO" if payload == "online" else "🔴 DISPOSITIVO DESCONECTADO (LWT)")

    # Confirmación ACK
    elif topic == ACK_TOPIC:
        print(f"🟢 Respuesta del ESP32: {payload}")

    # Actualización del estado consolidado
    elif topic == LEVEL_TOPIC:
        estado_actual['nivel'] = payload
    elif topic == BOMBA_TOPIC:
        estado_actual['bomba'] = payload
    elif topic == ALARMA_TOPIC:
        estado_actual['alarma'] = payload
    elif topic == DATETIME_TOPIC:
        estado_actual['fecha'] = payload

    # Mostrar estado consolidado si se actualizó algo
    if topic in [STATUS_TOPIC, LEVEL_TOPIC, BOMBA_TOPIC, ALARMA_TOPIC, DATETIME_TOPIC]:
        print("\n===== ESTADO CONSOLIDADO =====")
        print(f"Conexión:   {estado_actual.get('estado_conexion')}")
        print(f"Nivel:      {estado_actual.get('nivel')}")
        print(f"Bomba:      {estado_actual.get('bomba')}")
        print(f"Alarma:     {estado_actual.get('alarma')}")
        print(f"Fecha/hora: {estado_actual.get('fecha')}")
        print("==============================")

# ==============================
# ENVIAR TIEMPO MÁXIMO
# ==============================
def enviar_tiempo_maximo(client):
    while True:
        tiempo = input("\n⏱️ Ingresá tiempo máximo de bomba (segundos): ")
        if not tiempo.isdigit() or int(tiempo) <= 0:
            print("⚠️ Ingresá un número válido mayor que 0.")
            continue
        
        comando = {"command": "set_max", "tiempo": int(tiempo)}
        client.publish(CMD_TOPIC, json.dumps(comando), qos=1)
        print(f"📤 Tiempo máximo enviado: {tiempo}s")

# ==============================
# ENVIAR TIEMPO DE DESCANSO
# ==============================
def enviar_tiempo_descanso(client):
    while True:
        tiempo = input("\n🛑 Ingresá tiempo de descanso de la bomba (segundos): ")
        if not tiempo.isdigit() or int(tiempo) <= 0:
            print("⚠️ Ingresá un número válido mayor que 0.")
            continue
        
        comando = {"command": "set_descanso", "tiempo": int(tiempo)}
        client.publish(CMD_TOPIC, json.dumps(comando), qos=1)
        print(f"📤 Tiempo de descanso enviado: {tiempo}s")

# ==============================
# CONFIGURACIÓN DEL CLIENTE MQTT
# ==============================
client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

print("🔌 Conectando al broker...")
client.connect(BROKER, PORT, 60)

# Hilos paralelos para permitir ingreso de comandos mientras recibe mensajes MQTT
threading.Thread(target=enviar_tiempo_maximo, args=(client,), daemon=True).start()
threading.Thread(target=enviar_tiempo_descanso, args=(client,), daemon=True).start()

# ==============================
# LOOP PRINCIPAL
# ==============================
try:
    client.loop_forever()
except KeyboardInterrupt:
    print("\n🛑 Conexión finalizada por el usuario.")
