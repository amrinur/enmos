#include <SD.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <ModbusMaster.h>
#include <SHT31.h>
#include <rpcWiFi.h>
#include "TFT_eSPI.h"
#include "Free_Fonts.h"
#include <WiFiUdp.h>
#include <NTPClient.h>

// Display setup
TFT_eSPI tft;
TFT_eSprite spr = TFT_eSprite(&tft);
#define LCD_BACKLIGHT (72Ul)

// Communication setup
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
const long INTERVAL = 17000;        // Interval kirim data SD
const long SENSOR_INTERVAL = 30000; // Interval baca sensor

// Display timing
unsigned long previousDisplayMillis = 0;
const long DISPLAY_INTERVAL = 180000; // 3 minutes backlight timer
bool screenOn = true;

// FIFO constants
const int MAX_DATA_ROWS = 100;

typedef struct {
    float V;
    float F;
} READING;

// Tambahkan variabel global untuk NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "id.pool.ntp.org", 25200, 60000); // Offset 25200 detik untuk WIB
unsigned long baseTime = 0;  // Waktu epoch yang diambil dari NTP

// Tambah nama file untuk timestamp referensi
const char* timeRefFile = "/timref.csv";

// Function declarations
void checkWiFiConnection();
int countDataLines();
void setupDisplay();

// Fungsi untuk membaca timestamp referensi terbaru dari SD
unsigned long getStoredTimestamp() {
    unsigned long lastTimestamp = 0;
    
    if (SD.exists(timeRefFile)) {
        File file = SD.open(timeRefFile);
        if (file) {
            // Baca sampai baris terakhir
            while (file.available()) {
                String timestamp = file.readStringUntil('\n');
                timestamp.trim();  // Hapus whitespace/newline
                if (timestamp.length() > 0) {
                    lastTimestamp = timestamp.toInt();
                }
            }
            file.close();
        }
    }
    
    Serial.print("Last stored timestamp: ");
    Serial.println(lastTimestamp);
    return lastTimestamp;
}

// Fungsi untuk menyimpan timestamp referensi (append mode)
void saveTimestamp(unsigned long timestamp) {
    File file = SD.open(timeRefFile, FILE_WRITE);
    if (file) {
        file.println(timestamp);
        file.close();
        Serial.print("Saved new timestamp: ");
        Serial.println(timestamp);
    }
}

void setup() {
    // Initialize Serial communications
    Serial.begin(115200);
    serial.begin(19200);
    SerialMod.begin(9600);
    
    // Initialize I2C devices
    Wire.begin();
    sht.begin(0x44);
    node.begin(17, SerialMod);
    
    // Initialize SD card
    if (!SD.begin(SDCARD_SS_PIN)) {
        Serial.println("SD card initialization failed!");
        return;
    }
    Serial.println("SD card initialized.");
    
    // Initialize WiFi
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
    Serial.print("Connecting to WiFi");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.println("WiFi Connected!");
        
        // Inisialisasi NTP
        timeClient.begin();
        if (timeClient.update()) {
            unsigned long ntpTime = timeClient.getEpochTime();
            saveTimestamp(ntpTime);  // Simpan timestamp NTP ke SD
            baseTime = ntpTime;
        } else {
            baseTime = getStoredTimestamp();  // Gunakan timestamp tersimpan jika NTP gagal
        }
        Serial.print("Base time: ");
        Serial.println(baseTime);
    } else {
        baseTime = getStoredTimestamp();  // Gunakan timestamp tersimpan jika offline
    }
    
    // Initialize Display
    setupDisplay();
    
    // Display initial WiFi scanning message
    tft.fillScreen(TFT_BLACK);
    tft.setFreeFont(&FreeSansOblique12pt7b);
    tft.println(" ");
    tft.drawString("Scanning Network!", 8, 5);
    delay(1000);
    
    // Scan and display available networks
    int n = WiFi.scanNetworks();
    tft.fillScreen(TFT_BLACK);
    tft.setFreeFont(&FreeSans9pt7b);
    tft.setCursor(87, 22);
    tft.print(n);
    tft.println(" networks found");
    
    for (int i = 0; i < n; i++) {
        tft.println(String(i + 1) + ". " + String(WiFi.SSID(i)) + String(WiFi.RSSI(i)));
    }
    delay(2000);
    tft.fillScreen(TFT_BLACK);
}

