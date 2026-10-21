"""Scientific checks for the lab. Run with Python; no hardware/network required."""

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from scipy.io import loadmat
from scipy.signal import stft

from prepare_data import extract_frame, extract_stft_sequence, sha256, SHA256

LAB = Path(__file__).resolve().parent


def check_stft():
    # Exercise the actual classroom implementation with independently known tones.
    notebook = json.loads((LAB / "lab7_mmwave_real_data.ipynb").read_text())
    cell = next(c for c in notebook["cells"] if c["id"] == "real-stft-function")
    namespace = {"np": np}
    exec(compile("".join(cell["source"]), "notebook STFT function", "exec"), namespace)
    transform = namespace["frame_stft_power"]
    dt, wavelength, length, hop, nfft = 0.001, 0.004, 64, 16, 256
    time = np.arange(255) * dt
    # Opposite velocities in adjacent frames catch accidental cross-frame mixing.
    tones = np.exp(2j * np.pi * np.array([125, -125])[:, None] * time)
    # Opposite phases in two range bins catch complex averaging before power.
    traces = np.stack((tones, -tones), axis=2)
    power, centers, velocity = transform(traces, length, hop, nfft, dt, wavelength)
    assert power.shape == (2, 256, 12)
    np.testing.assert_allclose(centers, (np.arange(12) * hop + 31.5) * dt)
    for frame, expected in enumerate([0.25, -0.25]):
        np.testing.assert_allclose(velocity[power[frame].argmax(axis=0)], expected)
        np.testing.assert_allclose(power[frame].max(axis=0), 1.0, atol=1e-12)
    # Compare all power bins with SciPy's independent STFT implementation.
    _, scipy_times, reference = stft(tones[0], fs=1 / dt, window=np.hanning(length),
                                    nperseg=length, noverlap=length-hop, nfft=nfft,
                                    return_onesided=False, boundary=None, padded=False,
                                    detrend=False, scaling="spectrum")
    np.testing.assert_allclose(power[0], abs(np.fft.fftshift(reference, axes=0))**2, atol=1e-12)
    # SciPy timestamps even windows at L/2; our sample midpoint is (L-1)/2.
    np.testing.assert_allclose(centers, scipy_times - dt / 2)
    print("PASS: STFT signed tones, amplitude, range-power averaging, frame isolation, and SciPy agreement")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, help="Optionally compare to the original .mat")
    args = parser.parse_args()
    meta = json.loads((LAB / "data/uw/metadata.json").read_text(encoding="utf-8"))
    with np.load(LAB / "data/uw" / meta["file"], allow_pickle=False) as data:
        adc = data["adc"]
        assert data["frame_index"].item() == 0
    assert adc.shape == (255, 2, 4, 128)
    assert adc.dtype == np.complex64 and np.isfinite(adc).all()
    digest = hashlib.sha256(adc.astype("<c8").tobytes(order="C")).hexdigest()
    assert digest == meta["adc_payload_sha256_little_endian_c_order"]
    print("PASS: bundled ADC shape, dtype, finite values, payload SHA-256")

    sequence_meta = meta["stft_sequence"]
    with np.load(LAB / "data/uw" / sequence_meta["file"], allow_pickle=False) as data:
        sequence = data["adc"]
        assert np.array_equal(data["frame_indices"], np.arange(30))
    assert sequence.shape == (30, 255, 128)
    assert sequence.dtype == np.complex64 and np.isfinite(sequence).all()
    assert np.array_equal(sequence[0], adc[:, 0, 0, :])
    digest = hashlib.sha256(sequence.astype("<c8").tobytes(order="C")).hexdigest()
    assert digest == sequence_meta["adc_payload_sha256_little_endian_c_order"]
    print("PASS: 30-frame STFT subset integrity and agreement with the original bundled frame")

    # Sentinel indices encode RX and absolute column independently of reshape.
    # Check both first and last frame, both TXs, and boundary samples.
    width = 255 * 2 * 128
    sentinel = (np.arange(4)[:, None] + 1j*np.arange(30*width)[None, :])
    for frame_index in (0, 29):
        frame = extract_frame(sentinel, frame_index)
        for loop in (0, 1, 254):
            for tx in (0, 1):
                for rx in (0, 3):
                    for n in (0, 127):
                        column = frame_index*width + (2*loop+tx)*128+n
                        assert frame[loop, tx, rx, n] == rx + 1j*column
    del sentinel
    print("PASS: original column -> loop/TX/RX/sample mapping at frame boundaries")

    # Analytic integer-bin complex tones expose axis, sign, and fftshift errors.
    # Unwindowed tones must place all coherent power in one 3-D FFT bin.
    M, A, N = 32, 8, 64
    m, a, n = np.ogrid[:M, :A, :N]
    for doppler_bin, spatial_bin in ((-5, 2), (5, -2)):
        tone = np.exp(2j*np.pi*(9*n/N + doppler_bin*m/M + spatial_bin*a/A))
        r = np.fft.fft(tone, axis=2)
        rd = np.fft.fftshift(np.fft.fft(r, axis=0), axes=0)
        power = np.mean(abs(rd)**2, axis=1)
        kd, kr = np.unravel_index(np.argmax(power), power.shape)
        assert kr == 9 and kd == M//2 + doppler_bin
        spatial = np.fft.fftshift(np.fft.fft(rd[kd, :, kr]))
        q = np.fft.fftshift(np.fft.fftfreq(A))
        theta = np.degrees(np.arcsin(2*q[np.argmax(abs(spatial))]))
        expected_angle = 30.0 if spatial_bin > 0 else -30.0
        assert np.isclose(theta, expected_angle)
        fd = np.fft.fftshift(np.fft.fftfreq(M, meta["same_tx_chirp_interval_s"]))[kd]
        expected_fd = doppler_bin / (M*meta["same_tx_chirp_interval_s"])
        assert np.isclose(fd, expected_fd)
        assert np.isclose(abs(spatial).max(), M*A*N)
    print("PASS: analytic 3-D FFT peaks, positive/negative Doppler, ±30 deg angles")
    check_stft()

    if args.source:
        assert sha256(args.source) == SHA256, "Upstream MATLAB checksum mismatch"
        raw = loadmat(args.source, variable_names=["data_frames"])["data_frames"]
        assert np.array_equal(adc, extract_frame(raw))
        assert np.array_equal(sequence, extract_stft_sequence(raw))
        # Direct original-array slices verify TX0/RX0 for every frame independently.
        for frame_index in range(30):
            for loop_index in (0, 127, 254):
                begin = frame_index * width + 2 * loop_index * 128
                assert np.array_equal(sequence[frame_index, loop_index], raw[0, begin:begin+128])
        # Explicit source slices provide an independent check against the reshape.
        for loop, tx in ((0, 0), (0, 1), (127, 1), (254, 0), (254, 1)):
            begin = (2*loop+tx)*128
            assert np.array_equal(adc[loop, tx], raw[:, begin:begin+128])
        print("PASS: real subset equals upstream first frame, including direct chirp slices")
        print("PASS: STFT subset equals original TX0/RX0 samples across all 30 frames")
    print("All checks passed. The simulation notebook additionally checks target estimates and aliasing.")


if __name__ == "__main__":
    main()
