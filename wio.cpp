#include <SD.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <ModbusMaster.h>
#include <SHT31.h>
#include "RTC_SAMD51.h"
#include "DateTime.h"
#include "Wire.h"
#include <WiFi.h>  // Add WiFi library

RTC_SAMD51 rtc;
SHT31 sht;
SoftwareSerial serial(D2, D3);      // For data transmission
SoftwareSerial SerialMod(D1, D0);   // For Modbus
ModbusMaster node;

typedef struct {
  float V;
  float F;
} READING;

unsigned long previousMillis2 = 0;
const long INTERVAL = 11100;  // Interval for data transmission
bool isWiFiConnected = false;  // Add global variable for WiFi status
unsigned long lastEspResponse = 0;
const unsigned long ESP_TIMEOUT = 5000; // 5 seconds timeout

void setup() {
  Serial.begin(115200);
  serial.begin(9600);
  SerialMod.begin(9600);
  
  Wire.begin();
  sht.begin(0x44);    // SHT31 I2C Address
  rtc.begin();
  
  DateTime now = DateTime(F(_DATE), F(TIME_));
  rtc.adjust(now);
  
  node.begin(17, SerialMod);
  
  // Initialize SD card
  if (!SD.begin(SDCARD_SS_PIN)) {
    Serial.println("SD card initialization failed!");
    return;
  }
  Serial.println("SD card initialized.");
  
  // Add WiFi status check
  isWiFiConnected = (WiFi.status() == WL_CONNECTED);
}

void loop() {
  // Check for ESP WiFi status
  if (serial.available()) {
    String response = serial.readStringUntil('\n');
    Serial.print("Received from ESP: ");  // Debug line
    Serial.println(response);             // Debug line
    
    if (response.startsWith("WIFI:")) {
      isWiFiConnected = (response.substring(5).toInt() == 1);
      lastEspResponse = millis();
      Serial.print("WiFi Status parsed: ");
      Serial.println(isWiFiConnected);
    }
  }

  // If no response from ESP for 5 seconds, consider it disconnected
  if (millis() - lastEspResponse > ESP_TIMEOUT) {
    isWiFiConnected = false;
  }

  // Read sensors
  sht.read();
  float temperature = sht.getTemperature();
  float humidity = sht.getHumidity();
  
  DateTime now = rtc.now();
  
  // Read Modbus data
  READING r;
  uint8_t result = node.readHoldingRegisters(0002, 10);
  if (result == node.ku8MBSuccess) {
    r.V = (float)node.getResponseBuffer(0x00) / 100;
    r.F = (float)node.getResponseBuffer(0x09) / 100;
  } else {
    r.V = 0;
    r.F = 0;
  }

  // Log to SD card
  char filename[25];
  snprintf(filename, sizeof(filename), "/bisa.csv", now.year(), now.month());
  
  File file = SD.open(filename, FILE_WRITE);
  if (file) {
    file.printf("%04d/%02d/%02d %02d:%02d:%02d;%.2f;%.2f;%.2f;%.2f;%d\n",
      now.year(), now.month(), now.day(),
      now.hour(), now.minute(), now.second(),
      temperature, humidity, r.V, r.F,
      isWiFiConnected ? 1 : 0);  // Add WiFi status to CSV
    
    file.flush();
    file.close();
  } else {
    Serial.println("Error opening log file");
  }

  // Send data via serial at specified intervals
  unsigned long currentMillis1 = millis();
  if (currentMillis1 - previousMillis2 >= INTERVAL) {
    previousMillis2 = currentMillis1;
    
    String datakirim = String("1#") +
                       String(r.V, 1) + "#" +
                       String(r.F, 1) + "#" +
                       String(temperature, 1) + "#" +
                       String(humidity, 1);
    
    Serial.println(datakirim);
    serial.println(datakirim);
  }
  
  delay(1000);  // Small delay to prevent overwhelming the system
}
