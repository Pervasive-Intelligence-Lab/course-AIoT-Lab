# AIoT Lab

This repository contains the labs for the AIoT course. Start with [Lab 1: Camera Streaming](#lab-1-camera-streaming).

## Lab 1: Camera Streaming

### Goal

In this lab you will program an M5Stack AtomS3R-M12 to:

1. Initialize its camera.
2. Connect to Wi-Fi using WPA2-Enterprise (`eduroam`).
3. Host a live camera stream that you can open in a web browser.

The lab uses the [M5Stack AtomS3R-M12 Arduino guide](https://docs.m5stack.com/en/arduino/m5atoms3r-m12/program) as its hardware and software reference.

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

Open this file in Arduino IDE:

[`camera_edurom/camera_edurom.ino`](camera_edurom/camera_edurom.ino)

Keep these settings unchanged:

```cpp
// #define USE_ATOMS3R_CAM
#define USE_ATOMS3R_M12

#define STA_MODE
// #define AP_MODE
```

They select the AtomS3R-M12's OV3660 camera and Wi-Fi station mode. The `CAM` option targets the older GC0308-based AtomS3R-CAM and should remain commented out for the course hardware. Enable exactly one camera model.

### 5. Enter your Wi-Fi credentials

Find this section near the top of the sketch:

```cpp
const char* ssid     = "eduroam";
const char* identity = "your_UGA_ID";
const char* username = "your_UGA_ID";
const char* password = "your_UGA_password";
```

Replace only the placeholder values:

- `ssid`: leave this as `eduroam` unless your instructor gives you another network.
- `identity`: your institutional identity used for eduroam authentication.
- `username`: your institutional username used for eduroam authentication.
- `password`: your Wi-Fi password.

Do not commit or share a sketch containing your real password. The repository should contain placeholders, not personal credentials.

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

### 9. Start the camera stream

1. Open **Tools > Serial Monitor**.
2. Set the baud rate to **115200**.
3. Press the reset button briefly if the sketch does not start running.
4. Wait for messages similar to:

```text
Camera Init Success
Connecting to eduroam
Connected to eduroam
IP address: 10.x.x.x
```

5. Copy the complete IP address shown after `IP address:`.
6. On a computer connected to the same network, open `http://<IP-address>/` in a browser. For example:

```text
http://10.24.18.57/
```

You should see a live stream from the camera. Keep the Serial Monitor open while troubleshooting; it reports camera initialization, Wi-Fi connection, and frame activity.

### Expected result

You have completed Lab 1 when all of the following are true:

- The Serial Monitor reports `Camera Init Success`.
- The board reports `Connected to eduroam` and an IP address.
- The browser displays a continuously updating camera image.

### Troubleshooting

| Symptom | Likely cause | What to try |
| --- | --- | --- |
| `Camera Init Fail` | Wrong board or camera option selected | Select `M5AtomS3R` and keep `USE_ATOMS3R_M12` enabled. |
| No serial port | Cable, connection, or download-mode issue | Use a data cable and hold reset for about two seconds. |
| Wi-Fi timeout | Incorrect credentials or unavailable eduroam | Recheck `identity`, `username`, and `password`; confirm that your computer can join eduroam. |
| IP address does not appear | The board did not finish connecting | Briefly press reset and watch the Serial Monitor at 115200 baud. |
| Browser cannot connect | Computer and board are on different networks | Connect both to the same Wi-Fi network and use the exact IP address. |
| Stream stops or is blank | Connection was interrupted | Refresh the browser and reset the board. |

### Submission checklist

Submit the items required by your instructor. Unless told otherwise, keep these ready:

- A screenshot of the Serial Monitor showing successful camera initialization and the assigned IP address.
- A screenshot of the browser showing the live stream.
- Your completed sketch, with passwords removed before sharing.

### References

- [M5Stack AtomS3R-M12 Arduino compilation and upload guide](https://docs.m5stack.com/en/arduino/m5atoms3r-m12/program)
- [Arduino IDE installation](https://docs.m5stack.com/en/arduino/arduino_ide)
- [M5Stack board-manager setup](https://docs.m5stack.com/en/arduino/arduino_board)
- [M5Stack library installation](https://docs.m5stack.com/en/arduino/arduino_library)
