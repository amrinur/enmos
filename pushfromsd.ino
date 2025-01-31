#include <SD.h>
#include <SPI.h>
#include <SoftwareSerial.h>

SoftwareSerial serial(D2, D3);      // For data transmission
char filename[25] = "/mesinmf.csv";
unsigned long previousMillis = 0;
const long INTERVAL = 10000;  // 10 detik untuk pengiriman data

void setup() {
  Serial.begin(115200);
  serial.begin(19200);
  
  if (!SD.begin(SDCARD_SS_PIN)) {
    Serial.println("SD card initialization failed!");
    return;
  }
  Serial.println("SD card initialized.");
}

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= INTERVAL) {
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
