# Lab 0: Reading and Visualizing IMU Data over Serial

In this lab, you will use the built-in IMU on an M5AtomS3 to measure acceleration and angular velocity, then send the measurements to a computer over USB serial. After testing the serial output, you will run Python programs that display rotation around the Z axis as a 2D pointer and visualize three-axis rotation in 3D.

## Goals

By the end of this lab, you will be able to:

1. Initialize the M5AtomS3 and its built-in IMU in an Arduino sketch.
2. Send three-axis acceleration and angular velocity data over serial.
3. Read and parse serial data in Python using `pyserial`.
4. Visualize device rotation in real time using Pygame and OpenGL.

## What you need

- M5AtomS3 with a built-in IMU
- USB-C data cable
- Arduino IDE
- Python 3.9 or later
- Arduino library: `M5Unified`
- Python packages: `pyserial`, `pygame`, and `PyOpenGL`

No external wiring is required.

## Project files

```text
Lab-0-IMU-serial/
├── README.md
├── imu-serial/
│   └── imu-serial.ino       # Reads the IMU and sends data over serial
├── imu-compass.py           # 2D Z-axis rotation visualization
└── imu-compass-3d.py        # 3D orientation visualization
```

## 1. Configure Arduino IDE

1. Install and open [Arduino IDE](https://www.arduino.cc/en/software).
2. Follow the [M5Stack board-management guide](https://docs.m5stack.com/en/arduino/arduino_board) to install the M5Stack board package.
3. Open **Tools > Board** and select the board definition that matches your M5AtomS3.
4. Open **Tools > Manage Libraries**, search for `M5Unified`, and install it.

## 2. Upload the Arduino sketch

Open the following file in Arduino IDE:

[`imu-serial/imu-serial.ino`](imu-serial/imu-serial.ino)

Connect the board and select its serial port under **Tools > Port**, then click **Upload**. If Arduino IDE cannot connect to the board, confirm that the USB-C cable supports data transfer and try reconnecting the board or placing it in download mode.

The program sends two types of data at `115200` baud:

```text
ax:0.012345 ay:-0.023456 az:1.001234
gx:0.123456 gy:-0.234567 gz:1.345678
```

The values have the following meanings:

- `ax`, `ay`, and `az` are acceleration along the X, Y, and Z axes.
- `gx`, `gy`, and `gz` are angular velocity around the X, Y, and Z axes, measured in degrees per second.

Open **Tools > Serial Monitor**, set the baud rate to `115200`, and confirm that the values update continuously. Close Serial Monitor before running a Python program because the same serial port usually cannot be opened by two applications at once.

## 3. Install the Python dependencies

Open a terminal and enter the lab directory:

```bash
cd Lab-0-IMU-serial
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install pyserial pygame PyOpenGL
```

On Windows PowerShell, activate the virtual environment with:

```powershell
.venv\Scripts\Activate.ps1
```

Then install the packages with:

```powershell
python -m pip install pyserial pygame PyOpenGL
```

## 4. Find and configure the serial port

Both Python programs contain the following configuration near the top of the file:

```python
SERIAL_PORT = "/dev/cu.usbmodem31101"
BAUD_RATE = 115200
```

Change `SERIAL_PORT` to the port used by your board. Keep `BAUD_RATE` set to `115200`.

On macOS, list available serial ports with:

```bash
ls /dev/cu.*
```

On Windows, find the port under **Tools > Port** in Arduino IDE or in Device Manager. The name usually looks like this:

```python
SERIAL_PORT = "COM5"
```

On Linux, the port usually looks like `/dev/ttyACM0` or `/dev/ttyUSB0`. If you receive a permission error, add your user to the group that can access serial devices, then log out and back in:

```bash
sudo usermod -aG dialout $USER
```

## 5. Run the 2D rotation visualization

```bash
python3 imu-compass.py
```

Keep the device approximately level and rotate it around its Z axis. The red pointer will rotate based on the `gz` angular velocity. Press `R` to reset the displayed angle to 0°.

## 6. Run the 3D orientation visualization

```bash
python3 imu-compass-3d.py
```

Rotate the device. The sphere and pointer will move based on the angular velocity around the X, Y, and Z axes. Press `R` to reset all three displayed angles.

The two Python programs cannot use the same serial port at the same time. Close the current visualization window before starting the other program.

## How it works

The Arduino sketch reads the IMU approximately every 100 ms and sends the measurements as formatted text over serial. Each Python program uses a regular expression to extract `gx`, `gy`, and `gz`, then integrates angular velocity using the elapsed time `dt` between samples:

```text
change in angle = angular velocity × dt
```

The programs ignore angular velocities smaller than `0.5°/s` to reduce small movements caused by sensor noise while the device is stationary.

This lab uses simple gyroscope integration without combining accelerometer or magnetometer measurements. Small sensor errors therefore accumulate over time, and the displayed orientation may slowly drift even when the device is stationary. This behavior is expected; press `R` to reset the orientation when necessary.

## Expected result

You have completed the lab when all of the following are true:

- Serial Monitor continuously displays acceleration and angular velocity at `115200` baud.
- `imu-compass.py` displays a 2D pointer that follows rotation around the device's Z axis.
- `imu-compass-3d.py` displays a 3D sphere and pointer that respond to device rotation.
- Pressing `R` resets the displayed orientation.

## Troubleshooting

| Symptom | Likely cause | What to try |
| --- | --- | --- |
| No serial port appears in Arduino IDE | The cable only supports charging, or the board is not connected correctly | Use a USB-C data cable, reconnect the board, and check its download mode. |
| Python reports `No module named ...` | The Python packages are not installed in the active environment | Activate the virtual environment and run `python3 -m pip install pyserial pygame PyOpenGL`. |
| Python reports that the port does not exist | `SERIAL_PORT` does not match the board's actual port | Find the port again and update the configuration near the top of the script. |
| Python reports `Permission denied` | The user cannot access the serial port | Close programs that may be using the port; on Linux, also check membership in the `dialout` group. |
| Python reports that the port is busy | Serial Monitor or another script is using the port | Close Serial Monitor and any other serial applications, then try again. |
| The window opens but the visualization does not move | The Arduino sketch is not running, the baud rates differ, or the wrong port was selected | Check the output in Serial Monitor, close it, and run the Python program again. |
| The angle slowly changes while the device is stationary | Gyroscope bias accumulates during integration | Keep the device still at startup and press `R` to reset the orientation when needed. |

## Submission checklist

Unless your instructor specifies otherwise, prepare the following:

- A screenshot of Serial Monitor showing all six IMU measurements.
- A screenshot of the 2D rotation visualization.
- A screenshot of the 3D orientation visualization.
- A brief explanation of why gyroscope integration causes orientation drift.

## References

- [M5Unified library](https://github.com/m5stack/M5Unified)
- [M5Stack Arduino IDE setup](https://docs.m5stack.com/en/arduino/arduino_ide)
- [PySerial documentation](https://pyserial.readthedocs.io/)
- [Pygame documentation](https://www.pygame.org/docs/)
- [PyOpenGL documentation](https://pyopengl.sourceforge.net/documentation/)
