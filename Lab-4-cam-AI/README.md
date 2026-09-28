# Lab 4: AI Camera

In this lab you turn the **M5Stack AtomS3R-CAM** and the **Atomic Voice Base** into a small AI camera. The board takes a photo and sends it to the class server. The server asks a vision LLM what it sees and shows the photo and the description on a web page. The board also prints the description and can read it aloud through the Voice Base speaker.

The course's **AtomS3R-M12** has its own sketch, which works with or without the Voice Base. See [Part C](#part-c-atoms3r-m12-version). Both boards use the same server and dashboard.

```text
AtomS3R-CAM ──Wi-Fi / eduroam──► Flask server ──► Vision LLM (AnvilGPT)
     ▲                              │
     │  description (+ speech)      ├──► Web dashboard: camera, latest photo, description
     └──────────────────────────────┘
```

This lab reuses the Lab 2 relay code. The camera and eduroam code is copied from [`camera_edurom_relay`](../Lab-2-camera-streaming/camera_edurom_relay_m5/camera_edurom_relay.ino), and [`server-cam-ai.py`](server-cam-ai.py) is [`server-camera.py`](../Lab-2-camera-streaming/server-camera.py) with an LLM step added. The Lab 2 streaming cameras still work with the new server, and their latest frame is described every few seconds.

## Files

