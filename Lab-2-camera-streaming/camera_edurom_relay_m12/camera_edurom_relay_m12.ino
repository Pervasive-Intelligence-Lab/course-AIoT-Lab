/*
 * AtomS3R-M12 student relay version (OV3660, native JPEG)
 * Camera -> eduroam -> HTTP Server
 *
 * Arduino M5Stack Board Manager v2.1.4
 */

#include "camera_pins.h"
// Copy secrets.example.h to secrets.h in this folder before compiling.
#include "secrets.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include "img_converters.h"

// ============================================================
// WPA2 Enterprise / eduroam
// ============================================================

#if __has_include("esp_eap_client.h")
#include "esp_eap_client.h"
#else
#include "esp_wpa2.h"
#endif


// ============================================================
// Hardware selection
// ============================================================

// Student hardware: AtomS3R-M12 (OV3660), matching Camera.
// #define USE_ATOMS3R_CAM
#define USE_ATOMS3R_M12

#if defined(USE_ATOMS3R_CAM) == defined(USE_ATOMS3R_M12)
#error "Enable exactly one camera model: USE_ATOMS3R_CAM or USE_ATOMS3R_M12"
#endif


// ============================================================
// eduroam configuration
// ============================================================

const char* ssid         = WIFI_SSID;

// Credentials are loaded from the local secrets.h file.
const char* eap_identity = EAP_IDENTITY;
const char* eap_username = EAP_USERNAME;
const char* eap_password = EAP_PASSWORD;


// ============================================================
// Server configuration
// ============================================================

// The instructor will provide the server IP address in class.
// Set RELAY_UPLOAD_URL in your local secrets.h, including :8001/upload.
const char* uploadUrl = RELAY_UPLOAD_URL;


// Optional device identifier
// Give each student camera a unique ID so its stream has its own dashboard entry.
const char* deviceId = "atoms3r-m12-01";


// ============================================================
// Upload settings
// ============================================================

// Delay between frames.
// 100 ms ~= max 10 fps
// 200 ms ~= max 5 fps
#define FRAME_INTERVAL_MS 200

// JPEG quality when converting RGB565 -> JPEG
// Lower number = better quality / larger frame
// Typical range: 10-80
#define JPEG_QUALITY 80

// HTTP timeout
#define HTTP_TIMEOUT_MS 5000


// ============================================================
// Camera frame buffers
// ============================================================

camera_fb_t* fb = NULL;

uint8_t* out_jpg = NULL;
size_t out_jpg_len = 0;


// ============================================================
// Camera configuration
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

    // AtomS3R-CAM produces RGB565.
    // We convert RGB565 -> JPEG before upload.

    .pixel_format = PIXFORMAT_RGB565,
    .frame_size   = FRAMESIZE_QVGA,

#endif


#ifdef USE_ATOMS3R_M12

    // M12 can directly output JPEG.

    .pixel_format = PIXFORMAT_JPEG,
    .frame_size   = FRAMESIZE_UXGA,

#endif


    .jpeg_quality = 12,

    .fb_count = 2,

    .fb_location =
        CAMERA_FB_IN_PSRAM,

    .grab_mode =
        CAMERA_GRAB_LATEST,

    .sccb_i2c_port = 0
};


// ============================================================
// Connect to eduroam
// ============================================================

void connectEduroam()
{
    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        "Connecting to eduroam..."
    );

    Serial.print("Identity: ");
    Serial.println(eap_identity);

    Serial.println(
        "========================================"
    );


    // Disconnect previous connection
    WiFi.disconnect(true);

    delay(500);


    WiFi.mode(WIFI_STA);

    // Important for continuous camera transmission
    WiFi.setSleep(false);


