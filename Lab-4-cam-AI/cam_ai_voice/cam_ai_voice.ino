/*
 * Lab 4 - AI Camera
 * AtomS3R-CAM + Atomic Voice Base (Atomic Echo Base, ES8311)
 *
 *   Camera --Wi-Fi/eduroam--> Server --> Vision LLM --> Web page
 *                               |
 *                               +--> description text (+ optional speech) back to the board
 *
 * What the board does:
 *   1. Takes a photo every CAPTURE_INTERVAL_MS, or right away when the Voice
 *      Base microphone hears a loud sound (clap your hands).
 *   2. Plays a "shutter" click on the Voice Base speaker.
 *   3. POSTs the JPEG to  <SERVER_BASE_URL>/upload?describe=1
 *      The server asks a vision LLM and answers with a one-sentence description.
 *   4. Prints the description to Serial Monitor. If the server has text-to-speech
 *      enabled, downloads <SERVER_BASE_URL>/api/speech/<deviceId> and says it;
 *      otherwise plays a short chime.
 *
 * The web dashboard at <SERVER_BASE_URL>/ shows every camera, its latest photo
 * and the LLM description.
 *
 * Based on Lab-2-camera-streaming/camera_edurom_relay (camera + eduroam code).
 *
 * Board:     M5Stack > M5AtomS3R  (PSRAM: OPI PSRAM)
 * Libraries: M5Atomic-EchoBase (Library Manager)
 * Compiles with M5Stack board package 2.1.4 (as in Lab 2) and 3.3.x.
 */

#include "camera_pins.h"
// Copy secrets.example.h to secrets.h in this folder before compiling.
#include "secrets.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <M5EchoBase.h>
#include "esp_camera.h"
#include "img_converters.h"

#if __has_include("esp_eap_client.h")
#include "esp_eap_client.h"
#else
#include "esp_wpa2.h"
#endif


// ============================================================
// Hardware selection
// ============================================================

// Lab 4 kit: AtomS3R-CAM (GC0308, RGB565 -> JPEG on the board).
#define USE_ATOMS3R_CAM
// For the AtomS3R-M12, use the dedicated sketch in ../cam_ai_m12 instead.
// #define USE_ATOMS3R_M12

#if defined(USE_ATOMS3R_CAM) == defined(USE_ATOMS3R_M12)
#error "Enable exactly one camera model: USE_ATOMS3R_CAM or USE_ATOMS3R_M12"
#endif


// ============================================================
// Settings
// ============================================================

// Give every camera a unique ID so it gets its own dashboard card.
const char* deviceId   = "atoms3r-cam-ai-01";
const char* deviceName = "AI Camera 01";

// Take a photo this often (ms). 0 = only when a loud sound is heard.
// Every photo is one LLM request, so do not go much below 10 s.
#define CAPTURE_INTERVAL_MS 15000

// Clap trigger: a microphone level above this starts a capture.
// Set MIC_DEBUG to 1 to print the level and pick a threshold for your room.
// Set CLAP_THRESHOLD to 0 to disable the sound trigger.
#define CLAP_THRESHOLD 3000
#define MIC_DEBUG      0

// Speak the description when the server has TTS enabled.
#define ENABLE_SPEECH  1
#define SPEAKER_VOLUME 70  // 0-100

// JPEG quality for RGB565 -> JPEG (AtomS3R-CAM). Higher = better / larger.
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
const String speechUrl     = serverBaseUrl + "/api/speech/" + deviceId;


// ============================================================
// Atomic Voice Base (ES8311 codec + speaker + microphone)
// ============================================================

// AtomS3R pin map for the Atomic Voice Base (from the M5Atomic-EchoBase example).
// None of these pins are used by the camera.
#define ECHO_I2C_SDA 38
#define ECHO_I2C_SCL 39
#define ECHO_I2S_DIN 7
#define ECHO_I2S_WS  6
#define ECHO_I2S_DOUT 5
#define ECHO_I2S_BCK 8

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


// ============================================================
// Camera configuration (same as Lab 2)
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

#ifdef USE_ATOMS3R_CAM
    // GC0308 produces RGB565; we convert to JPEG before upload.
    .pixel_format = PIXFORMAT_RGB565,
    .frame_size   = FRAMESIZE_QVGA,  // 320 x 240
#endif

#ifdef USE_ATOMS3R_M12
    // OV3660 outputs JPEG directly. VGA is plenty for the LLM and keeps
    // uploads small.
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size   = FRAMESIZE_VGA,   // 640 x 480
#endif

    .jpeg_quality  = 12,
    .fb_count      = 2,
    .fb_location   = CAMERA_FB_IN_PSRAM,
    .grab_mode     = CAMERA_GRAB_LATEST,
    .sccb_i2c_port = 0
};


