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
char filename[25] = "/epul.csv";
unsigned long previousMillis = 0;
unsigned long previousMillisSensor = 0;
unsigned long lastTimestamp = 0;
const long INTERVAL = 17000;        // Interval kirim data SD
const long SENSOR_INTERVAL = 30000; // Interval baca sensor
const int TRANSMISSION_DELAY = 1000; // Delay between transmissions in milliseconds
const unsigned long TIMESTAMP_INCREMENT = 10; // Increment 10 detik
bool isTransmittingBackup = false;
File backupFile;
String backupHeader;

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

// NTP variables
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000); // Offset 25200 detik untuk WIB
unsigned long baseTime = 0;  // Waktu epoch yang diambil dari NTP

// Time reference file
const char* timeRefFile = "/timref.csv";

// Function declarations
void checkWiFiConnection();
int countDataLines();
void updateDisplay(float temperature, float humidity, float voltage, float frequency, bool modbusOK, bool wifiOK, bool sensorOK);
void setupDisplay();
bool verifyCSVFormat();

// Tambahkan di bagian konstanta global
const int NTP_TIMEOUT = 10000;  // 10 detik timeout untuk NTP
const char* NTP_SERVER = "time.google.com";  // Google NTP server sebagai backup

// Sederhanakan fungsi validasi
bool isValidUnixTimestamp(unsigned long timestamp) {
    String timestampStr = String(timestamp);
    return (timestampStr.length() >= 10);  // Hanya cek minimal 10 digit
}

// Function to verify CSV format and content
bool verifyCSVFormat() {
    File readFile = SD.open(filename);
    if (!readFile) return false;
    
    int lineCount = 0;
    String lastLine;
    
    while (readFile.available()) {
        String line = readFile.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
            lineCount++;
            lastLine = line;
        }
    }
    readFile.close();
    
    Serial.printf("CSV verification: %d lines, last line: %s\n", lineCount, lastLine.c_str());
    return true;
}

// Function to safely write data to CSV
void writeToCSV(float temperature, float humidity, float voltage, float frequency, unsigned long timestamp) {
    bool fileExists = SD.exists(filename);
    File dataFile = SD.open(filename, FILE_WRITE);
    if (dataFile) {
        if (!fileExists) {
            // If new file, write header without newline
            dataFile.print("Temperature;Humidity;Voltage;Frequency;Timestamp");
        } else {
            // If existing file, seek to end
            dataFile.seek(dataFile.size());
        }
        
        // Add new data with newline at start
        dataFile.printf("\n%.2f;%.2f;%.2f;%.2f;%lu",
                     temperature, humidity, voltage, frequency, timestamp);
        dataFile.close();
        Serial.println("Data written to CSV");
    } else {
        Serial.println("Error opening file for writing!");
    }
}

// Modifikasi fungsi getStoredTimestamp
unsigned long getStoredTimestamp() {
    unsigned long storedTime = 0;
    
    if (SD.exists(timeRefFile)) {
        File file = SD.open(timeRefFile);
        if (file) {
            while (file.available()) {
                String timestamp = file.readStringUntil('\n');
                timestamp.trim();
                if (timestamp.length() > 0) {
                    storedTime = timestamp.toInt();
                }
            }
            file.close();
        }
    }
    
    // Validasi timestamp yang tersimpan
    if (!isValidUnixTimestamp(storedTime)) {
        Serial.println("Invalid stored timestamp, trying Google NTP");
        timeClient.setPoolServerName(NTP_SERVER);
        
        // Coba dapatkan waktu dari Google NTP
        unsigned long startAttempt = millis();
        bool ntpSuccess = false;
        
        while (millis() - startAttempt < NTP_TIMEOUT) {
            if (timeClient.update()) {
                storedTime = timeClient.getEpochTime();
                if (isValidUnixTimestamp(storedTime)) {
                    saveTimestamp(storedTime);
                    ntpSuccess = true;
                    Serial.println("Successfully got time from Google NTP");
                    break;
                }
            }
            delay(100);
        }
        
        if (!ntpSuccess) {
            Serial.println("ERROR: Failed to get valid timestamp from any source");
            storedTime = 0; // Indikasi error
        }
    }
    
    Serial.print("Last stored timestamp: ");
    Serial.println(storedTime);
    return storedTime;
}

