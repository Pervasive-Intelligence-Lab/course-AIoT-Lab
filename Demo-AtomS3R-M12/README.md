# AtomS3R-M12 Realtime IMU Dashboard

This demo streams the M5Stack AtomS3R-M12's BMI270 accelerometer/gyroscope and BMM150 magnetometer over Wi-Fi to a live 3D browser dashboard.

```text
AtomS3R-M12
  -> UDP JSON samples at 50 Hz
  -> Python bridge
  -> Server-Sent Events
  -> browser dashboard
```

![Dashboard](docs/dashboard.png)

The firmware is provided in two equivalent forms:

- [`platformio/`](platformio/) is a complete PlatformIO project with an AtomS3R-M12 board definition.
- [`ino/AtomS3R_M12_IMU_Dashboard/`](ino/AtomS3R_M12_IMU_Dashboard/) is a conventional Arduino IDE sketch.

Both versions produce the same UDP messages and use the same backend and dashboard. Choose one firmware workflow; you do not need to flash both.

## Project structure

```text
Demo-AtomS3R-M12/
├── README.md
├── platformio/
│   ├── platformio.ini
│   ├── boards/m5stack-atoms3r.json
│   ├── include/secrets.example.h
│   └── src/main.cpp
├── ino/
│   └── AtomS3R_M12_IMU_Dashboard/
│       ├── AtomS3R_M12_IMU_Dashboard.ino
│       └── secrets.example.h
├── backend/server.py
├── web/
└── docs/dashboard.png
```

## What you need

- M5Stack AtomS3R-M12 and a USB-C data cable
- 2.4 GHz Wi-Fi: eduroam/WPA2-Enterprise or a WPA2-PSK network
- PlatformIO or Arduino IDE for the firmware
- Python 3.8 or later for the bridge; no Python packages are required
- A computer that can reach the board over the local network

This demo reads the M12 board's IMU. Its OV3660 camera is not used, so no camera pin or image-format selection is required.

## 1. Configure Wi-Fi credentials

Keep real credentials in an untracked `secrets.h`. Start by copying the example for the firmware version you selected.

### PlatformIO

From the `platformio` directory:

```powershell
Copy-Item include\secrets.example.h include\secrets.h
```

On macOS or Linux:

```bash
cp include/secrets.example.h include/secrets.h
```

### Arduino IDE

From `ino/AtomS3R_M12_IMU_Dashboard`:

```powershell
Copy-Item secrets.example.h secrets.h
```

On macOS or Linux:

```bash
cp secrets.example.h secrets.h
```

Edit the copied file:

```cpp
#define WIFI_SSID      "eduroam"
#define EAP_IDENTITY   "your_UGA_ID@uga.edu"
#define EAP_USERNAME   "your_UGA_ID@uga.edu"
#define EAP_PASSWORD   "your_UGA_password"
```

The demo's `.gitignore` excludes both generated `secrets.h` paths. Do not rename `secrets.example.h` itself or put real credentials in the tracked example.

For a normal WPA2-PSK network such as a phone hotspot, set `EAP_USERNAME` to an empty string, change `WIFI_SSID`, and set `WIFI_PASSWORD`.

`EAP_CA_CERT` can contain the PEM certificate for the campus RADIUS server. Leaving it empty matches the existing course example but does not verify the authentication server, which can expose credentials to a rogue access point.

## 2A. Build and flash with PlatformIO

The included board manifest configures the AtomS3R-M12's ESP32-S3-PICO-1-N8R8, 8 MB Flash, 8 MB OPI PSRAM, and native USB CDC. It avoids the incompatible ordinary AtomS3 memory configuration.

Open a terminal in `platformio` and run:

```bash
pio run
pio run -t upload
pio device monitor
```

The monitor uses `115200` baud. If automatic port detection fails, add a port explicitly:

```bash
pio run -t upload --upload-port COM3
pio device monitor --port COM3
```

Replace `COM3` with the port shown on your computer. In the VS Code extension, the Build, Upload, and Serial Monitor toolbar actions run the same operations.

## 2B. Build and flash with Arduino IDE

