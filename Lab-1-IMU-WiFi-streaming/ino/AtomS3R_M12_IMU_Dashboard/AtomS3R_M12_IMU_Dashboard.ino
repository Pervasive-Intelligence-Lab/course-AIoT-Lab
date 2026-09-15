// AtomS3R-M12 IMU streamer
//
//   BMI270 (accel+gyro) + BMM150 (mag)  --M5Unified-->  JSON datagrams  --UDP-->  backend/server.py
//
// The board joins eduroam (WPA2-Enterprise / PEAP) or ordinary Wi-Fi (WPA2-PSK)
// as configured in secrets.h, and listens on UDP STREAM_PORT.
// The PC backend sends a small "hello" datagram once a second; the board streams one
// JSON sample per datagram back to whoever said hello last (SAMPLE_HZ per second) and
// stops HELLO_TIMEOUT_MS after the last hello.
//
// Why UDP: a Wi-Fi fade loses a few packets. Over TCP that turned into a 0.5-2 s
// freeze (lwIP retransmit timer, head-of-line blocking) followed by a burst of stale
// samples. Over UDP the same fade is a few missing samples and the stream stays live.
// USB serial is only used for flashing + logs.

#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include "secrets.h"  // WIFI_SSID / EAP_IDENTITY / EAP_USERNAME / EAP_PASSWORD / ...

static const uint16_t STREAM_PORT = 5555;
static const uint32_t SAMPLE_HZ = 50;
static const uint32_t SAMPLE_PERIOD_US = 1000000UL / SAMPLE_HZ;
static const uint32_t WIFI_TIMEOUT_MS = 30000;
static const uint32_t HELLO_TIMEOUT_MS = 3000;

static WiFiUDP udp;
static IPAddress peerIp;
static uint16_t peerPort = 0;
static uint32_t lastHelloMs = 0;

// ---------------------------------------------------------------- Wi-Fi ----

// Select the commented configuration example in secrets.h:
// EAP_USERNAME nonempty = eduroam / PEAP; empty = ordinary Wi-Fi / WPA2-PSK.
static bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(500);
  WiFi.setSleep(false);  // modem sleep adds 100+ ms of latency bursts

  if (EAP_USERNAME[0]) {
    // eduroam: use the identity, username, and password for enterprise authentication.
    Serial.printf("\n[wifi] connecting to %s (WPA2-Enterprise/PEAP) as %s ...\n", WIFI_SSID, EAP_USERNAME);
    // Optional CA pinning: without it the PEAP tunnel accepts any RADIUS server certificate.
    const char* ca = EAP_CA_CERT[0] ? EAP_CA_CERT : nullptr;
    WiFi.begin(WIFI_SSID, WPA2_AUTH_PEAP, EAP_IDENTITY, EAP_USERNAME, EAP_PASSWORD, ca);
  } else {
    // Ordinary Wi-Fi: set EAP_USERNAME to ""; only WIFI_SSID and WIFI_PASSWORD are used.
    Serial.printf("\n[wifi] connecting to %s (WPA2-PSK) ...\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
    if (millis() - start > WIFI_TIMEOUT_MS) {
      Serial.printf("\n[wifi] FAILED (status=%d), retrying\n", (int)WiFi.status());
      return false;
    }
  }

  Serial.println();
  WiFi.setSleep(false);  // re-assert after association: modem sleep is the #1 source of 100 ms+ gaps
  Serial.printf("[wifi] status   : connected to %s\n", WIFI_SSID);
  Serial.printf("[wifi] local IP : %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("[wifi] RSSI     : %d dBm\n", (int)WiFi.RSSI());
  Serial.printf("[wifi] stream   : udp://%s:%u  (backend sends hello here)\n", WiFi.localIP().toString().c_str(), STREAM_PORT);
  return true;
}

// ---------------------------------------------------------------- setup ----

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
  // HWCDC in Arduino-ESP32 2.0.17 uses an unsigned retry counter.  A zero
  // timeout can underflow when the USB host is present but its serial monitor
  // is closed, leaving Serial.printf() stuck once the TX buffer fills.
  Serial.setTxTimeoutMs(1);
#endif
  delay(1500);  // give the USB CDC host time to (re)attach so early logs are visible

  Serial.println("\n=== AtomS3R-M12 IMU streamer ===");
  Serial.printf("[board] %s\n", M5.getBoard() == m5::board_t::board_unknown ? "unknown" : "detected");
  Serial.printf("[memory] flash=%u MB psram=%u MB\n",
                (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)),
                (unsigned)(ESP.getPsramSize() / (1024 * 1024)));
  if (ESP.getPsramSize() == 0) {
    Serial.println("[memory] WARNING: PSRAM is disabled; check the AtomS3R-M12 board configuration");
  }

  if (!M5.Imu.isEnabled()) {
    Serial.println("[imu] BMI270 not found - streaming zeros");
  } else {
    delay(50);
    const auto mask = M5.Imu.update();  // which sensors delivered data
    Serial.printf("[imu] type=%d accel=%s gyro=%s mag=%s\n", (int)M5.Imu.getType(),
                  (mask & m5::IMU_Class::sensor_mask_accel) ? "ok" : "MISSING",
                  (mask & m5::IMU_Class::sensor_mask_gyro) ? "ok" : "MISSING",
                  (mask & m5::IMU_Class::sensor_mask_mag) ? "ok" : "MISSING");
  }

  connectWiFi();  // blocks up to 30 s; on failure loop() runs 30 s (logging IMU data), then retries
  WiFi.setAutoReconnect(true);
  udp.begin(STREAM_PORT);
}

