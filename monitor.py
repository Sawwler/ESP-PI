import paho.mqtt.client as mqtt
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation

# Data storage
times = []
salinity_data = []
counter = 0

# MQTT Callbacks
def on_connect(client, userdata, flags, rc):
    print("Connected to Mosquitto Broker!")
    client.subscribe("sensor/salinity")

def on_message(client, userdata, msg):
    global counter
    try:
        # Decode the incoming message
        val = int(msg.payload.decode())
        print(f"Received Salinity: {val}")

        # Add to our arrays
        salinity_data.append(val)
        times.append(counter)
        counter += 1

        # Keep the graph from getting too crowded (last 50 points)
        if len(salinity_data) > 50:
            salinity_data.pop(0)
            times.pop(0)
    except ValueError:
        print("Received non-integer data.")

# Initialize MQTT Client
client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

# Connect to the local broker
client.connect("localhost", 1883, 60)
client.loop_start()

# Set up the Matplotlib Graph
fig, ax = plt.subplots()

def update_plot(frame):
    ax.clear()
    ax.plot(times, salinity_data, marker='o', color='b')
    ax.set_title("Sensor TDS")
    ax.set_xlabel("Lectura (tiempo)")
    ax.set_ylabel("TDS (ppm)")
    ax.grid(True)

    # Auto-scale the Y-axis to fit the data
    if salinity_data:
        #ax.set_ylim(min(salinity_data) - 100, max(salinity_data) + 100)
        ax.set_ylim(max(0, min(salinity_data) - 50), max(salinity_data) + 50)

# Start the animation loop (updates every 1000ms)
ani = FuncAnimation(fig, update_plot, interval=1000)
plt.show()