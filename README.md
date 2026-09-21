# CSCI 4900/6900 Course Projects

This repository contains the hands-on labs and supporting demos for **CSCI 4900/6900**.

Throughout the course, we will use the **M5Stack AtomS3R-M12** as our primary AIoT development platform. The labs will introduce sensing, embedded programming, wireless networking, computer vision, edge AI, and intelligent agents.

## Hardware Platform

Each student will use an **M5Stack AtomS3R-M12**.

The board integrates an ESP32-S3 processor with several sensing and communication capabilities, including:

* ESP32-S3 processor
* 8 MB Flash and 8 MB PSRAM
* 3 MP camera
* Built-in IMU
* 2.4 GHz Wi-Fi
* USB-C programming and communication
* GPIO and expansion interfaces

This allows us to build systems that connect the physical world with networking and AI:

```text
Physical World
      ↓
 Sensors / Camera
      ↓
  AtomS3R-M12
      ↓
 Wi-Fi / USB
      ↓
Computer / Edge / Cloud
      ↓
      AI
```

## Getting Started

Before starting the labs, please configure your **AtomS3R-M12** using the official M5Stack documentation.

### 1. Read the AtomS3R-M12 Documentation

Start with the official hardware documentation:

https://docs.m5stack.com/en/core/AtomS3R-M12

Familiarize yourself with:

* board interfaces
* camera
* IMU
* USB connection
* GPIO
* development environment

### 2. Configure Arduino IDE

Follow the official **AtomS3R-M12 Arduino tutorial**:

https://docs.m5stack.com/en/arduino/m5atoms3r-m12/program

The tutorial explains how to:

1. Install **Arduino IDE**
2. Install the **M5Stack Board Manager**
3. Select the **M5AtomS3R** board
4. Install the required M5Stack libraries
5. Connect the AtomS3R-M12 through USB
6. Enter download mode
7. Compile and upload an example program

Please complete this setup before starting the labs.

### 3. Verify Your Board

Before moving on, make sure that:

* Arduino IDE recognizes the board
* `M5AtomS3R` is selected as the development board
* The correct USB serial port appears
* You can successfully compile and upload a program
* Serial Monitor can receive output from the board

If the board cannot be detected, first check that you are using a **USB-C data cable**, not a charging-only cable.

If an Apple Silicon Mac reports `ctags: bad CPU type in executable` during compilation, follow the [command-line ctags repair guide](Lab-3-CSI-serial/README.md#5-fix-arduino-ctags-on-apple-silicon-macos).

---

## Course Labs

### Lab 0 — IMU and Serial Communication

📁 [`Lab-0-IMU-serial`](Lab-0-IMU-serial)

In this lab, you will learn how to read physical sensor measurements from the AtomS3R-M12 and send them to a computer.

You will:

* Read accelerometer and gyroscope data
* Send measurements through USB serial
* Receive serial data using Python
* Visualize device motion in 2D and 3D

```text
Motion
  ↓
 IMU
  ↓
AtomS3R-M12
  ↓
USB Serial
  ↓
Python
  ↓
Visualization
```

---

### Lab 1 — Camera Streaming over Wi-Fi

📁 [`Lab-1-camera-streaming`](Lab-1-camera-streaming)

In this lab, you will turn the AtomS3R-M12 into a network-connected camera.

You will:

* Initialize the onboard camera
* Connect the board to Wi-Fi
* Capture camera frames
* Run a lightweight web server
* Stream camera images over the network
* View the live stream from a browser

```text
Camera
   ↓
AtomS3R-M12
   ↓
 Wi-Fi
   ↓
Network
   ↓
Browser / Computer
```

---

### Lab 2 — ESP-Claw Edge AI Agent

📁 [`Lab-2-ESP-Claw`](Lab-2-ESP-Claw)

In this lab, you will explore how an embedded device can become an **AI agent**.

You will:

* Configure an ESP-based AI agent
* Connect the device to Wi-Fi
* Configure an LLM provider
* Interact with the agent
* Explore how AI reasoning can connect with physical devices

The goal is to move beyond traditional IoT systems:

```text
sense → transmit → process
```

toward intelligent AIoT systems:

```text
sense → understand → reason → communicate → act
```

---

## Course Demos

### AtomS3R-M12 Wi-Fi CSI over USB Serial

📁 [`Lab-3-CSI-serial`](Lab-3-CSI-serial)

An Arduino `.ino` demo that joins a 2.4 GHz access point in STA mode, pings the gateway to generate receive traffic, captures Wi-Fi CSI on the ESP32-S3, and streams raw CSI with packet metadata over USB serial as CSV. Includes a Python serial plotter for all CSI pairs (amplitude, phase, I/Q, history heatmaps, and RSSI), optional CSV recording, setup instructions, and troubleshooting.

### AtomS3R-M12 Realtime IMU Dashboard

📁 [`Demo-AtomS3R-M12`](Demo-AtomS3R-M12)

This demo streams the board's accelerometer, gyroscope, and magnetometer data over Wi-Fi to an interactive 3D browser dashboard. Its firmware is a single Arduino `.ino` sketch for the `M5AtomS3R` board definition, with 8 MB Flash and 8 MB OPI PSRAM. Build and upload it with Arduino IDE; the demo README walks through Wi-Fi credentials, flashing, and opening the dashboard.

The Python bridge uses only the standard library, and the browser assets are included in the repository.

---

## Repository Structure

```text
course-AIoT-Lab/
│
├── README.md
│
├── Demo-AtomS3R-M12/
│   ├── README.md
│   ├── ino/
│   ├── backend/
│   └── web/
│
├── Lab-0-IMU-serial/
│   ├── README.md
│   ├── imu-serial/
│   ├── imu-compass.py
│   └── imu-compass-3d.py
│
├── Lab-2-camera-streaming/
│   ├── README.md
│   ├── Camera/
│   ├── camera_edurom_relay/
│   └── camera_edurom_relay_m12/
│
└── Lab-2-ESP-Claw/
    └── README.md
```

Each lab and demo directory contains its own `README.md` with detailed instructions.

Please read the corresponding README before starting a lab or demo.

---

## Clone the Repository

```bash
git clone https://github.com/Pervasive-Intelligence-Lab/course-AIoT-Lab.git
cd course-AIoT-Lab
```

To start a lab, enter its directory. For example:

```bash
cd Lab-0-IMU-serial
```

Then follow the instructions in that directory's `README.md`.

---

## Important Notes

### Do Not Commit Credentials

Some labs may require Wi-Fi credentials or API keys.

Never commit passwords, API keys, or other private credentials to GitHub.

Use placeholders such as:

```cpp
const char* username = "your_username";
const char* password = "your_password";
```

### Use a USB Data Cable

A charging-only USB-C cable can power the AtomS3R-M12 but cannot upload programs or communicate over serial.

### Check the Serial Port

The serial port may change after reconnecting the board or entering download mode. Always verify the selected port before uploading.

### Read the Lab Instructions

Each lab may require different libraries, configuration steps, and software packages.

Always follow the README inside the corresponding lab directory.

---

## References

* [M5Stack AtomS3R-M12 Documentation](https://docs.m5stack.com/en/core/AtomS3R-M12)
* [AtomS3R-M12 Arduino Tutorial](https://docs.m5stack.com/en/arduino/m5atoms3r-m12/program)
* [Arduino IDE](https://www.arduino.cc/en/software)
* [M5Stack Documentation](https://docs.m5stack.com/)