// ============================================================
// Sounds
// ============================================================

// Play a sine tone (blocking).
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


// ============================================================
// Microphone level (for the clap trigger)
// ============================================================

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


// ============================================================
// Initialize Voice Base
// ============================================================

void initVoiceBase()
{
    Serial.println("Initializing Atomic Voice Base...");

    if (!echobase.init(AUDIO_SAMPLE_RATE, ECHO_I2C_SDA, ECHO_I2C_SCL,
                       ECHO_I2S_DIN, ECHO_I2S_WS, ECHO_I2S_DOUT, ECHO_I2S_BCK,
                       ECHO_WIRE)) {
        // The camera still works without the base; we just stay silent.
        Serial.println("Voice Base init failed - continuing without audio");
        return;
    }

    echobase.setSpeakerVolume(SPEAKER_VOLUME);
    echobase.setMicGain(ES8311_MIC_GAIN_18DB);
    echobase.setMute(false);  // enables the speaker amplifier
    audioReady = true;

    Serial.println("Voice Base Init Success");
}


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
// Initialize camera (same as Lab 2)
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

    Serial.println("Camera Init Success");
}


// ============================================================
// Upload one JPEG and receive the LLM description
// ============================================================

// Returns true and fills `description` when the server answered 200.
bool uploadAndDescribe(uint8_t* jpg, size_t len, String& description, bool& speechAvailable)
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
    int httpCode = http.POST(jpg, len);
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
// Download the spoken description and play it
// ============================================================

bool speakDescription()
{
    if (!audioReady) return false;

    WiFiClient client;
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);

    if (!http.begin(client, speechUrl)) return false;

    int httpCode = http.GET();
    if (httpCode != 200) {
        // 204 = server has no TTS configured
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

    Serial.printf("Speech: %d bytes (%.1f s)\n", got,
                  got / 2.0f / AUDIO_SAMPLE_RATE);

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


// ============================================================
// Capture -> upload -> describe -> speak
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

    uint8_t* jpg = NULL;
    size_t jpgLen = 0;

#ifdef USE_ATOMS3R_CAM
    // RGB565 -> JPEG (allocates a new buffer that we must free)
    if (!frame2jpg(fb, JPEG_QUALITY, &jpg, &jpgLen)) {
        Serial.println("JPEG conversion failed");
        esp_camera_fb_return(fb);
        soundError();
        return;
    }
    // The JPEG is a copy, so the camera buffer can go back right away.
    Serial.printf("Frame: %u x %u | JPEG: %.1f KB\n", fb->width, fb->height, jpgLen / 1024.0);
    esp_camera_fb_return(fb);
    fb = NULL;
#else
    // M12: the frame buffer is already a JPEG
    jpg = fb->buf;
    jpgLen = fb->len;
    Serial.printf("Frame: %u x %u | JPEG: %.1f KB\n", fb->width, fb->height, jpgLen / 1024.0);
#endif

    String description;
    bool speechAvailable = false;
    bool ok = uploadAndDescribe(jpg, jpgLen, description, speechAvailable);

#ifdef USE_ATOMS3R_CAM
    free(jpg);
#else
    esp_camera_fb_return(fb);
#endif

    if (!ok) {
        soundError();
        return;
    }

    Serial.println();
    Serial.print("AI sees: ");
    Serial.println(description);
    Serial.println();

    if (ENABLE_SPEECH && speechAvailable && speakDescription()) return;

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
    Serial.println("AtomS3R AI Camera -> Wi-Fi -> Server -> LLM");
    Serial.println("========================================");

    if (serverBaseUrl.indexOf('<') >= 0) {
        Serial.println("Set SERVER_BASE_URL in secrets.h to the server IP from class.");
    }

    initCamera();
    initVoiceBase();
    connectWiFi();

    Serial.println();
    Serial.println("System ready.");
    Serial.print("Server:    ");
    Serial.println(serverBaseUrl);
    Serial.print("Device ID: ");
    Serial.println(deviceId);
    if (CAPTURE_INTERVAL_MS > 0) {
        Serial.printf("Taking a photo every %d s", CAPTURE_INTERVAL_MS / 1000);
    }
    if (CLAP_THRESHOLD > 0 && audioReady) {
        Serial.print(CAPTURE_INTERVAL_MS > 0 ? " or when you clap." : "Clap to take a photo.");
    }
    Serial.println();

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

    if (CAPTURE_INTERVAL_MS > 0 && millis() - lastCapture >= CAPTURE_INTERVAL_MS) {
        trigger = true;
    }

    if (trigger) {
        captureAndDescribe();
        lastCapture = millis();
        flushMic();  // ignore our own voice / chime
    }
}
