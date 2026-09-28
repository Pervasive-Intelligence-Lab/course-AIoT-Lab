/*
 * Lab 4 - AI Camera, AtomS3R-CAM version
 * AtomS3R-CAM (GC0308, 0.3 MP, RGB565 -> JPEG on the board)
 *
 *   Camera --Wi-Fi/eduroam--> Server --> Vision LLM --> Web page
 *                               |
 *                               +--> description text back to the board
 *
 * What the board does:
 *   1. Takes a photo every CAPTURE_INTERVAL_MS, or right away when you press
 *      Enter in Serial Monitor.
 *   2. POSTs the JPEG to  <SERVER_BASE_URL>/upload?describe=1
 *      The server asks a vision LLM and answers with a one-sentence description.
 *   3. Prints "AI sees: ..." to Serial Monitor.
 *
 * The web dashboard at <SERVER_BASE_URL>/ shows every camera, its latest photo
 * and the LLM description. Same server as the AtomS3R-M12 sketch (cam_ai_m12).
 *
 * Based on Lab-2-camera-streaming/camera_edurom_relay (AtomS3R-CAM preset).
 *
 * Board:     M5Stack > M5AtomS3R  (PSRAM: OPI PSRAM)
 * Libraries: none beyond the M5Stack board package.
 * Compiles with M5Stack board package 2.1.4 (as in Lab 2) and 3.3.x.
 */

#include "camera_pins.h"
// Copy secrets.example.h to secrets.h in this folder before compiling.
#include "secrets.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include "img_converters.h"

#if __has_include("esp_eap_client.h")
#include "esp_eap_client.h"
#else
#include "esp_wpa2.h"
#endif


// ============================================================
// Settings
// ============================================================

// Give every camera a unique ID so it gets its own dashboard card.
const char* deviceId   = "atoms3r-cam-ai-01";
const char* deviceName = "AI Camera 01";

// Take a photo this often (ms). 0 = only when you press Enter in Serial Monitor.
// Every photo is one LLM request, so do not go much below 10 s.
#define CAPTURE_INTERVAL_MS 15000

// Photo size. The GC0308 is a 0.3 MP (640x480) sensor; the official M5Stack
// example uses FRAMESIZE_QVGA (320x240), which gives ~10-20 KB JPEGs.
#define CAM_FRAME_SIZE FRAMESIZE_QVGA

// JPEG quality for the RGB565 -> JPEG conversion: 1-100, higher = better / larger.
#define JPEG_QUALITY 80

// The server waits for the LLM before it answers, so allow enough time.
#define HTTP_TIMEOUT_MS 30000

// ============================================================
// Wi-Fi / server configuration (from secrets.h)
// ============================================================

const char* ssid = WIFI_SSID;

#ifndef WIFI_PASSWORD
// eduroam (WPA2-Enterprise)
const char* eap_identity = EAP_IDENTITY;
const char* eap_username = EAP_USERNAME;
const char* eap_password = EAP_PASSWORD;
#endif

// Example: "http://<SERVER_IP_FROM_CLASS>:8001" (no trailing slash)
const String serverBaseUrl = SERVER_BASE_URL;
const String uploadUrl     = serverBaseUrl + "/upload?describe=1";


// ============================================================
// Camera configuration (GC0308, RGB565), same as Lab 2
// ============================================================

