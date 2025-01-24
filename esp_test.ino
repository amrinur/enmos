void setup() {
  Serial.begin(9600);           // Debug serial
  DataSerial.begin(19200);      // Komunikasi dengan Wio
}

void loop() {
  // Kirim test message setiap detik
  DataSerial.println("ESP_TEST");
  Serial.println("Sent: ESP_TEST"); // Debug print
  delay(1000);
  
  // Baca respon dari Wio jika ada
  if (DataSerial.available()) {
    String response = DataSerial.readStringUntil('\n');
    Serial.println("Received: " + response);
  }
}
