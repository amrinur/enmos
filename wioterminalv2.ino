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
  snprintf(filename, sizeof(filename), "/bisa.csv");
  
  // Cek apakah file sudah ada
  if (!SD.exists(filename)) {
    // Jika file belum ada, buat header
    File headerFile = SD.open(filename, FILE_WRITE);
    if (headerFile) {
      headerFile.println("Timestamp;Temperature;Humidity;Voltage;Frequency");
      headerFile.close();
    }
  }
  
  File file = SD.open(filename, FILE_WRITE);
  if (file) {
    // Format data dengan lebar kolom yang konsisten
    char buffer[128];
    snprintf(buffer, sizeof(buffer), 
             "%04d-%02d-%02d %02d:%02d:%02d;%7.2f;%7.2f;%7.2f;%7.2f\n",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second(),
             temperature, humidity, r.V, r.F);
    
    file.print(buffer);
    file.close();
  }

  // Read first line from CSV and send to ESP at intervals
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillisCSV >= CSV_READ_INTERVAL) {
    previousMillisCSV = currentMillis;
    
    File readFile = SD.open(filename);
    if (readFile && readFile.available()) {
      // Skip header jika ada
      String header = readFile.readStringUntil('\n');
      
      if (readFile.available()) {
        // Baca baris data pertama
        String firstLine = readFile.readStringUntil('\n');
        
        // Parse data dengan format yang sudah ditentukan
        int pos1 = firstLine.indexOf(';');
        int pos2 = firstLine.indexOf(';', pos1 + 1);
        int pos3 = firstLine.indexOf(';', pos2 + 1);
        int pos4 = firstLine.indexOf(';', pos3 + 1);
        
        if (pos1 != -1 && pos2 != -1 && pos3 != -1) {
          // Ekstrak data dan hapus spasi
          String temp = firstLine.substring(pos1 + 1, pos2);
          String hum = firstLine.substring(pos2 + 1, pos3);
          String volt = firstLine.substring(pos3 + 1, pos4);
          
          temp.trim();
          hum.trim();
          volt.trim();
          
          String datakirim = String("1#") + 
                            volt + "#" +
                            String(r.F, 1) + "#" +
                            temp + "#" +
                            hum;
        
        // Kirim data ke ESP
        serial.println(datakirim);
        Serial.println("Sent to ESP: " + datakirim);
        
        // Simpan data yang belum terkirim
        String remainingData = "";
        while (readFile.available()) {
          remainingData += readFile.readStringUntil('\n');
          if (readFile.available()) {
            remainingData += '\n';
          }
        }
        readFile.close();
        
        // Tulis ulang file dengan sisa data
        SD.remove(filename);
        File writeFile = SD.open(filename, FILE_WRITE);
        if (writeFile) {
          writeFile.print(remainingData);
          writeFile.close();
        }
      }
    }
  }
  
  delay(1000);  // Small delay to prevent overwhelming the system
}