#if __has_include("esp_eap_client.h")

    // --------------------------------------------------------
    // New ESP-IDF / Arduino ESP32
    // --------------------------------------------------------

    esp_eap_client_set_identity(
        (uint8_t*)eap_identity,
        strlen(eap_identity)
    );

    esp_eap_client_set_username(
        (uint8_t*)eap_username,
        strlen(eap_username)
    );

    esp_eap_client_set_password(
        (uint8_t*)eap_password,
        strlen(eap_password)
    );

    esp_wifi_sta_enterprise_enable();


#else

    // --------------------------------------------------------
    // Older ESP-IDF / Arduino ESP32
    // --------------------------------------------------------

    esp_wifi_sta_wpa2_ent_set_identity(
        (uint8_t*)eap_identity,
        strlen(eap_identity)
    );

    esp_wifi_sta_wpa2_ent_set_username(
        (uint8_t*)eap_username,
        strlen(eap_username)
    );

    esp_wifi_sta_wpa2_ent_set_password(
        (uint8_t*)eap_password,
        strlen(eap_password)
    );

    esp_wifi_sta_wpa2_ent_enable();

#endif


    // Important:
    // Do NOT put password in WiFi.begin().
    WiFi.begin(ssid);


    Serial.print("Connecting");


    int retry = 0;

    while (WiFi.status() != WL_CONNECTED) {

        delay(500);

        Serial.print(".");

        retry++;


        // About 30 seconds
        if (retry >= 60) {

            Serial.println();

            Serial.println(
                "Failed to connect to eduroam."
            );

            Serial.println(
                "Restarting..."
            );

            delay(1000);

            ESP.restart();
        }
    }


    Serial.println();

    Serial.println(
        "========================================"
    );

    Serial.println(
        "eduroam connected!"
    );


    Serial.print("IP address: ");
    Serial.println(
        WiFi.localIP()
    );


    Serial.print("Gateway: ");
    Serial.println(
        WiFi.gatewayIP()
    );


    Serial.print("Subnet mask: ");
    Serial.println(
        WiFi.subnetMask()
    );


    Serial.print("RSSI: ");
    Serial.print(
        WiFi.RSSI()
    );
    Serial.println(" dBm");


    Serial.print("MAC: ");
    Serial.println(
        WiFi.macAddress()
    );


    Serial.println(
        "========================================"
    );
}


// ============================================================
// Initialize camera
// ============================================================

void initCamera()
{
    Serial.println(
        "Initializing camera..."
    );


    pinMode(
        POWER_GPIO_NUM,
        OUTPUT
    );


    digitalWrite(
        POWER_GPIO_NUM,
        LOW
    );


    delay(500);


    esp_err_t err =
        esp_camera_init(
            &camera_config
        );


    if (err != ESP_OK) {

        Serial.printf(
            "Camera init failed: 0x%x\n",
            err
        );

        delay(1000);

        ESP.restart();
    }


    Serial.println(
        "Camera Init Success"
    );
}


// ============================================================
// Upload JPEG
// ============================================================

bool uploadJPEG(
    uint8_t* data,
    size_t length
)
{
    if (
        WiFi.status()
        != WL_CONNECTED
    ) {

        Serial.println(
            "WiFi disconnected."
        );

        return false;
    }


    WiFiClient client;

    HTTPClient http;


    http.setTimeout(
        HTTP_TIMEOUT_MS
    );


    Serial.print(
        "POST "
    );

    Serial.print(
        uploadUrl
    );

    Serial.print(
        " | "
    );

    Serial.print(
        length
    );

    Serial.println(
        " bytes"
    );


    if (
        !http.begin(
            client,
            uploadUrl
        )
    ) {

        Serial.println(
            "HTTP begin failed"
        );

        return false;
    }


    // Tell server that the body is JPEG
    http.addHeader(
        "Content-Type",
        "image/jpeg"
    );


    // Device ID is useful if multiple cameras upload
    http.addHeader(
        "X-Device-ID",
        deviceId
    );


    // Prevent caching
    http.addHeader(
        "Cache-Control",
        "no-cache"
    );


    unsigned long start =
        millis();


    int httpCode =
        http.POST(
            data,
            length
        );


    unsigned long elapsed =
        millis() - start;


    if (httpCode > 0) {

        Serial.print(
            "HTTP response: "
        );

        Serial.print(
            httpCode
        );


        Serial.print(
            " | "
        );

        Serial.print(
            elapsed
        );

        Serial.println(
            " ms"
        );


        if (
            httpCode >= 200
            && httpCode < 300
        ) {

            http.end();

            return true;
        }

    } else {

        Serial.print(
            "HTTP POST failed: "
        );

        Serial.println(
            http.errorToString(
                httpCode
            )
        );
    }


    http.end();

    return false;
}


