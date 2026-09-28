# Lab 4: AI Camera

In this lab you turn an M5Stack AtomS3R camera board into a small AI camera. The board takes a photo and sends it to the class server. The server asks a vision LLM what it sees, sends back a short description, and shows the photo with the description on a web page.

There are two firmware versions, one for each board. Both use the same server and dashboard:

| Board | Camera | Sketch |
| --- | --- | --- |
| **AtomS3R-CAM** | GC0308, 0.3 MP, RGB565 (converted to JPEG on the board) | [`cam_ai_cam`](cam_ai_cam/cam_ai_cam.ino) |
| **AtomS3R-M12** | OV3660, 3 MP, 120° lens, native JPEG | [`cam_ai_m12`](cam_ai_m12/cam_ai_m12.ino) |

```text
AtomS3R-CAM / M12 ──Wi-Fi / eduroam──► Flask server ──► Vision LLM (AnvilGPT)
        ▲                                   │
        │        description (text)         ├──► Web dashboard: camera, latest photo, description
        └───────────────────────────────────┘
```

This lab reuses the Lab 2 relay code. The camera and eduroam code comes from [`camera_edurom_relay`](../Lab-2-camera-streaming/camera_edurom_relay_m5/camera_edurom_relay.ino) and [`camera_edurom_relay_m12`](../Lab-2-camera-streaming/camera_edurom_relay_m12/camera_edurom_relay_m12.ino). [`server-cam-ai.py`](server-cam-ai.py) is [`server-camera.py`](../Lab-2-camera-streaming/server-camera.py) with an LLM step added. The Lab 2 streaming cameras still work with the new server, and their latest frame is described every few seconds.

## Files

| Path | What it is |
| --- | --- |
| [`cam_ai_cam/cam_ai_cam.ino`](cam_ai_cam/cam_ai_cam.ino) | Firmware for the AtomS3R-CAM |
| [`cam_ai_m12/cam_ai_m12.ino`](cam_ai_m12/cam_ai_m12.ino) | Firmware for the AtomS3R-M12 |
| `*/camera_pins.h` | Camera pins (the same on both boards) |
| `*/secrets.example.h` | Template for your Wi-Fi credentials and the server address |
| [`server-cam-ai.py`](server-cam-ai.py) | Flask server: receives photos, calls the LLM, serves the dashboard |
| [`config.example.json`](config.example.json) | Template for the server's `config.json` (API key, model) |
| [`requirements.txt`](requirements.txt) | Python packages for the server |

## How it works

1. The board takes a photo every 15 seconds, or right away when you press **Enter** in Serial Monitor.
   - AtomS3R-CAM: the GC0308 produces a 320×240 RGB565 frame, and the board converts it to JPEG (about 10–20 KB).
   - AtomS3R-M12: the OV3660 produces a 640×480 JPEG directly (about 30–60 KB).
2. The board sends `POST /upload?describe=1` with the JPEG as the body and the `X-Device-ID` and `X-Device-Name` headers.
3. The server base64-encodes the photo and sends it to the vision LLM's `/chat/completions` endpoint as an `image_url` content block. It then replies with a one-sentence description as plain text.
4. The board prints `AI sees: ...` to Serial Monitor.
5. The dashboard at `http://<SERVER_IP>:8001/` polls `/api/cameras` every second. It shows one card per camera with its status, latest photo, and the LLM description underneath.

## Hardware

