#pragma once
// Copy this file to secrets.h in the same sketch folder and fill in your settings.
// secrets.h is local only and ignored by Git. Keep this example free of real credentials.

// ---- Option A: eduroam (WPA2-Enterprise), default ----
#define WIFI_SSID      "eduroam"
#define EAP_IDENTITY   "your_UGA_ID@uga.edu"
#define EAP_USERNAME   "your_UGA_ID@uga.edu"
#define EAP_PASSWORD   "your_UGA_password"

// ---- Option B: ordinary Wi-Fi (home router / hotspot) ----
// Comment out the four lines above and use these two instead.
// #define WIFI_SSID      "YOUR_WIFI_SSID"
// #define WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"

// The instructor will provide the server IP address in class.
// Replace <SERVER_IP_FROM_CLASS> only in your local secrets.h. No trailing slash.
#define SERVER_BASE_URL "http://<SERVER_IP_FROM_CLASS>:8001"
