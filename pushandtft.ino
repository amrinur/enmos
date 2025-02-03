#include <SD.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <ModbusMaster.h>
#include <SHT31.h>
#include <rpcWiFi.h>
#include "TFT_eSPI.h"
#include "Free_Fonts.h"

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

// Tambahkan variabel untuk timestamp
unsigned long startTime = 0;  // Akan diset di setup()

// Tambahkan variabel global baru untuk patokan timestamp
bool baselineRecorded = false;
unsigned long baselineTimestamp = 0;

// Function declarations
void checkWiFiConnection();
int countDataLines();
void updateDisplay(float temperature, float humidity, float voltage, float frequency, bool modbusOK, bool wifiOK, bool sensorOK);
void setupDisplay();

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
    }
    
    // Set waktu awal (1 Januari 2024 00:00:00 GMT+7)
    startTime = 1704047400;  // Unix timestamp untuk 2024-01-01 00:00:00 GMT+7
    
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
    
    // Check WiFi connection
    if (currentMillis - previousWiFiCheck >= WIFI_CHECK_INTERVAL) {
        previousWiFiCheck = currentMillis;
        checkWiFiConnection();
    }
    
    // Read sensors and update display
    if (currentMillis - previousMillisSensor >= SENSOR_INTERVAL) {
        previousMillisSensor = currentMillis;
        
        // Hitung current timestamp dari NTP
        unsigned long currentTimestamp = startTime + (currentMillis / 1000);
        unsigned long dataTimestamp;
        // Jika belum ada patokan, tulis baris patokan ke CSV
        if (!baselineRecorded) {
            dataTimestamp = currentTimestamp;  // gunakan nilai absolute sebagai patokan
            if (!SD.exists(filename)) {
                File headerFile = SD.open(filename, FILE_WRITE);
                if (headerFile) {
                    headerFile.println("Temperature;Humidity;Voltage;Frequency;Timestamp");
                    headerFile.close();
                }
            }
            // Tulis data patokan (tidak dikirim, hanya lokal), bisa gunakan marker atau nilai asli
            File baseFile = SD.open(filename, FILE_WRITE);
            if (baseFile) {
                baseFile.printf("%.2f;%.2f;%.2f;%.2f;%lu_baseline\n", 0.0, 0.0, 0.0, 0.0, currentTimestamp);
                baseFile.close();
            }
            baselineTimestamp = currentTimestamp;
            baselineRecorded = true;
            // Untuk pembacaan pertama, selisih dianggap 0
            dataTimestamp = 0;
        } else {
            // Hitung elapsed time sebagai selisih dari patokan
            dataTimestamp = currentTimestamp - baselineTimestamp;
        }
        
        // Read SHT31 sensor
        sht.read();
        float temperature = sht.getTemperature();
        float humidity = sht.getHumidity();
        
        // Read Modbus
        READING r;
        uint8_t result = node.readHoldingRegisters(0002, 10);
        if (result == node.ku8MBSuccess) {
            r.V = (float)node.getResponseBuffer(0x00) / 100;
            r.F = (float)node.getResponseBuffer(0x09) / 100;
        } else {
            r.V = 0;
            r.F = 0;
        }
        
        // Update display
        bool modbusOK = (result == node.ku8MBSuccess);
        bool sensorOK = (humidity != 0);
        updateDisplay(temperature, humidity, r.V, r.F, modbusOK, wifiConnected, sensorOK);
        
        // Handle data storage and transmission
        if (!wifiConnected) {
            // Implement FIFO for backup
            int currentLines = countDataLines();
            
            if (!SD.exists(filename)) {
                File headerFile = SD.open(filename, FILE_WRITE);
                if (headerFile) {
                    headerFile.println("Temperature;Humidity;Voltage;Frequency;Timestamp");
                    headerFile.close();
                }
            }
            
            // FIFO implementation with timestamp
            if (currentLines >= MAX_DATA_ROWS) {
                File readFile = SD.open(filename);
                String header = readFile.readStringUntil('\n');
                String remainingData = header + "\n";
                
                readFile.readStringUntil('\n'); // Skip first data line
                
                while (readFile.available()) {
                    remainingData += readFile.readStringUntil('\n');
                    if (readFile.available()) {
                        remainingData += '\n';
                    }
                }
                readFile.close();
                
                SD.remove(filename);
                File writeFile = SD.open(filename, FILE_WRITE);
                if (writeFile) {
                    writeFile.print(remainingData);
                    writeFile.printf("%.2f;%.2f;%.2f;%.2f;%lu\n",
                                   temperature, humidity, r.V, r.F, dataTimestamp);
                    writeFile.close();
                }
            } else {
                File dataFile = SD.open(filename, FILE_WRITE);
                if (dataFile) {
                    dataFile.printf("%.2f;%.2f;%.2f;%.2f;%lu\n",
                                  temperature, humidity, r.V, r.F, dataTimestamp);
                    dataFile.close();
                }
            }
            Serial.println("Data backed up to SD (FIFO)");
        } else {
            // Modified direct data transmission with correct order
            String datakirim = String("1#") + 
                             String(temperature, 2) + "#" +
                             String(humidity, 2) + "#" +
                             String(r.V, 2) + "#" +
                             String(r.F, 2) + "#" +
                             String(dataTimestamp);
            serial.println(datakirim);
            Serial.println("Data sent directly");
        }
    }
    
    // Process backed up data when WiFi is available
    if (currentMillis - previousMillis >= INTERVAL && wifiConnected) {
        previousMillis = currentMillis;
        
        File readFile = SD.open(filename);
        if (readFile && readFile.available()) {
            String header = readFile.readStringUntil('\n');
            String remainingData = header + "\n";
            bool dataSent = false;
            
            if (readFile.available()) {
                String line = readFile.readStringUntil('\n');
                if (line.length() > 0) {
                    int semicolons = 0;
                    for (unsigned int i = 0; i < line.length(); i++) {
                        if (line[i] == ';') semicolons++;
                    }
                    
                    if (semicolons == 4) {
                        int pos1 = line.indexOf(';');
                        int pos2 = line.indexOf(';', pos1 + 1);
                        int pos3 = line.indexOf(';', pos2 + 1);
                        int pos4 = line.indexOf(';', pos3 + 1);
                        
                        String temp = line.substring(0, pos1);
                        String hum = line.substring(pos1 + 1, pos2);
                        String volt = line.substring(pos2 + 1, pos3);
                        String freq = line.substring(pos3 + 1, pos4);
                        String timestamp = line.substring(pos4 + 1);
                        
                        temp.trim(); hum.trim(); volt.trim(); freq.trim(); timestamp.trim();
                        
                        String datakirim = String("1#") + 
                                         temp + "#" + 
                                         hum + "#" + 
                                         volt + "#" + 
                                         freq + "#" + 
                                         timestamp;
                        
                        serial.println(datakirim);
                        dataSent = true;
                    }
                }
            }
            
            while (readFile.available()) {
                remainingData += readFile.readStringUntil('\n');
                if (readFile.available()) {
                    remainingData += '\n';
                }
            }
            
            readFile.close();
            
            if (dataSent) {
                SD.remove(filename);
                File writeFile = SD.open(filename, FILE_WRITE);
                if (writeFile) {
                    writeFile.print(remainingData);
                    writeFile.close();
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

// void updateDisplay(float temperature, float humidity, float voltage, float frequency, 
//                   bool modbusOK, bool wifiOK, bool sensorOK) {
//     tft.setFreeFont(&FreeSans9pt7b);
//     spr.setFreeFont(&FreeSans9pt7b);
//     tft.setTextSize(1);

//     // Status indicators
//     tft.setTextColor(wifiOK ? TFT_GREEN : TFT_RED);
//     tft.drawString("WiFi", 8, 5);
    
//     tft.setTextColor(modbusOK ? TFT_GREEN : TFT_RED);
//     tft.drawString("Modbus", 112, 5);
    
//     tft.setTextColor(sensorOK ? TFT_GREEN : TFT_RED);
//     tft.drawString("Sensor", 183, 5);

//     // Draw dividing lines
//     tft.drawFastVLine(160, 25, 220, TFT_DARKCYAN);
//     tft.drawFastHLine(0, 135, 320, TFT_DARKCYAN);
//     tft.drawFastHLine(0, 25, 320, TFT_DARKCYAN);

//     // Temperature (Quadrant 1)
//     spr.createSprite(158, 102);
//     spr.fillSprite(TFT_BLACK);
//     spr.setTextColor(TFT_WHITE);
//     spr.drawString("Temp", 55, 8);
//     spr.setTextColor(TFT_GREEN);
//     spr.setFreeFont(FSSO24);
//     spr.drawString(String(temperature, 1), 25, 36);
//     spr.setTextColor(TFT_YELLOW);
//     spr.setFreeFont(&FreeSans9pt7b);
//     spr.drawString("C", 113, 85);
//     spr.pushSprite(0, 27);
//     spr.deleteSprite();

//     // Voltage (Quadrant 2)
//     spr.createSprite(158, 102);
//     spr.fillSprite(TFT_BLACK);
//     spr.setTextColor(TFT_WHITE);
//     spr.drawString("Voltage", 50, 8);
//     spr.setTextColor(TFT_GREEN);
//     spr.setFreeFont(FSSO24);
//     spr.drawString(String(voltage, 1), 11, 36);
//     spr.setTextColor(TFT_YELLOW);
//     spr.setFreeFont(&FreeSans9pt7b);
//     spr.drawString("VAC", 105, 85);
//     spr.pushSprite(162, 27);
//     spr.deleteSprite();

//     // Frequency (Quadrant 3)
//     spr.createSprite(158, 100);
//     spr.fillSprite(TFT_BLACK);
//     spr.setTextColor(TFT_WHITE);
//     spr.drawString("Freq", 62, 6);
//     spr.setTextColor(TFT_GREEN);
//     spr.setFreeFont(FSSO24);
//     spr.drawString(String(frequency, 1), 26, 34);
//     spr.setTextColor(TFT_YELLOW);
//     spr.setFreeFont(&FreeSans9pt7b);
//     spr.drawString("Hz", 108, 82);
//     spr.pushSprite(162, 137);
//     spr.deleteSprite();

//     // Humidity (Quadrant 4)
//     spr.createSprite(158, 100);
//     spr.fillSprite(TFT_BLACK);
//     spr.setTextColor(TFT_WHITE);
//     spr.drawString("Humidity", 43, 6);
//     spr.setTextColor(TFT_GREEN);
//     spr.setFreeFont(FSSO24);
//     spr.drawString(String(humidity, 1), 25, 34);
//     spr.setTextColor(TFT_YELLOW);
//     spr.setFreeFont(&FreeSans9pt7b);
//     spr.drawString("%RH", 65, 82);
//     spr.pushSprite(0, 137);
//     spr.deleteSprite();
// }

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
