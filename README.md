# Hardware/Software Co-Design for Canny Edge Detection Accelerator on Zynq-7000 SoC

A fully pipelined Canny Edge Detection IP core designed in Vivado HLS, integrated into a Zynq-7000 SoC system via AXI4-Stream/AXI DMA, with a Python-based verification framework using OpenCV as a golden-reference model.

## Author
- Dinh Xuan Kinh — Ho Chi Minh City University of Technology and Education (HCMUTE), Computer Engineering
  ([GitHub](https://github.com/113azprokinh-debug))

## Deliverables

| Deliverable                                  | Location                       |
| --------------------------------------------- | ------------------------------- |
| HLS implementation (hardware)                 | [hls/](hls)                     |
| Vivado block design (IP Integrator)            | [vivado/](vivado)               |
| Bare-metal firmware (Vitis/SDK)                | [sdk/](sdk)                      |
| Python verification framework (golden model)   | [python/](python)               |
| Reports (synthesis, timing, power, utilization)| [reports/](reports)             |

## Index
- [Setup](#setup)
- [Algorithm](#algorithm)
- [Hardware Architecture](#hardware-architecture)
  - [HLS Pipeline Stages](#hls-pipeline-stages)
  - [System Integration](#system-integration)
- [Implementation Results](#implementation-results)
  - [Resource Utilization](#resource-utilization)
  - [Timing Closure](#timing-closure)
  - [Power](#power)
- [Software Verification Framework](#software-verification-framework)
  - [Results](#results)
- [Limitations and Future Work](#limitations-and-future-work)

---

# Setup
To set up the Vivado HLS project and IP Integrator block design, refer to [hls/hls-setup.README.md](hls/hls-setup.README.md).
To set up the bare-metal application in Vitis/SDK, refer to [sdk/sdk-setup.README.md](sdk/sdk-setup.README.md).
For the Python verification framework, refer to [python/python.README.md](python/python.README.md).

> **Note on hardware deployment:** this project reached full bitstream generation with positive timing closure (see [Implementation Results](#implementation-results)), but was not deployed on a physical board due to hardware availability constraints. All accuracy results were obtained through pre-silicon HW/SW co-simulation against a software golden model.

---

# Algorithm
The Canny edge detection algorithm implemented in this core follows four hardware stages (the classical 5-stage algorithm, with double-threshold and hysteresis merged into a single hardware stage for pipeline efficiency):

- **Grayscale conversion + Gaussian Blur** — Converts the input frame to grayscale and applies a 3×3 Gaussian kernel to suppress noise before gradient computation.
- **Sobel Gradient** — Computes horizontal and vertical intensity gradients and classifies the gradient direction into 4 angle bins.
- **Non-Maximum Suppression (NMS)** — Thins detected edges down to single-pixel width by suppressing non-maximal gradient responses along the gradient direction.
- **Double Threshold + Hysteresis** — Classifies pixels as strong/weak/non-edge, then promotes weak edges connected to strong edges in their 8-neighborhood.

<!-- IMAGE: insert a block diagram of the 4-stage pipeline here, e.g. pictures/canny-pipeline.png -->

---

# Hardware Architecture

## HLS Pipeline Stages
Each stage is implemented as a separate HLS function connected by `hls::stream` FIFOs, with `#pragma HLS DATAFLOW` enabling all four stages to execute concurrently on consecutive frames. Each stage uses a 2-row line buffer and a 3×3 sliding window built directly from BRAM, avoiding floating-point and division operations entirely — all arithmetic is fixed-point/integer to meet the 10 ns clock target.

<!-- IMAGE: insert the line-buffer / sliding-window diagram here -->

The core exposes:
- An **AXI4-Stream** slave/master pair (`stream_in`, `stream_out`) for pixel-level video transfer.
- An **AXI4-Lite** control bus (`s_axi_CTRL_BUS`) for runtime configuration of `rows`, `cols`, `low_thresh`, and `high_thresh` from the ARM Cortex-A9.

| Stage                  | Source file                  |
| ----------------------- | ----------------------------- |
| Gaussian Blur            | [hls/sobel.cpp](hls/sobel.cpp) — `gaussian_filter` |
| Sobel Gradient           | [hls/sobel.cpp](hls/sobel.cpp) — `sobel_gradient`  |
| Non-Maximum Suppression  | [hls/sobel.cpp](hls/sobel.cpp) — `nms_filter`      |
| Double Threshold + Hysteresis | [hls/sobel.cpp](hls/sobel.cpp) — `hysteresis_threshold` |
| Testbench                | [hls/tb_sobel.cpp](hls/tb_sobel.cpp) |

## System Integration
The HLS IP core (`canny_core_0`) is integrated into a Zynq-7000 SoC design in Vivado IP Integrator alongside:
- **AXI DMA** (`axi_dma_0`) — handles scatter-gather transfer of frame data between DDR memory and the core's AXI4-Stream ports.
- **ZYNQ7 Processing System** — ARM Cortex-A9 PS, providing the `M_AXI_GP0` master for AXI4-Lite control and `S_AXI_HP0` slave for high-performance DMA memory access.
- **AXI SmartConnect / AXI Interconnect** — routes control and data buses between PS and PL.
- **Processor System Reset** — generates synchronized reset signals across the design.

<!-- IMAGE: insert the Vivado block design diagram here -->

---

# Implementation Results

## Resource Utilization

HLS Synthesis estimate (target device `xc7z020clg484-1`):

| Resource | Used | Available | Utilization (%) |
| -------- | ---- | --------- | ---------------- |
| BRAM_18K | 19   | 280       | 6                 |
| DSP48E   | 16   | 220       | 7                 |
| FF       | 3653 | 106400    | 3                 |
| LUT      | 6740 | 53200     | 12                |

Post-Implementation (full SoC design, place & route):

| Resource        | Used | Available | Utilization (%) |
| ---------------- | ---- | --------- | ---------------- |
| Slice LUTs        | 7673 | 53200     | 14.4              |
| Block RAM Tile    | 10   | 140       | 7.1               |
| DSPs              | 16   | 220       | 7.3               |
| Slice Registers   | 8740 | 106400    | 8.2               |

## Timing Closure
The implemented design closed timing with **positive slack** on the target clock:

| Metric                       | Value     |
| ----------------------------- | --------- |
| Worst Negative Slack (WNS)    | +0.067 ns |
| Worst Hold Slack (WHS)        | +0.017 ns |
| Total Negative Slack (TNS)    | 0.000 ns  |
| Worst Pulse Width Slack (WPWS)| 3.750 ns  |

Bitstream generation completed successfully.

## Power
On-chip power estimate from the implemented netlist:

| Domain  | Power (W) | Share |
| -------- | --------- | ----- |
| PS7      | 1.533     | 94%   |
| Clocks   | 0.031     | 2%    |
| Signals  | 0.021     | 1%    |
| Logic    | 0.018     | 1%    |
| BRAM     | 0.011     | 1%    |
| DSP      | 0.009     | 1%    |
| **Total dynamic** | **1.624** | **92%** |
| Device static | 0.145 | 8% |
| **Total on-chip power** | **1.768 W** | |

<!-- IMAGE: insert synthesis/timing/power/utilization report screenshots here -->

---

# Software Verification Framework
Since the design was not deployed on physical hardware, accuracy was validated through a Python-based HW/SW co-simulation framework using OpenCV's Canny implementation as a golden reference model. For each test image, the framework computes Precision, Recall, F1-Score, PSNR, MSE, and SSIM between the software golden output and the simulated hardware-accelerator output, and renders a pixel-level error map (white = match, red = miss, blue = false positive).

Source: [python/verify_accuracy.py](python/verify_accuracy.py)

## Results

Evaluated on three real-world datasets to test robustness across different edge characteristics:

| Dataset       | Precision | Recall | F1-Score | PSNR (dB) | MSE     | SSIM (%) | FP Rate |
| -------------- | --------- | ------ | -------- | --------- | ------- | -------- | ------- |
| Concrete Crack | 96.4%     | 91.9%  | 94.1%    | 6.14      | 15808.0 | 40.5%    | 1.4%    |
| Chest X-Ray    | 92.7%     | 67.1%  | 77.9%    | 10.76     | 5458.5  | 63.8%    | 0.5%    |
| PCB Defect     | 70.3%     | 98.6%  | 82.1%    | 20.17     | 625.5   | 96.5%    | 0.4%    |

<!-- IMAGE: insert the verification report figure here (input / golden model / hardware output / error map) -->

---

# Limitations and Future Work

- **No physical board deployment.** The bitstream was generated and timing-closed, but end-to-end validation on a Pynq/Zynq board with live video input was not performed due to hardware availability.
- **Fixed double-threshold.** Performance degrades on images with smooth gradients and low contrast (e.g. Chest X-Ray: Recall 67.1%), since a static threshold cannot adapt to local contrast the way adaptive or learning-based edge detectors can. A future iteration could explore adaptive thresholding based on local histogram statistics computed in the PS.
- **Single-resolution validation.** Resource and timing reports were generated for the synthesizable design; co-simulation was performed in software rather than on real-time video input through an HDMI/MIPI front-end.
