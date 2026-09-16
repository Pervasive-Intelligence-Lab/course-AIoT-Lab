// AtomS3R-M12 / ESP32-S3: AP -> STA CSI -> USB serial CSV.
#include <Arduino.h>
#include <WiFi.h>
#include <atomic>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <ping/ping_sock.h>

#if !CONFIG_IDF_TARGET_ESP32S3
#error "Select the M5AtomS3R (ESP32-S3) board."
#endif
#if !CONFIG_ESP_WIFI_CSI_ENABLED && !CONFIG_ESP32_WIFI_CSI_ENABLED
#error "This board package was built without CSI; install a CSI-enabled ESP32 core."
#endif

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

// Keep existing personal-Wi-Fi secrets.h files working without EAP settings.
#ifndef EAP_USERNAME
#define EAP_USERNAME ""
#endif
#ifndef EAP_IDENTITY
#define EAP_IDENTITY EAP_USERNAME
#endif
#ifndef EAP_PASSWORD
#define EAP_PASSWORD ""
#endif
#ifndef EAP_CA_CERT
#define EAP_CA_CERT ""
#endif

constexpr uint32_t SERIAL_BAUD = 921600;
constexpr uint32_t PING_INTERVAL_MS = 50;  // Target 20 packets/s, not a guaranteed CSI rate.
constexpr size_t MAX_CSI_BYTES = 640;
constexpr size_t QUEUE_DEPTH = 16;

struct CsiSample {
  uint32_t seq, timestamp;
  uint8_t mac[6];
  int8_t rssi, noise;
  uint8_t channel, sigMode, mcs, bandwidth;
  bool firstInvalid;
  uint16_t len;
  int8_t data[MAX_CSI_BYTES];
};

static QueueHandle_t samples;
static esp_ping_handle_t pingSession = nullptr;
static portMUX_TYPE captureMux = portMUX_INITIALIZER_UNLOCKED;
static bool captureEnabled = false;
static uint8_t apMac[6] = {};
static IPAddress activeGateway;
static std::atomic<uint32_t> received{0}, dropped{0}, pingReplies{0}, pingTimeouts{0};
static uint32_t printed = 0;
static char csvLine[MAX_CSI_BYTES * 5 + 256];

static void requireOk(esp_err_t err, const char *operation) {
  if (err == ESP_OK) return;
  Serial.printf("# FATAL %s: %s\n", operation, esp_err_to_name(err));
  while (true) delay(1000);
}

// Wi-Fi task owns info and info->buf only until this callback returns.
static void onCsi(void *, wifi_csi_info_t *info) {
  if (!info || !info->buf || !info->len) return;
  portENTER_CRITICAL(&captureMux);
  const bool accept = captureEnabled && memcmp(info->mac, apMac, 6) == 0;
  portEXIT_CRITICAL(&captureMux);
  if (!accept) return;
  const uint32_t seq = received.fetch_add(1);
  if (info->len > MAX_CSI_BYTES) {
    dropped.fetch_add(1);
    return;  // Never silently truncate a CSI record.
  }
  CsiSample s = {};
  s.seq = seq;
  s.timestamp = info->rx_ctrl.timestamp;
  memcpy(s.mac, info->mac, 6);
  s.rssi = info->rx_ctrl.rssi;
  s.noise = info->rx_ctrl.noise_floor;
  s.channel = info->rx_ctrl.channel;
  s.sigMode = info->rx_ctrl.sig_mode;
  s.mcs = info->rx_ctrl.mcs;
  s.bandwidth = info->rx_ctrl.cwb;
  s.firstInvalid = info->first_word_invalid;
  s.len = info->len;
  memcpy(s.data, info->buf, s.len);
  // This is a task callback, not an ISR. Never wait for serial or queue space here.
  if (xQueueSend(samples, &s, 0) != pdTRUE) dropped.fetch_add(1);
}

static void onPingSuccess(esp_ping_handle_t, void *) { pingReplies.fetch_add(1); }
static void onPingTimeout(esp_ping_handle_t, void *) { pingTimeouts.fetch_add(1); }

static void stopCapture() {
  portENTER_CRITICAL(&captureMux);
  captureEnabled = false;
  portEXIT_CRITICAL(&captureMux);
  if (pingSession) {
    requireOk(esp_ping_stop(pingSession), "ping stop");
    requireOk(esp_ping_delete_session(pingSession), "ping delete");
    pingSession = nullptr;
  }
}

static void startCapture(const wifi_ap_record_t &ap, IPAddress gateway) {
  stopCapture();
  requireOk(esp_wifi_set_ps(WIFI_PS_NONE), "disable Wi-Fi sleep");
  portENTER_CRITICAL(&captureMux);
  memcpy(apMac, ap.bssid, 6);
  captureEnabled = true;
  portEXIT_CRITICAL(&captureMux);
  activeGateway = gateway;
  Serial.printf("# connected ip=%s gateway=%s channel=%u\n",
                WiFi.localIP().toString().c_str(), gateway.toString().c_str(), ap.primary);
  if (gateway == IPAddress(0, 0, 0, 0)) {
    Serial.println("# No gateway: generate inbound traffic from another LAN device.");
    return;
  }
  esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
  config.count = ESP_PING_COUNT_INFINITE;
  config.interval_ms = PING_INTERVAL_MS;
  config.timeout_ms = 1000;
  config.data_size = 32;
  config.task_stack_size = 3072;
  const String target = gateway.toString();
  if (!ipaddr_aton(target.c_str(), &config.target_addr)) {
    Serial.println("# Invalid ping target");
    return;
  }
  esp_ping_callbacks_t callbacks = {};
  callbacks.on_ping_success = onPingSuccess;
  callbacks.on_ping_timeout = onPingTimeout;
  requireOk(esp_ping_new_session(&config, &callbacks, &pingSession), "ping create");
  requireOk(esp_ping_start(pingSession), "ping start");
}

