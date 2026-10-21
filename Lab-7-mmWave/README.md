# Lab 7 — mmWave Radar Signal Processing: Spectra and STFT

A **classroom lab in two standalone English notebooks**. Students run one cell at a time to explore **Range Spectrum → Doppler Spectrum → Angular Spectrum** with simulated and measured signals, then use **STFT** to examine measured Doppler content over time.

| Notebook | Data |
|---|---|
| [1. Simulation](lab7_mmwave_simulation.ipynb) | Locally generated signals with known target parameters |
| [2. Real Radar Data](lab7_mmwave_real_data.ipynb) | One real ADC/IQ frame for three spectra, plus a 30-frame TX0/RX0 subset for STFT |

Each notebook runs from a fresh kernel and defines its own imports, helpers, and variables.
The simulation does not require the measured-data files; the real-data notebook does not require running the simulation.
Saved outputs let you preview both notebooks on GitHub.

## Audience and lesson plan

Designed for CSCI 4900/6900. Students should know basic Python/NumPy arrays, sinusoids, and complex numbers; the role of the FFT is explained during the lesson. No radar board, MATLAB, or GPU is required.

Install the dependencies **before class**. Start with I/Q and the data-cube axes, then work through
Range, Doppler, and Angular Spectra in the simulation. Apply the same sequence to the bundled real recording,
then explore a time–Doppler spectrogram with STFT. Discuss the estimates, static clutter removal, and the window-length tradeoff.

In Notebook 1, stop at **Core checkpoint** and switch to Notebook 2.
In Notebook 2, complete the **Exit ticket**, then finish with Section 7, which uses range tracking and Doppler integration to measure how far the person moves in one second. Each stage asks students to predict, run, and interpret.
Core cells appear before the optional appendices and do not depend on them.

**Optional after class:** windowing/zero-padding demonstrations, antenna-aperture comparisons,
the simulated range–angle map, parameter sweeps, and velocity aliasing.
These materials are retained in the simulation notebook's appendices with instructor answers.

## Install dependencies and open the notebooks

The repository provides the tutorial, example data, and `requirements.txt`.
Use your existing Python 3.11 or later installation, including an existing Conda environment if you already use one.
A dedicated virtual environment is optional.
Installing dependencies requires an internet connection; both notebooks run offline afterward.

### macOS / Linux

From the repository root:

```bash
cd Lab-7-mmWave
python3 -m pip install -r requirements.txt
python3 -m jupyter lab lab7_mmwave_simulation.ipynb lab7_mmwave_real_data.ipynb
```

### Windows PowerShell

From the repository root:

```powershell
cd Lab-7-mmWave
py -m pip install -r requirements.txt
py -m jupyter lab lab7_mmwave_simulation.ipynb lab7_mmwave_real_data.ipynb
```

For the remaining `python ...` commands, use the same interpreter: `python3` on macOS/Linux or `py` on Windows.

### Select the Python kernel and check it

In VS Code, open each notebook, click **Select Kernel → Python Environments**, and select the Python installation where you installed `requirements.txt`.
If an import such as `numpy` fails, check that the notebook's kernel matches the Python used for installation.

Before class, use **Restart Kernel and Run All Cells** separately in each notebook to check the setup, including optional cells.
During class, follow only the core sections and their stop markers.
PNG figures are written to the git-ignored `figures/simulation/` and `figures/real_data/` directories.
After changing an earlier parameter, rerun every cell that depends on it.

The repository's root `.gitignore` excludes `.venv/`, other common environment directories, and Python/Jupyter caches.
Keep `requirements.txt`, both notebooks, scripts, the illustrations in `assets/`, and the small example files in `data/uw/` when committing to GitHub.
The downloaded full `.mat` file and exported PNGs are also ignored; embedded notebook figures are retained for GitHub previews.

To rerun both notebooks independently from the command line and save outputs:

```bash
python -m jupyter nbconvert --to notebook --execute --inplace \
  --ExecutePreprocessor.timeout=180 lab7_mmwave_simulation.ipynb
python -m jupyter nbconvert --to notebook --execute --inplace \
  --ExecutePreprocessor.timeout=180 lab7_mmwave_real_data.ipynb
```

## Example data

The bundled real measurements come from Xiangyu Gao et al.'s [mmWave radar signal processing repository](https://github.com/Xiangyu-Gao/mmWave-radar-signal-processing-and-microDoppler-classification), accompanying [Experiments with mmWave Automotive Radar Test-bed](https://arxiv.org/abs/1912.12566).

