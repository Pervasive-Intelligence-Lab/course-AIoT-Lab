"""
Lab 4 - AI Camera server

    AtomS3R-CAM --Wi-Fi/eduroam--> this server --> Vision LLM
                                        |
                                        +--> web dashboard (image + description)
                                        +--> optional speech for the Atomic Voice Base

This is the Lab 2 relay server (server-camera.py) extended with:
  * a vision-LLM call that turns each uploaded photo into a one-sentence description
  * a dashboard that shows every camera, its latest image and the description
  * an optional text-to-speech endpoint so the board can say the description

Configuration uses environment variables (see README.md). By default it uses
Purdue AnvilGPT (OpenAI-compatible API, vision model gemma4:26b-a4b):

    pip install -r requirements.txt
    cp config.example.json config.json   # then put your AnvilGPT API key in it
    python server-cam-ai.py --models     # check the key, list models you can use
    python server-cam-ai.py

    open http://<SERVER_IP>:8001/
"""

import array
import base64
import io
import json
import os
import shutil
import sys
import subprocess
import tempfile
import threading
import time
import wave
from collections import deque

import requests
from flask import Flask, Response, abort, jsonify, request


# ============================================================
# Configuration: config.json (next to this script) or environment variables
# ============================================================

# Use another file with:  python server-cam-ai.py --config my-config.json
CONFIG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "config.json")
if "--config" in sys.argv:
    CONFIG_PATH = sys.argv[sys.argv.index("--config") + 1]
CONFIG_PATH = os.environ.get("CONFIG", CONFIG_PATH)


def load_config(path):
    if not os.path.exists(path):
        return {}
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    if not isinstance(data, dict):
        raise SystemExit(f"{path} must contain a JSON object {{...}}")
    return data


CONFIG = load_config(CONFIG_PATH)


def setting(name, default=""):
    """Environment variable first, then config.json, then the default."""
    value = os.environ.get(name)
    if value is None:
        value = CONFIG.get(name)
    if value is None or str(value).startswith("<"):  # unfilled placeholder
        return default
    return str(value)


PORT = int(setting("PORT", "8001"))

# openai : any OpenAI-compatible /chat/completions API. Default: Purdue
#          AnvilGPT (Open WebUI). Also works with OpenAI, Ollama, vLLM, ...
# mock   : no network call, returns a fixed sentence (for testing)
LLM_PROVIDER = setting("LLM_PROVIDER", "openai").lower()

# AnvilGPT chat endpoint: https://anvilgpt.rcac.purdue.edu/api/chat/completions
# (For OpenAI itself use LLM_BASE_URL=https://api.openai.com/v1)
LLM_BASE_URL = setting(
    "LLM_BASE_URL", "https://anvilgpt.rcac.purdue.edu/api").rstrip("/")

# Must be a model that accepts images. On AnvilGPT: gemma4:26b-a4b.
# Run "python server-cam-ai.py --models" to see the models your key can use.
LLM_MODEL = setting("LLM_MODEL", "gemma4:26b-a4b")

LLM_API_KEY = (
    setting("ANVILGPT_API_KEY")
    or setting("LLM_API_KEY")
    or setting("OPENAI_API_KEY")
)

LLM_TIMEOUT_S = float(setting("LLM_TIMEOUT_S", "60"))

# AnvilGPT allows 60 requests/minute per user, and the whole class shares the
# server's key. Stay a bit below that; extra photos get an error instead.
MAX_LLM_PER_MINUTE = int(setting("MAX_LLM_PER_MINUTE", "50"))

PROMPT = setting(
    "PROMPT",
    "You are the brain of a small AI camera. Describe what you see in this photo "
    "in one short, plain sentence (at most 20 words). Do not start with "
    "'The image shows'.",
)

# Cameras that stream continuously (e.g. the Lab 2 sketches) do not ask for a
# description. Their latest frame is described in the background at most once
# every AUTO_DESCRIBE_INTERVAL_S seconds. Set to 0 to disable.
AUTO_DESCRIBE_INTERVAL_S = float(setting("AUTO_DESCRIBE_INTERVAL_S", "10"))