- M5Stack **AtomS3R-CAM** ([docs](https://docs.m5stack.com/en/core/AtomS3R-CAM)) or **AtomS3R-M12** ([docs](https://docs.m5stack.com/en/core/AtomS3R-M12)). Each has an ESP32-S3, 8 MB flash, and 8 MB PSRAM.
- USB-C data cable

No wiring is needed. Neither board has a user button; the side button is reset.

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

   `config.json` is ignored by Git. Never commit it or share it with students; only `config.example.json`, which holds a placeholder, belongs in the repository. On a shared server, restrict access with `chmod 600 config.json`.

4. Check the key and see the models it can use:

   ```bash
   python server-cam-ai.py --models
   ```

   The model marked `<- LLM_MODEL` must be in the list. If it isn't, set `LLM_MODEL` in `config.json` to an image-capable model from the list.

5. Start the server:

   ```bash
   python server-cam-ai.py
   ```

   The first lines of output show which config file was loaded. To load a different file, run `python server-cam-ai.py --config /path/to/config.json`.

6. Open `http://<SERVER_IP>:8001/` and share `<SERVER_IP>` with the class.

### Server settings (`config.json`)

Any of these settings can go in `config.json`. An environment variable with the same name overrides the file, for example `PORT=8002 python server-cam-ai.py`.

| Key | Default | Meaning |
| --- | --- | --- |
| `ANVILGPT_API_KEY` | – | AnvilGPT API key. `LLM_API_KEY` also works, for other providers. |
| `LLM_BASE_URL` | `https://anvilgpt.rcac.purdue.edu/api` | Any OpenAI-compatible API, for example `https://api.openai.com/v1` or Ollama's `http://localhost:11434/v1` |
| `LLM_MODEL` | `gemma4:26b-a4b` | Must accept images |
| `LLM_PROVIDER` | `openai` | `mock` tests the server without calling an LLM |
| `PROMPT` | one short sentence | The question sent to the LLM with each photo |
| `MAX_LLM_PER_MINUTE` | `50` | Server-wide LLM budget (AnvilGPT allows 60 per minute per user) |
| `AUTO_DESCRIBE_INTERVAL_S` | `10` | How often streaming (Lab 2) cameras are described; `0` turns this off |
| `PORT` | `8001` | HTTP port |

**Rate limit:** every board uses the server's single AnvilGPT key, and AnvilGPT allows 60 requests per minute per user. With one photo every 15 s, each board makes 4 requests per minute, so about 12 boards fit in the budget. For a larger class, raise `CAPTURE_INTERVAL_MS` in the sketch. When the limit is reached, AnvilGPT may return JSON `null` instead of an HTTP error; the server shows this on the dashboard.

**Test without hardware or an API key:**

```bash
LLM_PROVIDER=mock python server-cam-ai.py
curl -X POST --data-binary @photo.jpg -H "X-Device-ID: test-01" \
     "http://localhost:8001/upload?describe=1"
```

---

## Part B: Board (students)

### 1. Arduino setup

Set up Arduino IDE as in Lab 2. For details, see the [AtomS3R-CAM](https://docs.m5stack.com/en/arduino/m5atoms3r-cam/program) or [AtomS3R-M12](https://docs.m5stack.com/en/arduino/m5atoms3r-m12/program) Arduino guide.

1. Install the **M5Stack** board package. Versions 2.1.4 and 3.3.x both compile these sketches.
2. Select **Tools > Board > M5Stack > M5AtomS3R** for either board. Keep **PSRAM: OPI PSRAM**, which is the default.

No extra libraries are needed: the camera driver, Wi-Fi, and HTTP client come with the board package.

### 2. Configure

1. Open the sketch for your board: [`cam_ai_cam/cam_ai_cam.ino`](cam_ai_cam/cam_ai_cam.ino) (AtomS3R-CAM) or [`cam_ai_m12/cam_ai_m12.ino`](cam_ai_m12/cam_ai_m12.ino) (AtomS3R-M12).
2. In that sketch's folder, copy `secrets.example.h` to `secrets.h`. Fill in your eduroam credentials and the server address given in class:

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

4. You can also change these settings in the sketch:

   | Setting | Default | Meaning |
   | --- | --- | --- |
   | `CAPTURE_INTERVAL_MS` | `15000` | Time between photos; `0` means photos are taken only when you press Enter |
   | `CAM_FRAME_SIZE` | CAM: `FRAMESIZE_QVGA`, M12: `FRAMESIZE_VGA` | M12: `FRAMESIZE_SVGA` or `FRAMESIZE_XGA` give more detail but slow the upload and the LLM call |
   | `JPEG_QUALITY` (CAM) | `80` | 1–100; higher means better quality and a larger file |
   | `CAM_JPEG_QUALITY` (M12) | `12` | 0–63; lower means better quality and a larger file |

### 3. Upload and run

1. Put the board in download mode: hold the reset button for about 2 s until the green LED turns on, then release it.
2. Select the port and click **Upload**, then press reset once.
3. Open Serial Monitor at **115200 baud**. You should see something like this (AtomS3R-CAM shown):

   ```text
   Camera Init Success (sensor PID 0x009b)
   eduroam connected!
   IP address: 10.x.x.x
   System ready.
   Press Enter in Serial Monitor to take a photo now.
   ---------------- New photo ----------------
   Frame: 320 x 240 | JPEG: 14.2 KB
   POST http://<SERVER_IP>:8001/upload?describe=1 | 14545 bytes
   HTTP response: 200 | 2350 ms

   AI sees: A person holding a coffee mug in front of a laptop.
   ```

   On the M12, the sensor PID is `0x3660` and frames are `640 x 480`.

4. Open `http://<SERVER_IP>:8001/` and find your camera's card. Point the camera at something new and press Enter in Serial Monitor. A new photo and description should appear within a few seconds.

### Expected result

- Serial Monitor shows `Camera Init Success`, a Wi-Fi connection, and `HTTP response: 200` followed by `AI sees: ...`.
- The dashboard shows your `deviceName`, your latest photo, and the LLM description underneath it.

## Server API

| Method and path | Used by | Description |
| --- | --- | --- |
| `POST /upload?describe=1` | Lab 4 board | JPEG body. Waits for the LLM and returns the description as `text/plain`. Returns 429 if the camera's previous photo is still being described, and 502 on an LLM error. |
| `POST /upload` | Lab 2 boards | JPEG body; returns `OK`. The frame is described in the background. |
| `GET /api/cameras` | Dashboard | All cameras with their status and latest description |
| `GET /snapshot/<id>` | Dashboard | Latest JPEG |
| `GET /stream/<id>` | – | MJPEG stream, as in Lab 2 |
| `POST /api/describe/<id>` | Dashboard button | Describes the latest frame now |

## Troubleshooting

| Symptom | What to try |
| --- | --- |
| `Camera init failed` | Select `M5AtomS3R` and make sure PSRAM is enabled. Use `cam_ai_cam` for the AtomS3R-CAM and `cam_ai_m12` for the AtomS3R-M12. |
| Wi-Fi timeout | Recheck `secrets.h`, then see the Lab 2 troubleshooting table. |
| `HTTP POST failed: connection refused` or `read Timeout` | Check `SERVER_BASE_URL` (it needs `:8001` and no trailing slash) and that the server is running. |
| `HTTP response: 502` with `Set ANVILGPT_API_KEY in config.json` | The server has no API key. Fill in `config.json` next to `server-cam-ai.py`. |
| `HTTP response: 502` with `LLM HTTP 401/404` | The key is wrong, or the model isn't available to this key. Run `python server-cam-ai.py --models`. |
| `LLM returned null` or `Server limit ... reached` | The rate limit was reached. Raise `CAPTURE_INTERVAL_MS`. |
| `HTTP response: 429` | The camera's previous photo is still being described. This is harmless. |
| Enter does nothing | In Serial Monitor, set the line ending to **Newline** and click in the input box before pressing Enter. |
| Dark or green-tinted first photo | Auto-exposure is still settling. The next photo should look normal. |
| Two cameras share one card | Give each board a unique `deviceId`. |

## Submission checklist

- Screenshot of Serial Monitor showing `HTTP response: 200` and an `AI sees:` line
- Screenshot of the dashboard with your camera, photo, and description
- Your sketch and `secrets.example.h`. **Do not submit `secrets.h`.**

## References

- [AtomS3R-CAM Arduino guide](https://docs.m5stack.com/en/arduino/m5atoms3r-cam/program) and [hardware](https://docs.m5stack.com/en/core/AtomS3R-CAM)
- [AtomS3R-M12 Arduino guide](https://docs.m5stack.com/en/arduino/m5atoms3r-m12/program) and [hardware](https://docs.m5stack.com/en/core/AtomS3R-M12)
- [AnvilGPT API](https://docs.rcac.purdue.edu/userguides/anvil/anvilgpt/api/)
- [Lab 2: Camera Streaming](../Lab-2-camera-streaming/README.md)