void loop() {
    unsigned long currentMillis = millis();
    
    // Check backlight timer
    if (screenOn && currentMillis - previousDisplayMillis >= DISPLAY_INTERVAL) {
        previousDisplayMillis = currentMillis;
        screenOn = false;
        digitalWrite(LCD_BACKLIGHT, LOW);
        Serial.println("Display off");
    }

    // Pembacaan sensor dan update display setiap 1 detik
    static unsigned long lastDisplayUpdate = 0;
    if (currentMillis - lastDisplayUpdate >= 1000) {
        lastDisplayUpdate = currentMillis;
        
        // Baca sensor dan modbus untuk display
        sht.read();
        float t = sht.getTemperature();
        float h = sht.getHumidity();
        
        READING r;
        uint8_t result = node.readHoldingRegisters(0002, 10);
        if (result == node.ku8MBSuccess) {
            r.V = (float)node.getResponseBuffer(0x00) / 100;
            r.F = (float)node.getResponseBuffer(0x09) / 100;
            tft.setTextColor(TFT_GREEN);
            tft.drawString("Modbus", 112, 5);
        } else {
            r.V = 0;
            r.F = 0;
            tft.setTextColor(TFT_RED);
            tft.drawString("Modbus", 112, 5);
        }

        // Update display status dan garis
        tft.setFreeFont(&FreeSans9pt7b);
        spr.setFreeFont(&FreeSans9pt7b);
        tft.setTextSize(1);

        // Status WiFi
        if (WiFi.status() == WL_CONNECTED) {
            tft.setTextColor(TFT_GREEN);
        } else {
            tft.setTextColor(TFT_RED);
        }
        tft.drawString("WiFi", 8, 5);

        // Status Sensor
        if (h == 0) {
            tft.setTextColor(TFT_RED);
        } else {
            tft.setTextColor(TFT_GREEN);
        }
        tft.drawString("Sensor", 183, 5);

        // Draw dividing lines
        tft.drawFastVLine(160, 25, 220, TFT_DARKCYAN);
        tft.drawFastHLine(0, 135, 320, TFT_DARKCYAN);
        tft.drawFastHLine(0, 25, 320, TFT_DARKCYAN);

        // Update quadrants
        updateQuadrant1(t);     // Temperature
        updateQuadrant2(r.V);   // Voltage
        updateQuadrant3(r.F);   // Frequency
        updateQuadrant4(h);     // Humidity
    }

    // Proses penyimpanan dan pengiriman data
    if (currentMillis - previousMillisSensor >= SENSOR_INTERVAL) {
        previousMillisSensor = currentMillis;
        unsigned long timestamp = baseTime + (currentMillis / 1000);
        saveTimestamp(timestamp);
        
        // Gunakan data sensor yang sudah dibaca sebelumnya
        float temperature = sht.getTemperature();
        float humidity = sht.getHumidity();
        
        // Handle data storage and transmission
        if (!wifiConnected) {
            // Debug print
            Serial.println("WiFi disconnected, backing up data...");
            
            // Check if file exists and create with header if needed
            if (!SD.exists(filename)) {
                File headerFile = SD.open(filename, FILE_WRITE);
                if (headerFile) {
                    headerFile.println("Temperature;Humidity;Voltage;Frequency;Timestamp");
                    headerFile.close();
                    Serial.println("Created new CSV file with header");
                }
            }
            
            // Get current number of data rows
            int currentLines = countDataLines();
            Serial.print("Current lines in CSV: ");
            Serial.println(currentLines);
            
            if (currentLines >= MAX_DATA_ROWS) {
                // Debug FIFO operation
                Serial.println("Max rows reached, implementing FIFO...");
                
                File readFile = SD.open(filename);
                if (!readFile) {
                    Serial.println("Failed to open file for reading!");
                    return;
                }
                
                String header = readFile.readStringUntil('\n');
                String remainingData = header + "\n";
                readFile.readStringUntil('\n'); // Skip oldest line
                
                // Read remaining lines
                while (readFile.available()) {
                    String line = readFile.readStringUntil('\n');
                    remainingData += line + "\n";
                }
                readFile.close();
                
                // Write updated data
                if (SD.remove(filename)) {
                    File writeFile = SD.open(filename, FILE_WRITE);
                    if (writeFile) {
                        writeFile.print(remainingData);
                        writeFile.printf("%.2f;%.2f;%.2f;%.2f;%lu\n",
                                    temperature, humidity, r.V, r.F, timestamp);
                        writeFile.close();
                        Serial.println("FIFO update successful");
                    }
                }
            } else {
                // Append new data
                File dataFile = SD.open(filename, FILE_WRITE);
                if (dataFile) {
                    dataFile.printf("%.2f;%.2f;%.2f;%.2f;%lu\n",
                                temperature, humidity, r.V, r.F, timestamp);
                    dataFile.close();
                    Serial.println("Data appended to CSV");
                } else {
                    Serial.println("Failed to open file for append!");
                }
            }
        } else {
            // Modified direct data transmission with correct order
            String datakirim = String("1#") + 
                             String(temperature, 2) + "#" +
                             String(humidity, 2) + "#" +
                             String(r.V, 2) + "#" +
                             String(r.F, 2) + "#" +
                             String(timestamp);
            serial.println(datakirim);
            Serial.println("Data sent directly");
        }
    }
    
    // Process backed up data when WiFi is available
    if (currentMillis - previousMillis >= INTERVAL && wifiConnected) {
        previousMillis = currentMillis;
        Serial.println("Checking backup data...");
        
        if (SD.exists(filename)) {
            File readFile = SD.open(filename);
            if (readFile && readFile.available()) {
                String header = readFile.readStringUntil('\n');
                String remainingData = header + "\n";
                bool anyDataSent = false;
                
                // Proses semua baris data
                while (readFile.available()) {
                    String line = readFile.readStringUntil('\n');
                    line.trim();
                    
                    if (line.length() > 0) {
                        Serial.println("Processing line: " + line);
                        
                        int pos1 = line.indexOf(';');
                        int pos2 = line.indexOf(';', pos1 + 1);
                        int pos3 = line.indexOf(';', pos2 + 1);
                        int pos4 = line.indexOf(';', pos3 + 1);
                        
                        if (pos1 > 0 && pos2 > pos1 && pos3 > pos2 && pos4 > pos3) {
                            String temp = line.substring(0, pos1);
                            String hum = line.substring(pos1 + 1, pos2);
                            String volt = line.substring(pos2 + 1, pos3);
                            String freq = line.substring(pos3 + 1, pos4);
                            String timestamp = line.substring(pos4 + 1);
                            
                            String datakirim = String("1#") + 
                                             temp + "#" + 
                                             hum + "#" + 
                                             volt + "#" + 
                                             freq + "#" + 
                                             timestamp;
                            
                            serial.println(datakirim);
                            Serial.println("Sent backup: " + datakirim);
                            anyDataSent = true;
                            
                            // Tambah delay kecil untuk memastikan data terkirim
                            delay(100);
                        }
                    }
                }
                readFile.close();
                
                // Hapus file backup jika semua data telah terkirim
                if (anyDataSent) {
                    SD.remove(filename);
                    File writeFile = SD.open(filename, FILE_WRITE);
                    if (writeFile) {
                        writeFile.println(header);
                        writeFile.close();
                        Serial.println("Backup file cleared, header restored");
                    }
                }
            }
        }
    }
}