static void printSample(const CsiSample &s) {
  // Buffer is sized for -128 plus a comma per byte, metadata, quotes and newline.
  size_t n = snprintf(csvLine, sizeof(csvLine),
      "CSI_DATA,%lu,%lu,%02x:%02x:%02x:%02x:%02x:%02x,%d,%d,%u,%u,%u,%u,%u,%u,\"[",
      (unsigned long)s.seq, (unsigned long)s.timestamp,
      s.mac[0], s.mac[1], s.mac[2], s.mac[3], s.mac[4], s.mac[5],
      s.rssi, s.noise, s.channel, s.sigMode, s.mcs, s.bandwidth, s.len, s.firstInvalid);
  for (size_t i = 0; i < s.len; ++i) {
    n += snprintf(csvLine + n, sizeof(csvLine) - n, "%s%d", i ? "," : "", s.data[i]);
  }
  n += snprintf(csvLine + n, sizeof(csvLine) - n, "]\"\n");
  Serial.write(reinterpret_cast<const uint8_t *>(csvLine), n);
  ++printed;
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  const uint32_t start = millis();
  while (!Serial && millis() - start < 3000) delay(10);
  Serial.println("# AtomS3R CSI: STA receive, HT20, LLTF only, USB CSV");
  Serial.println("type,seq,timestamp_us,mac,rssi,noise_floor,channel,sig_mode,mcs,cwb,len,first_word_invalid,data");
  if (!WIFI_SSID[0] || strcmp(WIFI_SSID, "YOUR_2G4_SSID") == 0 ||
      strcmp(WIFI_SSID, "your-ssid") == 0) {
    Serial.println("# Copy secrets.example.h to secrets.h and set your Wi-Fi credentials.");
    while (true) delay(1000);
  }
  // Check for missing or placeholder EAP credentials. If EAP_USERNAME is empty, assume personal Wi-Fi.
  if (EAP_USERNAME[0] && (!EAP_IDENTITY[0] || !EAP_PASSWORD[0] ||
      strcmp(EAP_USERNAME, "your-username@your-university.edu") == 0 ||
      strcmp(EAP_PASSWORD, "your-password") == 0)) {
    Serial.println("# Set EAP_IDENTITY, EAP_USERNAME and EAP_PASSWORD in secrets.h.");
    while (true) delay(1000);
  }
  samples = xQueueCreate(QUEUE_DEPTH, sizeof(CsiSample));
  if (!samples) {
    Serial.println("# FATAL: cannot allocate CSI queue");
    while (true) delay(1000);
  }
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  requireOk(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20), "HT20");
  wifi_csi_config_t config = {};
  config.lltf_en = true;
  config.htltf_en = false;
  config.stbc_htltf2_en = false;
  config.ltf_merge_en = false;
  config.channel_filter_en = false;
  config.manu_scale = false;  // Driver automatic scaling; raw amplitude is not calibrated.
  requireOk(esp_wifi_set_csi_config(&config), "CSI config");
  requireOk(esp_wifi_set_csi_rx_cb(onCsi, nullptr), "CSI callback");
  requireOk(esp_wifi_set_csi(true), "CSI enable");
  // No promiscuous mode: only receive through the associated STA interface.
  if (EAP_USERNAME[0]) {
#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
    Serial.println("# Connecting with WPA2-Enterprise / PEAP (eduroam)");
    const char *ca = EAP_CA_CERT[0] ? EAP_CA_CERT : nullptr;
    if (!ca) Serial.println("# WARNING: EAP_CA_CERT is empty; server certificate verification is disabled.");
    WiFi.begin(WIFI_SSID, WPA2_AUTH_PEAP, EAP_IDENTITY, EAP_USERNAME, EAP_PASSWORD, ca);
#else
    Serial.println("# FATAL: this board package was built without WPA2-Enterprise support.");
    while (true) delay(1000);
#endif
  } else {
    Serial.println("# Connecting with personal Wi-Fi");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

void loop() {
  static uint32_t lastCheck = 0, lastRetry = 0, lastStatus = 0;
  static bool connected = false;
  const uint32_t now = millis();
  if (now - lastCheck >= 500) {
    lastCheck = now;
    wifi_ap_record_t ap = {};
    if (WiFi.status() == WL_CONNECTED && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
      const IPAddress gateway = WiFi.gatewayIP();
      if (!connected || memcmp(apMac, ap.bssid, 6) || gateway != activeGateway) {
        startCapture(ap, gateway);
      }
      connected = true;
      lastRetry = now;
    } else {
      if (connected) {
        stopCapture();
        Serial.println("# Wi-Fi disconnected; reconnecting");
      }
      connected = false;
      if (now - lastRetry >= 15000) {
        lastRetry = now;
        Serial.println("# Retrying Wi-Fi; check SSID, credentials, EAP settings and 2.4 GHz coverage");
        WiFi.reconnect();
      }
    }
  }
  CsiSample s;
  // Bound each batch so connection handling still runs under heavy traffic.
  for (size_t i = 0; i < 8 && xQueueReceive(samples, &s, 0) == pdTRUE; ++i) {
    if (Serial) printSample(s);
    else dropped.fetch_add(1);
  }
  if (now - lastStatus >= 5000) {
    lastStatus = now;
    Serial.printf("# status wifi=%s received=%lu printed=%lu dropped=%lu ping_ok=%lu ping_timeout=%lu\n",
        connected ? "up" : "down", (unsigned long)received.load(), (unsigned long)printed,
        (unsigned long)dropped.load(), (unsigned long)pingReplies.load(), (unsigned long)pingTimeouts.load());
  }
  delay(1);
}
