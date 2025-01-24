void setup() {
  Serial.begin(115200);     // Debug serial
  serial.begin(19200);      // Komunikasi dengan ESP
}

void loop() {
  // Baca data dari ESP
  if (serial.available()) {
    String received = serial.readStringUntil('\n');
    Serial.println("Received: " + received);
    
    // Kirim balik konfirmasi
    serial.println("WIO_ACK");
    Serial.println("Sent: WIO_ACK");
  }
}
