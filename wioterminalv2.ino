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

unsigned long previousMillisCSV = 0;
const long CSV_READ_INTERVAL = 11100;  // Interval for reading CSV and sending to ESP

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

  // Read sensors and write to CSV
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
      temperature, humidity, r.V, r.F);  // Add WiFi status to CSV
    
    file.flush();
    file.close();
  } else {
    Serial.println("Error opening log file");
  }

  // Read last line from CSV and send to ESP at intervals
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillisCSV >= CSV_READ_INTERVAL) {
    previousMillisCSV = currentMillis;
    
    File readFile = SD.open(filename);
    if (readFile) {
      // Seek to end of file
      readFile.seek(readFile.size());
      long position = readFile.position();
      
      // Find start of last line
      long lastPos = position-2;
      while (lastPos > 0) {
        readFile.seek(lastPos);
        char c = readFile.read();
        if (c == '\n') {
          break;
        }
        lastPos--;
      }
      
      // Read last line
      String lastLine = readFile.readStringUntil('\n');
      readFile.close();
      
      // Parse CSV line
      int pos1 = lastLine.indexOf(';');
      int pos2 = lastLine.indexOf(';', pos1 + 1);
      int pos3 = lastLine.indexOf(';', pos2 + 1);
      int pos4 = lastLine.indexOf(';', pos3 + 1);
      
      if (pos1 != -1 && pos2 != -1 && pos3 != -1 && pos4 != -1) {
        String temp = lastLine.substring(pos2 + 1, pos3);
        String hum = lastLine.substring(pos3 + 1, pos4);
        String volt = lastLine.substring(pos4 + 1);
        
        // Format: 1#voltage#frequency#temperature#humidity
        String datakirim = String("1#") + 
                          volt + "#" +
                          String(r.F, 1) + "#" +
                          temp + "#" +
                          hum;
        
        serial.println(datakirim);
        Serial.println("Sent to ESP: " + datakirim); // Debug print
      }
    }
  }
  
  delay(1000);  // Small delay to prevent overwhelming the system
}