// Function to save timestamp reference
void saveTimestamp(unsigned long timestamp) {
    File file = SD.open(timeRefFile, FILE_WRITE);
    if (file) {
        file.println(timestamp);
        file.close();
        Serial.print("Saved new timestamp: ");
        Serial.println(timestamp);
    }
}

// Modifikasi setup untuk menambah inisialisasi NTP yang lebih baik
void setup() {
    // Initialize Serial communications
    Serial.begin(115200);
    serial.begin(19200);
    SerialMod.begin(9600);
    
    // Initialize I2C devices
    Wire.begin();
    sht.begin();
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
        
        // Initialize NTP dengan timeout
        timeClient.begin();
        unsigned long startAttempt = millis();
        bool ntpInitialized = false;
        
        while (millis() - startAttempt < NTP_TIMEOUT) {
            if (timeClient.update()) {
                lastTimestamp = timeClient.getEpochTime();
                if (isValidUnixTimestamp(lastTimestamp)) {
                    ntpInitialized = true;
                    saveTimestamp(lastTimestamp);
                    Serial.println("NTP initialized successfully");
                    break;
                }
            }
            delay(100);
        }
        
        if (!ntpInitialized) {
            Serial.println("NTP init failed, trying Google NTP");
            timeClient.setPoolServerName(NTP_SERVER);
            startAttempt = millis();
            
            while (millis() - startAttempt < NTP_TIMEOUT) {
                if (timeClient.update()) {
                    lastTimestamp = timeClient.getEpochTime();
                    if (isValidUnixTimestamp(lastTimestamp)) {
                        saveTimestamp(lastTimestamp);
                        Serial.println("Google NTP initialized successfully");
                        break;
                    }
                }
                delay(100);
            }
        }
        
        if (!isValidUnixTimestamp(lastTimestamp)) {
            lastTimestamp = getStoredTimestamp();
        }
        
    } else {
        lastTimestamp = getStoredTimestamp();
    }
    
    // Initialize Display
    setupDisplay();
    
    // Create CSV with header if it doesn't exist
    if (!SD.exists(filename)) {
        File dataFile = SD.open(filename, FILE_WRITE);
        if (dataFile) {
            dataFile.println("Temperature;Humidity;Voltage;Frequency;Timestamp");
            dataFile.close();
            Serial.println("Created new CSV file with header");
        }
    }
    
    // Display setup messages
    tft.fillScreen(TFT_BLACK);
    tft.setFreeFont(&FreeSansOblique12pt7b);
    tft.println(" ");
    tft.drawString("Scanning Network!", 8, 5);
    delay(1000);
    
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