# none   : no speech; the board plays a chime instead
# openai : OpenAI-compatible /audio/speech. Default: AnvilGPT
#          (https://anvilgpt.rcac.purdue.edu/api/v1/audio/speech).
#          MP3 or other compressed replies are decoded with ffmpeg.
# say    : macOS built-in "say" command (offline, free)
# espeak : espeak-ng / espeak command (offline, free, Linux)
TTS_PROVIDER = setting("TTS_PROVIDER", "none").lower()
TTS_BASE_URL = setting(
    "TTS_BASE_URL", "https://anvilgpt.rcac.purdue.edu/api/v1").rstrip("/")
TTS_MODEL = setting("TTS_MODEL", "")   # empty = server default
TTS_VOICE = setting("TTS_VOICE", "")   # empty = server default
TTS_API_KEY = setting("TTS_API_KEY") or LLM_API_KEY

# The Atomic Voice Base firmware plays 16 kHz, 16-bit, mono PCM.
SPEECH_SAMPLE_RATE = 16000

# A camera is "online" if it uploaded within this many seconds. AI cameras
# send one photo every few seconds, streaming cameras several per second.
ONLINE_TIMEOUT_S = 5
AI_ONLINE_TIMEOUT_S = 60


app = Flask(__name__)

lock = threading.Lock()

frames = {}    # device_id -> latest JPEG bytes
cameras = {}   # device_id -> metadata + latest description
speech_cache = {}  # device_id -> (description_seq, pcm_bytes)
llm_calls = deque()  # timestamps of recent LLM requests (rate limit)


# ============================================================
# Vision LLM
# ============================================================

def describe_image(jpeg):
    """Send one JPEG to the configured vision LLM and return its text."""

    if LLM_PROVIDER == "mock":
        time.sleep(0.5)
        return f"(mock) A {len(jpeg) // 1024} KB photo was received."

    with lock:
        now = time.time()
        while llm_calls and now - llm_calls[0] > 60:
            llm_calls.popleft()
        if len(llm_calls) >= MAX_LLM_PER_MINUTE:
            raise RuntimeError(
                f"Server limit of {MAX_LLM_PER_MINUTE} LLM requests/minute reached; "
                "increase CAPTURE_INTERVAL_MS on the cameras")
        llm_calls.append(now)

    b64 = base64.b64encode(jpeg).decode("ascii")

    # OpenAI-compatible chat completions (default)
    headers = {"Content-Type": "application/json"}
    if LLM_API_KEY:
        headers["Authorization"] = f"Bearer {LLM_API_KEY}"
    elif not LLM_BASE_URL.startswith(("http://localhost", "http://127.")):
        raise RuntimeError("Set ANVILGPT_API_KEY in config.json")

    r = requests.post(
        f"{LLM_BASE_URL}/chat/completions",
        headers=headers,
        json={
            "model": LLM_MODEL,
            "messages": [{
                "role": "user",
                "content": [
                    {"type": "text", "text": PROMPT},
                    {"type": "image_url", "image_url": {
                        "url": f"data:image/jpeg;base64,{b64}"}},
                ],
            }],
            "stream": False,
        },
        timeout=LLM_TIMEOUT_S,
    )
    r.raise_for_status()

    reply = r.json()
    if reply is None:
        # AnvilGPT answers with JSON null instead of HTTP 429 when the
        # rate limit (60 requests/minute per user) is exceeded.
        raise RuntimeError("LLM returned null (rate limit exceeded?)")
    if "choices" not in reply:
        raise RuntimeError(f"Unexpected LLM reply: {str(reply)[:200]}")

    return (reply["choices"][0]["message"].get("content") or "").strip()


def run_description(device_id, seq, jpeg):
    """Describe one frame and store the result. Caller must have set 'busy'."""

    start = time.time()
    text, error = "", ""

    try:
        text = describe_image(jpeg) or "(empty response)"
    except requests.HTTPError as e:
        error = f"LLM HTTP {e.response.status_code}: {e.response.text[:200]}"
    except Exception as e:  # network error, timeout, missing key, ...
        error = f"{type(e).__name__}: {e}"

    elapsed = time.time() - start

    with lock:
        cam = cameras[device_id]
        cam["busy"] = False
        cam["described_seq"] = seq
        cam["last_describe"] = time.time()
        cam["llm_ms"] = int(elapsed * 1000)
        if error:
            cam["error"] = error
        else:
            cam["error"] = ""
            cam["description"] = text
            cam["description_seq"] = seq
            cam["description_time"] = time.time()

    if error:
        print(f"[LLM] {device_id} #{seq} failed after {elapsed:.1f}s: {error}")
    else:
        print(f"[LLM] {device_id} #{seq} ({elapsed:.1f}s): {text}")

    return text, error