static camera_config_t camera_config = {
    .pin_pwdn     = PWDN_GPIO_NUM,
    .pin_reset    = RESET_GPIO_NUM,
    .pin_xclk     = XCLK_GPIO_NUM,
    .pin_sscb_sda = SIOD_GPIO_NUM,
    .pin_sscb_scl = SIOC_GPIO_NUM,

    .pin_d7 = Y9_GPIO_NUM,
    .pin_d6 = Y8_GPIO_NUM,
    .pin_d5 = Y7_GPIO_NUM,
    .pin_d4 = Y6_GPIO_NUM,
    .pin_d3 = Y5_GPIO_NUM,
    .pin_d2 = Y4_GPIO_NUM,
    .pin_d1 = Y3_GPIO_NUM,
    .pin_d0 = Y2_GPIO_NUM,

    .pin_vsync = VSYNC_GPIO_NUM,
    .pin_href  = HREF_GPIO_NUM,
    .pin_pclk  = PCLK_GPIO_NUM,

    .xclk_freq_hz = 20000000,
    .ledc_timer   = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    // The GC0308 has no JPEG encoder: it outputs RGB565 and the ESP32
    // converts each photo to JPEG before upload (frame2jpg).
    .pixel_format = PIXFORMAT_RGB565,
    .frame_size   = CAM_FRAME_SIZE,

    .jpeg_quality  = 12,  // not used for RGB565
    .fb_count      = 2,
    .fb_location   = CAMERA_FB_IN_PSRAM,
    .grab_mode     = CAMERA_GRAB_LATEST,
    .sccb_i2c_port = 0
};


// ============================================================
// Connect to Wi-Fi (eduroam or ordinary WPA2)
// ============================================================

void connectWiFi()
{
    Serial.println();
    Serial.println("========================================");
    Serial.print("Connecting to ");
    Serial.println(ssid);
    Serial.println("========================================");

    WiFi.disconnect(true);
    delay(500);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

#ifdef WIFI_PASSWORD

    // Ordinary WPA2-Personal network (home / lab router / phone hotspot)
    WiFi.begin(ssid, WIFI_PASSWORD);

#else

    Serial.print("Identity: ");
    Serial.println(eap_identity);

#if __has_include("esp_eap_client.h")
    esp_eap_client_set_identity((uint8_t*)eap_identity, strlen(eap_identity));
    esp_eap_client_set_username((uint8_t*)eap_username, strlen(eap_username));
    esp_eap_client_set_password((uint8_t*)eap_password, strlen(eap_password));
    esp_wifi_sta_enterprise_enable();
#else
    esp_wifi_sta_wpa2_ent_set_identity((uint8_t*)eap_identity, strlen(eap_identity));
    esp_wifi_sta_wpa2_ent_set_username((uint8_t*)eap_username, strlen(eap_username));
    esp_wifi_sta_wpa2_ent_set_password((uint8_t*)eap_password, strlen(eap_password));
    esp_wifi_sta_wpa2_ent_enable();
#endif

    // Do NOT put the password in WiFi.begin() for eduroam.
    WiFi.begin(ssid);

#endif

    Serial.print("Connecting");
    int retry = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        if (++retry >= 60) {  // about 30 s
            Serial.println();
            Serial.println("Failed to connect to Wi-Fi. Restarting...");
            delay(1000);
            ESP.restart();
        }
    }

    Serial.println();
    Serial.println("========================================");
#ifdef WIFI_PASSWORD
    Serial.println("Wi-Fi connected!");
#else
    Serial.println("eduroam connected!");
#endif
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    Serial.print("MAC: ");
    Serial.println(WiFi.macAddress());
    Serial.println("========================================");
}


// ============================================================
// Initialize camera
// ============================================================

void initCamera()
{
    Serial.println("Initializing camera...");

    pinMode(POWER_GPIO_NUM, OUTPUT);
    digitalWrite(POWER_GPIO_NUM, LOW);  // LOW = camera power on
    delay(500);

    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed: 0x%x\n", err);
        delay(1000);
        ESP.restart();
    }

    sensor_t* s = esp_camera_sensor_get();
    Serial.printf("Camera Init Success (sensor PID 0x%04x)\n", s ? s->id.PID : 0);

    // Let auto-exposure and white balance settle: the first frames are dark.
    for (int i = 0; i < 5; i++) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb) esp_camera_fb_return(fb);
        delay(100);
    }
}


// ============================================================
// Upload one JPEG and receive the LLM description
// ============================================================