| Path | What it is |
| --- | --- |
| [`cam_ai_voice/cam_ai_voice.ino`](cam_ai_voice/cam_ai_voice.ino) | Board firmware: camera, Voice Base, Wi-Fi, upload |
| [`cam_ai_voice/camera_pins.h`](cam_ai_voice/camera_pins.h) | AtomS3R-CAM camera pins |
| [`cam_ai_voice/secrets.example.h`](cam_ai_voice/secrets.example.h) | Template for your Wi-Fi credentials and the server address |
| [`cam_ai_m12/cam_ai_m12.ino`](cam_ai_m12/cam_ai_m12.ino) | Firmware for the AtomS3R-M12, with optional Voice Base (see [Part C](#part-c-atoms3r-m12-version)) |
| [`cam_ai_m12/camera_pins.h`](cam_ai_m12/camera_pins.h), [`secrets.example.h`](cam_ai_m12/secrets.example.h) | AtomS3R-M12 camera pins and settings template |
| [`server-cam-ai.py`](server-cam-ai.py) | Flask server: receives photos, calls the LLM, serves the dashboard |
| [`config.example.json`](config.example.json) | Template for the server's `config.json` (API key, model) |
| [`requirements.txt`](requirements.txt) | Python packages for the server |

## How it works

1. Every 15 seconds, or when the microphone hears a loud sound such as a clap, the board takes a photo. The GC0308 sensor produces a 320×240 RGB565 frame, which the board converts to JPEG (about 10–20 KB).
2. The speaker plays a shutter click.
3. The board sends `POST /upload?describe=1` with the JPEG as the body and the `X-Device-ID` and `X-Device-Name` headers.
4. The server base64-encodes the photo and sends it to the vision LLM's `/chat/completions` endpoint as an `image_url` content block. It then replies with the one-sentence description as plain text.
5. The board prints `AI sees: ...` to Serial Monitor. If the server has text-to-speech turned on, the board downloads the speech and plays it. Otherwise it plays a chime.
6. The dashboard at `http://<SERVER_IP>:8001/` polls `/api/cameras` every second. It shows one card per camera with its status, latest photo, and the LLM description underneath.

## Hardware

- M5Stack **AtomS3R-CAM** (ESP32-S3, GC0308 0.3 MP camera, 8 MB flash, 8 MB PSRAM)
- M5Stack **Atomic Voice Base** (Atomic Echo Base): ES8311 codec, MEMS microphone, NS4150B amplifier, 1 W speaker
- USB-C data cable

| Board | Camera | Sketch | Voice Base |
| --- | --- | --- | --- |
| AtomS3R-CAM | GC0308, 0.3 MP, RGB565 (converted to JPEG on the board) | [`cam_ai_voice`](cam_ai_voice/cam_ai_voice.ino) | Used |
| AtomS3R-M12 | OV3660, 3 MP, 120° lens, native JPEG | [`cam_ai_m12`](cam_ai_m12/cam_ai_m12.ino) | Optional (`USE_VOICE_BASE`) |

Plug the AtomS3R-CAM into the Voice Base. No wiring is needed. The two devices use separate pins:

| Signal | AtomS3R GPIO | Used by |
| --- | --- | --- |
| I2C SDA / SCL | 38 / 39 | ES8311 codec + PI4IOE5V6408 speaker enable (on `Wire1`) |
| I2S DIN / WS / DOUT / BCK | 7 / 6 / 5 / 8 | Microphone and speaker audio |
| Camera SCCB SDA / SCL | 12 / 9 | GC0308 (I2C port 0) |
| Camera power | 18 | LOW = camera on |

The camera's SCCB bus uses I2C port 0, so the sketch connects the codec on `Wire1` (port 1). Both cannot use `Wire` at the same time.

---

## Part A: Server (instructor)

The server runs on a computer that the boards can reach on port 8001. It uses **Purdue AnvilGPT** by default ([API docs](https://docs.rcac.purdue.edu/userguides/anvil/anvilgpt/api/)). AnvilGPT is OpenAI-compatible, and `gemma4:26b-a4b` accepts image input.

1. Create an API key in AnvilGPT: click your avatar, then open **Settings > Account > API Keys**.
2. Install the Python packages:

   ```bash
   cd Lab-4-cam-AI
   python3 -m venv .venv && source .venv/bin/activate
   pip install -r requirements.txt
   ```

3. Copy the config template and paste your key into `config.json`:

   ```bash
   cp config.example.json config.json
   ```

   ```json
   {
     "ANVILGPT_API_KEY": "sk-...",
     "LLM_BASE_URL": "https://anvilgpt.rcac.purdue.edu/api",
     "LLM_MODEL": "gemma4:26b-a4b",
     ...
   }
   ```

   `config.json` is ignored by Git, so never commit it or share it with students. Only `config.example.json`, which holds a placeholder, belongs in the repository. On a shared server, restrict access to the file with `chmod 600 config.json`.

4. Check the key and see the models it can use:

   ```bash
   python server-cam-ai.py --models
   ```

   The model marked `<- LLM_MODEL` must be in the list. If it is missing, set `LLM_MODEL` in `config.json` to an image-capable model from the list.

5. Start the server:

   ```bash
   python server-cam-ai.py
   ```

   The first lines show which config file was loaded. To use a different file, run `python server-cam-ai.py --config /path/to/config.json`.

6. Open `http://<SERVER_IP>:8001/` and share `<SERVER_IP>` with the class.

### Server settings (`config.json`)

Each setting can go in `config.json`. An environment variable with the same name overrides the file, for example `PORT=8002 python server-cam-ai.py`.

| Key | Default | Meaning |
| --- | --- | --- |
| `ANVILGPT_API_KEY` | – | AnvilGPT API key (`LLM_API_KEY` also works for other providers) |
| `LLM_BASE_URL` | `https://anvilgpt.rcac.purdue.edu/api` | Any OpenAI-compatible API, for example `https://api.openai.com/v1` or Ollama's `http://localhost:11434/v1` |
| `LLM_MODEL` | `gemma4:26b-a4b` | Must accept images |
| `LLM_PROVIDER` | `openai` | Set to `mock` to test without an LLM |
| `PROMPT` | one short sentence | What the LLM is asked |
| `MAX_LLM_PER_MINUTE` | `50` | Server-wide LLM budget (AnvilGPT allows 60/min per user) |
| `AUTO_DESCRIBE_INTERVAL_S` | `10` | How often streaming (Lab 2) cameras are described; `0` turns this off |
| `TTS_PROVIDER` | `none` | `openai` (AnvilGPT `/api/v1/audio/speech`), `say` (macOS), `espeak` (Linux), or `none` |
| `TTS_MODEL`, `TTS_VOICE` | server default | Optional settings for `TTS_PROVIDER=openai` |
| `PORT` | `8001` | HTTP port |

**Rate limit:** all boards share the server's single AnvilGPT key, and AnvilGPT allows 60 requests per minute per user. With one photo every 15 s, each board uses 4 requests per minute, so about 12 boards fit in the budget. For a larger class, raise `CAPTURE_INTERVAL_MS` in the sketch. When the limit is reached, AnvilGPT may return JSON `null` instead of an error. The server reports this on the dashboard.

**Speech (optional):** with `TTS_PROVIDER=openai`, the server asks AnvilGPT for speech. It converts WAV replies itself and needs `ffmpeg` on the `PATH` for MP3 or other compressed formats. `TTS_PROVIDER=say` (macOS) and `TTS_PROVIDER=espeak` (`sudo apt install espeak-ng`) work offline. To check the audio in a browser, open `http://<SERVER_IP>:8001/api/speech/<deviceId>?format=wav`.

**Test without hardware or an API key:**

```bash
LLM_PROVIDER=mock python server-cam-ai.py
curl -X POST --data-binary @photo.jpg -H "X-Device-ID: test-01" \
     "http://localhost:8001/upload?describe=1"
```

---

## Part B: Board (students)

### 1. Arduino setup

Set up Arduino IDE as in Lab 2, following the [AtomS3R-CAM Arduino guide](https://docs.m5stack.com/en/arduino/m5atoms3r-cam/program):

1. Install the **M5Stack** board package. Versions 2.1.4 and 3.3.x both compile this sketch.
2. Select **Tools > Board > M5Stack > M5AtomS3R**. Keep **PSRAM: OPI PSRAM**, which is the default.
3. Install the **M5Atomic-EchoBase** library, which provides `M5EchoBase.h` for the Voice Base. Without it, compiling fails with `fatal error: M5EchoBase.h: No such file or directory`.
   1. Open **Tools > Manage Libraries…**, or click the books icon in the left sidebar.
   2. Search for `EchoBase`.
   3. Install **M5Atomic-EchoBase** by M5Stack.

   If the search finds nothing, download the ZIP from [GitHub](https://github.com/m5stack/M5Atomic-EchoBase) (**Code > Download ZIP**). Then add it with **Sketch > Include Library > Add .ZIP Library…** and restart Arduino IDE.

   After installation, the M5Stack examples are under **File > Examples > M5Atomic-EchoBase** (`RecordPlay`, `LoopBack`). They also include `M5Unified.h`, but this lab's sketch does not need it. The camera driver ships with the board package.

### 2. Configure

1. Open [`cam_ai_voice/cam_ai_voice.ino`](cam_ai_voice/cam_ai_voice.ino).
2. Copy `cam_ai_voice/secrets.example.h` to `cam_ai_voice/secrets.h`. Fill in your eduroam credentials and the server address given in class:

   ```cpp
   #define WIFI_SSID      "eduroam"
   #define EAP_IDENTITY   "your_UGA_ID@uga.edu"
   #define EAP_USERNAME   "your_UGA_ID@uga.edu"
   #define EAP_PASSWORD   "your_UGA_password"
   #define SERVER_BASE_URL "http://<SERVER_IP_FROM_CLASS>:8001"
   ```

   To use an ordinary WPA2 network instead, comment out the eduroam lines and define `WIFI_SSID` and `WIFI_PASSWORD`.

3. At the top of the sketch, give your camera a unique ID and name:

   ```cpp
   const char* deviceId   = "atoms3r-cam-ai-07";
   const char* deviceName = "Alice's AI camera";
   ```

4. Optional settings in the sketch:

   | Setting | Default | Meaning |
   | --- | --- | --- |
   | `CAPTURE_INTERVAL_MS` | `15000` | Photo interval; `0` means clap only |
   | `CLAP_THRESHOLD` | `3000` | Microphone level that triggers a photo; `0` turns it off |
   | `MIC_DEBUG` | `0` | Set to `1` to print microphone levels for tuning the threshold |
   | `ENABLE_SPEECH` | `1` | Speak the description when the server has TTS |
   | `SPEAKER_VOLUME` | `70` | 0–100 |

### 3. Upload and run

1. Put the board in download mode: hold the reset button for about 2 s until the green LED turns on, then release it.
2. Select the port and click **Upload**, then press reset once.
3. Open Serial Monitor at **115200 baud**. You should see:

   ```text
   Camera Init Success
   Voice Base Init Success
   eduroam connected!
   IP address: 10.x.x.x
   System ready.
   ---------------- New photo ----------------
   Frame: 320 x 240 | JPEG: 14.2 KB
   POST http://<SERVER_IP>:8001/upload?describe=1 | 14545 bytes
   HTTP response: 200 | 2350 ms

   AI sees: A person holding a coffee mug in front of a laptop.
   ```

4. Open `http://<SERVER_IP>:8001/` and find your camera's card. Clap near the board, and a new photo and description should appear within a few seconds.

### Expected result

- Serial Monitor shows `Camera Init Success`, `Voice Base Init Success`, a Wi-Fi connection, and `HTTP response: 200` followed by `AI sees: ...`.
- The dashboard shows your `deviceName`, your latest photo, and the LLM description underneath it.
- The speaker clicks when a photo is taken, then either speaks the description or plays a chime.

---

## Part C: AtomS3R-M12 version

[`cam_ai_m12/cam_ai_m12.ino`](cam_ai_m12/cam_ai_m12.ino) is the same AI camera for the **AtomS3R-M12** ([hardware docs](https://docs.m5stack.com/en/core/AtomS3R-M12)), based on the Lab 2 [`camera_edurom_relay_m12`](../Lab-2-camera-streaming/camera_edurom_relay_m12/camera_edurom_relay_m12.ino). It uses the same server, dashboard, and `secrets.h` settings as Part B.

Differences from the AtomS3R-CAM sketch:

- **Camera:** the OV3660 outputs JPEG directly, so the board uploads the frame buffer without converting it. The default size is VGA (640×480, about 30–60 KB). The sketch discards the first few frames while auto-exposure settles.
- **No extra library by default:** the Voice Base is off (`#define USE_VOICE_BASE 0`), so the M5Atomic-EchoBase library is not needed.
- **Triggers:** a photo every `CAPTURE_INTERVAL_MS`, or right away when you press **Enter** in Serial Monitor. The M12 has no user button; the side button is reset.
- **With a Voice Base:** set `#define USE_VOICE_BASE 1` and install M5Atomic-EchoBase (Part B step 1). You then get the shutter click, clap trigger, and spoken description, just like the CAM version. The M12 reserves G5–G8, G38, and G39 for Atomic bases, and the camera uses none of them.

Steps:

1. Follow Part B step 1 for the board settings (**M5AtomS3R**, **OPI PSRAM**). Skip the library unless you use a Voice Base.
2. Open `cam_ai_m12/cam_ai_m12.ino` and copy `cam_ai_m12/secrets.example.h` to `cam_ai_m12/secrets.h`. Fill it in as in Part B step 2.
3. At the top of the sketch, set a unique `deviceId` and `deviceName`. Optionally change these settings:

   | Setting | Default | Meaning |
   | --- | --- | --- |
   | `CAPTURE_INTERVAL_MS` | `15000` | Photo interval; `0` means on demand only |
   | `CAM_FRAME_SIZE` | `FRAMESIZE_VGA` | `FRAMESIZE_SVGA` or `FRAMESIZE_XGA` for more detail, at the cost of slower uploads and LLM calls |
   | `CAM_JPEG_QUALITY` | `12` | 0–63, where lower means better quality and a larger file |
   | `USE_VOICE_BASE` | `0` | `1` if the M12 sits on an Atomic Voice Base |

4. Upload as in Part B step 3 and open Serial Monitor at 115200 baud:

   ```text
   Camera Init Success (sensor PID 0x3660)
   eduroam connected!
   System ready.
   Press Enter in Serial Monitor to take a photo now.
   ---------------- New photo ----------------
   Frame: 640 x 480 | JPEG: 41.3 KB
   HTTP response: 200 | 2810 ms

   AI sees: A cluttered desk with a monitor, keyboard and a coffee cup.
   ```

5. Find your `deviceName` on the dashboard.

## Server API

| Method and path | Used by | Description |
| --- | --- | --- |
| `POST /upload?describe=1` | Lab 4 board | JPEG body. Waits for the LLM and returns the description as `text/plain`. Header `X-Speech-Available: 1` means speech is available. Returns 429 if the camera's previous photo is still being described and 502 on an LLM error. |
| `POST /upload` | Lab 2 boards | JPEG body, returns `OK`. The frame is described in the background. |
| `GET /api/cameras` | Dashboard | All cameras with status and latest description |
| `GET /snapshot/<id>` | Dashboard | Latest JPEG |
| `GET /stream/<id>` | – | MJPEG stream, same as Lab 2 |
| `POST /api/describe/<id>` | Dashboard button | Describe the latest frame now |
| `GET /api/speech/<id>` | Lab 4 board | Latest description as 16 kHz, 16-bit mono PCM (`?format=wav` for a WAV file). Returns 204 if TTS is off. |

## Troubleshooting

| Symptom | What to try |
| --- | --- |
| `fatal error: M5EchoBase.h: No such file or directory` | Install the **M5Atomic-EchoBase** library, as in [Part B step 1](#1-arduino-setup). |
| `Camera init failed` | Select `M5AtomS3R` and make sure PSRAM is enabled. Use `cam_ai_voice` for the AtomS3R-CAM and `cam_ai_m12` for the AtomS3R-M12. |
| `Voice Base init failed` | Seat the AtomS3R-CAM firmly in the base. The camera still works without audio. |
| Wi-Fi timeout | Recheck `secrets.h`; see the Lab 2 troubleshooting table. |
| `HTTP POST failed: connection refused` or `read Timeout` | Check `SERVER_BASE_URL` (including `:8001`, no trailing slash) and that the server is running. |
| `HTTP response: 502` with `Set ANVILGPT_API_KEY in config.json` | The server has no API key: fill in `config.json` next to `server-cam-ai.py`. |
| `HTTP response: 502` with `LLM HTTP 401/404` | Wrong key, or the model is not available to this key; run `python server-cam-ai.py --models`. |
| `LLM returned null` or `Server limit ... reached` | Rate limit reached; raise `CAPTURE_INTERVAL_MS`. |
| `HTTP response: 429` | The previous photo from this camera is still being described; this is harmless. |
| Photos taken constantly | Room is loud: set `MIC_DEBUG 1`, check the printed levels, and raise `CLAP_THRESHOLD`, or set it to `0`. |
| Clap does nothing | Lower `CLAP_THRESHOLD`, using the `MIC_DEBUG` output as a guide. |
| Chime instead of speech | The server has `TTS_PROVIDER=none`, or TTS failed; check the server log. |
| Two cameras share one card | Give each board a unique `deviceId`. |

## Submission checklist

- Screenshot of Serial Monitor showing `HTTP response: 200` and an `AI sees:` line
- Screenshot of the dashboard with your camera, photo, and description
- Your sketch and `secrets.example.h`. **Do not submit `secrets.h`.**

## References

- [AtomS3R-CAM Arduino guide](https://docs.m5stack.com/en/arduino/m5atoms3r-cam/program)
- [AtomS3R-CAM hardware](https://docs.m5stack.com/en/core/AtomS3R-CAM)
- [AtomS3R-M12 hardware](https://docs.m5stack.com/en/core/AtomS3R-M12) and [Arduino guide](https://docs.m5stack.com/en/arduino/m5atoms3r-m12/program)
- [Atomic Voice Base / Echo Base](https://docs.m5stack.com/en/atom/Atomic%20Echo%20Base) and the [M5Atomic-EchoBase library](https://github.com/m5stack/M5Atomic-EchoBase)
- [AnvilGPT API](https://docs.rcac.purdue.edu/userguides/anvil/anvilgpt/api/)
- [Lab 2: Camera Streaming](../Lab-2-camera-streaming/README.md)
