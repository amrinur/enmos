/*
ENMOS V3 - Optimized Version
*/
#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>

// WiFi and MQTT Configuration - These are better as const char* since they're string literals
static const char* WIFI_SSID = "lime";
static const char* WIFI_PASSWORD = "00000000";
static const char* MQTT_USERNAME = "brokerTTH";
static const char* MQTT_PASSWORD = "brokerTTH";
static const uint16_t MQTT_PORT = 1884;

// Device identification
static const char* DEVICE_ID = "Txm3r8gHsxcdSqbWcKOpxjFi2mtenQ";

// MQTT Topics - Using const char* for string constants
static const char* MQTT_BASE_TOPIC = "ENMOSV2/";
static const char* MQTT_PUB_RECORD = "ENMOSV2/records";
static const char* MQTT_PUB_TEMP = "ENMOSV2/Txm3r8gHsxcdSqbWcKOpxjFi2mtenQ/resource/temp";
static const char* MQTT_PUB_HUM = "ENMOSV2/Txm3r8gHsxcdSqbWcKOpxjFi2mtenQ/resource/hum";
static const char* MQTT_PUB_VOLT = "ENMOSV2/Txm3r8gHsxcdSqbWcKOpxjFi2mtenQ/resource/volt";
static const char* MQTT_PUB_FREQ = "ENMOSV2/Txm3r8gHsxcdSqbWcKOpxjFi2mtenQ/resource/freq";
static const char* MQTT_PUB_TIME = "ENMOSV2/Txm3r8gHsxcdSqbWcKOpxjFi2mtenQ/resource/tim";
static const char* MQTT_PUB_WARNING = "ENMOSV2/Warning";

// Thresholds - Using constexpr for compile-time constants
static constexpr float TEMP_MIN = 20.0f;
static constexpr float TEMP_MAX = 30.0f;
static constexpr float HUM_MIN = 40.0f;
static constexpr float HUM_MAX = 80.0f;
static constexpr float VOLT_MIN = 213.0f;
static constexpr float VOLT_MAX = 227.0f;

// Timing constants
static constexpr unsigned long PUBLISH_INTERVAL = 10000;
static constexpr uint8_t RECONNECT_DELAY = 2;  // seconds

// MQTT Broker IP - Using IPAddress class
const IPAddress MQTT_HOST(36, 95, 203, 54);

// Global objects
SoftwareSerial DataSerial(12, 13);
AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;
Ticker wifiReconnectTimer;

// Global variables with better memory management
struct SensorData {
    float temp;
    float hum;
    float volt;
    float freq;
    float timestamp;  // Changed from 'time' to 'timestamp'
} sensorData;

char warningMsg[50];
unsigned long previousMillis = 0;

// Function declarations
void connectToWifi();
void connectToMqtt();
void checkThresholds();
void publishData();

void setup() {
    Serial.begin(9600);
    DataSerial.begin(19200);

    // Setup WiFi event handlers
    WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP& event) {
        Serial.println(F("Connected to Wi-Fi."));
        connectToMqtt();
    });

    WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& event) {
        Serial.println(F("Disconnected from Wi-Fi."));
        mqttReconnectTimer.detach();
        wifiReconnectTimer.once(RECONNECT_DELAY, connectToWifi);
    });

    // Setup MQTT client
    mqttClient.onConnect([](bool sessionPresent) {
        Serial.println(F("Connected to MQTT."));
    });

    mqttClient.onDisconnect([](AsyncMqttClientDisconnectReason reason) {
        Serial.println(F("Disconnected from MQTT."));
        if (WiFi.isConnected()) {
            mqttReconnectTimer.once(RECONNECT_DELAY, connectToMqtt);
        }
    });

    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    mqttClient.setCredentials(MQTT_USERNAME, MQTT_PASSWORD);

    connectToWifi();
}