def background_describer():
    """Describe the latest frame of streaming cameras every few seconds."""

    while True:
        time.sleep(0.5)

        if AUTO_DESCRIBE_INTERVAL_S <= 0:
            continue

        job = None
        now = time.time()

        with lock:
            for device_id, cam in cameras.items():
                if (
                    not cam["busy"]
                    and cam["seq"] > cam["described_seq"]
                    and now - cam["last_describe"] >= AUTO_DESCRIBE_INTERVAL_S
                ):
                    cam["busy"] = True
                    job = (device_id, cam["seq"], frames[device_id])
                    break

        if job:
            run_description(*job)


# ============================================================
# Text-to-speech (optional, for the Atomic Voice Base)
# ============================================================

def resample_pcm16(pcm, src_rate, dst_rate=SPEECH_SAMPLE_RATE):
    """Linear-interpolation resampler for mono 16-bit PCM (standard library only)."""

    src = array.array("h")
    src.frombytes(pcm[: len(pcm) // 2 * 2])

    if src_rate == dst_rate or len(src) < 2:
        return src.tobytes()

    n_out = int(len(src) * dst_rate / src_rate)
    out = array.array("h", bytes(2 * n_out))
    step = src_rate / dst_rate
    last = len(src) - 1

    for i in range(n_out):
        pos = i * step
        j = int(pos)
        if j >= last:
            out[i] = src[last]
        else:
            frac = pos - j
            out[i] = int(src[j] + (src[j + 1] - src[j]) * frac)

    return out.tobytes()


def wav_to_pcm16_mono(wav_bytes):
    with wave.open(io.BytesIO(wav_bytes)) as w:
        if w.getsampwidth() != 2:
            raise RuntimeError("TTS WAV must be 16-bit")
        rate, channels = w.getframerate(), w.getnchannels()
        data = w.readframes(w.getnframes())

    if channels > 1:  # keep the first channel
        s = array.array("h")
        s.frombytes(data)
        data = s[::channels].tobytes()

    return resample_pcm16(data, rate)


def decode_audio(data):
    """Any audio file (WAV, MP3, ...) -> 16 kHz mono PCM. Non-WAV needs ffmpeg."""

    if data[:4] == b"RIFF":
        return wav_to_pcm16_mono(data)

    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise RuntimeError("TTS returned compressed audio; install ffmpeg to decode it")

    out = subprocess.run(
        [ffmpeg, "-loglevel", "error", "-i", "pipe:0",
         "-f", "s16le", "-ac", "1", "-ar", str(SPEECH_SAMPLE_RATE), "pipe:1"],
        input=data, capture_output=True, check=True, timeout=30,
    )
    return out.stdout


def synthesize_speech(text):
    """Return 16 kHz, 16-bit, mono PCM for text, or None if TTS is disabled."""

    if TTS_PROVIDER == "openai":
        body = {"input": text, "response_format": "wav"}
        if TTS_MODEL:
            body["model"] = TTS_MODEL
        if TTS_VOICE:
            body["voice"] = TTS_VOICE
        r = requests.post(
            f"{TTS_BASE_URL}/audio/speech",
            headers={"Authorization": f"Bearer {TTS_API_KEY}"},
            json=body,
            timeout=LLM_TIMEOUT_S,
        )
        r.raise_for_status()
        return decode_audio(r.content)

    if TTS_PROVIDER in ("say", "espeak"):
        with tempfile.TemporaryDirectory() as tmp:
            if TTS_PROVIDER == "say":
                path = os.path.join(tmp, "speech.wav")
                cmd = ["say", "-o", path, "--data-format=LEI16@16000", text]
            else:
                exe = shutil.which("espeak-ng") or shutil.which("espeak") or "espeak-ng"
                path = os.path.join(tmp, "speech.wav")
                cmd = [exe, "-w", path, text]
            subprocess.run(cmd, check=True, timeout=30,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            with open(path, "rb") as f:
                return wav_to_pcm16_mono(f.read())

    return None


def pcm_to_wav(pcm, rate=SPEECH_SAMPLE_RATE):
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm)
    return buf.getvalue()


# ============================================================
# Camera upload
# ============================================================

@app.route("/upload", methods=["POST"])
def upload():
    """
    Body: one JPEG. Headers: X-Device-ID, optional X-Device-Name.

    POST /upload             -> "OK" (Lab 2 behaviour; described in background)
    POST /upload?describe=1  -> waits for the LLM and returns the description
                                as text/plain (used by the Lab 4 AI camera)
    """

    device_id = request.headers.get("X-Device-ID", "unknown")
    device_name = request.headers.get("X-Device-Name", device_id)
    frame = request.get_data()  # raw body, whatever the Content-Type

    if not frame:
        return "Empty frame", 400

    want_description = request.args.get("describe", "0") not in ("0", "", "false")

    with lock:
        cam = cameras.setdefault(device_id, {
            "id": device_id,
            "seq": 0,
            "busy": False,
            "described_seq": 0,
            "last_describe": 0.0,
            "description": "",
            "description_seq": 0,
            "description_time": 0.0,
            "error": "",
            "llm_ms": 0,
        })
        frames[device_id] = frame
        cam["seq"] += 1
        cam["name"] = device_name
        cam["last_seen"] = time.time()
        cam["frame_size"] = len(frame)
        cam["ai_camera"] = want_description or cam.get("ai_camera", False)
        seq = cam["seq"]

        # Only one LLM call per camera at a time.
        run_now = want_description and not cam["busy"]
        if run_now:
            cam["busy"] = True

    print(f"{device_id} ({device_name}): {len(frame)} bytes, frame #{seq}")

    if not want_description:
        return "OK", 200

    if not run_now:
        return Response("Busy: previous photo is still being described",
                        status=429, mimetype="text/plain")

    text, error = run_description(device_id, seq, frame)

    if error:
        return Response(error, status=502, mimetype="text/plain")

    return Response(
        text,
        content_type="text/plain; charset=utf-8",
        headers={
            "X-Frame-Seq": str(seq),
            "X-Speech-Available": "1" if TTS_PROVIDER != "none" else "0",
        },
    )


# ============================================================
# Speech for the board
# ============================================================

def speech_for(device_id):
    with lock:
        cam = cameras.get(device_id)
        if not cam or not cam["description"]:
            return None
        seq, text = cam["description_seq"], cam["description"]
        cached = speech_cache.get(device_id)

    if cached and cached[0] == seq:
        return cached[1]

    pcm = synthesize_speech(text)
    if pcm is not None:
        with lock:
            speech_cache[device_id] = (seq, pcm)
    return pcm


@app.route("/api/speech/<device_id>")
def speech(device_id):
    """
    Latest description as speech.
      default      : raw PCM, 16 kHz, 16-bit, mono (for the Atomic Voice Base)
      ?format=wav  : WAV file (for a browser or for debugging)
    204 when TTS is disabled or there is no description yet.
    """

    try:
        pcm = speech_for(device_id)
    except Exception as e:
        print(f"[TTS] {device_id} failed: {e}")
        return Response(f"TTS failed: {e}", status=502, mimetype="text/plain")

    if pcm is None:
        return "", 204

    if request.args.get("format") == "wav":
        return Response(pcm_to_wav(pcm), mimetype="audio/wav")

    return Response(pcm, mimetype="application/octet-stream",
                    headers={"X-Sample-Rate": str(SPEECH_SAMPLE_RATE)})


# ============================================================
# Dashboard APIs
# ============================================================

@app.route("/api/cameras")
def camera_list():
    now = time.time()
    result = []

    with lock:
        for device_id, cam in cameras.items():
            age = now - cam["last_seen"]
            result.append({
                "id": device_id,
                "name": cam["name"],
                "online": age < (AI_ONLINE_TIMEOUT_S if cam["ai_camera"]
                                 else ONLINE_TIMEOUT_S),
                "last_seen": round(age, 1),
                "frame_size": cam["frame_size"],
                "seq": cam["seq"],
                "busy": cam["busy"],
                "description": cam["description"],
                "description_seq": cam["description_seq"],
                "description_age": round(now - cam["description_time"], 1)
                                   if cam["description_time"] else None,
                "llm_ms": cam["llm_ms"],
                "error": cam["error"],
            })

    result.sort(key=lambda c: c["name"].lower())
    return jsonify({
        "model": "mock" if LLM_PROVIDER == "mock"
                 else f"{LLM_MODEL} @ {LLM_BASE_URL.split('//')[-1].split('/')[0]}",
        "tts": TTS_PROVIDER,
        "cameras": result,
    })


@app.route("/snapshot/<device_id>")
def snapshot(device_id):
    """Latest JPEG from one camera."""

    with lock:
        frame = frames.get(device_id)

    if frame is None:
        abort(404)

    return Response(frame, mimetype="image/jpeg",
                    headers={"Cache-Control": "no-store"})


@app.route("/api/describe/<device_id>", methods=["POST"])
def describe_now(device_id):
    """Dashboard button: describe the camera's latest frame now."""

    with lock:
        cam = cameras.get(device_id)
        if cam is None:
            abort(404)
        if cam["busy"]:
            return jsonify({"ok": False, "error": "busy"}), 429
        cam["busy"] = True
        job = (device_id, cam["seq"], frames[device_id])

    threading.Thread(target=run_description, args=job, daemon=True).start()
    return jsonify({"ok": True})


# ============================================================
# MJPEG stream (unchanged from Lab 2)
# ============================================================

@app.route("/stream/<device_id>")
def stream(device_id):

    def generate():
        last_frame = None
        while True:
            with lock:
                frame = frames.get(device_id)
            if frame is not None and frame is not last_frame:
                last_frame = frame
                yield (b"--frame\r\n"
                       b"Content-Type: image/jpeg\r\n\r\n" + frame + b"\r\n")
            time.sleep(0.03)

    return Response(generate(),
                    mimetype="multipart/x-mixed-replace; boundary=frame")


# ============================================================
# Dashboard
# ============================================================

DASHBOARD_HTML = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>AtomS3R AI Camera Dashboard</title>
<style>
body { font-family: Arial, sans-serif; margin: 30px; background: #f5f5f5; color: #222; }
h1 { margin-bottom: 4px; }
#subtitle { color: #666; margin-bottom: 25px; font-size: 14px; }
#camera-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(360px, 1fr)); gap: 20px; }
#empty { color: #666; }
.camera-card { background: white; border-radius: 10px; padding: 15px; box-shadow: 0 2px 8px rgba(0,0,0,0.12); }
.camera-name { font-size: 20px; font-weight: bold; }
.camera-id { color: #666; font-size: 13px; margin-top: 4px; }
.camera-status { margin-top: 5px; margin-bottom: 10px; font-size: 14px; }
.camera-image { width: 100%; aspect-ratio: 4 / 3; object-fit: contain; background: #111; display: block; border-radius: 4px; }
.online { color: green; }
.offline { color: red; }
.ai-box { margin-top: 12px; padding: 10px 12px; background: #eef4ff; border-left: 4px solid #3b6fd8; border-radius: 4px; }
.ai-label { font-size: 12px; font-weight: bold; color: #3b6fd8; text-transform: uppercase; letter-spacing: .04em; }
.ai-text { font-size: 17px; margin-top: 4px; line-height: 1.35; min-height: 1.35em; }
.ai-meta { font-size: 12px; color: #666; margin-top: 6px; display: flex; justify-content: space-between; align-items: center; gap: 8px; }
.ai-error { color: #b00020; font-size: 13px; margin-top: 6px; word-break: break-word; }
button { font-size: 12px; padding: 4px 10px; border: 1px solid #3b6fd8; background: white; color: #3b6fd8; border-radius: 4px; cursor: pointer; }
button:disabled { opacity: .5; cursor: default; }
</style>
</head>
<body>

<h1>AtomS3R AI Camera Dashboard</h1>
<div id="subtitle">Camera &rarr; Wi-Fi &rarr; Server &rarr; Vision LLM &rarr; Web page</div>
<div id="empty">Waiting for cameras to upload&hellip;</div>
<div id="camera-grid"></div>

<template id="card-template">
  <div class="camera-card">
    <div class="camera-name"></div>
    <div class="camera-id"></div>
    <div class="camera-status"></div>
    <img class="camera-image" alt="latest photo">
    <div class="ai-box">
      <div class="ai-label">AI sees</div>
      <div class="ai-text"></div>
      <div class="ai-error"></div>
      <div class="ai-meta"><span class="ai-age"></span><button>Describe now</button></div>
    </div>
  </div>
</template>

<script>
const grid = document.getElementById("camera-grid");
const template = document.getElementById("card-template");
const cards = new Map();

function makeCard(cam) {
  const card = template.content.firstElementChild.cloneNode(true);
  const el = {
    card,
    name: card.querySelector(".camera-name"),
    id: card.querySelector(".camera-id"),
    status: card.querySelector(".camera-status"),
    img: card.querySelector(".camera-image"),
    text: card.querySelector(".ai-text"),
    error: card.querySelector(".ai-error"),
    age: card.querySelector(".ai-age"),
    button: card.querySelector("button"),
    seq: -1,
  };
  el.id.textContent = "ID: " + cam.id;
  el.button.onclick = async () => {
    el.button.disabled = true;
    await fetch("/api/describe/" + encodeURIComponent(cam.id), { method: "POST" });
    updateCameras();
  };
  grid.appendChild(card);
  cards.set(cam.id, el);
  return el;
}

async function updateCameras() {
  try {
    const data = await (await fetch("/api/cameras")).json();
    document.getElementById("subtitle").textContent =
      "Camera → Wi-Fi → Server → Vision LLM (" + data.model + ") → Web page";
    document.getElementById("empty").style.display = data.cameras.length ? "none" : "block";

    for (const cam of data.cameras) {
      const el = cards.get(cam.id) || makeCard(cam);
      el.name.textContent = cam.name;

      // Reload the photo only when a new frame has arrived.
      if (cam.seq !== el.seq) {
        el.seq = cam.seq;
        el.img.src = "/snapshot/" + encodeURIComponent(cam.id) + "?t=" + cam.seq;
      }

      el.status.textContent = cam.online
        ? "● Online · photo #" + cam.seq + " · " + (cam.frame_size / 1024).toFixed(1) + " KB"
        : "● Offline (" + cam.last_seen + "s ago)";
      el.status.className = "camera-status " + (cam.online ? "online" : "offline");

      if (cam.busy) {
        el.text.textContent = cam.description ? cam.description : "";
        el.age.textContent = "Asking the LLM…";
      } else if (cam.description) {
        el.text.textContent = cam.description;
        el.age.textContent = "photo #" + cam.description_seq + " · " +
          cam.description_age + "s ago · LLM " + (cam.llm_ms / 1000).toFixed(1) + "s";
      } else {
        el.text.textContent = "";
        el.age.textContent = "No description yet";
      }
      el.error.textContent = cam.error || "";
      el.button.disabled = cam.busy;
    }
  } catch (error) {
    console.log(error);
  }
}

setInterval(updateCameras, 1000);
updateCameras();
</script>
</body>
</html>
"""


@app.route("/")
def index():
    return DASHBOARD_HTML


# ============================================================
# Start server
# ============================================================

def list_models():
    """Print the models this API key can use (checks the key, too)."""

    headers = {"Authorization": f"Bearer {LLM_API_KEY}"} if LLM_API_KEY else {}
    try:
        r = requests.get(f"{LLM_BASE_URL}/models", headers=headers, timeout=30)
    except requests.RequestException as e:
        print(f"Cannot reach {LLM_BASE_URL}: {e}")
        return
    if r.status_code != 200:
        print(f"GET {LLM_BASE_URL}/models -> HTTP {r.status_code}: {r.text[:200]}")
        if r.status_code == 401:
            print(f"Check ANVILGPT_API_KEY in {CONFIG_PATH}")
        return
    models = (r.json() or {}).get("data", [])
    print(f"{len(models)} models at {LLM_BASE_URL}:")
    for m in models:
        print(" ", m.get("id"), "  <- LLM_MODEL" if m.get("id") == LLM_MODEL else "")


if __name__ == "__main__":
    if "--models" in sys.argv:
        list_models()
        sys.exit(0)

    print("=" * 60)
    print(f"AI camera server on http://0.0.0.0:{PORT}/")
    print(f"Config: {CONFIG_PATH}" + ("" if CONFIG else "  (not found, using defaults/environment)"))
    print(f"LLM : {LLM_PROVIDER} / {LLM_MODEL} @ {LLM_BASE_URL}"
          + ("" if LLM_API_KEY or LLM_PROVIDER == "mock" else "  (no API key set)"))
    print(f"TTS : {TTS_PROVIDER}")
    print(f"Auto-describe streaming cameras every {AUTO_DESCRIBE_INTERVAL_S:g}s"
          if AUTO_DESCRIBE_INTERVAL_S > 0 else "Auto-describe disabled")
    print("=" * 60)

    threading.Thread(target=background_describer, daemon=True).start()
    app.run(host="0.0.0.0", port=PORT, threaded=True)