void setupDisplay() {
    tft.begin();
    tft.init();
    tft.setRotation(3);
    spr.createSprite(TFT_HEIGHT, TFT_WIDTH);
    spr.setRotation(3);
    
    pinMode(LCD_BACKLIGHT, OUTPUT);
    digitalWrite(LCD_BACKLIGHT, HIGH);
    
    tft.fillScreen(TFT_BLACK);
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

void updateQuadrant1(float temp) {
    spr.createSprite(158, 102);
    spr.fillSprite(TFT_BLACK);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("Temp", 55, 8);
    spr.setTextColor(TFT_GREEN);
    spr.setFreeFont(FSSO24);
    spr.drawString(String(temp, 1), 25, 36);
    spr.setTextColor(TFT_YELLOW);
    spr.setFreeFont(&FreeSans9pt7b);
    spr.drawString("C", 113, 85);
    spr.pushSprite(0, 27);
    spr.deleteSprite();
}

void updateQuadrant2(float voltage) {
    spr.createSprite(158, 102);
    spr.fillSprite(TFT_BLACK);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("Voltage", 50, 8);
    spr.setTextColor(TFT_GREEN);
    spr.setFreeFont(FSSO24);
    spr.drawString(String(voltage, 1), 11, 36);
    spr.setTextColor(TFT_YELLOW);
    spr.setFreeFont(&FreeSans9pt7b);
    spr.drawString("VAC", 105, 85);
    spr.pushSprite(162, 27);
    spr.deleteSprite();
}

void updateQuadrant3(float freq) {
    spr.createSprite(158, 100);
    spr.fillSprite(TFT_BLACK);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("Freq", 62, 6);
    spr.setTextColor(TFT_GREEN);
    spr.setFreeFont(FSSO24);
    spr.drawString(String(freq, 1), 26, 34);
    spr.setTextColor(TFT_YELLOW);
    spr.setFreeFont(&FreeSans9pt7b);
    spr.drawString("Hz", 108, 82);
    spr.pushSprite(162, 137);
    spr.deleteSprite();
}

void updateQuadrant4(float humidity) {
    spr.createSprite(158, 100);
    spr.fillSprite(TFT_BLACK);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("Humidity", 43, 6);
    spr.setTextColor(TFT_GREEN);
    spr.setFreeFont(FSSO24);
    spr.drawString(String(humidity, 1), 25, 34);
    spr.setTextColor(TFT_YELLOW);
    spr.setFreeFont(&FreeSans9pt7b);
    spr.drawString("%RH", 65, 82);
    spr.pushSprite(0, 137);
    spr.deleteSprite();
}