* Original file: `pms1000_30fs.mat`, 30 frames, approximately 16.8 MB.
* Classroom subset: the first frame, `data/uw/pms1000_frame000.npz`, approximately **632 KB**.
* STFT subset: `data/uw/pms1000_tx0_rx0_30frames.npz`, approximately **2.3 MB**; raw TX0/RX0 samples from all 30 frames, shaped `[frame, loop, ADC sample] = [30, 255, 128]`.
* Scene photo: `data/uw/pms1000.jpg`, supplied alongside the upstream example; it shows a person in a parking lot, without a documented mapping to frame 0.
* First-frame contents: complex ADC/IQ with shape `[255 loops, 2 TX, 4 RX, 128 samples]`, rather than point clouds or precomputed spectra.
* Extraction: frame selection, axis rearrangement, and a lossless dtype conversion. No windowing, filtering, or normalization.
* Reproducibility: a pinned upstream commit, source-file and array SHA-256 hashes, configuration, and the complete MIT license are included.

For an explanation of loops, transmitters, receivers, and samples, see [the four data dimensions](data/uw/README.md#what-do-the-four-dimensions-mean).
See the [data provenance and rebuilding instructions](data/uw/README.md) for the source and extraction details. Both subsets work offline. To rebuild both:

```bash
python prepare_data.py
```

Notebook 1 generates explicitly labeled **simulated data** with known target parameters.
Notebook 2 uses **real measurements** without independent target ground truth, so its outputs are not an accuracy evaluation.
The range/Doppler/angle pipeline uses four RX channels from one TX. The STFT lesson uses TX0/RX0 over all 30 frames.

## STFT lesson

**Short-Time Fourier Transform (STFT)** means sliding a short window along a signal and computing an FFT at each position.
A **spectrogram** displays the resulting power against time and frequency; radar Doppler frequency can be converted to radial velocity.
Section 5 of the real-data notebook explains window length, hop, overlap, and the time–frequency tradeoff, then compares 64- and 128-chirp Hann windows on measured data.
It includes a visible NumPy implementation, line-by-line code comments, interpretation prompts, and saved plots.

The one-second time axis uses the paper's nominal **30 fps** because frame timestamps are unavailable.
Every STFT window stays within one frame. Gray intervals indicate no evaluated full-window centers, including frame edges and gaps.
The notebook computes Range FFTs from raw samples, preserves complex phase for STFT, and averages power over a fixed range gate.

## Files

```text
Lab-7-mmWave/
├── README.md
├── lab7_mmwave_simulation.ipynb         # Core lesson + optional experiments
├── lab7_mmwave_real_data.ipynb          # Recorded-data processing + review
├── requirements.txt
├── prepare_data.py                     # Download, verify, and rebuild both real-data subsets
├── verify_lab.py                       # Check indexing, integrity, and spectral coordinates
├── assets/                             # Hardware photos, chip diagram, configuration table, workflow, and credits
└── data/uw/
    ├── pms1000_frame000.npz
    ├── pms1000_tx0_rx0_30frames.npz      # Raw single-channel sequence for STFT
    ├── pms1000.jpg                     # Upstream scene photo; no frame synchronization supplied
    ├── metadata.json
    ├── README.md
    └── LICENSE.upstream.txt
```

## Instructor checks

```bash
python verify_lab.py
# Optional: also compare the subset directly with the original MATLAB file
python verify_lab.py --source /path/to/pms1000_30fs.mat
```

The simulation notebook checks target estimates in its core and velocity aliasing in an appendix.
The real-data notebook checks both bundled arrays' shapes and SHA-256 hashes before processing.
`verify_lab.py` also checks STFT signed-frequency peaks, normalization, frame boundaries, and agreement with SciPy.
Emphasize these distinctions during class:

* **FFT bin spacing and resolution:** zero-padding creates a denser grid; it does not add bandwidth, observation time, or aperture.
* **Power plots and complex intermediate arrays:** use `abs()` for display; preserve phase for subsequent FFTs.
* **Spectral peaks and target detection:** selecting a peak does not perform CFAR, classification, clustering, or tracking.
* **Coordinate conventions and physical directions:** measured velocity signs and angles follow the source conventions; the subset provides no independent direction verification.

Further reading: [TI mmWave training](https://www.ti.com/video/series/mmwave-training-series.html) and [TI SWRA553A](https://www.ti.com/lit/an/swra553a/swra553a.pdf).
