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
char filename[25] = "/tth.csv";
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

// NTP setup
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "id.pool.ntp.org", 25200, 60000); // Offset 25200 detik untuk WIB
unsigned long baseTime = 0;  // Waktu epoch yang diambil dari NTP

// Time reference file
const char* timeRefFile = "/timref.csv";

// Function declarations
void checkWiFiConnection();
int countDataLines();
void setupDisplay();
void updateDisplay(float temperature, float humidity, float voltage, float frequency, 
                  bool modbusOK, bool wifiOK, bool sensorOK, unsigned long timestamp);

// Function to get stored timestamp
unsigned long getStoredTimestamp() {
    unsigned long lastTimestamp = 0;
    
    if (SD.exists(timeRefFile)) {
        File file = SD.open(timeRefFile);
        if (file) {
            while (file.available()) {
                String timestamp = file.readStringUntil('\n');
                timestamp.trim();
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

// Function to save timestamp
void saveTimestamp(unsigned long timestamp) {
    File file = SD.open(timeRefFile, FILE_WRITE);
    if (file) {
        file.println(timestamp);
        file.close();
        Serial.print("Saved new timestamp: ");
        Serial.println(timestamp);
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

void setup() {
    // Initialize Serial communications
    Serial.begin(115200);
    serial.begin(19200);
    SerialMod.begin(9600);
    
    // Initialize I2C devices
    Wire.begin();
    sht.begin(0x44);
    node.begin(17, SerialMod);
    
    // Initialize display
    setupDisplay();
    
    // Initialize SD card
    if (!SD.begin(SDCARD_SS_PIN)) {
        Serial.println("SD card initialization failed!");
        return;
    }
    Serial.println("SD card initialized.");
    
    // Initialize WiFi
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
    // Display WiFi scanning message
    tft.fillScreen(TFT_BLACK);
    tft.setFreeFont(&FreeSansOblique12pt7b);
    tft.println(" ");
    tft.drawString("Scanning Network!", 8, 5);
    delay(1000);
    
    // Scan and display networks
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
    
    // Connect to WiFi
    Serial.print("Connecting to WiFi");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.println("WiFi Connected!");
        
        timeClient.begin();
        if (timeClient.update()) {
            unsigned long ntpTime = timeClient.getEpochTime();
            saveTimestamp(ntpTime);
            baseTime = ntpTime;
        } else {
            baseTime = getStoredTimestamp();
        }
    } else {
        baseTime = getStoredTimestamp();
    }
}

void updateDisplay(float temperature, float humidity, float voltage, float frequency, 
                  bool modbusOK, bool wifiOK, bool sensorOK, unsigned long timestamp) {
    // Status indicators
    tft.setTextColor(wifiOK ? TFT_GREEN : TFT_RED);
    tft.drawString("WiFi", 8, 5);
    tft.drawString("HTTP", 53, 5);
    
    tft.setTextColor(modbusOK ? TFT_GREEN : TFT_RED);
    tft.drawString("Modbus", 112, 5);
    
    tft.setTextColor(sensorOK ? TFT_GREEN : TFT_RED);
    tft.drawString("Sensor", 183, 5);

    // Time display
    spr.createSprite(70, 20);
    spr.fillSprite(TFT_BLACK);
    spr.setTextColor(TFT_WHITE);
    spr.setCursor(18, 14.5);
    spr.setFreeFont(&FreeSans9pt7b);
    spr.print(timestamp % 86400 / 3600, DEC);
    spr.print(':');
    spr.print(timestamp % 3600 / 60, DEC);
    spr.pushSprite(255, 5);
    spr.deleteSprite();

    // Dividing lines
    tft.drawFastVLine(160, 25, 220, TFT_DARKCYAN);
    tft.drawFastHLine(0, 135, 320, TFT_DARKCYAN);
    tft.drawFastHLine(0, 25, 320, TFT_DARKCYAN);

    // Temperature quadrant
    spr.createSprite(158, 102);
    spr.fillSprite(TFT_BLACK);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("Temp", 55, 8);
    spr.setTextColor(TFT_GREEN);
    spr.setFreeFont(FSSO24);
    spr.drawString(String(temperature, 1), 25, 36);
    spr.setTextSize(1);
    spr.setTextColor(TFT_YELLOW);
    spr.setFreeFont(&FreeSans9pt7b);
    spr.drawString("C", 113, 85);
    spr.pushSprite(0, 27);
    spr.deleteSprite();

    // Voltage quadrant
    spr.createSprite(158, 102);
    spr.fillSprite(TFT_BLACK);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("Voltage", 50, 8);
    spr.setTextColor(TFT_GREEN);
    spr.setFreeFont(FSSO24);
    spr.drawString(String(voltage, 1), 11, 36);
    spr.setTextSize(1);
    spr.setTextColor(TFT_YELLOW);
    spr.setFreeFont(&FreeSans9pt7b);
    spr.drawString("VAC", 105, 85);
    spr.pushSprite(162, 27);
    spr.deleteSprite();

    // Frequency quadrant
    spr.createSprite(158, 100);
    spr.fillSprite(TFT_BLACK);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("Freq", 62, 6);
    spr.setTextColor(TFT_GREEN);
    spr.setFreeFont(FSSO24);
    spr.drawString(String(frequency, 1), 26, 34);
    spr.setTextSize(1);
    spr.setTextColor(TFT_YELLOW);
    spr.setFreeFont(&FreeSans9pt7b);
    spr.drawString("Hz", 108, 82);
    spr.pushSprite(162, 137);
    spr.deleteSprite();

    // Humidity quadrant
    spr.createSprite(158, 100);
    spr.fillSprite(TFT_BLACK);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("Humidity", 43, 6);
    spr.setTextColor(TFT_GREEN);
    spr.setFreeFont(FSSO24);
    spr.drawString(String(humidity, 1), 25, 34);
    spr.setTextSize(1);
    spr.setTextColor(TFT_YELLOW);
    spr.setFreeFont(&FreeSans9pt7b);
    spr.drawString("%RH", 65, 82);
    spr.pushSprite(0, 137);
    spr.deleteSprite();
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
        
        // Read sensors
        sht.read();
        float temperature = sht.getTemperature();
        float humidity = sht.getHumidity();
        
        // Calculate timestamp
        unsigned long timestamp = baseTime + (currentMillis / 1000);
        saveTimestamp(timestamp);
        
        // Read Modbus
        READING r;
        uint8_t result = node.readHoldingRegisters(0002, 10);
        bool modbusOK = (result == node.ku8MBSuccess);
        if (modbusOK) {
            r.V = (float)node.getResponseBuffer(0x00) / 100;
            r.F = (float)node.getResponseBuffer(0x09) / 100;
        } else {
            r.V = 0;
            r.F = 0;
        }
        
        // Update display
        updateDisplay(temperature, humidity, r.V, r.F, modbusOK, 
                     wifiConnected, (humidity != 0), timestamp);

        // Handle data storage and transmission
        if (!wifiConnected) {
            Serial.println("WiFi disconnected, backing up data...");
            
            if (!SD.exists(filename)) {
                File headerFile = SD.open(filename, FILE_WRITE);
                if (headerFile) {
                    headerFile.println("Temperature;Humidity;Voltage;Frequency;Timestamp");
                    headerFile.close();
                    Serial.println("Created new CSV file with header");
                }
            }
            
            int currentLines = countDataLines();
            
            if (currentLines >= MAX_DATA_ROWS) {
                File readFile = SD.open(filename);
                if (!readFile) {
                    Serial.println("Failed to open file for reading!");
                    return;
                }
                
                String header = readFile.readStringUntil('\n');
                String remainingData = header + "\n";
                readFile.readStringUntil('\n'); // Skip oldest line
                
                while (readFile.available()) {
                    String line = readFile.readStringUntil('\n');
                    remainingData += line + "\n";
                }
                readFile.close();
                
                if (SD.remove(filename)) {
                    File writeFile = SD.open(filename, FILE_WRITE);
                    if (writeFile) {
                        writeFile.print(remainingData);
                        writeFile.printf("%.2f;%.2f;%.2f;%.2f;%lu\n",
                                    temperature, humidity, r.V, r.F, timestamp);
                        writeFile.close();
                    }
                }
            } else {
                File dataFile = SD.open(filename, FILE_WRITE);
                if (dataFile) {
                    dataFile.printf("%.2f;%.2f;%.2f;%.2f;%lu\n",
                                temperature, humidity, r.V, r.F, timestamp);
                    dataFile.close();
                }
            }
        } else {
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
    
    // Process backed up data when WiFi is available - fixed
    if (currentMillis - previousMillis >= INTERVAL && wifiConnected) {
        previousMillis = currentMillis;
        Serial.println("Checking backup data...");
        
        if (SD.exists(filename)) {
            File readFile = SD.open(filename);
            if (readFile && readFile.available()) {
                String header = readFile.readStringUntil('\n');
                String remainingData = "";
                remainingData += header;
                remainingData += "\n";
                bool dataSent = false;
                
                if (readFile.available()) {
                    String line = readFile.readStringUntil('\n');
                    line.trim();  // Remove any whitespace/newline
                    
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
                            
                            // Clean up the strings
                            temp.trim(); 
                            hum.trim(); 
                            volt.trim(); 
                            freq.trim(); 
                            timestamp.trim();
                            
                            String datakirim = String("1#") + 
                                             temp + "#" + 
                                             hum + "#" + 
                                             volt + "#" + 
                                             freq + "#" + 
                                             timestamp;
                            
                            serial.println(datakirim);
                            Serial.println("Sent backup: " + datakirim);
                            dataSent = true;
                        }
                    }
                }
                
                // Copy remaining lines
                while (readFile.available()) {
                    String line = readFile.readStringUntil('\n');
                    if (line.length() > 0) {
                        remainingData += line;
                        remainingData += "\n";
                    }
                }
                readFile.close();
                
                if (dataSent) {
                    SD.remove(filename);
                    File writeFile = SD.open(filename, FILE_WRITE);
                    if (writeFile) {
                        writeFile.print(remainingData);
                        writeFile.close();
                        Serial.println("Backup file updated");
                    }
                }
            }
        }
    }
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