1. Install the M5Stack board package by following the [M5Stack Arduino board guide](https://docs.m5stack.com/en/arduino/arduino_board).
2. Select **Tools > Board > M5Stack > M5AtomS3R**.
3. Install **M5Unified 0.2.20** with Library Manager. This is the version used to validate the sensor scaling in this demo.
4. Open [`AtomS3R_M12_IMU_Dashboard.ino`](ino/AtomS3R_M12_IMU_Dashboard/AtomS3R_M12_IMU_Dashboard.ino).
5. Select the board's USB port, click **Verify**, and then click **Upload**.
6. Open Serial Monitor at `115200` baud.

If uploading does not start, hold the reset button for about two seconds until the internal green LED lights, release it, and retry. This places the board in download mode.

## 3. Confirm the board and find its IP address

After boot, the serial output should look similar to this:

```text
=== AtomS3R-M12 IMU streamer ===
[board] detected
[memory] flash=8 MB psram=8 MB
[imu] type=6 accel=ok gyro=ok mag=ok

[wifi] connecting to eduroam (WPA2-Enterprise/PEAP) as your_UGA_ID@uga.edu ...
........
[wifi] status   : connected to eduroam
[wifi] local IP : 10.x.x.x
[wifi] RSSI     : -58 dBm
[wifi] stream   : udp://10.x.x.x:5555  (backend sends hello here)
[status] wifi=up ip=10.x.x.x rssi=-58 peer=none sent=0 failed=0 | a=... g=... m=...
```

Record the address on the `[wifi] local IP` line. A PSRAM warning usually means the wrong board was selected or the M12 memory configuration was not applied. `[board] unknown` means M5Unified did not recognize the device; update M5Unified and confirm that you selected AtomS3R rather than AtomS3.

## 4. Start the Python bridge

From the demo root, pass the board address to the backend:

```bash
python backend/server.py 10.x.x.x
```

The script sends a small `hello` datagram to the board's UDP port `5555` once per second, receives reply samples on the same socket, serves `web/`, and opens <http://localhost:8000>.

Useful options are:

```bash
python backend/server.py 10.x.x.x --no-browser
python backend/server.py 10.x.x.x --port 8080
python backend/server.py 10.x.x.x --host 0.0.0.0
```

Use `--host 0.0.0.0` only on a trusted network; it lets other devices on the LAN open the dashboard. The computer must be able to reach the board. If a campus network isolates clients, test both devices on an approved 2.4 GHz hotspot.

## 5. Use the dashboard

Open <http://localhost:8000>.

- **3D view:** the board model follows the fused orientation. The arrows represent acceleration, magnetic field, and angular velocity.
- **Compass and Euler angles:** heading, roll, pitch, and yaw update live. Without magnetometer calibration, heading is gyro-only and will drift.
- **Telemetry cards:** live accelerometer, gyroscope, and magnetometer values include five-second sparklines.
- **Calibrate mag:** click once, slowly rotate the board through every orientation for about 15 seconds, and click again. Calibration is stored in the browser's `localStorage`.
- **Zero heading:** makes the current direction read 0 degrees.
- **Reset:** reinitializes the orientation filter.

## How it works

The firmware uses M5Unified to initialize the BMI270 and the BMM150 connected through its auxiliary sensor interface. It samples at 50 Hz and sends one JSON object per UDP datagram:

```json
{"n":123,"t":4567.890,"a":[0.01,-0.02,1.00],"g":[0.1,0.2,-0.3],"m":[12.3,4.5,-38.2],"r":-58}
```

The Python bridge relays each valid sample to every open browser tab using Server-Sent Events. The frontend runs a Madgwick filter and magnetometer calibration in the browser, then drives the 3D model and telemetry views.

UDP is intentional: a weak Wi-Fi link may lose an individual 20 ms sample, but it does not hold newer measurements behind TCP retransmissions. The header reports receive rate, RSSI, estimated loss, and the longest arrival gap so link quality is visible during the demo.

## Troubleshooting

| Symptom | Likely cause | What to try |
| --- | --- | --- |
| Upload cannot start | Board is not in download mode or the wrong port is selected | Hold reset for about two seconds, release it when the green LED lights, and select the new port. |
| `[memory] ... psram=0 MB` | Ordinary AtomS3 configuration is active | PlatformIO: use the supplied project unchanged. Arduino IDE: select `M5AtomS3R`. |
| `[imu] ... MISSING` or `[board] unknown` | Wrong board or old M5Unified version | Select AtomS3R and update M5Unified. |
| Wi-Fi times out | Credentials, network type, or signal is wrong | Check `secrets.h`; confirm PEAP details or test a 2.4 GHz WPA2-PSK hotspot. |
| Backend stays at `connecting` | Wrong board IP, firewall, or client isolation | Recheck the serial IP, allow Python on the private network, and test a hotspot. |
| Dashboard opens but does not move | Firmware is not receiving backend hello packets | Check serial `[udp] streaming to ...` output and keep the board and computer on a mutually reachable network. |
| Heading is wrong or drifts | Magnetometer is uncalibrated or near a magnet | Move away from magnetic objects and run **Calibrate mag**. |

## References

- This course demo is adapted from [`Pervasive-Intelligence-Lab/Demo-AtomS3R-CAM`](https://github.com/Pervasive-Intelligence-Lab/Demo-AtomS3R-CAM). The migrated firmware preserves the source working tree's USB CDC transmit-timeout fix.
- [M5Stack AtomS3R-M12 hardware documentation](https://docs.m5stack.com/en/core/AtomS3R-M12)
- [M5Stack AtomS3R-M12 Arduino quick start](https://docs.m5stack.com/en/arduino/m5atoms3r-m12/program)
- [M5Unified](https://github.com/m5stack/M5Unified)
- [PlatformIO Espressif 32 platform](https://docs.platformio.org/en/latest/platforms/espressif32.html)

The vendored three.js files in `web/vendor/` retain their MIT license in [`LICENSE.three.txt`](web/vendor/LICENSE.three.txt).
