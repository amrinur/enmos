const mysql = require('mysql');
const mqtt = require('mqtt');

// Konfigurasi koneksi MySQL
const dbConnection = mysql.createPool({
  host: 'localhost',
  user: 'root',
  database: 'newdata',
  connectionLimit: 10,
});

// Konfigurasi MQTT
const mqttClient = mqtt.connect('mqtt://broker.hivemq.com', {
  username: '',
  password: '',
});

// Fungsi untuk menyimpan data ke MySQL dengan urutan yang benar
function saveDataToDatabase(frequency, humidity, temperature, voltage, timestamp) {
  const query = `
    INSERT INTO sensor_readings (frequency, humidity, temperature, voltage, created_at)
    VALUES (?, ?, ?, ?, ?)`; 
  
  // Swap the positions in the array to correct the order
  dbConnection.query(query, [humidity, frequency, voltage, temperature, timestamp], (err, results) => {
    if (err) {
      console.error('Error saving data to MySQL:', err);
    } else {
      console.log('Data saved to MySQL:', {
        id: results.insertId,
        frequency: humidity,    // Swapped
        humidity: frequency,    // Swapped
        temperature: voltage,   // Swapped
        voltage: temperature,   // Swapped
        created_at: timestamp,
      });
    }
  });
}

// Event listener untuk koneksi MQTT
mqttClient.on('connect', () => {
  console.log('Connected to MQTT broker');
  
  // Subscribe ke topik-topik yang digunakan
  const topics = ['frequency_data', 'humidity_data', 'temperature_data', 'voltage_data', 'timestamp_data'];
  topics.forEach((topic) => {
    mqttClient.subscribe(topic, (err) => {
      if (err) {
        console.error('Failed to subscribe to ' + topic + ':', err);
      } else {
        console.log('Subscribed to ' + topic + ' topic');
      }
    });
  });
});

// Variabel untuk menyimpan data sementara
let frequency = null;
let humidity = null;
let temperature = null;
let voltage = null;
let timestamp = null;

// Event listener untuk menerima pesan MQTT
mqttClient.on('message', (topic, message) => {
  const value = message.toString();
  
  if (topic === 'timestamp_data') {
    timestamp = value; // Simpan timestamp sebagai string dari Wio Terminal
    console.log('Received timestamp: ' + timestamp);
  } else {
    const numericValue = parseFloat(value);
    if (isNaN(numericValue)) {
      console.error('Received invalid data on topic ' + topic + ': ' + message);
      return;
    }

    console.log('Received message on topic ' + topic + ': ' + numericValue);

    // Simpan data berdasarkan topik
    switch (topic) {
      case 'voltage_data':
        voltage = numericValue;
        break;
      case 'frequency_data':
        frequency = numericValue;
        break;
      case 'temperature_data':
        temperature = numericValue;
        break;
      case 'humidity_data':
        humidity = numericValue;
        break;
      default:
        console.error('Unknown topic: ' + topic);
    }
  }

  // Jika semua data sudah diterima, simpan ke database dengan urutan yang benar
  if (frequency !== null && humidity !== null && temperature !== null && voltage !== null && timestamp !== null) {
    saveDataToDatabase(frequency, humidity, temperature, voltage, timestamp);

    // Reset variabel setelah menyimpan data
    frequency = null;
    humidity = null;
    temperature = null;
    voltage = null;
    timestamp = null;
  }
});

// Menangani error koneksi MQTT
mqttClient.on('error', (err) => {
  console.error('MQTT Connection Error:', err);
});

// Menangani error koneksi database MySQL
dbConnection.on('error', (err) => {
  console.error('MySQL Connection Error:', err);
});
