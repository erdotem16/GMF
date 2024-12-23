#include <Wire.h>
#include <HardwareSerial.h>

// Menginisialisasi UART2 untuk modul Holybro
HardwareSerial TelemetrySerial(2);

const int MPU_ADDR = 0x68; // Alamat I2C dari MPU6050

float accX, accY, accZ;
float gyroX, gyroY, gyroZ;
float roll, pitch, yaw;
unsigned long previousTime, currentTime;
float elapsedTime;

float maxRoll = 40.0; // Batas atas alert untuk roll
float minRoll = -40.0; // Batas bawah alert untuk roll

float kalmanRoll, kalmanPitch;
float biasRoll = 0, biasPitch = 0;
float P[2][2] = { {1, 0}, {0, 1} };
float R = 0.03;
float Q_angle = 0.01;
float Q_bias = 0.01;
const float biasLimit = 0.5; // Batas maksimum untuk bias

void setup() {
  Serial.begin(57600);
  TelemetrySerial.begin(57600, SERIAL_8N1, 16, 17);
  Wire.begin();

  // Inisialisasi MPU6050
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); Wire.write(0x00); // Wake up MPU6050
  Wire.endTransmission(true);

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1C); Wire.write(0x10); // Rentang akselerometer +/-8g
  Wire.endTransmission(true);

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1B); Wire.write(0x00); // Rentang gyroscope +/-250 °/s
  Wire.endTransmission(true);

  Serial.println("MPU6050 initialized successfully!");
  previousTime = millis();
}

void loop() {
  int16_t rawAccX, rawAccY, rawAccZ, rawGyroX, rawGyroY, rawGyroZ;

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B); Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 6, true);
  rawAccX = (Wire.read() << 8 | Wire.read());
  rawAccY = (Wire.read() << 8 | Wire.read());
  rawAccZ = (Wire.read() << 8 | Wire.read());

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x43); Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 6, true);
  rawGyroX = (Wire.read() << 8 | Wire.read());
  rawGyroY = (Wire.read() << 8 | Wire.read());
  rawGyroZ = (Wire.read() << 8 | Wire.read());

  accX = rawAccX / 4096.0 * 9.81;
  accY = rawAccY / 4096.0 * 9.81;
  accZ = rawAccZ / 4096.0 * 9.81;
  gyroX = rawGyroX / 131.0 * (3.14159265 / 180);
  gyroY = rawGyroY / 131.0 * (3.14159265 / 180);
  gyroZ = rawGyroZ / 131.0 * (3.14159265 / 180);

  currentTime = millis();
  elapsedTime = (currentTime - previousTime) / 1000.0;
  previousTime = currentTime;
  if (elapsedTime < 0.001) elapsedTime = 0.00001;
  
  if (accZ == 0) accZ = 0.00001; // Hindari pembagian nol
  float accRoll = atan(accY / sqrt(pow(accX, 2) + pow(accZ, 2))) * (180.0 / 3.14159265);
  float accPitch = atan(-1 * accX / sqrt(pow(accY, 2) + pow(accZ, 2))) * (180.0 / 3.14159265);

  kalmanFilter(&kalmanRoll, &biasRoll, gyroX, accRoll, elapsedTime);
  kalmanFilter(&kalmanPitch, &biasPitch, gyroY, accPitch, elapsedTime);

  yaw += gyroZ * elapsedTime * (180.0 / 3.14159265);

  // Fitur Alert: cek jika roll melebihi batas
  if (kalmanRoll > maxRoll || kalmanRoll < minRoll) {
    Serial.print("ALERT! Roll melebihi batas||maxRoll:  ");
    Serial.print(maxRoll); 
    Serial.print(", minRoll: ");
    Serial.println(minRoll);

    TelemetrySerial.print("ALERT! Roll melebihi batas||maxRoll:  "); 
    TelemetrySerial.print(maxRoll); 
    TelemetrySerial.print(", minRoll: ");
    TelemetrySerial.println(minRoll);
  }

  Serial.print("Roll: "); Serial.print(kalmanRoll);
  Serial.print(", Pitch: "); Serial.print(kalmanPitch);
  Serial.print(", Yaw: "); Serial.println(yaw);

  TelemetrySerial.print("Roll: "); TelemetrySerial.print(kalmanRoll);
  TelemetrySerial.print(", Pitch: "); TelemetrySerial.print(kalmanPitch);
  TelemetrySerial.print(", Yaw: "); TelemetrySerial.println(yaw);

  // Memeriksa input keyboard untuk menaikkan atau menurunkan batas roll
  if (TelemetrySerial.available()) {
    // char input = Serial.read();
    char input = TelemetrySerial.read();
    if (input == 'w' || input == 'W') {
      maxRoll += 5;
      minRoll -= 5;
      Serial.println("Batas roll dinaikkan!");
      Serial.print("Max Roll: "); Serial.println(maxRoll);
      Serial.print("Min Roll: "); Serial.println(minRoll);
      TelemetrySerial.println("Batas roll dinaikkan!");
      TelemetrySerial.print("Max Roll: "); TelemetrySerial.println(maxRoll);
      TelemetrySerial.print("Min Roll: "); TelemetrySerial.println(minRoll);

    } else if (input == 's' || input == 'S') {
      maxRoll -= 5;
      minRoll += 5;
      Serial.println("Batas roll diturunkan!");
      Serial.print("Max Roll: "); Serial.println(maxRoll);
      Serial.print("Min Roll: "); Serial.println(minRoll);
      TelemetrySerial.println("Batas roll diturunkan!");
      TelemetrySerial.print("Max Roll: "); TelemetrySerial.println(maxRoll);
      TelemetrySerial.print("Min Roll: "); TelemetrySerial.println(minRoll);
    }
    delay(500);
   }
}

void kalmanFilter(float *angle, float *bias, float newRate, float newAngle, float dt) {
  float rate = newRate - *bias;
  *angle += dt * rate;

  P[0][0] += dt * (dt * P[1][1] - P[0][1] - P[1][0] + Q_angle);
  P[0][1] -= dt * P[1][1];
  P[1][0] -= dt * P[1][1];
  P[1][1] += Q_bias * dt;

  float S = P[0][0] + R;
  float K[2];
  K[0] = P[0][0] / S;
  K[1] = P[1][0] / S;

  float y = newAngle - *angle;
  *angle += K[0] * y;
  *bias += K[1] * y;

  // Menambahkan batasan untuk bias
  if (*bias > biasLimit) {
    *bias = 0;
  } else if (*bias < -biasLimit) {
    *bias = 0;
  }

  float P00_temp = P[0][0];
  float P01_temp = P[0][1];

  P[0][0] -= K[0] * P00_temp;
  P[0][1] -= K[0] * P01_temp;
  P[1][0] -= K[1] * P00_temp;
  P[1][1] -= K[1] * P01_temp;
  delay(10);
}