// Modifikasi getNextTimestamp untuk validasi
unsigned long getNextTimestamp() {
    if (wifiConnected) {
        // Coba update NTP dengan timeout
        unsigned long startAttempt = millis();
        while (millis() - startAttempt < 2000) { // 2 detik timeout untuk update reguler
            if (timeClient.update()) {
                unsigned long ntpTime = timeClient.getEpochTime();
                if (isValidUnixTimestamp(ntpTime)) {
                    lastTimestamp = ntpTime;
                    saveTimestamp(lastTimestamp);
                    return lastTimestamp;
                }
            }
            delay(100);
        }
        
        // Jika gagal dengan pool.ntp.org, coba Google NTP
        timeClient.setPoolServerName(NTP_SERVER);
        startAttempt = millis();
        while (millis() - startAttempt < 2000) {
            if (timeClient.update()) {
                unsigned long ntpTime = timeClient.getEpochTime();
                if (isValidUnixTimestamp(ntpTime)) {
                    lastTimestamp = ntpTime;
                    saveTimestamp(lastTimestamp);
                    return lastTimestamp;
                }
            }
            delay(100);
        }
    }
    
    // Validasi timestamp sebelum increment
    if (!isValidUnixTimestamp(lastTimestamp)) {
        Serial.println("ERROR: Invalid timestamp detected");
        return 0; // Indikasi error
    }
    
    // Increment hanya jika timestamp valid
    lastTimestamp += TIMESTAMP_INCREMENT;
    if (isValidUnixTimestamp(lastTimestamp)) {
        saveTimestamp(lastTimestamp);
        return lastTimestamp;
    }
    
    Serial.println("ERROR: Timestamp increment resulted in invalid time");
    return 0; // Indikasi error
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

    // Update display every second
    static unsigned long lastDisplayUpdate = 0;
    if (currentMillis - lastDisplayUpdate >= 1000) {
        lastDisplayUpdate = currentMillis;
        
        // Read sensors for display
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
    }
    
    // Check WiFi connection
    if (currentMillis - previousWiFiCheck >= WIFI_CHECK_INTERVAL) {
        previousWiFiCheck = currentMillis;
        checkWiFiConnection();
    }
    
    // Read sensors and log data
    if (currentMillis - previousMillisSensor >= SENSOR_INTERVAL) {
        previousMillisSensor = currentMillis;
        
        // Calculate relative time from baseTime
        unsigned long timestamp = getNextTimestamp();
        
        // Read sensors
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
        
        // Handle data logging and transmission
        if (!wifiConnected) {
            Serial.println("WiFi disconnected, logging data...");
            
            // Check file exists and handle FIFO if needed
            int currentLines = countDataLines();
            if (currentLines >= MAX_DATA_ROWS) {
                // Implement FIFO
                File readFile = SD.open(filename);
                if (!readFile) {
                    Serial.println("Failed to open file for FIFO!");
                    return;
                }
                
                // Read header and handle FIFO data
                String header = readFile.readStringUntil('\n');
                String remainingData = header;  // Header without newline
                readFile.readStringUntil('\n'); // Skip oldest data
                
                // Read remaining lines
                while (readFile.available()) {
                    String line = readFile.readStringUntil('\n');
                    if (line.length() > 0) {
                        remainingData += "\n" + line;  // Add newline before each data line
                    }
                }
                readFile.close();
                
                // Write updated data
                if (SD.remove(filename)) {
                    File writeFile = SD.open(filename, FILE_WRITE);
                    if (writeFile) {
                        writeFile.print(remainingData);  // Write header and existing data
                        writeFile.printf("\n%.2f;%.2f;%.2f;%.2f;%lu",  // Add new data
                                     temperature, humidity, r.V, r.F, timestamp);
                        writeFile.close();
                        Serial.println("FIFO update successful");
                    }
                }
            } else {
                // Append new data
                writeToCSV(temperature, humidity, r.V, r.F, timestamp);
            }
        } else {
            // Direct transmission when online
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
    
   if (currentMillis - previousMillis >= INTERVAL && wifiConnected) {
    previousMillis = currentMillis;
    
    // Start new backup transmission if not already transmitting
    if (!isTransmittingBackup && SD.exists(filename)) {
        backupFile = SD.open(filename);
        if (backupFile && backupFile.available()) {
            isTransmittingBackup = true;
            backupHeader = backupFile.readStringUntil('\n');
            Serial.println("Starting backup transmission");
        }
    }
    
    // Continue existing transmission
    if (isTransmittingBackup && backupFile) {
        static int linesSent = 0;
        static String tempData = "";
        
        // Process one line at a time
        if (backupFile.available()) {
            String line = backupFile.readStringUntil('\n');
            line.trim();
            
            if (line.length() > 0) {
                // Parse and send data
                int pos1 = line.indexOf(';');
                int pos2 = line.indexOf(';', pos1 + 1);
                int pos3 = line.indexOf(';', pos2 + 1);
                int pos4 = line.indexOf(';', pos3 + 1);
                
                if (pos1 > 0 && pos2 > pos1 && pos3 > pos2 && pos4 > pos3) {
                    String datakirim = String("1#") + 
                                     line.substring(0, pos1) + "#" + 
                                     line.substring(pos1 + 1, pos2) + "#" + 
                                     line.substring(pos2 + 1, pos3) + "#" + 
                                     line.substring(pos3 + 1, pos4) + "#" + 
                                     line.substring(pos4 + 1);
                    
                    serial.println(datakirim);
                    Serial.println("Sent backup: " + datakirim);
                    linesSent++;
                    
                    // Store the line in case we need to preserve it
                    tempData += line + "\n";
                }
                delay(TRANSMISSION_DELAY); // Add delay between transmissions
            }
        } else {
            // End of file reached
            backupFile.close();
            
            // Reset file with header and any unconfirmed data
            SD.remove(filename);
            File writeFile = SD.open(filename, FILE_WRITE);
            if (writeFile) {
                writeFile.print(backupHeader);  // Write header without newline
                if (tempData.length() > 0) {  // Only write data if exists
                    writeFile.print("\n" + tempData);  // Add newline before data
                }
                writeFile.close();
                verifyCSVFormat();
                Serial.printf("Backup transmission complete. Sent %d lines\n", linesSent);
            }
            
            // Reset transmission state
            isTransmittingBackup = false;
            linesSent = 0;
            tempData = "";
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

void updateDisplay(float temperature, float humidity, float voltage, float frequency, 
                  bool modbusOK, bool wifiOK, bool sensorOK) {
    tft.setFreeFont(&FreeSans9pt7b);
    spr.setFreeFont(&FreeSans9pt7b);
    tft.setTextSize(1);

    // Status indicators
    tft.setTextColor(wifiOK ? TFT_GREEN : TFT_RED);
    tft.drawString("WiFi", 8, 5);
    
    tft.setTextColor(modbusOK ? TFT_GREEN : TFT_RED);
    tft.drawString("Modbus", 112, 5);
    
    tft.setTextColor(sensorOK ? TFT_GREEN : TFT_RED);
    tft.drawString("Sensor", 183, 5);

    // Draw dividing lines
    tft.drawFastVLine(160, 25, 220, TFT_DARKCYAN);
    tft.drawFastHLine(0, 135, 320, TFT_DARKCYAN);
    tft.drawFastHLine(0, 25, 320, TFT_DARKCYAN);

    // Temperature (Quadrant 1)
    spr.createSprite(158, 102);
    spr.fillSprite(TFT_BLACK);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("Temp", 55, 8);
    spr.setTextColor(TFT_GREEN);
    spr.setFreeFont(FSSO24);
    spr.drawString(String(temperature, 1), 25, 36);
    spr.setTextColor(TFT_YELLOW);
    spr.setFreeFont(&FreeSans9pt7b);
    spr.drawString("C", 113, 85);
    spr.pushSprite(0, 27);
    spr.deleteSprite();

    // Voltage (Quadrant 2)
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

    // Frequency (Quadrant 3)
    spr.createSprite(158, 100);
    spr.fillSprite(TFT_BLACK);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("Freq", 62, 6);
    spr.setTextColor(TFT_GREEN);
    spr.setFreeFont(FSSO24);
    spr.drawString(String(frequency, 1), 26, 34);
    spr.setTextColor(TFT_YELLOW);
    spr.setFreeFont(&FreeSans9pt7b);
    spr.drawString("Hz", 108, 82);
    spr.pushSprite(162, 137);
    spr.deleteSprite();

    // Humidity (Quadrant 4)
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
