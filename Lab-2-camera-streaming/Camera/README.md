# Camera

A basic HTTP camera stream for the **M5Stack AtomS3R-M12**. The board connects to ordinary Wi-Fi and serves its camera stream directly to a web browser on port **80**.

```text
AtomS3R-M12 -- local Wi-Fi / HTTP --> Browser
```

## Setup

1. Open [`Camera.ino`](Camera.ino) in Arduino IDE and select **M5Stack > M5AtomS3R**. See the [lab setup guide](../README.md#2-install-the-m5stack-board-package) if the board package is not installed.
2. Copy [`secrets.example.h`](secrets.example.h) to `secrets.h` in this folder. If `secrets.h` already exists, edit its Wi-Fi settings:

   ```cpp
   #define WIFI_SSID      "YOUR_WIFI_SSID"
   #define WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"
   ```

3. Replace both placeholders with your ordinary Wi-Fi network name and password. Use a network that lets the computer connect to the camera. `secrets.h` is ignored by Git.
4. Compile and upload the sketch. It is configured for the M12 camera with native JPEG output, UXGA resolution, and PSRAM frame buffers.
5. Open Serial Monitor at **115200 baud**. After `Camera Init Success` and a successful Wi-Fi connection, it prints the camera's IP and a URL.
6. Connect your computer to the same network and open the printed URL:

   ```text
   http://<CAMERA_IP>/
   ```

The browser receives the live MJPEG stream directly from the board. No eduroam account, Flask server, or relay address is required.

If Wi-Fi times out, check `WIFI_SSID` and `WIFI_PASSWORD`, then reset the board. If the browser cannot connect, confirm it is using the camera's current IP and that the Wi-Fi network allows connections between devices.
