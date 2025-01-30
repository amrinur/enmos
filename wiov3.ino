#include <SD.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <ModbusMaster.h>
#include <SHT31.h>
#include "RTC_SAMD51.h"
#include "DateTime.h"
#include "Wire.h"

RTC_SAMD51 rtc;
SHT31 sht;
SoftwareSerial serial(D2, D3);      // For data transmission
SoftwareSerial SerialMod(D1, D0);   // For Modbus
ModbusMaster node;

// Struktur data untuk satu record
struct DataRecord {
    char timestamp[20];
    float temp;
    float hum;
    float volt;
    float freq;
    bool sent;  // Flag untuk status pengiriman
};

// Buffer circular dengan ukuran tetap
const int BUFFER_SIZE = 60;  // Menyimpan data 20 menit (60 data dengan interval 20 detik)
DataRecord dataBuffer[BUFFER_SIZE];
int bufferHead = 0;  // Indeks untuk membaca data
int bufferTail = 0;  // Indeks untuk menulis data
int dataCount = 0;   // Jumlah data dalam buffer

// Helper functions untuk buffer
void addToBuffer(DataRecord data) {
    dataBuffer[bufferTail] = data;
    bufferTail = (bufferTail + 1) % BUFFER_SIZE;
    if (dataCount < BUFFER_SIZE) {
        dataCount++;
    } else {
        bufferHead = (bufferHead + 1) % BUFFER_SIZE; // Overwrite data terlama
    }
    saveBufferToSD(); // Backup buffer ke SD
}

void saveBufferToSD() {
    SD.remove(filename);
    File file = SD.open(filename, FILE_WRITE);
    if (file) {
        file.println("Timestamp;Temperature;Humidity;Voltage;Frequency");
        int index = bufferHead;
        for (int i = 0; i < dataCount; i++) {
            DataRecord record = dataBuffer[index];
            file.printf("%s;%.2f;%.2f;%.2f;%.2f\n",
                       record.timestamp,
                       record.temp, record.hum,
                       record.volt, record.freq);
            index = (index + 1) % BUFFER_SIZE;
        }
        file.close();
    }
}

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
}

void loop() {
  unsigned long currentMillis = millis();

  // Baca sensor dan tambah ke buffer setiap 20 detik
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
    DataRecord newData;
    snprintf(newData.timestamp, sizeof(newData.timestamp),
            "%04d/%02d/%02d %02d:%02d:%02d",
            now.year(), now.month(), now.day(),
            now.hour(), now.minute(), now.second());
    newData.temp = temperature;
    newData.hum = humidity;
    newData.volt = r.V;
    newData.freq = r.F;
    newData.sent = false;
    
    // Tambah ke buffer
    addToBuffer(newData);
  }

  // Kirim data dari buffer setiap 20 detik
  if (currentMillis - previousMillisCSV >= CSV_READ_INTERVAL && dataCount > 0) {
    previousMillisCSV = currentMillis;
    
    // Ambil data tertua yang belum terkirim
    DataRecord record = dataBuffer[bufferHead];
    String datakirim = String("1#") + 
                      String(record.volt, 2) + "#" +
                      String(record.freq, 2) + "#" +
                      String(record.temp, 2) + "#" +
                      String(record.hum, 2);
    
    serial.println(datakirim);
    Serial.println("Trying to send: " + datakirim);
    
    // Update buffer
    bufferHead = (bufferHead + 1) % BUFFER_SIZE;
    dataCount--;
    saveBufferToSD(); // Update backup
  }
}
