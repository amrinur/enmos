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
const long INTERVAL = 20000;  // Interval for data transmission
bool isWiFiConnected = false;  // Add global variable for WiFi status
unsigned long lastEspResponse = 0;
const unsigned long ESP_TIMEOUT = 5000; // 5 seconds timeout

unsigned long previousMillisCSV = 0;
const long CSV_READ_INTERVAL = 1000;  // Interval for reading CSV and sending to ESP

// Tambahkan variabel global untuk tracking nomor antrian
unsigned long queueNumber = 1;

void setup() {
  Serial.begin(115200);
  serial.begin(19200);
  SerialMod.begin(9600);
  
  Wire.begin();
  sht.begin();    // SHT31 I2C Address
  rtc.begin();
  
  DateTime now = DateTime(F(__DATE__), F(__TIME__));
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
  
  File file = SD.open(filename, FILE_WRITE);
  if (file) {
    // Format CSV dengan padding dan nomor antrian
    char buffer[128];
    snprintf(buffer, sizeof(buffer), 
             "%04lu;%04d-%02d-%02d %02d:%02d:%02d;%8.2f;%8.2f;%8.2f;%8.2f\n",
             queueNumber,
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second(),
             temperature, humidity, r.V, r.F);
    
    file.print(buffer);
    queueNumber++;
    
    file.flush();
    file.close();
  } else {
    Serial.println("Error opening log file");
  }

  // Read first line from CSV and send to ESP at intervals (FIFO)
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillisCSV >= CSV_READ_INTERVAL) {
    previousMillisCSV = currentMillis;
    
    File readFile = SD.open(filename);
    String remainingData = "";
    
    if (readFile && readFile.available()) {
      // Baca dan proses baris pertama
      char firstLine[128];
      int i = 0;
      while (readFile.available() && i < sizeof(firstLine) - 1) {
        char c = readFile.read();
        if (c == '\n') break;
        firstLine[i++] = c;
      }
      firstLine[i] = '\0';
      
      // Parse CSV dengan format baru
      String line = String(firstLine);
      int pos0 = line.indexOf(';');
      int pos1 = line.indexOf(';', pos0 + 1);
      int pos2 = line.indexOf(';', pos1 + 1);
      int pos3 = line.indexOf(';', pos2 + 1);
      int pos4 = line.indexOf(';', pos3 + 1);
      
      if (pos0 != -1 && pos1 != -1 && pos2 != -1 && pos3 != -1 && pos4 != -1) {
        // Parsing tanpa menggunakan trim()
        String qNum = line.substring(0, pos0);
        String temp = line.substring(pos2 + 1, pos3);
        String hum = line.substring(pos3 + 1, pos4);
        String volt = line.substring(pos4 + 1);
        
        // Hapus spasi manual jika diperlukan
        temp.replace(" ", "");
        hum.replace(" ", "");
        volt.replace(" ", "");
        
        String datakirim = String("1#") + 
                          volt + "#" +
                          String(r.F, 1) + "#" +
                          temp + "#" +
                          hum;
        
        serial.println(datakirim);
        Serial.print("Queue #"); Serial.print(qNum); 
        Serial.println(" sent to ESP: " + datakirim);
        
        // FIFO: Simpan sisa data
        char buffer[128];
        while (readFile.available()) {
          int bytesRead = readFile.readBytesUntil('\n', buffer, sizeof(buffer)-1);
          if (bytesRead > 0) {
            buffer[bytesRead] = '\0';
            remainingData += String(buffer) + "\n";
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
