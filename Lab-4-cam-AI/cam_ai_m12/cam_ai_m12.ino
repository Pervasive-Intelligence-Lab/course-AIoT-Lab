/*
 * Lab 4 - AI Camera, AtomS3R-M12 version
 * AtomS3R-M12 (OV3660, 3 MP, native JPEG), optional Atomic Voice Base
 *
 *   Camera --Wi-Fi/eduroam--> Server --> Vision LLM --> Web page
 *                               |
 *                               +--> description text (+ optional speech) back to the board
 *
 * What the board does:
 *   1. Takes a photo every CAPTURE_INTERVAL_MS, or right away when you press
 *      Enter in Serial Monitor. With the Voice Base, a clap also works.
 *   2. POSTs the JPEG to  <SERVER_BASE_URL>/upload?describe=1
 *      The server asks a vision LLM and answers with a one-sentence description.
 *   3. Prints "AI sees: ..." to Serial Monitor. With the Voice Base, it also
 *      plays a shutter click and says the description (or a chime).
 *
 * The web dashboard at <SERVER_BASE_URL>/ shows every camera, its latest photo
 * and the LLM description. Same server as the AtomS3R-CAM sketch (cam_ai_voice).
 *
 * Based on Lab-2-camera-streaming/camera_edurom_relay_m12.
 *
 * Board:     M5Stack > M5AtomS3R  (PSRAM: OPI PSRAM)
 * Libraries: none extra. With USE_VOICE_BASE 1: M5Atomic-EchoBase.
 * Compiles with M5Stack board package 2.1.4 (as in Lab 2) and 3.3.x.
 */

#include "camera_pins.h"
// Copy secrets.example.h to secrets.h in this folder before compiling.
#include "secrets.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"

#if __has_include("esp_eap_client.h")
#include "esp_eap_client.h"
#else
#include "esp_wpa2.h"
#endif


// ============================================================
// Settings
// ============================================================

// Give every camera a unique ID so it gets its own dashboard card.
const char* deviceId   = "atoms3r-m12-ai-01";
const char* deviceName = "M12 AI Camera 01";

// Take a photo this often (ms). 0 = only on demand (Enter / clap).
// Every photo is one LLM request, so do not go much below 10 s.
#define CAPTURE_INTERVAL_MS 15000

// Photo size sent to the LLM. OV3660 supports up to FRAMESIZE_QXGA (2048x1536).
//   FRAMESIZE_VGA  (640x480)   ~30-60 KB, fast  (default)
//   FRAMESIZE_SVGA (800x600)   ~50-90 KB
//   FRAMESIZE_XGA  (1024x768)  more detail, slower upload and LLM
#define CAM_FRAME_SIZE FRAMESIZE_VGA

// JPEG quality from the camera: 0-63, lower = better quality / larger file.
#define CAM_JPEG_QUALITY 12

// The server waits for the LLM before it answers, so allow enough time.
#define HTTP_TIMEOUT_MS 30000

// Set to 1 if the M12 sits on an Atomic Voice Base (Atomic Echo Base).
// This needs the M5Atomic-EchoBase library (Tools > Manage Libraries).
#define USE_VOICE_BASE 0

#if USE_VOICE_BASE
// Clap trigger: a microphone level above this starts a capture (0 = off).
// Set MIC_DEBUG to 1 to print the level and pick a threshold for your room.
#define CLAP_THRESHOLD 3000
#define MIC_DEBUG      0
#define ENABLE_SPEECH  1
#define SPEAKER_VOLUME 70  // 0-100
#endif


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
const String speechUrl     = serverBaseUrl + "/api/speech/" + deviceId;


// ============================================================
// Camera configuration (OV3660, native JPEG)
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

    // The OV3660 compresses to JPEG itself: no conversion on the ESP32.
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size   = CAM_FRAME_SIZE,

    .jpeg_quality  = CAM_JPEG_QUALITY,
    .fb_count      = 2,
    .fb_location   = CAMERA_FB_IN_PSRAM,
    .grab_mode     = CAMERA_GRAB_LATEST,
    .sccb_i2c_port = 0
};


// ============================================================
// Atomic Voice Base (optional)
// ============================================================

#if USE_VOICE_BASE

#include <Wire.h>
#include <M5EchoBase.h>

// AtomS3R pin map for the Atomic Voice Base (from the M5Atomic-EchoBase
// example). On the M12 these are the GPIOs reserved for Atomic bases
// (G5/G6/G7/G8/G38/G39); the camera does not use them.
#define ECHO_I2C_SDA  38
#define ECHO_I2C_SCL  39
#define ECHO_I2S_DIN  7
#define ECHO_I2S_WS   6
#define ECHO_I2S_DOUT 5
#define ECHO_I2S_BCK  8