// ============================================================
// Capture and upload one frame
// ============================================================

void captureAndUpload()
{
    fb =
        esp_camera_fb_get();


    if (!fb) {

        Serial.println(
            "Camera capture failed"
        );

        return;
    }


    // --------------------------------------------------------
    // AtomS3R-CAM
    // RGB565 -> JPEG
    // --------------------------------------------------------

#ifdef USE_ATOMS3R_CAM

    out_jpg = NULL;

    out_jpg_len = 0;


    bool converted =
        frame2jpg(
            fb,
            JPEG_QUALITY,
            &out_jpg,
            &out_jpg_len
        );


    if (!converted) {

        Serial.println(
            "JPEG conversion failed"
        );


        esp_camera_fb_return(
            fb
        );


        fb = NULL;

        return;
    }

#endif


    // --------------------------------------------------------
    // AtomS3R-M12
    // Already JPEG
    // --------------------------------------------------------

#ifdef USE_ATOMS3R_M12

    out_jpg =
        fb->buf;


    out_jpg_len =
        fb->len;

#endif


    Serial.print(
        "Frame: "
    );

    Serial.print(
        fb->width
    );

    Serial.print(
        " x "
    );

    Serial.print(
        fb->height
    );

    Serial.print(
        " | JPEG: "
    );

    Serial.print(
        out_jpg_len / 1024.0
    );

    Serial.println(
        " KB"
    );


    // Upload
    uploadJPEG(
        out_jpg,
        out_jpg_len
    );


    // --------------------------------------------------------
    // Return camera buffer
    // --------------------------------------------------------

    esp_camera_fb_return(
        fb
    );

    fb = NULL;


    // --------------------------------------------------------
    // AtomS3R-CAM:
    // frame2jpg allocated a new buffer.
    // Must free it.
    // --------------------------------------------------------

#ifdef USE_ATOMS3R_CAM

    if (out_jpg) {

        free(
            out_jpg
        );

        out_jpg = NULL;

        out_jpg_len = 0;
    }

#endif


#ifdef USE_ATOMS3R_M12

    // Do NOT free out_jpg because
    // it points directly to fb->buf.

    out_jpg = NULL;

    out_jpg_len = 0;

#endif
}


// ============================================================
// Setup
// ============================================================

void setup()
{
    Serial.begin(
        115200
    );


    delay(1000);


    Serial.println();
    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        "AtomS3R-M12 Camera -> eduroam -> Server"
    );

    Serial.println(
        "========================================"
    );


    // Initialize camera
    initCamera();


    // Connect WiFi
    connectEduroam();


    Serial.println();
    Serial.println(
        "System ready."
    );

    Serial.print(
        "Uploading to: "
    );

    Serial.println(
        uploadUrl
    );
}


// ============================================================
// Loop
// ============================================================

void loop()
{
    // --------------------------------------------------------
    // Reconnect WiFi if needed
    // --------------------------------------------------------

    if (
        WiFi.status()
        != WL_CONNECTED
    ) {

        Serial.println(
            "WiFi lost. Reconnecting..."
        );

        connectEduroam();

        return;
    }


    // --------------------------------------------------------
    // Capture and upload
    // --------------------------------------------------------

    captureAndUpload();


    // Frame rate control
    delay(
        FRAME_INTERVAL_MS
    );
}
