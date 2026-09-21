from flask import Flask, request, Response, jsonify
import threading
import time

app = Flask(__name__)

frames = {}
cameras = {}

lock = threading.Lock()


# ============================================================
# Camera upload
# ============================================================

@app.route("/upload", methods=["POST"])
def upload():

    device_id = request.headers.get(
        "X-Device-ID",
        "unknown"
    )

    device_name = request.headers.get(
        "X-Device-Name",
        device_id
    )

    frame = request.data

    if not frame:
        return "Empty frame", 400


    with lock:

        frames[device_id] = frame

        cameras[device_id] = {
            "id": device_id,
            "name": device_name,
            "last_seen": time.time(),
            "frame_size": len(frame)
        }


    print(
        f"{device_id} ({device_name}): "
        f"{len(frame)} bytes"
    )

    return "OK", 200


# ============================================================
# Camera list API
# ============================================================

@app.route("/api/cameras")
def camera_list():

    now = time.time()

    result = []

    with lock:

        for device_id, info in cameras.items():

            age = now - info["last_seen"]

            result.append({
                "id": device_id,
                "name": info["name"],
                "online": age < 5,
                "last_seen": round(age, 1),
                "frame_size": info["frame_size"]
            })

    return jsonify(result)


# ============================================================
# MJPEG stream
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

                yield (
                    b"--frame\r\n"
                    b"Content-Type: image/jpeg\r\n\r\n"
                    + frame
                    + b"\r\n"
                )

            time.sleep(0.03)


    return Response(
        generate(),
        mimetype=
        "multipart/x-mixed-replace; boundary=frame"
    )


# ============================================================
# Dynamic dashboard
# ============================================================

@app.route("/")
def index():

    return """
<!DOCTYPE html>

<html>

<head>

<title>AtomS3R Camera Dashboard</title>

<style>

body {
    font-family: Arial;
    margin: 30px;
    background: #f5f5f5;
}

h1 {
    margin-bottom: 25px;
}

#camera-grid {
    display: grid;
    grid-template-columns:
        repeat(auto-fill, minmax(360px, 1fr));

    gap: 20px;
}

.camera-card {
    background: white;
    border-radius: 10px;
    padding: 15px;

    box-shadow:
        0 2px 8px rgba(0,0,0,0.12);
}

.camera-name {
    font-size: 20px;
    font-weight: bold;
}

.camera-id {
    color: #666;
    font-size: 13px;
    margin-top: 4px;
}

.camera-status {
    margin-top: 5px;
    margin-bottom: 10px;
}

.camera-image {
    width: 100%;
    background: #111;
}

.online {
    color: green;
}

.offline {
    color: red;
}

</style>

</head>


<body>

<h1>AtomS3R Camera Dashboard</h1>

<div id="camera-grid"></div>


<script>

let existingCameras = new Set();


async function updateCameras() {

    try {

        const response =
            await fetch("/api/cameras");

        const cameras =
            await response.json();


        const grid =
            document.getElementById(
                "camera-grid"
            );


        for (const camera of cameras) {

            let card =
                document.getElementById(
                    "camera-" + camera.id
                );


            // --------------------------------
            // New camera
            // --------------------------------

            if (!card) {

                card =
                    document.createElement(
                        "div"
                    );

                card.className =
                    "camera-card";

                card.id =
                    "camera-" + camera.id;


                card.innerHTML = `

                    <div class="camera-name">
                        ${camera.name}
                    </div>

                    <div class="camera-id">
                        ID: ${camera.id}
                    </div>

                    <div
                        class="camera-status"
                        id="status-${camera.id}">
                    </div>

                    <img
                        class="camera-image"
                        src="/stream/${camera.id}"
                    >

                `;


                grid.appendChild(card);
            }


            // --------------------------------
            // Update status
            // --------------------------------

            const status =
                document.getElementById(
                    "status-" + camera.id
                );


            if (camera.online) {

                status.innerHTML =
                    `● Online`;

                status.className =
                    "camera-status online";

            } else {

                status.innerHTML =
                    `● Offline
                    (${camera.last_seen}s ago)`;

                status.className =
                    "camera-status offline";
            }

        }

    } catch (error) {

        console.log(error);

    }

}


// Update camera list every second
setInterval(
    updateCameras,
    1000
);


// Initial load
updateCameras();

</script>


</body>

</html>
"""


# ============================================================
# Start server
# ============================================================

if __name__ == "__main__":

    app.run(
        host="0.0.0.0",
        port=8001,
        threaded=True
    )