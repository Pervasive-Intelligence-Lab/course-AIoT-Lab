#!/usr/bin/env python3
"""Plot the CSV stream from AtomS3R_CSI_STA.ino (Python 3.10+)."""

import argparse
from collections import deque
import csv
from dataclasses import dataclass
import io
import json
from pathlib import Path
import queue
import sys
import threading
import time

import numpy as np
import serial
from serial.tools import list_ports

HEADER = "type,seq,timestamp_us,mac,rssi,noise_floor,channel,sig_mode,mcs,cwb,len,first_word_invalid,data"
MAX_CSI_BYTES = 640  # Matches the firmware; never truncate a record.


@dataclass
class Packet:
    seq: int
    timestamp_us: int
    mac: str
    rssi: int
    channel: int
    raw: np.ndarray
    first_invalid: bool
    arrived: float

    def components(self):
        # Firmware order is imaginary, real. Keep original pair indices.
        imag = self.raw[0::2].astype(float)
        real = self.raw[1::2].astype(float)
        if self.first_invalid:
            imag[:2] = np.nan
            real[:2] = np.nan
        amplitude = np.hypot(real, imag)
        phase = np.arctan2(imag, real)
        phase[amplitude == 0] = np.nan  # Zero magnitude has no defined phase.
        return real, imag, amplitude, phase


def parse_packet(line, arrived=None):
    """Ignore logs/header. Raise ValueError for a malformed CSI record."""
    if not line.startswith("CSI_DATA,"):
        return None
    try:
        row = next(csv.reader([line], strict=True))
        if len(row) != 13:
            raise ValueError("expected 13 CSV columns")
        seq, timestamp = int(row[1]), int(row[2])
        rssi, noise, channel, mode, mcs, bandwidth, length, invalid = map(int, row[4:12])
        data = json.loads(row[12])
        if not 0 <= seq <= 0xFFFFFFFF or not 0 <= timestamp <= 0xFFFFFFFF:
            raise ValueError("invalid counter")
        if invalid not in (0, 1) or not 0 < length <= MAX_CSI_BYTES or length % 2:
            raise ValueError("invalid CSI length/flag")
        if not isinstance(data, list) or len(data) != length:
            raise ValueError("CSI array length differs from len")
        if any(type(value) is not int or not -128 <= value <= 127 for value in data):
            raise ValueError("CSI values must be signed int8")
        return Packet(seq, timestamp, row[3], rssi, channel,
                      np.asarray(data, dtype=np.int8), bool(invalid),
                      time.monotonic() if arrived is None else arrived)
    except (csv.Error, json.JSONDecodeError, TypeError, ValueError) as exc:
        raise ValueError(f"bad CSI record: {exc}") from exc


class LineBuffer:
    """Preserve partial reads across serial timeouts; bound malformed input."""

    def __init__(self):
        self.pending = b""
        self.discarding = False
        self.oversized = 0

    def feed(self, chunk):
        self.pending += chunk
        while b"\n" in self.pending:
            line, self.pending = self.pending.split(b"\n", 1)
            if self.discarding:
                self.discarding = False
            elif len(line) > 8192:
                self.oversized += 1
            else:
                yield line.decode("utf-8", errors="replace").strip()
        if len(self.pending) > 8192:
            if not self.discarding:
                self.oversized += 1
            self.pending = b""
            self.discarding = True


def demo_line(seq):
    """Synthetic CSI for UI testing; never represents a hardware measurement."""
    x = np.arange(64)
    amplitude = 38 + 15 * np.sin(x * 0.14 + seq * 0.08)
    phase = x * 0.11 + 0.8 * np.sin(seq * 0.1 + x * 0.04)
    raw = np.empty(128, dtype=int)
    raw[0::2] = np.rint(amplitude * np.sin(phase)).astype(int)
    raw[1::2] = np.rint(amplitude * np.cos(phase)).astype(int)
    stream = io.StringIO()
    csv.writer(stream).writerow(["CSI_DATA", seq, seq * 50000, "00:00:00:00:00:00",
                                -48 + int(3 * np.sin(seq * 0.1)), -95, 6, 1, 0, 0,
                                len(raw), 1, json.dumps(raw.tolist())])
    return stream.getvalue().strip()


