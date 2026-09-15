# AtomS3R-M12 Wi-Fi CSI → USB Serial

Use the **M5Stack AtomS3R-M12 / ESP32-S3** to read channel state information (CSI) from its built-in Wi-Fi radio with an Arduino `.ino` sketch.

```text
AtomS3R STA --ping--> 2.4 GHz router/AP
AtomS3R STA <--reply-- router/AP
       ↓ Wi-Fi driver extracts CSI
       ↓ FreeRTOS queue
       ↓ USB-C Serial (CSV)
     Computer
```

CSI flows from the **board to the computer** over serial. Uploading the Arduino firmware transfers the program in the opposite direction. The demo requires one ESP32 and a standard access point. The board uses `WIFI_STA` and pings the gateway to generate incoming packets. This approach follows the [Espressif router CSI example](https://github.com/espressif/esp-csi/tree/master/examples/get-started/csi_recv_router). SoftAP and promiscuous mode are disabled. The station still transmits ping requests and normal Wi-Fi protocol traffic.

## 1. Configure and Upload

1. Install Arduino IDE and the M5Stack board package using the [official M5Stack tutorial](https://docs.m5stack.com/en/arduino/m5atoms3r-m12/program).
2. Open [`ino/AtomS3R_CSI_STA/AtomS3R_CSI_STA.ino`](ino/AtomS3R_CSI_STA/AtomS3R_CSI_STA.ino).
3. Copy `secrets.example.h` to `secrets.h` in the same directory, then enter your SSID and password. This demo's `.gitignore` excludes `secrets.h`.
4. Use a **2.4 GHz personal Wi-Fi network with an SSID and password**. The sketch does not implement eduroam / WPA2-Enterprise authentication. A lab router or a phone hotspot configured for 2.4 GHz is suitable.
5. Select these Arduino IDE settings:

   | Option | Setting |
   | --- | --- |
   | Board | M5AtomS3R |
   | Flash Size | 8 MB |
   | PSRAM | OPI PSRAM |
   | USB CDC On Boot | Enabled |
   | USB Mode | Hardware CDC and JTAG |
   | Core Debug Level | None |

6. Connect a USB-C data cable, select the serial port, and upload. If necessary, follow the board's official instructions to enter download mode.
7. Open Serial Monitor at **921600 baud** and press Reset. Expect a `# connected ...` message followed by continuous `CSI_DATA,...` lines.

The sketch uses Arduino WiFi, the ESP-IDF Wi-Fi / ping APIs, and FreeRTOS included in the board package. No additional M5Unified or CSI library is required. Native USB CDC is not limited by a physical UART baud rate, but use 921600 consistently in the serial tools.

## 2. Output Format

The sketch prints a CSV header at startup. Lines beginning with `#` contain status information. Each CSI packet occupies one line:

```text
type,seq,timestamp_us,mac,rssi,noise_floor,channel,sig_mode,mcs,cwb,len,first_word_invalid,data
```

| Field | Meaning |
| --- | --- |
| `seq` | Accepted CSI callback sequence number, starting at 0; queue overflow and other drops can leave gaps |
| `timestamp_us` | Driver receive timestamp in microseconds; this 32-bit counter wraps and is not the computer's clock |
| `mac` | Wireless transmitter MAC address; the demo accepts only the current AP's BSSID |
| `rssi`, `noise_floor` | Driver-reported signal strength and noise floor in dBm |
| `channel` | Current primary Wi-Fi channel, selected by the AP |
| `sig_mode`, `mcs`, `cwb` | Receive PHY metadata; `cwb=0` indicates 20 MHz; MCS does not describe the rate of non-HT packets |
| `len` | Number of bytes in `data`, not the number of usable subcarriers |
| `first_word_invalid` | When 1, the first four bytes must be excluded from analysis |
| `data` | Quoted integer array in the CSV record; each value is a signed int8 |

The default configuration uses **HT20 and LLTF only** to limit output volume. The sketch preserves the complete driver-reported length instead of assuming a fixed length. According to the [ESP32-S3 CSI documentation](https://docs.espressif.com/projects/esp-idf/en/v5.0.5/esp32s3/api-guides/wifi.html#wi-fi-channel-state-information), each byte pair is ordered **imaginary, real**:

```python
# row is one record parsed with csv.DictReader.
import json
import math

raw = json.loads(row["data"])
assert len(raw) == int(row["len"])
start = 4 if int(row["first_word_invalid"]) else 0
samples = []
for i in range(start, len(raw) - 1, 2):
    imag, real = raw[i], raw[i + 1]
    samples.append((i // 2, math.hypot(real, imag), math.atan2(imag, real)))
# Preserve original indices; use the official LLTF table to exclude DC/null bins.
```

CSI is generated when packets arrive. By default, the sketch requests a ping every 50 ms, targeting about 20 packets per second. AP rate limits, packet loss, and other incoming traffic affect the actual CSI rate. CSI records are not limited to ping replies. Automatic gain control, frequency offsets, and packet formats affect amplitude and phase. Raw amplitude has no calibrated physical unit, and raw phase cannot directly represent distance.

## 3. Python Live Plotting and Recording

[`plot_csi.py`](plot_csi.py) reads the firmware's output directly. Use **Python 3.10+** and close Arduino Serial Monitor / Serial Plotter before starting it.

### Install and Run

```bash
cd Lab-3-CSI-serial
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install -r requirements.txt
python3 plot_csi.py --list-ports
python3 plot_csi.py --port /dev/cu.usbmodemXXXX
```

Replace the port with the actual name shown by `--list-ports`. On Windows, activate the environment with `.venv\Scripts\Activate.ps1` and use a port such as `COM5`. Linux ports often look like `/dev/ttyACM0`. If exactly one USB serial device is available, run `python3 plot_csi.py` to select it automatically.

Defaults are **921600 baud**, a history of **200 packets**, and **10 plot refreshes per second**. A background thread reads serial continuously. Each refresh adds the packets received since the previous refresh to the history plots and displays the latest packet in the snapshot plots. Close the plot window or press `Ctrl+C` in the terminal to exit and release the serial port.

### The Six Plots

| Plot | Data |
| --- | --- |
| Latest packet amplitude | `sqrt(real² + imag²)` for every CSI complex pair |
| Latest packet phase | `atan2(imag, real)` for every CSI complex pair, from −π to π |
| Amplitude heatmap | The most recent N packets across every CSI complex pair |
| Phase heatmap | The most recent N packets across every CSI complex pair |
| Real / imaginary curves | All raw I/Q points in the latest packet |
| RSSI history | Received signal strength against computer receive time in seconds |

The **CSI pair index** is the complex-pair index in the driver's array, not a physical subcarrier number. The plots retain every index, including DC/null bins, without smoothing, normalization, or phase calibration. When `first_word_invalid=1`, the first two pairs appear as gaps. Phase is also left blank for zero-amplitude pairs. Variable packet lengths are supported, with missing positions shown in gray. Recordings preserve all original bytes.

The heatmap's vertical axis shows **packet offsets relative to the latest packet**, with the newest packet at the top. It does not assume evenly spaced Wi-Fi arrivals. At about 20 packets per second, the default 200-packet history covers roughly 10 seconds. Use the displayed metadata when interpreting history across changes in AP, channel, or packet format.

### Record Every Valid Received CSI Packet

```bash
mkdir -p captures
python3 plot_csi.py --port /dev/cu.usbmodemXXXX --history 400 --save-csv captures/session01.csv
```

The CSV file contains a fixed header and the original CSI records, including every data byte and metadata field. Recording is independent of the displayed history length. An existing output file causes an error to prevent overwriting; choose a new filename. Firmware status lines beginning with `#` appear in the terminal, and boot logs are excluded from the CSV.

The window reports `received` (successfully parsed packets), `malformed` (invalid records), and `plot queue drops` (packets discarded from an overloaded display queue). The display queue holds up to 1024 packets and removes the oldest packet when full. CSV records are written before display queuing, so display drops do not remove recorded packets. Data lost on the board or serial link cannot be recovered. If the serial connection fails, the window shows a stopped status. Check the terminal error, reconnect the device, and restart the script.

### Preview and Test Without a Board

```bash
python3 plot_csi.py --demo
python3 plot_csi.py --demo --snapshot /tmp/csi-preview.png
python3 -m unittest -v test_plot_csi.py
```

`--demo` uses clearly labeled synthetic data to check the plots and program behavior. It does not represent a hardware measurement. If the live window receives no data, inspect the firmware status in the terminal, check Wi-Fi, the serial port, and baud rate, and confirm that the board is running this demo's firmware.

Validation: six automated tests passed, covering real/imaginary ordering, invalid bytes, malformed records, fragmented serial reads, display queue overflow, and CSV recording. A roughly six-second capture from `/dev/cu.usbmodem3101` parsed 587 packets with zero format errors and 128 bytes per packet. The resulting plots were visually checked. Port names may change after reconnecting the board.

## 4. Configuration and Troubleshooting

- **Wi-Fi connection fails:** Check `secrets.h`, 2.4 GHz coverage, and the authentication type. The sketch retries every 15 seconds and updates the BSSID and gateway ping session after reconnection or AP roaming.
- **Connected but no CSI:** Inspect the cumulative `ping_ok` and `ping_timeout` counters printed every five seconds. If only timeouts increase, the router may block ping. Run `ping BOARD_IP` from a computer on the same LAN to generate incoming traffic, or try an AP that permits ping. Client isolation may also prevent the computer from reaching the board.
- **Ping works but no CSI:** Confirm the ESP32-S3 target, a CSI-enabled board package, and an AP configured for 802.11g/n OFDM traffic. Not every received Wi-Fi frame produces CSI.
- **CSI is disabled in the SDK:** The M5Stack 3.3.9 SDK installed on the development machine enables CSI. Use that version or check `CONFIG_ESP_WIFI_CSI_ENABLED` in your SDK. Adding a `#define` in the sketch cannot enable a feature missing from the precompiled Wi-Fi library.
- **macOS reports `ctags: bad CPU type in executable`:** Follow the [command-line repair instructions below](#5-fix-arduino-ctags-on-apple-silicon-macos).
- **No serial output:** Check USB CDC, the selected port, and the data cable. Press Reset after opening the terminal to see the header again.
- **`dropped` increases:** Possible causes include a slow serial consumer, a closed serial port, a full queue, or an unexpectedly long record. Increase `PING_INTERVAL_MS` to lower the request rate. The callback never waits for serial output. It buffers up to 16 records, dropping and counting records that exceed its limits.
- **Old records near a disconnection:** The queue may still contain records received before the connection dropped. Use each record's MAC address and receive timestamp to identify them.

The sketch copies CSI into fixed buffers instead of retaining driver-owned pointers. CSV formatting runs in `loop()`. Keep the Wi-Fi callback short when modifying the captured fields.

## 5. Fix Arduino ctags on Apple Silicon macOS

### Why the Error Occurs

Arduino runs `ctags` before compiling an `.ino` sketch to generate function prototypes. The installed `ctags` may be an Intel `x86_64` executable, while an Apple Silicon Mac uses `arm64`. If macOS cannot run the Intel tool, it reports:

```text
fork/exec .../builtin/tools/ctags/5.8-arduino11/ctags: bad CPU type in executable
```

Arduino IDE itself can still open while this helper fails. The following repair builds the same Arduino ctags release as a native ARM64 executable. For background on architecture compatibility and the Rosetta alternative, see the [official Arduino guidance](https://support.arduino.cc/hc/en-us/articles/7765785712156-Error-bad-CPU-type-in-executable-on-macOS).

### Step 1: Confirm the Architecture

Run in Terminal:

```bash
uname -m
file "$HOME/Library/Arduino15/packages/builtin/tools/ctags/5.8-arduino11/ctags"
```

On an affected Apple Silicon installation, the first command prints `arm64` and the second reports `Mach-O 64-bit executable x86_64`. After repair, the second command should report `arm64` as well.

These instructions target **ctags 5.8-arduino11** in the default Arduino installation directory. If the error names a different version or path, inspect that installation before applying this repair.

### Step 2: Check Build Prerequisites

```bash
xcrun --find clang
python3 --version
make --version
```

The script needs Python 3, Clang, Make, curl, and tar. If Apple's developer tools are missing, run the following command, finish the installer, and then continue:

```bash
xcode-select --install
```

### Step 3: Run the Repair Script

Close Arduino IDE while replacing the tool. From the repository root, run:

```bash
bash Lab-3-CSI-serial/scripts/fix_arduino_ctags_macos.sh
```

If your terminal is already in `Lab-3-CSI-serial`, run:

```bash
bash scripts/fix_arduino_ctags_macos.sh
```

The [repair script](scripts/fix_arduino_ctags_macos.sh) performs these operations:

1. Checks that the terminal is running natively on Apple Silicon. If the installed ARM64 ctags already works, it exits without replacing anything.
2. Downloads the official [`arduino/ctags` source at tag `5.8-arduino11`](https://github.com/arduino/ctags/tree/5.8-arduino11) into a temporary build directory.
3. Renames the old source's `__unused__` and `__printf__` macros to `CTAGS_UNUSED` and `CTAGS_PRINTF`, preserving their behavior while avoiding collisions with newer macOS SDK headers.
4. Builds and runs the native executable before installation. The build commands used inside the source directory are:

   ```bash
   ./configure CC='xcrun clang' \
     CFLAGS='-O2 -arch arm64 -std=gnu89 -Wno-error=implicit-function-declaration'
   make -j4
   file ./ctags
   ./ctags --version
   ```

   `xcrun` selects Apple's compiler and SDK. The flags select ARM64 and the legacy C dialect used by this source. The final warning flag allows its older configure probes to run with modern Clang.

5. Backs up the installed tool as `ctags.x86_64.backup` in the same directory, preserving that backup if it already exists.
6. Replaces the tool at the original Arduino path, verifies it, and removes the temporary build directory. If the build fails, it keeps that directory and prints its location so you can inspect `config.log`.

The default installation is inside your user directory, so this repair normally needs no `sudo`. It preserves Arduino's normal function-prototype generation step. Reinstalling the Arduino tool package may overwrite the repair; rerun the script if the same architecture error returns.

### Step 4: Verify the Installed Tool and Compile

```bash
file "$HOME/Library/Arduino15/packages/builtin/tools/ctags/5.8-arduino11/ctags"
"$HOME/Library/Arduino15/packages/builtin/tools/ctags/5.8-arduino11/ctags" --version
```

Expect an `arm64` executable and a successful ctags version message. Reopen Arduino IDE and click **Verify**.

For a command-line build, run from the repository root with Arduino IDE installed at its default location:

```bash
"/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli" compile \
  --fqbn 'm5stack:esp32:m5stack_atoms3r:PSRAM=opi,CDCOnBoot=cdc,USBMode=hwcdc' \
  --build-path "$(mktemp -d "${TMPDIR:-/tmp}/atoms3r-csi-build.XXXXXX")" \
  Lab-3-CSI-serial/ino/AtomS3R_CSI_STA
```

The M5Stack board package must already be installed. This command compiles and links the firmware; it does not upload it.

### Restore the Previous Tool If Needed

Close Arduino IDE, then restore the backup created by the script:

```bash
ctags_tool_dir="$HOME/Library/Arduino15/packages/builtin/tools/ctags/5.8-arduino11"
cp -p "$ctags_tool_dir/ctags.x86_64.backup" "$ctags_tool_dir/ctags"
```

The original manual repair on the development machine used the backup name `ctags.x86_64.backup-20260915`. To restore that specific backup instead, use:

```bash
ctags_tool_dir="$HOME/Library/Arduino15/packages/builtin/tools/ctags/5.8-arduino11"
cp -p "$ctags_tool_dir/ctags.x86_64.backup-20260915" "$ctags_tool_dir/ctags"
```

Restoring the Intel executable can bring back the original architecture error on a Mac that cannot run it.

## Validation

Build target: M5Stack ESP32 **3.3.9**, `m5stack:esp32:m5stack_atoms3r`, OPI PSRAM, Hardware CDC, and CDC On Boot.

After repairing the local `ctags` architecture mismatch, a complete build using Arduino's default tool path passed, including automatic function prototype generation and linking. The program uses 885,507 bytes of flash and 48,312 bytes of static RAM, excluding runtime allocations such as the queue. Live serial capture and plotting were verified as described above.

For hardware acceptance, confirm that `ping_ok` and `received` increase, each CSV array length equals `len`, and CSI changes when an object moves between the router and board. Turn the AP off and back on to check recovery. These movement and reconnection checks remain separate from compilation and serial capture validation.
