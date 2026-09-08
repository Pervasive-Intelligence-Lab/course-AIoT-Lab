#!/usr/bin/env python3
"""
Tiny bridge between the AtomS3R-M12 and the browser.  Standard library only.

    ESP32 (udp://<board-ip>:5555, one JSON sample per datagram)  -->  this script  -->  browser (HTTP + Server-Sent Events)

The script sends a "hello" datagram to the board once a second; the board streams to
whoever said hello last.  Lost datagrams are simply lost (no retransmit, no freeze).

Usage:
    python backend/server.py <board-ip>            # then open http://localhost:8000
    python backend/server.py <board-ip> --port 8080
"""

import argparse
import json
import os
import queue
import socket
import sys
import threading
import time
import webbrowser
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

WEB_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "web")
STREAM_PORT = 5555
HELLO_PERIOD_S = 1.0
STALL_S = 1.0          # no datagram for this long -> "stalled"
LOST_S = 5.0           # ... for this long -> "disconnected" (board rebooted / Wi-Fi gone)


class Hub:
    """Fan-out of board samples to every connected browser tab."""

    def __init__(self):
        self._lock = threading.Lock()
        self._subs = set()
        self.status = {"esp": "connecting", "hz": 0.0, "rssi": None, "lost_pct": 0.0, "gap_ms": 0}

    def subscribe(self):
        # Small queue: a tab that falls behind gets the newest samples, never a backlog.
        q = queue.Queue(maxsize=8)
        with self._lock:
            self._subs.add(q)
        return q

    def unsubscribe(self, q):
        with self._lock:
            self._subs.discard(q)

    def publish(self, msg):
        with self._lock:
            subs = list(self._subs)
        for q in subs:
            try:
                q.put_nowait(msg)
            except queue.Full:  # slow tab: drop its oldest sample
                try:
                    q.get_nowait()
                    q.put_nowait(msg)
                except (queue.Empty, queue.Full):
                    pass

    def set_status(self, **kw):
        self.status.update(kw)
        self.publish(("status", json.dumps(self.status)))


class LinkStats:
    """Per-second link quality: rate, RSSI (reported by the board), loss from sequence gaps, max arrival gap."""

    def __init__(self):
        self.reset()

    def reset(self):
        self.count = 0
        self.lost = 0
        self.max_gap = 0.0
        self.last_seq = None
        self.last_at = None
        self.rssi = None

    def observe(self, sample, now):
        self.count += 1
        seq = sample.get("n")
        if isinstance(seq, int) and self.last_seq is not None and seq > self.last_seq + 1:
            self.lost += seq - self.last_seq - 1
        self.last_seq = seq
        if self.last_at is not None:
            self.max_gap = max(self.max_gap, now - self.last_at)
        self.last_at = now
        r = sample.get("r")
        if isinstance(r, int) and r < 0:
            self.rssi = r

    def snapshot(self, elapsed):
        total = self.count + self.lost
        s = {
            "hz": round(self.count / elapsed, 1),
            "rssi": self.rssi,
            "lost_pct": round(100.0 * self.lost / total, 1) if total else 0.0,
            "gap_ms": round(self.max_gap * 1000),
        }
        self.count = self.lost = 0
        self.max_gap = 0.0
        return s