#define AUDIO_SAMPLE_RATE 16000  // Server speech is also 16 kHz

// The camera's SCCB bus uses I2C port 0, so the codec gets its own port (Wire1).
#define ECHO_WIRE Wire1

M5EchoBase echobase;
bool audioReady = false;

// The codec runs 16-bit stereo; one "frame" = left + right sample.
#define MIC_CHUNK_FRAMES 800  // 50 ms at 16 kHz
static int16_t micBuf[MIC_CHUNK_FRAMES * 2];

#define PLAY_CHUNK_FRAMES 512
static int16_t playBuf[PLAY_CHUNK_FRAMES * 2];

// Longest speech clip we accept (16 kHz mono 16-bit). 20 s = 640 KB in PSRAM.
#define MAX_SPEECH_BYTES (AUDIO_SAMPLE_RATE * 2 * 20)

void playTone(int freqHz, int durationMs, float amplitude = 0.4f)
{
    if (!audioReady) return;

    const int total = AUDIO_SAMPLE_RATE * durationMs / 1000;
    const int fade  = AUDIO_SAMPLE_RATE / 200;  // 5 ms fade in/out, avoids clicks
    int done = 0;

    while (done < total) {
        int n = min(PLAY_CHUNK_FRAMES, total - done);
        for (int i = 0; i < n; i++) {
            int k = done + i;
            float env = 1.0f;
            if (k < fade) env = (float)k / fade;
            if (total - k < fade) env = (float)(total - k) / fade;
            int16_t s = (int16_t)(amplitude * env * 32767.0f *
                                  sinf(2.0f * PI * freqHz * k / AUDIO_SAMPLE_RATE));
            playBuf[2 * i]     = s;
            playBuf[2 * i + 1] = s;
        }
        echobase.play((uint8_t*)playBuf, n * 4, false);
        done += n;
    }
}

// Write a little silence so the last samples leave the DMA buffer, then clear it.
void finishPlayback()
{
    if (!audioReady) return;
    memset(playBuf, 0, sizeof(playBuf));
    for (int i = 0; i < 4; i++) {
        echobase.play((uint8_t*)playBuf, sizeof(playBuf), i == 3);
    }
}

void soundShutter() { playTone(1800, 40); playTone(1200, 60); finishPlayback(); }
void soundChime()   { playTone(880, 120); playTone(1320, 180); finishPlayback(); }
void soundError()   { playTone(330, 150); playTone(220, 250); finishPlayback(); }
void soundReady()   { playTone(660, 80);  playTone(990, 80);  finishPlayback(); }

// Records 50 ms and returns the RMS level around the mean (0 - 32767).
int readMicLevel()
{
    if (!audioReady) return 0;
    if (!echobase.record((uint8_t*)micBuf, sizeof(micBuf))) return 0;

    const int n = MIC_CHUNK_FRAMES * 2;
    int64_t sum = 0;
    for (int i = 0; i < n; i++) sum += micBuf[i];
    const int32_t mean = sum / n;

    uint64_t sq = 0;
    for (int i = 0; i < n; i++) {
        int32_t d = micBuf[i] - mean;
        sq += (uint64_t)(d * d);
    }
    return (int)sqrtf((float)sq / n);
}

// Drop old microphone samples (e.g. our own speaker sound) after a pause.
void flushMic()
{
    for (int i = 0; i < 4; i++) readMicLevel();
}

void initVoiceBase()
{
    Serial.println("Initializing Atomic Voice Base...");

    if (!echobase.init(AUDIO_SAMPLE_RATE, ECHO_I2C_SDA, ECHO_I2C_SCL,
                       ECHO_I2S_DIN, ECHO_I2S_WS, ECHO_I2S_DOUT, ECHO_I2S_BCK,
                       ECHO_WIRE)) {
        Serial.println("Voice Base init failed - continuing without audio");
        return;
    }

    echobase.setSpeakerVolume(SPEAKER_VOLUME);
    echobase.setMicGain(ES8311_MIC_GAIN_18DB);
    echobase.setMute(false);  // enables the speaker amplifier
    audioReady = true;

    Serial.println("Voice Base Init Success");
}

