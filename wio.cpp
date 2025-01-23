#include <SD.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <ModbusMaster.h>
#include <SHT31.h>
#include "RTC_SAMD51.h"
#include "DateTime.h"
#include "Wire.h"

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

bool espConnected = false;
const String ESP_CHECK_CMD = "STATUS";
const String ESP_CONNECTED = "CONNECTED";
unsigned long lastEspCheck = 0;
const long ESP_CHECK_INTERVAL = 5000;  // Check ESP connection every 5 seconds

void setup() {
  Serial.begin(115200);
  serial.begin(19200);  // Pastikan sama dengan baudrate DataSerial di ESP
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
}

bool checkEspConnection() {
  if (millis() - lastEspCheck >= ESP_CHECK_INTERVAL) {
    lastEspCheck = millis();
    serial.println("STATUS");
    
    unsigned long timeout = millis() + 1000;
    while (millis() < timeout) {
      if (serial.available()) {
        String response = serial.readStringUntil('\n');
        response.trim();
        espConnected = (response == "CONNECTED");
        return espConnected;
      }
    }
    espConnected = false;
  }
  return espConnected;
}

bool isFileExists(const char* filename) {
  return SD.exists(filename);
}

void saveToSD(DateTime now, float temperature, float humidity, float voltage, float frequency) {
  char filename[13];
  sprintf(filename, "/%02d%02d%02d.csv", now.year() % 100, now.month(), now.day());
  
  // Check if file exists, if not create and add headers
  if (!isFileExists(filename)) {
    File dataFile = SD.open(filename, FILE_WRITE);
    if (dataFile) {
      dataFile.println("Timestamp,Voltage,Frequency,Temperature,Humidity");
      dataFile.close();
    }
  }
  
  File dataFile = SD.open(filename, FILE_WRITE);
  if (dataFile) {
    char timestamp[9];
    sprintf(timestamp, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
    
    char buffer[100];
    sprintf(buffer, "%s,%.2f,%.2f,%.2f,%.2f",
            timestamp, voltage, frequency, temperature, humidity);
    dataFile.println(buffer);
    dataFile.close();
    Serial.println("Data saved to SD: " + String(filename));
  } else {
    Serial.println("Error opening data file");
  }
}

void loop() {
  bool isEspConnected = checkEspConnection();
  
  // Read sensor data
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
  }

  if (!isEspConnected) {
    // Save to SD card when ESP/WiFi is not connected
    saveToSD(now, temperature, humidity, r.V, r.F);
  } else {
    // Send data via serial when ESP is connected
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis2 >= INTERVAL) {
      previousMillis2 = currentMillis;
      String datakirim = String("1#") + String(r.V, 1) + "#" +
                        String(r.F, 1) + "#" +
                        String(temperature, 1) + "#" +
                        String(humidity, 1);
      serial.println(datakirim);
    }
  }
  
  delay(1000);
}
