// From the PlatformIO project, copy this file to include/secrets.h and fill in
// your eduroam credentials. include/secrets.h is git-ignored.
#pragma once

#define WIFI_SSID      "eduroam"
#define EAP_IDENTITY   "your_UGA_ID@uga.edu"   // outer identity (usually same as username)
#define EAP_USERNAME   "your_UGA_ID@uga.edu"   // inner identity
#define EAP_PASSWORD   "your_UGA_password"

// Testing off campus? Set EAP_USERNAME to "" and the board joins WIFI_SSID as a normal
// WPA2-PSK network (phone hotspot, home router) using this password instead.
#define WIFI_PASSWORD  ""

// Optional: PEM of the CA that signs your campus RADIUS server certificate. Leave empty
// to skip server verification (like the course example) - convenient, but a rogue
// "eduroam" access point could then harvest the password above.
#define EAP_CA_CERT    ""
// #define EAP_CA_CERT  "-----BEGIN CERTIFICATE-----\n" \
//                      "MIIB...\n" \
//                      "-----END CERTIFICATE-----\n"
