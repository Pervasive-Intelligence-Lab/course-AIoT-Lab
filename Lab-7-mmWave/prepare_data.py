"""Rebuild both bundled ADC subsets from the pinned upstream MATLAB file.

Usage: python prepare_data.py [--source /path/to/pms1000_30fs.mat]
The normal notebook is offline; this script is only for provenance/rebuilding.
"""

import argparse
import hashlib
from pathlib import Path
import ssl
from urllib.request import urlopen

import certifi
import numpy as np
from scipy.io import loadmat

COMMIT = "c49a101b8ffe75f79abfa75b46b926c8592c7e48"
URL = (
    "https://raw.githubusercontent.com/Xiangyu-Gao/"
    "mmWave-radar-signal-processing-and-microDoppler-classification/"
    f"{COMMIT}/template%20data/pms1000_30fs.mat"
)
SHA256 = "fac5697907a6098b486c9cfafe20f6c9710a0c54d49dbad34e3fd5a215e2a9ed"
DATA = Path(__file__).resolve().parent / "data" / "uw"


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def extract_frame(raw, frame_index=0):
    """[RX, concatenated time] -> [loop, TX, RX, ADC sample].

    MATLAB source slices 128 consecutive columns per transmitted chirp,
    then separates odd/even chirps. NumPy C-order reshape below expresses
    precisely that indexing; it is not a generic MATLAB order='F' reshape.
    """
    rx, loops, tx, samples, frames = 4, 255, 2, 128, 30
    width = loops * tx * samples
    if raw.shape != (rx, frames * width) or not np.iscomplexobj(raw):
        raise ValueError(f"Unexpected upstream ADC array: {raw.shape}, {raw.dtype}")
    if not 0 <= frame_index < frames:
        raise ValueError("frame_index must be in [0, 29]")
    frame = raw[:, frame_index * width : (frame_index + 1) * width]
    cube = frame.reshape(rx, loops, tx, samples).transpose(1, 2, 0, 3)
    if not np.isfinite(cube).all():
        raise ValueError("Non-finite ADC samples")
    # Integer-valued I/Q samples here are exactly representable in complex64.
    compact = cube.astype(np.complex64)
    if not np.array_equal(compact, cube):
        raise ValueError("complex64 conversion would lose ADC precision")
    return compact.copy()


def extract_stft_sequence(raw):
    """Keep TX0/RX0 from all 30 frames as [frame, loop, ADC sample]."""
    # Preserve frame boundaries and raw complex samples for offline STFT lessons.
    return np.stack([extract_frame(raw, i)[:, 0, 0, :] for i in range(30)])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, help="Use an already downloaded .mat")
    args = parser.parse_args()
    DATA.mkdir(parents=True, exist_ok=True)
    source = args.source or DATA / "pms1000_30fs.mat"
    if args.source is None and not source.exists():
        temporary = source.with_suffix(".mat.tmp")
        try:
            context = ssl.create_default_context(cafile=certifi.where())
            print(f"Downloading 16.8 MB from {URL}")
            with urlopen(URL, timeout=60, context=context) as response, temporary.open("wb") as out:
                for chunk in iter(lambda: response.read(1024 * 1024), b""):
                    out.write(chunk)
            if sha256(temporary) != SHA256:
                raise ValueError("Download SHA-256 mismatch; no data was extracted")
            temporary.replace(source)
        finally:
            temporary.unlink(missing_ok=True)
    if sha256(source) != SHA256:
        raise ValueError("Source SHA-256 mismatch; use the pinned upstream file")
    raw = loadmat(source, variable_names=["data_frames"])["data_frames"]
    cube = extract_frame(raw)
    destination = DATA / "pms1000_frame000.npz"
    np.savez_compressed(destination, adc=cube, frame_index=np.int64(0))
    print(f"Saved {destination.name}: {cube.shape}, {cube.dtype}, {destination.stat().st_size:,} bytes")
    print("ADC payload SHA-256:", hashlib.sha256(cube.tobytes(order="C")).hexdigest())
    sequence = extract_stft_sequence(raw)
    sequence_path = DATA / "pms1000_tx0_rx0_30frames.npz"
    np.savez_compressed(sequence_path, adc=sequence, frame_indices=np.arange(30, dtype=np.int64))
    print(f"Saved {sequence_path.name}: {sequence.shape}, {sequence.dtype}, {sequence_path.stat().st_size:,} bytes")
    print("STFT ADC payload SHA-256:", hashlib.sha256(sequence.astype("<c8").tobytes(order="C")).hexdigest())


if __name__ == "__main__":
    main()
