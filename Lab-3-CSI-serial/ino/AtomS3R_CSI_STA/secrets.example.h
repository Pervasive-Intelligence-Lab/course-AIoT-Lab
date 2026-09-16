#pragma once
// Copy to secrets.h in this directory (git-ignored). Keep real credentials there.
// Both eduroam and personal Wi-Fi require 2.4 GHz coverage on the ESP32-S3.

// eduroam: WPA2-Enterprise / PEAP with MSCHAPv2.
#define WIFI_SSID      "eduroam"
#define EAP_USERNAME   "your-username@your-university.edu"  // Inner identity, including realm.
#define EAP_IDENTITY   EAP_USERNAME  // Outer identity; use your campus IT settings.
#define EAP_PASSWORD   "your-password"

// Personal Wi-Fi: set EAP_USERNAME to "", change WIFI_SSID, and fill this in.
#define WIFI_PASSWORD  ""

// Optional: PEM CA certificate supplied by campus IT for the RADIUS server.
// Leaving this empty disables server certificate verification; a rogue AP could
// steal your credentials. Configure the campus CA when using real credentials.
#define EAP_CA_CERT    ""
// Replace the definition above with the full PEM, for example:
// #define EAP_CA_CERT "-----BEGIN CERTIFICATE-----\n" \
//                     "...\n" \
//                     "-----END CERTIFICATE-----\n"
