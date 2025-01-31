#include <SD.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <ModbusMaster.h>
#include <SHT31.h>
#include <rpcWiFi.h>

SoftwareSerial serial(D2, D3);      // For data transmission
SoftwareSerial SerialMod(D1, D0);   // For Modbus
ModbusMaster node;
SHT31 sht;

// WiFi config
const char* ssid = "lime";
const char* password = "00000000";
bool wifiConnected = false;
unsigned long previousWiFiCheck = 0;
const long WIFI_CHECK_INTERVAL = 14000;

// File and timing
char filename[25] = "/mesinmf.csv";
unsigned long previousMillis = 0;
unsigned long previousMillisSensor = 0;
const long INTERVAL = 10000;        // Interval kirim data SD
const long SENSOR_INTERVAL = 30000; // Interval baca sensor

// Tambahkan konstanta untuk FIFO
const int MAX_DATA_ROWS = 100;  // Maksimum baris data yang disimpan

typedef struct {
  float V;
  float F;
} READING;

// Fungsi untuk menghitung jumlah baris data
int countDataLines() {
    File readFile = SD.open(filename);
    int lineCount = -1;  // -1 karena baris pertama adalah header
    
    if (readFile) {
        while (readFile.available()) {
            String line = readFile.readStringUntil('\n');
            lineCount++;
        }
        readFile.close();
    }
    return lineCount;
}

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
    node.begin(17, SerialMod);
    
    if (!SD.begin(SDCARD_SS_PIN)) {
        Serial.println("SD card initialization failed!");
        return;
    }
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.println("SD card initialized.");
}

void loop() {
    unsigned long currentMillis = millis();

    // Cek WiFi setiap 14 detik
    if (currentMillis - previousWiFiCheck >= WIFI_CHECK_INTERVAL) {
        previousWiFiCheck = currentMillis;
        checkWiFiConnection();
    }

    // Baca sensor setiap 20 detik
    if (currentMillis - previousMillisSensor >= SENSOR_INTERVAL) {
        previousMillisSensor = currentMillis;
        
        // Baca sensor SHT31
        sht.read();
        float temperature = sht.getTemperature();
        float humidity = sht.getHumidity();
        
        // Baca Modbus
        READING r;
        uint8_t result = node.readHoldingRegisters(0002, 10);
        if (result == node.ku8MBSuccess) {
            r.V = (float)node.getResponseBuffer(0x00) / 100;
            r.F = (float)node.getResponseBuffer(0x09) / 100;
        } else {
            r.V = 0;
            r.F = 0;
        }

        if (!wifiConnected) {
            // Implementasi FIFO untuk backup
            int currentLines = countDataLines();
            
            // Jika file belum ada, buat dengan header
            if (!SD.exists(filename)) {
                File headerFile = SD.open(filename, FILE_WRITE);
                if (headerFile) {
                    headerFile.println("Timestamp;Temperature;Humidity;Voltage;Frequency");
                    headerFile.close();
                }
            }
            
            // Jika jumlah data melebihi batas
            if (currentLines >= MAX_DATA_ROWS) {
                // Baca semua data kecuali baris pertama (yang tertua)
                File readFile = SD.open(filename);
                String header = readFile.readStringUntil('\n');
                String remainingData = header + "\n";
                
                // Skip baris pertama data (FIFO)
                readFile.readStringUntil('\n');
                
                // Salin sisa data
                while (readFile.available()) {
                    remainingData += readFile.readStringUntil('\n');
                    if (readFile.available()) {
                        remainingData += '\n';
                    }
                }
                readFile.close();
                
                // Tulis ulang file
                SD.remove(filename);
                File writeFile = SD.open(filename, FILE_WRITE);
                if (writeFile) {
                    writeFile.print(remainingData);
                    // Tambah data baru
                    char buffer[128];
                    snprintf(buffer, sizeof(buffer), 
                             "%s;%.2f;%.2f;%.2f;%.2f\n",
                             "backup",
                             temperature, humidity, r.V, r.F);
                    writeFile.print(buffer);
                    writeFile.close();
                }
            } else {
                // Tambah data baru seperti biasa
                File dataFile = SD.open(filename, FILE_WRITE);
                if (dataFile) {
                    char buffer[128];
                    snprintf(buffer, sizeof(buffer), 
                             "%s;%.2f;%.2f;%.2f;%.2f\n",
                             "backup",
                             temperature, humidity, r.V, r.F);
                    dataFile.print(buffer);
                    dataFile.close();
                }
            }
            Serial.println("Data backed up to SD (FIFO)");
        } else {
            // Kirim langsung jika WiFi hidup
            String datakirim = String("1#") + 
                             String(r.V, 2) + "#" +
                             String(r.F, 2) + "#" +
                             String(temperature, 2) + "#" +
                             String(humidity, 2);
            serial.println(datakirim);
            Serial.println("Data sent directly");
        }
    }

    // Kirim data backup dari SD jika WiFi hidup
    if (currentMillis - previousMillis >= INTERVAL && wifiConnected) {
        previousMillis = currentMillis;
        Serial.println("Processing CSV..."); 
        
        File readFile = SD.open(filename);
        if (readFile && readFile.available()) {
            // Skip header
            String header = readFile.readStringUntil('\n');
            String remainingData = header + "\n";
            bool dataSent = false;
            
            // Baca satu baris data
            if (readFile.available()) {
                String line = readFile.readStringUntil('\n');
                if (line.length() > 0) {
                    int semicolons = 0;
                    for (unsigned int i = 0; i < line.length(); i++) {
                        if (line[i] == ';') semicolons++;
                    }
                    
                    if (semicolons == 4) {
                        // Ekstrak dan kirim data
                        int pos1 = line.indexOf(';');
                        int pos2 = line.indexOf(';', pos1 + 1);
                        int pos3 = line.indexOf(';', pos2 + 1);
                        int pos4 = line.indexOf(';', pos3 + 1);
                        
                        String temp = line.substring(pos1 + 1, pos2);
                        String hum = line.substring(pos2 + 1, pos3);
                        String volt = line.substring(pos3 + 1, pos4);
                        String freq = line.substring(pos4 + 1);
                        
                        temp.trim(); hum.trim(); volt.trim(); freq.trim();
                        
                        String datakirim = String("1#") + volt + "#" + freq + "#" + 
                                         temp + "#" + hum;
                        
                        serial.println(datakirim);
                        Serial.println("Sent data: " + datakirim);
                        dataSent = true;
                    }
                }
            }
            
            // Salin sisa data
            while (readFile.available()) {
                remainingData += readFile.readStringUntil('\n');
                if (readFile.available()) {
                    remainingData += '\n';
                }
            }
            
            readFile.close();
            
            // Update file jika ada data yang terkirim
            if (dataSent) {
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
