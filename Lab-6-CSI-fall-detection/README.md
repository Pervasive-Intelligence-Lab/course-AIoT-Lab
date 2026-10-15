# Lab 6 — Practical ESP32 Wi-Fi Sensing: Fall Detection with CSI

Companion lab for **Lecture 11** (CSCI 4900/6900). One Jupyter notebook, two parts:

| Part | Content | Needs a board? |
| --- | --- | --- |
| **A. Tsinghua *Hands-on Wireless Sensing* examples** | Python port of the book's MATLAB `main.m` for Ch. 3 *CSI Features* (`naive_aoa`, `naive_tof`, `naive_spectrum`) and Ch. 4 *CSI Sanitization* (`set_template`, `nonlinear_calib`, `sto_calib_div/mul`, `cfo_calib`, `rco_calib`, `agc_calib`, `naive_intrusion`), plus the *Understanding CSI* speed model (ACF ≈ sinc(kvτ)) | No |
| **B. AtomS3R-M12 fall detection** | Parse Lab 3 CSI CSV → clean (AGC, Hampel, resample) → motion energy, wander, ACF speed → fall state machine → evaluation on 50 sessions → sample-rate experiment → random forest (leave-one-room-out) → your own recording | No (simulated capture in the Lab 3 format); yes for B11 |

## Run

```bash
cd Lab-6-CSI-fall-detection
python3 -m venv .venv
source .venv/bin/activate          # Windows: .venv\Scripts\Activate.ps1
python3 -m pip install -r requirements.txt
jupyter lab lab6_esp32_csi_fall_detection.ipynb
```

Run all cells from top to bottom (about 2–3 minutes on a laptop; Section B8 is the slowest).
The notebook writes a simulated capture to `data/esp32_sim/` and figures to `figures/` (both git-ignored).

## Use your own board

1. In [`Lab-3-CSI-serial/ino/AtomS3R_CSI_STA/AtomS3R_CSI_STA.ino`](../Lab-3-CSI-serial/ino/AtomS3R_CSI_STA/AtomS3R_CSI_STA.ino) change `PING_INTERVAL_MS` from `50` to `10` (100 packets/s). The default 20 packets/s is too slow to see the speed of a fall (notebook B9).
2. Put the AP and the AtomS3R on stands at 1.2–1.5 m, 2–4 m apart, with a mattress between them.
3. Record with Lab 3's plotter: `python3 plot_csi.py --port /dev/cu.usbmodemXXXX --save-csv captures/trial01.csv`, and write down when each activity happens.
4. In notebook Section B2 set `CSV_PATH` (and `LABELS_PATH`, or `None`), then rerun B2–B7.

**Safety:** only fall onto a mattress, and record only people who agreed.

## Files

```text
Lab-6-CSI-fall-detection/
├── lab6_esp32_csi_fall_detection.ipynb   # the lab (saved with outputs)
├── requirements.txt
└── data/tsinghua/                        # the book's test CSI (.mat, [T S A L])
    ├── csi_src_test.mat                  # 999 × 57 × 3 × 2
    ├── csi_calib_test.mat                # 200 × 57 × 3
    └── calib_template_test.mat           # reference template from set_template.m
```

## Credits

* Part A ports code and uses test data from the Tsinghua MobiSense group's *Hands-on Wireless Sensing with Wi-Fi* tutorial ([Understanding CSI](https://tns.thss.tsinghua.edu.cn/wst/docs/pre/)). The Python `set_template` reproduces the book's template to ~1e−15.
* Part B uses the CSV format of [Lab 3](../Lab-3-CSI-serial), built on Espressif [ESP-CSI](https://github.com/espressif/esp-csi); placement advice follows [ESP-CSI DIY Wi-Fi human presence detection](https://www.hackster.io/limengdu0117/esp-csi-diy-wifi-human-presence-detection-f80508).
* Simulated sessions are for teaching and are **not** hardware measurements.
