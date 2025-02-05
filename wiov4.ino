#include <SD.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <ModbusMaster.h>
#include <SHT31.h>
#include "RTC_SAMD51.h"
#include "DateTime.h"
#include "Wire.h"
#include <rpcWiFi.h>    // Tambahkan library WiFi untuk Wio Terminal

RTC_SAMD51 rtc;
SHT31 sht;
SoftwareSerial serial(D2, D3);      // For data transmission
SoftwareSerial SerialMod(D1, D0);   // For Modbus
ModbusMaster node;

// Konfigurasi WiFi
const char* ssid = "lime";
const char* password = "00000000";
bool wifiConnected = false;
unsigned long previousWiFiCheck = 0;
const long WIFI_CHECK_INTERVAL = 14000; // Cek WiFi setiap 30 detik

typedef struct {
  float V;
  float F;
} READING;

unsigned long previousMillis2 = 0;
const long INTERVAL = 20000;        // 20 detik untuk pembacaan sensor
unsigned long previousMillisCSV = 0;
const long CSV_READ_INTERVAL = 10000;  // 20 detik untuk pengiriman data
unsigned long previousMillisSensor = 0;

char filename[25] = "/muftim.csv";

void checkWiFiConnection() {
    if (WiFi.status() != WL_CONNECTED) {
        wifiConnected = false;
        Serial.println("WiFi disconnected");
        WiFi.disconnect();
        WiFi.begin(ssid, password);
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 10) {
            delay(500);
            attempts++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            wifiConnected = true;
            Serial.println("WiFi reconnected");
        }
    } else {
        wifiConnected = true;
    }
}

void setup() {
  Serial.begin(115200);
  serial.begin(19200);
  SerialMod.begin(9600);
  
  Wire.begin();
  sht.begin(0x44);
  rtc.begin();
  
  DateTime now = DateTime(F(__DATE__), F(__TIME__));
  rtc.adjust(now);
  
  node.begin(17, SerialMod);
  
  if (!SD.begin(SDCARD_SS_PIN)) {
    Serial.println("SD card initialization failed!");
    return;
  }
  Serial.println("SD card initialized.");

  // Inisialisasi WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi...");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      Serial.println("WiFi connected");
  } else {
      Serial.println("WiFi connection failed");
  }
}

void loop() {
  unsigned long currentMillis = millis();

  // Cek status WiFi setiap 30 detik
  if (currentMillis - previousWiFiCheck >= WIFI_CHECK_INTERVAL) {
      previousWiFiCheck = currentMillis;
      checkWiFiConnection();
  }

  // Baca sensor dan tulis ke CSV setiap 20 detik
  if (currentMillis - previousMillisSensor >= INTERVAL) {
    previousMillisSensor = currentMillis;
    Serial.println("Reading sensors..."); // Debug
    
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

    // Jika WiFi mati, simpan ke CSV
    if (!wifiConnected) {
        // Write to CSV
        if (!SD.exists(filename)) {
            File headerFile = SD.open(filename, FILE_WRITE);
            if (headerFile) {
                headerFile.println("Timestamp;Temperature;Humidity;Voltage;Frequency");
                headerFile.close();
            }
        }
        
        File file = SD.open(filename, FILE_WRITE);
        if (file) {
            char buffer[128];
            snprintf(buffer, sizeof(buffer), 
                     "%04d/%02d/%02d %02d:%02d:%02d;%.2f;%.2f;%.2f;%.2f\n",
                      now.year(), now.month(), now.day(),
                      now.hour(), now.minute(), now.second(),
                      temperature, humidity, r.V, r.F);

            
            file.print(buffer);
            file.close();
            Serial.println("WiFi down, data saved to CSV");
        }
    } else {
        // Jika WiFi tersambung, kirim langsung ke ESP
        String datakirim = String("1#") + 
                         String(r.V, 2) + "#" +
                         String(r.F, 2) + "#" +
                         String(temperature, 2) + "#" +
                         String(humidity, 2);
        
        serial.println(datakirim);
        Serial.println("WiFi up, data sent directly");
    }
  }

  // Kirim data dari CSV hanya jika WiFi terhubung
  if (currentMillis - previousMillisCSV >= CSV_READ_INTERVAL && wifiConnected) {
    previousMillisCSV = currentMillis;
    Serial.println("Processing CSV..."); 
    
    File readFile = SD.open(filename);
    if (readFile && readFile.available()) {
      // Skip header
      String header = readFile.readStringUntil('\n');
      
      // Baca maksimal 5 baris data setiap interval
      int linesProcessed = 0;
      String remainingData = "";
      bool headerAdded = false;
      
      while (readFile.available() && linesProcessed < 5) {
        String line = readFile.readStringUntil('\n');
        if (line.length() > 0) {
          int semicolons = 0;
          for (unsigned int i = 0; i < line.length(); i++) {
            if (line[i] == ';') semicolons++;
          }
          
          if (semicolons == 4) {
            // Ekstrak timestamp dan data
            int pos1 = line.indexOf(';');
            String timestamp = line.substring(0, pos1); // Ambil timestamp
            int pos2 = line.indexOf(';', pos1 + 1);
            int pos3 = line.indexOf(';', pos2 + 1);
            int pos4 = line.indexOf(';', pos3 + 1);
            
            String temp = line.substring(pos1 + 1, pos2);
            String hum = line.substring(pos2 + 1, pos3);
            String volt = line.substring(pos3 + 1, pos4);
            String freq = line.substring(pos4 + 1);
            
            temp.trim(); hum.trim(); volt.trim(); freq.trim(); timestamp.trim();
            
            // Format baru: 1#voltage#frequency#temperature#humidity#timestamp
            String datakirim = String("1#") + volt + "#" + freq + "#" + 
                             temp + "#" + hum + "#" + timestamp;
            
            serial.println(datakirim);
            Serial.println("Sent backup data with timestamp: " + datakirim);
            
            delay(100);
            linesProcessed++;
          }
        }
        
        // Simpan sisa data yang belum diproses
        while (readFile.available()) {
          if (!headerAdded) {
            remainingData = "Timestamp;Temperature;Humidity;Voltage;Frequency\n";
            headerAdded = true;
          }
          remainingData += readFile.readStringUntil('\n');
          if (readFile.available()) {
            remainingData += '\n';
          }
        }
      }
      
      readFile.close();
      
      // Tulis ulang file dengan sisa data
      if (remainingData.length() > 0) {
        SD.remove(filename);
        File writeFile = SD.open(filename, FILE_WRITE);
        if (writeFile) {
          writeFile.print(remainingData);
          writeFile.close();
          Serial.println("File updated with remaining data");
        }
      }
    }
  }
}
