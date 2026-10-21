# Image credits

The real-data tutorial reproduces Figures 1, 2, and 4 and Table I from
Xiangyu Gao, Guanbin Xing, Sumit Roy, and Hui Liu,
[*Experiments with mmWave Automotive Radar Test-bed*](https://arxiv.org/html/1912.12566v4#S1.F1)
(arXiv:1912.12566v4).

| Local file | Source | Subject |
|---|---|---|
| `uw_radar_testbed.jpg` | [platform.jpg](https://arxiv.org/html/1912.12566v4/figure/platform.jpg) | Radar and capture boards |
| `uw_vehicle_platform.png` | [mount_car.png](https://arxiv.org/html/1912.12566v4/figure/mount_car.png) | Vehicle-mounted collection platform |
| `uw_awr1642_block_diagram.png` | [Figure 2](https://arxiv.org/html/1912.12566v4#S3.F2), PDF page 2 | Chip architecture and receiver chains; the paper credits Texas Instruments |
| `uw_fmcw_configuration.png` | [Table I](https://arxiv.org/html/1912.12566v4#S5.T1), PDF page 3 | Published FMCW configuration |
| `uw_processing_workflow.png` | [Figure 4](https://arxiv.org/html/1912.12566v4#S4.F4), PDF page 3 | Range, velocity, and angle processing workflow |

The paper is distributed under
[Creative Commons Attribution-NonCommercial-ShareAlike 4.0 license](https://creativecommons.org/licenses/by-nc-sa/4.0/),
separately from the upstream dataset repository's MIT license.
The chip diagram retains the paper's attribution to Texas Instruments.
The two Figure 1 photographs are unchanged downloads, displayed side by side.
The other three images are cropped page renders from the [version 4 PDF](https://arxiv.org/pdf/1912.12566v4),
with the original diagram/table content preserved on a white background.
The notebook scales their display size and adds separate explanatory captions.

The files are stored locally so the illustration works offline and can be included on GitHub.

## Dataset scene photograph

The additional image [`../data/uw/pms1000.jpg`](../data/uw/pms1000.jpg) comes from
[`template data/pms1000.jpg`](https://github.com/Xiangyu-Gao/mmWave-radar-signal-processing-and-microDoppler-classification/blob/c49a101b8ffe75f79abfa75b46b926c8592c7e48/template%20data/pms1000.jpg)
in Xiangyu Gao et al.'s repository, at the same pinned commit as the radar example.
It is an unchanged copy covered by the repository's [MIT license](../data/uw/LICENSE.upstream.txt).
This scene photograph is separate from the paper illustrations listed above.
The upstream files do not specify a mapping from the photograph to an individual radar frame.
