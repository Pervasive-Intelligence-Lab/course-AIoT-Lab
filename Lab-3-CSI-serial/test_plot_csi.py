"""Protocol and serial-stream regression checks; no board or GUI required."""
import csv
import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import numpy as np

from plot_csi import HEADER, LineBuffer, Reader, demo_line, parse_packet


def record(values, invalid=0, length=None):
    stream = io.StringIO()
    csv.writer(stream).writerow([
        "CSI_DATA", 12, 0xFFFFFFFF, "aa:bb:cc:dd:ee:ff", -52, -95, 6,
        1, 0, 0, len(values) if length is None else length, invalid,
        json.dumps(values),
    ])
    return stream.getvalue().strip()


class ProtocolTests(unittest.TestCase):
    def test_signed_values_and_imaginary_real_order(self):
        p = parse_packet(record([3, 4, -128, 127]), arrived=1.5)
        real, imag, amplitude, phase = p.components()
        np.testing.assert_array_equal(real, [4, 127])
        np.testing.assert_array_equal(imag, [3, -128])
        self.assertEqual(amplitude[0], 5)
        self.assertAlmostEqual(phase[0], np.arctan2(3, 4))
        self.assertGreater(amplitude[1], 180)  # No int8 overflow during magnitude math.
        self.assertEqual(p.timestamp_us, 0xFFFFFFFF)

    def test_invalid_prefix_keeps_indices_and_raw_bytes(self):
        p = parse_packet(record([1, 2, 3, 4, 6, 8, 0, 0], invalid=1))
        _, _, amplitude, phase = p.components()
        self.assertTrue(np.isnan(amplitude[:2]).all())
        self.assertEqual(amplitude[2], 10)
        self.assertTrue(np.isnan(phase[3]))
        np.testing.assert_array_equal(p.raw, [1, 2, 3, 4, 6, 8, 0, 0])

    def test_metadata_and_logs(self):
        for line in (HEADER, "# connected", "ESP-ROM boot", ""):
            self.assertIsNone(parse_packet(line))
        for line in (record([1, 2], length=4), record([1]), record([128, 0]),
                     record([1.5, 0]), record([True, 0]), record([1, 2], invalid=2),
                     record([0] * 642), 'CSI_DATA,1,"broken'):
            with self.subTest(line=line[:60]), self.assertRaises(ValueError):
                parse_packet(line)

    def test_partial_reads_timeouts_and_oversized_lines(self):
        framer = LineBuffer()
        line = record([1, 2]).encode()
        self.assertEqual(list(framer.feed(line[:20])), [])
        self.assertEqual(list(framer.feed(b"")), [])
        self.assertEqual(list(framer.feed(line[20:] + b"\r\n# ok\n")),
                         [line.decode(), "# ok"])
        self.assertEqual(list(framer.feed(b"x" * 9000)), [])
        self.assertEqual(list(framer.feed(b"tail\n" + line + b"\n")), [line.decode()])
        self.assertEqual(framer.oversized, 1)

    def test_gui_overflow_does_not_drop_recording(self):
        reader = Reader("unused", 921600)
        output = io.StringIO()
        for i in range(1030):
            reader.accept(demo_line(i), output)
        self.assertEqual(reader.received, 1030)
        self.assertEqual(reader.plot_drops, 6)
        self.assertEqual(reader.packets.get_nowait().seq, 6)
        self.assertEqual(len(output.getvalue().splitlines()), 1030)

    def test_serial_reader_to_csv_with_fragmented_input(self):
        reader = Reader("fake-port", 921600)
        wire = ("# connected\n" + record([-2, 3]) + "\n" + record([4, 5]) + "\n").encode()

        class FakeSerial:
            in_waiting = 4096
            closed = False
            chunks = [wire[:35], b"", wire[35:120], wire[120:]]

            def read(self, _):
                if self.chunks:
                    return self.chunks.pop(0)
                reader.stop.set()
                return b""

            def close(self):
                self.closed = True

        connection = FakeSerial()
        with tempfile.TemporaryDirectory() as folder:
            reader.output = Path(folder) / "capture.csv"
            with patch("plot_csi.serial.Serial", return_value=connection):
                reader.start()
                reader.join(timeout=3)
            self.assertFalse(reader.is_alive())
            self.assertFalse(reader.error)
            with reader.output.open() as f:
                rows = list(csv.DictReader(f))
            self.assertEqual(len(rows), 2)
            self.assertEqual(rows[0]["data"], "[-2, 3]")
        self.assertTrue(connection.closed)
        self.assertEqual(reader.received, 2)


if __name__ == "__main__":
    unittest.main()