def esp_reader(hub, host, port):
    """Say hello every second, forward every datagram, report link stats once a second."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    if os.name == "nt":
        # Windows raises WSAECONNRESET on the next recv after an ICMP "port
        # unreachable" (e.g. while the board is rebooting). Ask the stack to
        # ignore it; the ioctl is not exposed on every Python build, so the
        # recv loop below also treats that error as "no data" as a fallback.
        try:
            sock.ioctl(getattr(socket, "SIO_UDP_CONNRESET", 0x9800000C), False)
        except (AttributeError, OSError, ValueError):
            pass
    sock.settimeout(0.25)
    stats = LinkStats()
    last_hello = 0.0
    last_data = None
    last_report = time.monotonic()
    state = None
    recv_err_logged = False

    def set_state(new, **kw):
        nonlocal state
        if new != state:
            state = new
            print(f"[esp] {new}")
        hub.set_status(esp=new, **kw)

    set_state("connecting", hz=0.0, rssi=None, lost_pct=0.0, gap_ms=0)
    while True:
        now = time.monotonic()
        if now - last_hello >= HELLO_PERIOD_S:
            last_hello = now
            try:
                sock.sendto(b"hello", (host, port))
            except OSError as e:
                print(f"[esp] hello failed: {e}")
        try:
            data, _ = sock.recvfrom(2048)
        except socket.timeout:
            if last_data is not None:
                age = now - last_data
                if age >= LOST_S:
                    last_data = None
                    stats.reset()
                    set_state("connecting", hz=0.0, rssi=None, lost_pct=0.0, gap_ms=0)
                elif age >= STALL_S:
                    set_state("stalled", hz=0.0, gap_ms=round(age * 1000))
            continue
        except OSError as e:  # ICMP "port unreachable" while the board is offline (Windows)
            if not recv_err_logged:  # once per outage, not every 100 ms
                print(f"[esp] recv error: {e}")
                recv_err_logged = True
            time.sleep(0.1)
            continue

        now = time.monotonic()
        line = data.strip()
        if not (line.startswith(b"{") and line.endswith(b"}")):
            continue
        try:
            text = line.decode("ascii")
            sample = json.loads(text)
        except (UnicodeDecodeError, ValueError):
            continue
        if last_data is None:
            print(f"[esp] receiving from {host}:{port}")
            last_report = now
        last_data = now
        recv_err_logged = False
        stats.observe(sample, now)
        hub.publish(("imu", text))
        if now - last_report >= 1.0:
            set_state("connected", **stats.snapshot(now - last_report))
            last_report = now
        elif state != "connected":
            set_state("connected")


class Handler(SimpleHTTPRequestHandler):
    hub = None  # injected in main()
    # ES modules need a JavaScript MIME type; don't let a stray Windows registry entry change it.
    extensions_map = {**SimpleHTTPRequestHandler.extensions_map, ".js": "text/javascript", ".html": "text/html"}

    def __init__(self, *a, **kw):
        super().__init__(*a, directory=WEB_DIR, **kw)

    def log_message(self, fmt, *args):  # keep the console quiet
        if getattr(self, "path", None) in ("/", "/stream"):
            super().log_message(fmt, *args)

    def do_GET(self):
        if self.path != "/stream":
            return super().do_GET()

        try:
            self.connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        except OSError:
            pass
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        self.close_connection = True  # this handler never returns to the request loop

        q = self.hub.subscribe()
        try:
            self.wfile.write(b"retry: 500\n\n")
            self._sse("status", json.dumps(self.hub.status))
            while True:
                try:
                    event, data = q.get(timeout=1.0)
                except queue.Empty:
                    self.wfile.write(b": keepalive\n\n")  # SSE comment, keeps the socket warm
                    self.wfile.flush()
                    continue
                self._sse(event, data)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass
        finally:
            self.hub.unsubscribe(q)

    def _sse(self, event, data):
        self.wfile.write(f"event: {event}\ndata: {data}\n\n".encode())
        self.wfile.flush()


class Server(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = os.name != "nt"  # on Windows SO_REUSEADDR would let two instances share the port


def main():
    ap = argparse.ArgumentParser(description="AtomS3R-M12 IMU bridge (ESP32 UDP -> browser SSE)")
    ap.add_argument("board_ip", help="IP printed by the board on USB serial, e.g. 10.20.30.40")
    ap.add_argument("--port", type=int, default=8000, help="HTTP port for the web UI (default 8000)")
    ap.add_argument("--host", default="127.0.0.1", help="HTTP bind address (use 0.0.0.0 to open the UI to your LAN)")
    ap.add_argument("--esp-port", type=int, default=STREAM_PORT, help="board UDP port (default 5555)")
    ap.add_argument("--no-browser", action="store_true", help="do not auto-open the web UI")
    args = ap.parse_args()
    sys.stdout.reconfigure(line_buffering=True)  # logs show up promptly even when redirected

    hub = Hub()
    Handler.hub = hub
    threading.Thread(target=esp_reader, args=(hub, args.board_ip, args.esp_port), daemon=True).start()

    try:
        httpd = Server((args.host, args.port), Handler)
    except OSError as e:
        sys.exit(f"[web] cannot listen on {args.host}:{args.port} ({e}) - is another server.py running? try --port 8080")
    url = f"http://localhost:{args.port}"
    print(f"[web] serving {os.path.abspath(WEB_DIR)} at {url}")
    if not args.no_browser:
        threading.Timer(0.5, webbrowser.open, args=(url,)).start()
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n[web] bye")
        sys.exit(0)


if __name__ == "__main__":
    main()