// Download the spoken description and play it.
bool speakDescription()
{
    if (!audioReady) return false;

    WiFiClient client;
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    if (!http.begin(client, speechUrl)) return false;

    int httpCode = http.GET();
    if (httpCode != 200) {  // 204 = server has no TTS configured
        Serial.printf("Speech: HTTP %d\n", httpCode);
        http.end();
        return false;
    }

    int size = http.getSize();
    if (size <= 0 || size > MAX_SPEECH_BYTES) {
        Serial.printf("Speech: unexpected size %d\n", size);
        http.end();
        return false;
    }

    // Download the whole clip first so Wi-Fi hiccups do not cause gaps.
    uint8_t* pcm = (uint8_t*)ps_malloc(size);
    if (!pcm) {
        Serial.println("Speech: out of memory");
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    int got = 0;
    unsigned long deadline = millis() + HTTP_TIMEOUT_MS;
    while (got < size && http.connected() && millis() < deadline) {
        int avail = stream->available();
        if (avail > 0) {
            got += stream->readBytes(pcm + got, min(avail, size - got));
        } else {
            delay(2);
        }
    }
    http.end();

    Serial.printf("Speech: %d bytes (%.1f s)\n", got, got / 2.0f / AUDIO_SAMPLE_RATE);

    // Server sends mono 16-bit; the codec wants stereo frames.
    const int16_t* mono = (const int16_t*)pcm;
    int frames = got / 2;
    for (int done = 0; done < frames;) {
        int n = min(PLAY_CHUNK_FRAMES, frames - done);
        for (int i = 0; i < n; i++) {
            playBuf[2 * i]     = mono[done + i];
            playBuf[2 * i + 1] = mono[done + i];
        }
        echobase.play((uint8_t*)playBuf, n * 4, false);
        done += n;
    }
    finishPlayback();

    free(pcm);
    return got > 0;
}

#else  // no Voice Base: sounds do nothing

void soundShutter() {}
void soundChime()   {}
void soundError()   {}
void soundReady()   {}
void flushMic()     {}

#endif  // USE_VOICE_BASE


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
            soundError();
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
bool uploadAndDescribe(const uint8_t* jpg, size_t len, String& description, bool& speechAvailable)
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
    const char* wanted[] = {"X-Speech-Available"};
    http.collectHeaders(wanted, 1);

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
    speechAvailable = http.header("X-Speech-Available") == "1";
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
// Capture -> upload -> describe (-> speak)
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
        soundError();
        return;
    }

    soundShutter();

    Serial.printf("Frame: %u x %u | JPEG: %.1f KB\n",
                  fb->width, fb->height, fb->len / 1024.0);

    // The frame buffer is already a JPEG: upload it directly, then give it
    // back to the camera driver.
    String description;
    bool speechAvailable = false;
    bool ok = uploadAndDescribe(fb->buf, fb->len, description, speechAvailable);
    esp_camera_fb_return(fb);

    if (!ok) {
        soundError();
        return;
    }

    Serial.println();
    Serial.print("AI sees: ");
    Serial.println(description);
    Serial.println();

#if USE_VOICE_BASE
    if (ENABLE_SPEECH && speechAvailable && speakDescription()) return;
#endif

    soundChime();
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
    Serial.println("AtomS3R-M12 AI Camera -> Wi-Fi -> Server -> LLM");
    Serial.println("========================================");

    if (serverBaseUrl.indexOf('<') >= 0) {
        Serial.println("Set SERVER_BASE_URL in secrets.h to the server IP from class.");
    }

    initCamera();
#if USE_VOICE_BASE
    initVoiceBase();
#endif
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
#if USE_VOICE_BASE
    if (CLAP_THRESHOLD > 0 && audioReady) Serial.println("Or clap your hands.");
#endif

    soundReady();
    flushMic();
    captureAndDescribe();  // first photo right away
    flushMic();
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

#if USE_VOICE_BASE
    // Listening also paces the loop: each readMicLevel() takes 50 ms.
    if (audioReady) {
        int level = readMicLevel();
#if MIC_DEBUG
        Serial.printf("mic level: %d\n", level);
#endif
        if (CLAP_THRESHOLD > 0 && level > CLAP_THRESHOLD) {
            Serial.printf("Sound trigger (level %d)\n", level);
            trigger = true;
        }
    } else {
        delay(50);
    }
#else
    delay(50);
#endif

    if (CAPTURE_INTERVAL_MS > 0 && millis() - lastCapture >= CAPTURE_INTERVAL_MS) {
        trigger = true;
    }

    if (trigger) {
        captureAndDescribe();
        lastCapture = millis();
        flushMic();  // ignore our own voice / chime
    }
}
