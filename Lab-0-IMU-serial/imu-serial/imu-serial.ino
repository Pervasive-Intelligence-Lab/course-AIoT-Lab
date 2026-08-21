#include <M5Unified.h>

void setup(void) {
  M5.begin();
  Serial.begin(115200);
}

void loop(void) {
  auto imu_update = M5.Imu.update();

  if (imu_update) {
    auto data = M5.Imu.getImuData();

    Serial.printf(
      "ax:%f ay:%f az:%f\r\n",
      data.accel.x,
      data.accel.y,
      data.accel.z
    );

    Serial.printf(
      "gx:%f gy:%f gz:%f\r\n",
      data.gyro.x,
      data.gyro.y,
      data.gyro.z
    );
  }

  delay(100);
}