class Reader(threading.Thread):
    """Serial and disk IO stay off the GUI thread; GUI queue is bounded."""

    def __init__(self, port, baud, output=None, demo=False):
        super().__init__(daemon=True)
        self.port, self.baud, self.output, self.demo = port, baud, output, demo
        self.stop = threading.Event()
        self.packets = queue.Queue(maxsize=1024)
        self.received = self.bad = self.plot_drops = 0
        self.status = "Starting..."
        self.error = ""
        self.board_status = ""

    def accept(self, line, recording):
        if line.startswith("#"):
            self.board_status = line[:180]
            print(line, flush=True)
        try:
            packet = parse_packet(line)
        except ValueError:
            self.bad += 1
            return
        if packet is None:
            return
        # Save every validated packet before GUI queuing. Do not change raw bytes.
        if recording:
            recording.write(line + "\n")
        self.received += 1
        try:
            self.packets.put_nowait(packet)
        except queue.Full:
            try:
                self.packets.get_nowait()
                self.plot_drops += 1
            except queue.Empty:
                pass
            self.packets.put_nowait(packet)

    def run(self):
        recording = connection = None
        try:
            if self.output:
                # Exclusive creation protects an existing recording from overwrite.
                recording = Path(self.output).open("x", encoding="utf-8", buffering=1)
                recording.write(HEADER + "\n")
            if self.demo:
                self.status = "DEMO - synthetic data"
                seq = 0
                while not self.stop.is_set():
                    self.accept(demo_line(seq), recording)
                    seq += 1
                    self.stop.wait(0.05)
            else:
                connection = serial.Serial(self.port, self.baud, timeout=0.1)
                self.status = f"Connected: {self.port} @ {self.baud}"
                print(self.status, flush=True)
                buffer = LineBuffer()
                while not self.stop.is_set():
                    chunk = connection.read(min(4096, connection.in_waiting or 1))
                    for line in buffer.feed(chunk):
                        self.accept(line, recording)
                    self.bad += buffer.oversized
                    buffer.oversized = 0
        except (OSError, serial.SerialException, ValueError) as exc:
            self.error = str(exc)
            self.status = "STOPPED: serial/recording error (see terminal)"
            print(f"Error: {exc}\nClose Arduino Serial Monitor; check the port and output path.",
                  file=sys.stderr, flush=True)
        finally:
            if connection:
                connection.close()
            if recording:
                recording.close()