// Returns true and fills `description` when the server answered 200.
bool uploadAndDescribe(const uint8_t* jpg, size_t len, String& description)
{
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi disconnected.");
        return false;
    }

    WiFiClient client;
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);

    Serial.printf("POST %s | %u bytes\n", uploadUrl.c_str(), (unsigned)len);

    if (!http.begin(client, uploadUrl)) {
        Serial.println("HTTP begin failed");
        return false;
    }

    http.addHeader("Content-Type", "image/jpeg");
    http.addHeader("X-Device-ID", deviceId);
    http.addHeader("X-Device-Name", deviceName);

    unsigned long start = millis();
    int httpCode = http.POST((uint8_t*)jpg, len);
    unsigned long elapsed = millis() - start;

    if (httpCode <= 0) {
        Serial.print("HTTP POST failed: ");
        Serial.println(http.errorToString(httpCode));
        http.end();
        return false;
    }

    Serial.printf("HTTP response: %d | %lu ms\n", httpCode, elapsed);

    String body = http.getString();
    http.end();

    if (httpCode != 200) {
        Serial.print("Server error: ");
        Serial.println(body);
        return false;
    }

    description = body;
    return true;
}


// ============================================================
// Capture -> upload -> describe
// ============================================================

void captureAndDescribe()
{
    Serial.println();
    Serial.println("---------------- New photo ----------------");

    // Throw away one frame so we do not send an old picture from the buffer.
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);

    fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        return;
    }

    // RGB565 -> JPEG. frame2jpg allocates a new buffer that we must free.
    uint8_t* jpg = NULL;
    size_t jpgLen = 0;
    bool converted = frame2jpg(fb, JPEG_QUALITY, &jpg, &jpgLen);

    Serial.printf("Frame: %u x %u | JPEG: %.1f KB\n",
                  fb->width, fb->height, jpgLen / 1024.0);

    // The JPEG is a copy, so the camera buffer can go back right away.
    esp_camera_fb_return(fb);

    if (!converted) {
        Serial.println("JPEG conversion failed");
        return;
    }

    String description;
    bool ok = uploadAndDescribe(jpg, jpgLen, description);
    free(jpg);

    if (!ok) return;

    Serial.println();
    Serial.print("AI sees: ");
    Serial.println(description);
    Serial.println();
}


// ============================================================
// Setup
// ============================================================

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("AtomS3R-CAM AI Camera -> Wi-Fi -> Server -> LLM");
    Serial.println("========================================");

    if (serverBaseUrl.indexOf('<') >= 0) {
        Serial.println("Set SERVER_BASE_URL in secrets.h to the server IP from class.");
    }

    initCamera();
    connectWiFi();

    Serial.println();
    Serial.println("System ready.");
    Serial.print("Server:    ");
    Serial.println(serverBaseUrl);
    Serial.print("Device ID: ");
    Serial.println(deviceId);
    if (CAPTURE_INTERVAL_MS > 0) {
        Serial.printf("Taking a photo every %d s.\n", CAPTURE_INTERVAL_MS / 1000);
    }
    Serial.println("Press Enter in Serial Monitor to take a photo now.");

    captureAndDescribe();  // first photo right away
}


// ============================================================
// Loop
// ============================================================

unsigned long lastCapture = 0;

void loop()
{
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi lost. Reconnecting...");
        connectWiFi();
        return;
    }

    bool trigger = false;

    // Serial Monitor: press Enter (send a newline) to take a photo.
    if (Serial.available()) {
        while (Serial.available()) Serial.read();
        Serial.println("Serial trigger");
        trigger = true;
    }

    delay(50);

    if (CAPTURE_INTERVAL_MS > 0 && millis() - lastCapture >= CAPTURE_INTERVAL_MS) {
        trigger = true;
    }

    if (trigger) {
        captureAndDescribe();
        lastCapture = millis();
    }
}
