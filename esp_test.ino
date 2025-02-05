#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>

#define WIFI_SSID "lime"
#define WIFI_PASSWORD "00000000"

const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char* mqtt_user = "";
const char* mqtt_password = "";

SoftwareSerial DataSerial(12, 13);

const char* topic_voltage = "voltage_data";
const char* topic_frequency = "frequency_data";
const char* topic_temperature = "temperature_data";
const char* topic_humidity = "humidity_data";
const char* topic_timestamp = "timestamp_data";

AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;
WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

unsigned long previousMillis = 0;
const long interval = 10000;

bool mqttConnected = false;

void connectToWifi() {
  Serial.println("Connecting to Wi-Fi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void onWifiConnect(const WiFiEventStationModeGotIP& event) {
  Serial.println("Connected to Wi-Fi.");
  connectToMqtt();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected& event) {
  Serial.println("Disconnected from Wi-Fi.");
  mqttReconnectTimer.detach();
  wifiReconnectTimer.once(2, connectToWifi);
}

void connectToMqtt() {
  Serial.println("Connecting to MQTT...");
  mqttClient.connect();
}

void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT.");
  mqttConnected = true;
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.println("Disconnected from MQTT.");
  mqttConnected = false;
  if (WiFi.isConnected()) {
    mqttReconnectTimer.once(2, connectToMqtt);
  }
}

void onMqttPublish(uint16_t packetId) {
  Serial.printf("Publish acknowledged. Packet ID: %i\n", packetId);
}

String arrData[5];
String voltage, frequency, temperature, humidity, timestamp;

void setup() {
  Serial.begin(9600);
  DataSerial.begin(19200);

  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onPublish(onMqttPublish);
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCredentials(mqtt_user, mqtt_password);

  connectToWifi();
}

void loop() {
  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    connectToWifi();
    delay(1000);
    return;
  }

  // Check MQTT connection
  if (!mqttConnected) {
    connectToMqtt();
    delay(1000);
    return;
  }

  String Data = "";
  while (DataSerial.available() > 0) {
    Data += char(DataSerial.read());
  }
  Data.trim();

  if (Data != "") {
    int index = 0;
    for (int i = 0; i <= Data.length(); i++) {
      if (Data[i] != '#')
        arrData[index] += Data[i];
      else
        index++;
    }

    if (index == 5) {
      voltage = arrData[1];
      frequency = arrData[2];
      temperature = arrData[3];
      humidity = arrData[4];
      timestamp = arrData[5];

      unsigned long currentMillis = millis();
      if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;

        if (mqttConnected && WiFi.status() == WL_CONNECTED) {
          uint16_t packetIdPub1 = mqttClient.publish(topic_temperature, 1, true, temperature.c_str());
          delay(100);  // Add small delay between publishes
          uint16_t packetIdPub2 = mqttClient.publish(topic_voltage, 1, true, voltage.c_str());
          delay(100);
          uint16_t packetIdPub3 = mqttClient.publish(topic_frequency, 1, true, frequency.c_str());
          delay(100);
          uint16_t packetIdPub4 = mqttClient.publish(topic_humidity, 1, true, humidity.c_str());
          delay(100);
          uint16_t packetIdPub5 = mqttClient.publish(topic_timestamp, 1, true, timestamp.c_str());
          
          Serial.println("Data published successfully");
        } else {
          Serial.println("Connection lost, attempting to reconnect...");
        }
      }
    }
    delay(1000);  // Reduced delay
    for (int i = 0; i < 5; i++) arrData[i] = "";
  }
}
