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
  serial.begin(19200);
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
    
    serial.println(ESP_CHECK_CMD);
    unsigned long timeout = millis() + 1000;  // 1 second timeout
    
    while (millis() < timeout) {
      if (serial.available()) {
        String response = serial.readStringUntil('\n');
        response.trim();
        espConnected = (response == ESP_CONNECTED);
        return espConnected;
      }
    }
    espConnected = false;
  }
  return espConnected;
}

void loop() {
  checkEspConnection();
  
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

  // Modified data handling logic
  if (!espConnected) {
    // ESP is not connected - save to SD card
    char filename[25];
    snprintf(filename, sizeof(filename), "/bisa.csv", now.year(), now.month());
    
    File file = SD.open(filename, FILE_WRITE);
    if (file) {
      file.printf("%04d/%02d/%02d %02d:%02d:%02d;%.2f;%.2f;%.2f;%.2f\n",
        now.year(), now.month(), now.day(),
        now.hour(), now.minute(), now.second(),
        temperature, humidity, r.V, r.F);
      
      file.flush();
      file.close();
      Serial.println("Data saved to SD card (ESP disconnected)");
    } else {
      Serial.println("Error opening log file");
    }
  }

  // Send data via serial when ESP is connected
  unsigned long currentMillis1 = millis();
  if (currentMillis1 - previousMillis2 >= INTERVAL && espConnected) {
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
