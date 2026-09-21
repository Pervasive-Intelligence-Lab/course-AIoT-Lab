#pragma once
// Copy this file to secrets.h in the same sketch folder and fill in your credentials.
// secrets.h is local only and ignored by Git. Keep this example free of real credentials.

#define WIFI_SSID      "eduroam"
#define EAP_IDENTITY   "your_UGA_ID@uga.edu"
#define EAP_USERNAME   "your_UGA_ID@uga.edu"
#define EAP_PASSWORD   "your_UGA_password"

// The instructor will provide the server IP address in class.
// Replace <SERVER_IP_FROM_CLASS> only in your local secrets.h.
#define RELAY_UPLOAD_URL "http://<SERVER_IP_FROM_CLASS>:8001/upload"