// ----------------------------------------------------------------- loop ----

void loop() {
  static uint32_t seq = 0;
  static uint32_t nextSampleUs = micros();
  static uint32_t lastStatusMs = 0;
  static uint32_t lastWifiAttemptMs = millis();
  static uint32_t lastRssiMs = 0;
  static int rssi = 0;
  static uint32_t sentSinceStatus = 0, failedSinceStatus = 0, hellosSinceStatus = 0;

  // --- keep Wi-Fi alive: if auto-reconnect has not recovered in 30 s, redo the
  //     full WPA2-Enterprise handshake ourselves.
  if (WiFi.status() == WL_CONNECTED) {
    lastWifiAttemptMs = millis();
  } else if (millis() - lastWifiAttemptMs > WIFI_TIMEOUT_MS) {
    connectWiFi();
    udp.begin(STREAM_PORT);
    lastWifiAttemptMs = millis();
  }

  // --- hello from the backend: remember who to stream to ---------------------
  while (udp.parsePacket() > 0) {
    if (!(udp.remoteIP() == peerIp) || udp.remotePort() != peerPort) {
      peerIp = udp.remoteIP();
      peerPort = udp.remotePort();
      Serial.printf("[udp] streaming to %s:%u\n", peerIp.toString().c_str(), peerPort);
    }
    lastHelloMs = millis();
    hellosSinceStatus++;
    udp.flush();
  }
  const bool havePeer = peerPort != 0 && millis() - lastHelloMs < HELLO_TIMEOUT_MS;
  if (!havePeer && peerPort != 0) {
    Serial.println("[udp] backend went quiet, stream paused");
    peerPort = 0;
  }

  // --- sample at SAMPLE_HZ ----------------------------------------------------
  const uint32_t nowUs = micros();
  if ((int32_t)(nowUs - nextSampleUs) < 0) {
    delayMicroseconds(200);
    return;
  }
  nextSampleUs += SAMPLE_PERIOD_US;
  if ((int32_t)(nowUs - nextSampleUs) > (int32_t)SAMPLE_PERIOD_US) nextSampleUs = nowUs;  // we fell behind

  M5.Imu.update();
  const auto& d = M5.Imu.getImuData();  // accel [g], gyro [dps], mag: see below

  // M5Unified scales the BMM150 with the AK8963 constant, giving milligauss for the
  // 13-bit X/Y axes but 2x that for the 15-bit Z axis. Convert to microtesla:
  // X/Y: 0.3 uT/LSB  ->  /10,   Z: 0.15 uT/LSB  ->  /20.
  const float mx = d.mag.x * 0.1f, my = d.mag.y * 0.1f, mz = d.mag.z * 0.05f;

  if (millis() - lastRssiMs >= 1000) {  // RSSI is a driver call, not worth doing at 50 Hz
    lastRssiMs = millis();
    rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  }

  char line[224];
  const int len = snprintf(
      line, sizeof(line),
      "{\"n\":%lu,\"t\":%.3f,\"a\":[%.4f,%.4f,%.4f],\"g\":[%.3f,%.3f,%.3f],\"m\":[%.2f,%.2f,%.2f],\"r\":%d}\n",
      (unsigned long)seq++, nowUs * 1e-3,
      d.accel.x, d.accel.y, d.accel.z,
      d.gyro.x, d.gyro.y, d.gyro.z,
      mx, my, mz, rssi);

  if (havePeer && len > 0) {
    // sendto() on a UDP socket never waits for the peer: a lost datagram costs one sample, not a stall.
    udp.beginPacket(peerIp, peerPort);
    udp.write((const uint8_t*)line, len);
    if (udp.endPacket()) sentSinceStatus++; else failedSinceStatus++;
  }

  // --- periodic status line on USB serial ------------------------------------
  if (millis() - lastStatusMs >= 2000) {
    lastStatusMs = millis();
    Serial.printf("[status] wifi=%s ip=%s rssi=%d peer=%s hellos=%lu sent=%lu failed=%lu | a=%.2f,%.2f,%.2f g=%.1f,%.1f,%.1f m=%.1f,%.1f,%.1f\n",
                  WiFi.status() == WL_CONNECTED ? "up" : "down",
                  WiFi.localIP().toString().c_str(), rssi,
                  havePeer ? peerIp.toString().c_str() : "none", (unsigned long)hellosSinceStatus,
                  (unsigned long)sentSinceStatus, (unsigned long)failedSinceStatus,
                  d.accel.x, d.accel.y, d.accel.z, d.gyro.x, d.gyro.y, d.gyro.z, mx, my, mz);
    sentSinceStatus = failedSinceStatus = hellosSinceStatus = 0;
  }
}
