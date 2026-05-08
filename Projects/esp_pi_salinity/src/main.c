#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "esp_adc/adc_oneshot.h"

// --- Configuration ---
#define WIFI_SSID      "Fiscalia"
#define WIFI_PASS      "R3d_c4s7ro"
#define MQTT_BROKER    "mqtt://192.168.1.101"

static const char *TAG = "SALINITY_NODE";
bool is_sending = true;
bool mqtt_connected = false; // NEW: Tracks actual connection state
esp_mqtt_client_handle_t client = NULL;

// --- MQTT Event Handler ---
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    
    if (event_id == MQTT_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "MQTT Connected to Raspberry Pi!");
        mqtt_connected = true; // Safe to send data now
        esp_mqtt_client_subscribe(client, "sensor/control", 0);
    } 
    else if (event_id == MQTT_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "MQTT Disconnected. Waiting to reconnect...");
        mqtt_connected = false; // Stop sending data
    }
    else if (event_id == MQTT_EVENT_DATA) {
        if (strncmp(event->topic, "sensor/control", event->topic_len) == 0) {
            if (strncmp(event->data, "pause", event->data_len) == 0) {
                is_sending = false;
                ESP_LOGI(TAG, "Data transmission PAUSED by Raspberry Pi");
            } else if (strncmp(event->data, "resume", event->data_len) == 0) {
                is_sending = true;
                ESP_LOGI(TAG, "Data transmission RESUMED by Raspberry Pi");
            }
        }
    }
}

// --- Wi-Fi Event Handler ---
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    wifi_event_sta_disconnected_t* disconn = (wifi_event_sta_disconnected_t*) event_data;
    ESP_LOGE(TAG, "Wi-Fi disconnected! Reason code: %d", disconn->reason);
    ESP_LOGI(TAG, "Retrying Wi-Fi connection...");
    esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Starting MQTT Client...");
        esp_mqtt_client_start(client);
    }
}

// --- Sensor Reading Task ---
void sensor_task(void *pvParameter) {
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config = { .unit_id = ADC_UNIT_1 };
    adc_oneshot_new_unit(&init_config, &adc1_handle);

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12, 
    };
    adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_2, &config);

    int adc_raw;
    char payload[16];

    while (1) {
        if (is_sending && mqtt_connected) {
            adc_oneshot_read(adc1_handle, ADC_CHANNEL_2, &adc_raw);
            
            // 1. Convert raw ADC value to Voltage (Assuming 12-bit ADC and 3.3V reference)
            float voltage = ((float)adc_raw / 4095.0) * 3.3;

            // 2. Temperature Compensation (Assuming a standard 25°C room temp for now)
            float temperature = 25.0; 
            float compensation_coefficient = 1.0 + 0.02 * (temperature - 25.0);
            float compensation_voltage = voltage / compensation_coefficient;

            // 3. Convert Voltage to TDS value (ppm) using the standard polynomial formula
            float tds_value = (133.42 * compensation_voltage * compensation_voltage * compensation_voltage 
                             - 255.86 * compensation_voltage * compensation_voltage 
                             + 857.39 * compensation_voltage) * 0.5;

            // If the sensor is sitting in air, it might read slightly negative due to noise. Clamp it to 0.
            if (tds_value < 0) {
                tds_value = 0;
            }

            // Format the TDS value as an integer string
            snprintf(payload, sizeof(payload), "%.0f", tds_value);
            
            ESP_LOGI(TAG, "Publishing TDS: %s ppm", payload);
            esp_mqtt_client_publish(client, "sensor/salinity", payload, 0, 1, 0);
        } else if (!mqtt_connected) {
            ESP_LOGD(TAG, "Waiting for MQTT connection...");
        }
        
        vTaskDelay(2000 / portTICK_PERIOD_MS); 
    }
}

// --- Main Application Entry Point ---
void app_main(void) {
    // NEW: Robust NVS Initialization (Fixes the Wi-Fi crash after changing partitions)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            // Adding this forces it to wait longer for the router to assign an IP
            .listen_interval = 1,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER,
    };
    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);
}