class Dashboard:
    def __init__(self, history, title):
        import matplotlib.pyplot as plt

        self.history = deque(maxlen=history)
        self.origin = None
        self.fig, axes = plt.subplots(3, 2, figsize=(13, 9))
        self.fig.subplots_adjust(top=0.86, bottom=0.08, hspace=0.6, wspace=0.32)
        self.fig.suptitle(title, fontsize=17, y=0.98)
        self.status = self.fig.text(0.07, 0.90, "Waiting for CSI...", fontsize=9)
        self.amp_ax, self.phase_ax, self.amp_history_ax, self.phase_history_ax, self.iq_ax, self.rssi_ax = axes.flat
        self.amp_line, = self.amp_ax.plot([], [], color="#007f9e", lw=1.5)
        self.phase_line, = self.phase_ax.plot([], [], color="#a44a3f", lw=1.2)
        self.real_line, = self.iq_ax.plot([], [], label="Real (I)", lw=1.2)
        self.imag_line, = self.iq_ax.plot([], [], label="Imaginary (Q)", lw=1.2)
        self.iq_ax.legend(loc="upper right", fontsize=8)
        self.rssi_line, = self.rssi_ax.plot([], [], color="#507c30")
        for ax, name, ylabel in [
            (self.amp_ax, "Latest packet - all CSI pairs", "Amplitude (raw units)"),
            (self.phase_ax, "Latest packet - raw phase", "Phase (radians)"),
            (self.iq_ax, "Latest packet - real / imaginary", "Signed int8 value"),
        ]:
            ax.set(title=name, xlabel="CSI pair index (driver order)", ylabel=ylabel)
            ax.grid(alpha=0.25)
        self.phase_ax.set_ylim(-np.pi, np.pi)
        self.iq_ax.set_ylim(-130, 130)
        self.rssi_ax.set(title="Received signal strength", xlabel="PC receive time (seconds)", ylabel="RSSI (dBm)")
        self.rssi_ax.grid(alpha=0.25)
        self.heatmaps = []
        for ax, name, cmap, limits in [
            (self.amp_history_ax, "Amplitude history", "viridis", (0, 80)),
            (self.phase_history_ax, "Phase history", "twilight", (-np.pi, np.pi)),
        ]:
            colors = plt.get_cmap(cmap).copy()
            colors.set_bad("#e8e8e8")
            img = ax.imshow(np.full((2, 64), np.nan), aspect="auto", origin="lower",
                            interpolation="nearest", cmap=colors, vmin=limits[0], vmax=limits[1])
            ax.set(title=name, xlabel="CSI pair index (driver order)", ylabel="Packets before latest")
            self.fig.colorbar(img, ax=ax, pad=0.02, fraction=0.04)
            self.heatmaps.append(img)

    def update(self, packets, status):
        self.history.extend(packets)
        if self.history:
            latest = self.history[-1]
            age = max(0, time.monotonic() - latest.arrived)
            status += f"\nseq={latest.seq} | ch={latest.channel} | MAC={latest.mac} | bytes={len(latest.raw)} | last packet {age:.1f}s ago"
        self.status.set_text(status)
        if not packets:
            return
        if self.origin is None:
            self.origin = self.history[0].arrived
        real, imag, amplitude, phase = latest.components()
        x = np.arange(len(real))
        for line, values in [(self.amp_line, amplitude), (self.phase_line, phase),
                             (self.real_line, real), (self.imag_line, imag)]:
            line.set_data(x, values)
        for ax in (self.amp_ax, self.phase_ax, self.iq_ax):
            ax.set_xlim(-0.5, max(0.5, len(x) - 0.5))
        finite = amplitude[np.isfinite(amplitude)]
        self.amp_ax.set_ylim(0, max(10, float(finite.max()) * 1.15) if len(finite) else 10)
        # Variable lengths are padded with NaN, never cropped or reindexed.
        width = max(len(p.raw) // 2 for p in self.history)
        amplitudes = np.full((len(self.history), width), np.nan)
        phases = np.full_like(amplitudes, np.nan)
        for i, packet in enumerate(self.history):
            _, _, a, p = packet.components()
            amplitudes[i, :len(a)], phases[i, :len(p)] = a, p
        extent = (-0.5, width - 0.5, -len(self.history) + 0.5, 0.5)
        for img, values in zip(self.heatmaps, (amplitudes, phases)):
            img.set_data(values)
            img.set_extent(extent)
        finite = amplitudes[np.isfinite(amplitudes)]
        self.heatmaps[0].set_clim(0, max(10, float(finite.max())) if len(finite) else 10)
        times = [p.arrived - self.origin for p in self.history]
        self.rssi_line.set_data(times, [p.rssi for p in self.history])
        self.rssi_ax.relim()
        self.rssi_ax.autoscale_view()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="e.g. /dev/cu.usbmodemXXXX, /dev/ttyACM0, or COM5")
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--list-ports", action="store_true")
    parser.add_argument("--history", type=int, default=200, help="packets retained in heatmaps (default: 200)")
    parser.add_argument("--fps", type=int, default=10, help="plot refresh rate; serial is read continuously")
    parser.add_argument("--save-csv", metavar="PATH", help="save every valid packet to a new CSV file")
    parser.add_argument("--demo", action="store_true", help="use clearly labeled synthetic data")
    parser.add_argument("--snapshot", metavar="PNG", help="with --demo, render a test image and exit")
    args = parser.parse_args()
    if args.list_ports:
        ports = list(list_ports.comports())
        for port in ports:
            print(f"{port.device}\t{port.description}")
        if not ports:
            print("No serial ports found.")
        return 0
    if not 2 <= args.history <= 5000 or not 1 <= args.fps <= 30 or args.baud <= 0:
        parser.error("require history=2..5000, fps=1..30, baud>0")
    if args.snapshot and not args.demo:
        parser.error("--snapshot requires --demo")
    if not args.demo and not args.port:
        ports = [p for p in list_ports.comports() if p.vid is not None]
        if len(ports) != 1:
            parser.error("use --list-ports, then --port PORT (no unique USB serial device)")
        args.port = ports[0].device
        print(f"Selected USB serial port: {args.port}", flush=True)
    if args.snapshot:
        import matplotlib
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.animation import FuncAnimation

    label = "DEMO - synthetic CSI" if args.demo else "AtomS3R - live Wi-Fi CSI"
    dashboard = Dashboard(args.history, label)
    if args.snapshot:
        now = time.monotonic()
        packets = [parse_packet(demo_line(i), now - (args.history - 1 - i) * 0.05)
                   for i in range(args.history)]
        dashboard.update(packets, "Synthetic data for plot verification; not a hardware capture")
        dashboard.fig.savefig(args.snapshot, dpi=140)
        plt.close(dashboard.fig)
        print(f"Saved synthetic preview: {args.snapshot}")
        return 0
    reader = Reader(args.port, args.baud, args.save_csv, args.demo)
    reader.start()

    def refresh(_):
        packets = []
        # At most one queue's worth per refresh, to keep window controls responsive.
        for _ in range(1024):
            try:
                packets.append(reader.packets.get_nowait())
            except queue.Empty:
                break
        dashboard.update(packets, f"{reader.status}\nreceived={reader.received} | malformed={reader.bad} | plot queue drops={reader.plot_drops}")

    animation = FuncAnimation(dashboard.fig, refresh, interval=1000 / args.fps, cache_frame_data=False)
    dashboard.fig.canvas.mpl_connect("close_event", lambda _: reader.stop.set())
    try:
        plt.show()
    except KeyboardInterrupt:
        plt.close(dashboard.fig)
    finally:
        reader.stop.set()
        reader.join(timeout=2)
    return 1 if reader.error else 0


if __name__ == "__main__":
    raise SystemExit(main())
