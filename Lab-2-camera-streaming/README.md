# AIoT Lab

This repository contains the labs for the AIoT course. Start with [Lab 1: Camera Streaming](#lab-1-camera-streaming).

## Lab 1: Camera Streaming

### Goal

In this lab you will program an M5Stack AtomS3R-M12 to:

1. Initialize its camera.
2. Connect to Wi-Fi using WPA2-Enterprise (`eduroam`).
3. Upload camera frames to the instructor's Flask relay server and view the stream in a web browser.

The lab uses the [M5Stack AtomS3R-M12 Arduino guide](https://docs.m5stack.com/en/arduino/m5atoms3r-m12/program) as its hardware and software reference.

### Why we use a relay on eduroam

For this lab's eduroam setup, we use an instructor-hosted relay instead of relying on direct connections from students' browsers to the cameras. Campus Wi-Fi restrictions can prevent one client device from reaching another, even when both are connected to eduroam.

The M12 sends JPEG frames over eduroam to the instructor's server. The Flask application [`server-camera.py`](server-camera.py) receives the frames at `/upload`, keeps the latest frame for each `deviceId`, and serves a dashboard and live MJPEG streams to web browsers. The instructor runs Flask on the server; students configure their M12 and open the server dashboard.

```text
M12 camera -- eduroam / JPEG upload --> Instructor's Flask server --> Student's browser
```

**The server IP address will be provided in class. It is intentionally omitted from the repository.** Enter it only in your local, Git-ignored `secrets.h`. In the examples below, `<SERVER_IP_FROM_CLASS>` is a placeholder and must be replaced before use.

### What you need

- M5Stack AtomS3R-M12
- USB-C data cable
- Computer with administrator rights to install Arduino software
- Wi-Fi access. The provided sketch is configured for `eduroam`.
- Your institutional Wi-Fi identity, username, and password

No external wiring is required: the camera is built into the device.

### 1. Install Arduino IDE

1. Install the current [Arduino IDE](https://www.arduino.cc/en/software) for your operating system.
2. Open Arduino IDE once the installation is complete.

### 2. Install the M5Stack board package

1. Open **Arduino IDE > Settings** on macOS, or **File > Preferences** on Windows/Linux.
2. In **Additional boards manager URLs**, add the M5Stack board-manager URL from the [M5Stack board setup guide](https://docs.m5stack.com/en/arduino/arduino_board).
3. Open **Tools > Board > Boards Manager**.
4. Search for `M5Stack` and install the M5Stack board package.
5. Select **Tools > Board > M5Stack > M5AtomS3R**.

The board selection is important. The AtomS3R-M12 uses the `M5AtomS3R` board definition.

### 3. Install the required library

1. Open **Tools > Manage Libraries**.
2. Search for `M5AtomS3`.
3. Install the **M5AtomS3** library.
4. Accept the installation of any required dependencies.

The camera example uses the ESP32 camera support included by the M5Stack/ESP32 board package. Do not install a different camera library unless your instructor asks you to.

### 4. Open the lab sketch

For the classroom relay experiment, open this file in Arduino IDE:

[`camera_edurom_relay_m12/camera_edurom_relay_m12.ino`](camera_edurom_relay_m12/camera_edurom_relay_m12.ino)

Choose the sketch for your hardware and streaming method:

| Sketch | Default hardware | Where to view the video |
| --- | --- | --- |
| [`camera_edurom`](camera_edurom/camera_edurom.ino) | AtomS3R-M12 | The camera's IP address in a browser |
| [`camera_edurom_relay`](camera_edurom_relay/camera_edurom_relay.ino) | AtomS3R-CAM (instructor's device) | The relay server's dashboard |
| [`camera_edurom_relay_m12`](camera_edurom_relay_m12/camera_edurom_relay_m12.ino) | AtomS3R-M12 (student devices) | The relay server's dashboard |

Each sketch folder has its own local `secrets.h`; follow step 5 for whichever sketch you use. For the student relay version, also follow [Relay streaming with the student M12](#relay-streaming-with-the-student-m12) below. Keep the sketches in separate folders so Arduino IDE builds one version at a time.

For the student `camera_edurom_relay_m12` sketch, keep these camera settings unchanged:

```cpp
// #define USE_ATOMS3R_CAM
#define USE_ATOMS3R_M12
```

They select the AtomS3R-M12's OV3660 camera. The `CAM` option targets the older GC0308-based AtomS3R-CAM and should remain commented out for the course hardware. Enable exactly one camera model.

### 5. Configure your local Wi-Fi credentials

1. In the sketch folder you selected, copy `secrets.example.h` to `secrets.h`.
2. Open `secrets.h` and replace the placeholder identity, username, and password with your own credentials.
3. Keep `secrets.h` beside the `.ino` file. If you use more than one sketch, configure a local `secrets.h` in each folder.

```cpp
#define WIFI_SSID      "eduroam"
#define EAP_IDENTITY   "your_UGA_ID@uga.edu"
#define EAP_USERNAME   "your_UGA_ID@uga.edu"
#define EAP_PASSWORD   "your_UGA_password"
```

- `WIFI_SSID`: leave this as `eduroam` unless your instructor gives you another network.
- `EAP_IDENTITY`: your institutional identity used for eduroam authentication.
- `EAP_USERNAME`: your institutional username used for eduroam authentication.
- `EAP_PASSWORD`: your Wi-Fi password.

For either relay sketch, also set this entry in your local `secrets.h` using the server IP address provided in class:

```cpp
#define RELAY_UPLOAD_URL "http://<SERVER_IP_FROM_CLASS>:8001/upload"
```

If you already have a `secrets.h` from the earlier experiment, add `RELAY_UPLOAD_URL` to it. The `.ino` file reads this value automatically; keep the example header's placeholder unchanged. Give each camera a unique `deviceId` in the relay sketch so the dashboard can distinguish students' streams.

All sketches include their local `secrets.h`. The lab's `.gitignore` excludes these files; only the placeholder `secrets.example.h` files should be committed. Do not add real credentials to the `.ino` files or the example headers, and do not share `secrets.h` with your submission.

### 6. Put the board into download mode

1. Connect the AtomS3R-M12 to your computer with the USB-C data cable.
2. Press and hold the device reset button for about two seconds.
3. Release it when the internal green LED turns on.

The green LED turns off after release. This indicates that the board is waiting for a program upload.

### 7. Select the USB port

In Arduino IDE, open **Tools > Port** and select the port belonging to the AtomS3R-M12. On macOS it commonly appears as a device beginning with `/dev/cu.`.

If no port appears, try the following:

- Use a USB-C cable that supports data, not charging only.
- Disconnect and reconnect the board.
- Put the board into download mode again.
- Close other programs that may be using the serial port.

### 8. Compile and upload

1. Click **Verify** to compile the sketch.
2. Fix any missing-board or missing-library errors before continuing.
3. Click **Upload**.
4. If Arduino IDE cannot connect, put the board into download mode again while the upload is starting.

The upload is complete when Arduino IDE reports that the device has been reset or that the upload finished successfully.

### 9. View the relay stream

1. The instructor starts [`server-camera.py`](server-camera.py) on the class server and provides its IP address in class.
2. Open **Tools > Serial Monitor** and set the baud rate to **115200**.
3. Briefly press reset if the sketch does not start running.
4. Wait for `Camera Init Success`, `eduroam connected!`, and successful `HTTP response: 200` messages. These show that the camera connected and the server accepted an uploaded frame.
5. Open the server dashboard in your browser, replacing the placeholder with the address provided in class:

```text
http://<SERVER_IP_FROM_CLASS>:8001/
```

6. Find the camera with your `deviceId` on the dashboard and confirm that its image updates.

The camera sends frames to `http://<SERVER_IP_FROM_CLASS>:8001/upload`; the browser uses the dashboard URL above. Use the server's IP address for viewing the relay stream. The camera's IP printed in Serial Monitor is useful for troubleshooting but is not the dashboard address.

### Expected result

You have completed Lab 1 when all of the following are true:

- The Serial Monitor reports `Camera Init Success`.
- The board reports `eduroam connected!`, an IP address, and successful `HTTP response: 200` uploads.
- The Flask dashboard displays a continuously updating image under your camera's `deviceId`.

### Troubleshooting

| Symptom | Likely cause | What to try |
| --- | --- | --- |
| `Camera Init Fail` | Wrong board or camera option selected | Select `M5AtomS3R` and keep `USE_ATOMS3R_M12` enabled. |
| No serial port | Cable, connection, or download-mode issue | Use a data cable and hold reset for about two seconds. |
| Wi-Fi timeout | Incorrect credentials or unavailable eduroam | Recheck `EAP_IDENTITY`, `EAP_USERNAME`, and `EAP_PASSWORD` in `secrets.h`; confirm that your computer can join eduroam. |
| IP address does not appear | The board did not finish connecting | Briefly press reset and watch the Serial Monitor at 115200 baud. |
| Browser cannot open the dashboard | Wrong server address or the Flask server is unavailable | Use the server IP provided in class with port 8001 and check with the instructor that Flask is running. |
| Camera connects to eduroam but uploads fail | Incorrect relay endpoint or the server cannot be reached | Check `RELAY_UPLOAD_URL` in local `secrets.h`, including `:8001/upload`, and confirm the server is running. |
| Dashboard shows another camera's frames under your ID | Two boards share a `deviceId` | Give your camera a unique `deviceId` and upload the sketch again. |
| Stream stops or is blank | Connection was interrupted | Refresh the browser and reset the board. |

### Submission checklist

Submit the items required by your instructor. Unless told otherwise, keep these ready:

- A screenshot of the Serial Monitor showing successful camera initialization and the assigned IP address.
- A screenshot of the browser showing the live stream.
- Your completed sketch and `secrets.example.h`; exclude your local `secrets.h` when sharing.

### Relay streaming with the student M12

1. Open [`camera_edurom_relay_m12/camera_edurom_relay_m12.ino`](camera_edurom_relay_m12/camera_edurom_relay_m12.ino) in Arduino IDE. Select the `M5AtomS3R` board as in step 2.
2. Copy the folder's `secrets.example.h` to `secrets.h` and fill in your eduroam credentials. The existing `.gitignore` also excludes this new folder's `secrets.h`.
3. Set `RELAY_UPLOAD_URL` in your local `secrets.h` to `http://<SERVER_IP_FROM_CLASS>:8001/upload`, replacing the placeholder with the server IP provided in class. The sketch loads `uploadUrl` from this setting; the repository does not publish the server IP.
4. Give `deviceId` a unique value, such as `atoms3r-m12-01` or `atoms3r-m12-02`. Cameras using the same ID overwrite each other's latest frame on the server.
5. Keep `USE_ATOMS3R_M12` enabled and `USE_ATOMS3R_CAM` commented out. The camera configuration matches the first `camera_edurom` experiment: JPEG output, UXGA resolution, JPEG quality 12, and two frame buffers in PSRAM. The M12 uploads the camera's JPEG buffer directly and returns it to the camera driver after the upload.
6. Compile and upload, then open Serial Monitor at **115200 baud**. Look for `Camera Init Success`, `eduroam connected!`, and successful `HTTP response: 200` messages.
7. The instructor runs [`server-camera.py`](server-camera.py) on the server. Open `http://<SERVER_IP_FROM_CLASS>:8001/`, using the address provided in class, and find your camera's `deviceId` on the dashboard. The camera must be able to reach the server on port 8001.

This is a separate M12 preset; the original `camera_edurom_relay` remains configured for the instructor's AtomS3R-CAM. The M12 selection follows the [M5Stack M12 programming guide](https://docs.m5stack.com/en/arduino/m5atoms3r-m12/program). Hardware capture and eduroam-to-server streaming still need to be checked on a physical M12.

### Direct streaming reference

The original [`camera_edurom`](camera_edurom/camera_edurom.ino) sketch hosts a stream on the camera itself. On a network that permits direct connections between client devices, open `http://<CAMERA_IP>/` using the camera's IP from Serial Monitor. Keep `STA_MODE` enabled for that sketch. For the classroom eduroam experiment, use the M12 relay workflow above.

### References

- [M5Stack AtomS3R-M12 Arduino compilation and upload guide](https://docs.m5stack.com/en/arduino/m5atoms3r-m12/program)
- [Arduino IDE installation](https://docs.m5stack.com/en/arduino/arduino_ide)
- [M5Stack board-manager setup](https://docs.m5stack.com/en/arduino/arduino_board)
- [M5Stack library installation](https://docs.m5stack.com/en/arduino/arduino_library)