void loop() {
    if (!DataSerial.available()) {
        return;
    }

    // Read and parse data
    String data = DataSerial.readStringUntil('\n');
    if (data.length() == 0) {
        return;
    }

    // Parse sensor data
    if (parseSensorData(data)) {
        checkThresholds();
        
        // Publish data at intervals
        unsigned long currentMillis = millis();
        if (currentMillis - previousMillis >= PUBLISH_INTERVAL) {
            previousMillis = currentMillis;
            publishData();
        }
    }
}

bool parseSensorData(const String& data) {
    char* str = strdup(data.c_str());
    char* token = strtok(str, "#");
    int index = 0;
    
    while (token != NULL && index < 5) {
        float value = atof(token);
        switch(index) {
            case 0: sensorData.temp = value; break;
            case 1: sensorData.volt = value; break;
            case 2: sensorData.hum = value; break;
            case 3: sensorData.freq = value; break;
            case 4: sensorData.timestamp = value; break;  // Changed from 'time' to 'timestamp'
        }
        token = strtok(NULL, "#");
        index++;
    }
    
    free(str);
    return (index == 5);
}

void checkThresholds() {
    if (sensorData.temp < TEMP_MIN) {
        snprintf(warningMsg, sizeof(warningMsg), "Low temperature: %.1f", sensorData.temp);
    } else if (sensorData.temp > TEMP_MAX) {
        snprintf(warningMsg, sizeof(warningMsg), "High temperature: %.1f", sensorData.temp);
    } else if (sensorData.hum < HUM_MIN) {
        snprintf(warningMsg, sizeof(warningMsg), "Low humidity: %.1f", sensorData.hum);
    } else if (sensorData.hum > HUM_MAX) {
        snprintf(warningMsg, sizeof(warningMsg), "High humidity: %.1f", sensorData.hum);
    } else if (sensorData.volt < VOLT_MIN) {
        snprintf(warningMsg, sizeof(warningMsg), "Low voltage: %.1f", sensorData.volt);
    } else if (sensorData.volt > VOLT_MAX) {
        snprintf(warningMsg, sizeof(warningMsg), "High voltage: %.1f", sensorData.volt);
    } else {
        warningMsg[0] = '\0';
    }
}

void publishData() {
    char buffer[100];
    
    snprintf(buffer, sizeof(buffer), "%.2f", sensorData.temp);
    mqttClient.publish(MQTT_PUB_TEMP, 1, true, buffer);
    
    snprintf(buffer, sizeof(buffer), "%.2f", sensorData.hum);
    mqttClient.publish(MQTT_PUB_HUM, 1, true, buffer);
    
    snprintf(buffer, sizeof(buffer), "%.2f", sensorData.volt);
    mqttClient.publish(MQTT_PUB_VOLT, 1, true, buffer);
    
    snprintf(buffer, sizeof(buffer), "%.2f", sensorData.freq);
    mqttClient.publish(MQTT_PUB_FREQ, 1, true, buffer);
    
    snprintf(buffer, sizeof(buffer), "%.2f", sensorData.timestamp);  // Changed from 'time' to 'timestamp'
    mqttClient.publish(MQTT_PUB_TIME, 1, true, buffer);
    
    if (strlen(warningMsg) > 0) {
        snprintf(buffer, sizeof(buffer), "%s#%s", DEVICE_ID, warningMsg);
        mqttClient.publish(MQTT_PUB_WARNING, 1, true, buffer);
    }
    
    // Publish complete record
    snprintf(buffer, sizeof(buffer), "%s#%.2f#%.2f#%.2f#%.2f#%.2f",
             DEVICE_ID, sensorData.temp, sensorData.volt, sensorData.freq, 
             sensorData.hum, sensorData.timestamp);  // Changed from 'time' to 'timestamp'
    mqttClient.publish(MQTT_PUB_RECORD, 1, true, buffer);
}

void connectToWifi() {
    Serial.println(F("Connecting to Wi-Fi..."));
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectToMqtt() {
    Serial.println(F("Connecting to MQTT..."));
    mqttClient.connect();
}
