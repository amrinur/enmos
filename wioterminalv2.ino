#include <SD.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <ModbusMaster.h>
#include <SHT31.h>
#include "RTC_SAMD51.h"
#include "DateTime.h"
#include "Wire.h"

// Struktur data untuk satu record
struct SensorData {
    char timestamp[20];
    float temp;
    float hum;
    float volt;
    float freq;
};

// Queue implementation
const int QUEUE_SIZE = 50;
SensorData dataQueue[QUEUE_SIZE];
int qHead = 0;  // index untuk data terdepan
int qTail = 0;  // index untuk data terbaru
int qCount = 0; // jumlah data dalam queue

// Queue helper functions
void initQueue() {
    qHead = qTail = qCount = 0;
}

bool isQueueFull() {
    return qCount >= QUEUE_SIZE;
}

bool isQueueEmpty() {
    return qCount == 0;
}

// Tambah data ke queue
bool enqueue(SensorData data) {
    if (isQueueFull()) return false;
    
    dataQueue[qTail] = data;
    qTail = (qTail + 1) % QUEUE_SIZE;
    qCount++;
    return true;
}

// Ambil dan hapus data terdepan
bool dequeue() {
    if (isQueueEmpty()) return false;
    
    qHead = (qHead + 1) % QUEUE_SIZE;
    qCount--;
    return true;
}

// Simpan seluruh queue ke CSV
void saveQueueToCSV() {
    if (SD.exists(filename)) {
        SD.remove(filename);
    }
    
    File file = SD.open(filename, FILE_WRITE);
    if (file) {
        file.println("Timestamp;Temperature;Humidity;Voltage;Frequency");
        
        int current = qHead;
        for (int i = 0; i < qCount; i++) {
            SensorData data = dataQueue[current];
            file.printf("%s;%.2f;%.2f;%.2f;%.2f\n",
                       data.timestamp,
                       data.temp, data.hum,
                       data.volt, data.freq);
            current = (current + 1) % QUEUE_SIZE;
        }
        file.close();
    }
}

typedef struct {
  float V;
  float F;
} READING;

unsigned long previousMillis2 = 0;
const long INTERVAL = 20000;        // 20 detik untuk pembacaan sensor
unsigned long previousMillisCSV = 0;
const long CSV_READ_INTERVAL = 20000;  // 20 detik untuk pengiriman data
unsigned long previousMillisSensor = 0;

char filename[25] = "/wiwoho.csv";

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
  
  initQueue();
    
  // Load existing data from CSV if exists
  if (SD.exists(filename)) {
      File file = SD.open(filename);
      if (file) {
          String header = file.readStringUntil('\n'); // Skip header
          
          while (file.available() && !isQueueFull()) {
              SensorData data;
              String line = file.readStringUntil('\n');
              
              // Parse CSV line to SensorData
              sscanf(line.c_str(), "%[^;];%f;%f;%f;%f",
                     data.timestamp,
                     &data.temp, &data.hum,
                     &data.volt, &data.freq);
                     
              enqueue(data);
          }
          file.close();
      }
  }
}

void loop() {
  unsigned long currentMillis = millis();
  
  // Baca sensor dan tambah ke queue setiap 20 detik
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

    // Buat record baru
    SensorData newData;
    snprintf(newData.timestamp, sizeof(newData.timestamp),
            "%04d-%02d-%02d %02d:%02d:%02d",
            now.year(), now.month(), now.day(),
            now.hour(), now.minute(), now.second());
    newData.temp = temperature;
    newData.hum = humidity;
    newData.volt = r.V;
    newData.freq = r.F;
    
    // Tambah ke queue dan update CSV
    if (enqueue(newData)) {
        saveQueueToCSV();
        Serial.println("New data queued and saved");
    }
  }

  // Kirim data dari queue setiap 20 detik
  if (currentMillis - previousMillisCSV >= CSV_READ_INTERVAL && !isQueueEmpty()) {
    previousMillisCSV = currentMillis;
    
    // Ambil data terdepan
    SensorData frontData = dataQueue[qHead];
    
    // Format dan kirim data
    String datakirim = String("1#") + 
                      String(frontData.volt, 2) + "#" +
                      String(frontData.freq, 2) + "#" +
                      String(frontData.temp, 2) + "#" +
                      String(frontData.hum, 2);
                      
    serial.println(datakirim);
    Serial.println("Sent from queue: " + datakirim);
    
    // Hapus data yang sudah terkirim
    if (dequeue()) {
        saveQueueToCSV();
        Serial.println("Data dequeued and CSV updated");
    }
  }
}
