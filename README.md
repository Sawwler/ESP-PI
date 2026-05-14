##Flujo de operación:

1. El ESP32-C6:

A. Operación Inicial (Setup / app_main)

Fase de encendido y revisión:
    Arranque del Hardware y verifica su memoria interna. Si detecta que el mapa de memoria cambió, borra y reinicia para evitar errores.
    Enciende la antena Wi-Fi en modo Estación (STA).
    Busca el SSID configurado y el ESP32 recibe su dirección IP.
    El ESP32 busca al Raspberry Pi (comando para ver la IP desde el pi: hostname -I). Se conecta a Mosquitto y se suscribe al canal de control (sensor/control).
    Crea la tarea sensor_task en FreeRTOS y el ESP corre por su cuenta en segundo plano.

B. Operación General:

Ritmo constante del ESP cada 2 segundos:
    Lee el valor analógico (configurado para GPIO 4).
    Convierte 0-4095 en Voltaje, aplica la compensación de ruido por temperatura y calcula los ppm (con formula TDS).
    Revisa dos variables: is_sending (mensajes desde el PI) y mqtt_connected (señal mqtt).
    Si el PI da el OK, empaqueta el número y lo lanza al tema sensor/salinity.
    Delay cada 2s para estabilidad.

2. Raspberry Pi:

El Pi tiene dos flujos que corren en paralelo: el servicio del Bróker y el script de Python.
A. Operación Inicial (Setup del Sistema y Python)
    Mosquitto: El servicio inicia automáticamente al bootear el Pi. se abre el puerto 1883 y se queda escuchando. No hace nada más hasta que alguien le hable.
    Arranque de monitor.py:
        Matplotlib prepara la ventana del gráfico
        El script se conecta al bróker que vive en su propia dirección (localhost).
        procesa los datoslos datos que lleguan a sensor/salinity

B. Operación General (Estado Estacionario)
    El script de Python está en reposo por default.
    Cuando llega un dato, el bróker despierta al script. Se ejecuta la función on_message.
    El nuevo valor se guarda en un array. se borra el valor más viejo luego de cierta cantidad de datos para no saturar la gráfica ni memoria.

    Refresco Visual: La función animate redibuja la línea azul con el nuevo punto.

    Control Humano (Opcional): Si tú decides enviar un comando desde la terminal o el script, el flujo es inverso: Pi -> Bróker -> ESP32.
