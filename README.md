# TianshangPulse

> **Edge AI smartwatch firmware on ESP32-P4.** Forms a fully offline health monitoring ecosystem with [TianshangHealth](https://github.com/Tianshang301/TianshangHealth) (Android APP).
>
> **简体中文**: [docs/README.zh-CN.md](docs/README.zh-CN.md)

## Highlights

- **On-device AI**: 7-parameter logistic regression on RR-interval features (val AUC 0.928) with IMU + PPG motion gating, zero cloud dependency
- **Sensors**: MAX30102 (PPG heart rate / SpO2) + MPU6886 (accelerometer / gyroscope)
- **Connectivity**: BLE GATT Server — real-time heart rate / SpO2 / anomaly notifications, offline event caching with batch upload
- **UI**: LVGL with customizable theme

## Tech Stack

| Category | Technology |
|----------|------------|
| Platform | ESP32-P4 (400MHz dual-core RISC-V, 16MB PSRAM) |
| Framework | ESP-IDF v5.4+ (C/C++) + FreeRTOS |
| Inference | Logistic regression (7 params, hardcoded) + motion gating; TensorFlow Lite Micro reserved for future deep models |
| UI | LVGL v9 |
| BLE | NimBLE GATT Server |

## Repository Layout

```
firmware/                     # ESP-IDF project
├── main/
│   ├── config.h              # Key constants (arena / buffers / MTU / sample rates)
│   ├── main.c                # Entry point + sensor/inference tasks
│   ├── platform/             # Target abstraction (ESP32-P4 / ESP32-S3 constants)
│   ├── tflite/               # Inference engine (init/run/deinit, PSRAM arena)
│   ├── sensors/              # Unified sensor interface + MAX30102 + MPU6886
│   ├── ble/                  # GATT Server + offline event cache
│   ├── ui/                   # LVGL UI
│   └── power/                # Power management (active / light-sleep / standby)
├── sdkconfig.defaults        # Common config (FreeRTOS / BLE / I2C port)
├── sdkconfig.defaults.esp32p4   # P4: PSRAM 120M, 16MB flash
├── sdkconfig.defaults.esp32s3   # S3: OPI PSRAM 80M, 16MB flash, custom partition
├── partitions_8mb.csv        # Custom 8MB partition table (esp32s3, legacy N8R8)
├── partitions_16mb.csv       # Custom 16MB partition table (esp32s3, N16R8)
└── main/idf_component.yml    # Component dependencies (lvgl, esp-tflite-micro, esp_lvgl_port, esp_lcd_ili9341)
```

Other directories: `docs/` (protocol / hardware / power / model / memory layout), `scripts/` (model conversion & verification), `model/`, `data/`, `hardware/`

Hardware-prep handoff state (16MB flash + ILI9341 display driver, verified on host/compile only): [`docs/SYNC_HARDWARE_PREP.md`](docs/SYNC_HARDWARE_PREP.md)

## Build

### Prerequisites

- [ESP-IDF v5.4](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/)
- Target chip — one of:
  - **ESP32-P4** (16MB flash + 16MB PSRAM)
  - **ESP32-S3 N16R8** (16MB flash + 8MB octal PSRAM) — recommended starting point

### First build

```bash
# Load ESP-IDF environment (PowerShell)
. $env:IDF_PATH\export.ps1

cd firmware

# ESP32-S3 (default development target)
idf.py set-target esp32s3

# or ESP32-P4
# idf.py set-target esp32p4

idf.py build
```

On the first build, the ESP Component Manager automatically fetches the `lvgl`, `esp-tflite-micro`, `esp_lvgl_port` and `esp_lcd_ili9341` components.

The SDK picks the matching `sdkconfig.defaults.<TARGET>` automatically. Target-specific constants live in `firmware/main/platform/`.

### Flash & monitor

```bash
idf.py flash monitor
```

> Note: `esp32s3` uses a custom 8MB partition table (`partitions_8mb.csv`).
> When switching targets, run `idf.py fullclean` once and delete `sdkconfig` if
> you hit a target mismatch error.

## Memory Strategy

- Internal SRAM (768KB): hot-path data, PPG buffers, task stacks
- PSRAM (16MB): TFLite arena (1MB), model weights, offline event cache
- Detailed layout: `docs/MEMORY_LAYOUT.md`

## Power Targets

| Mode | Target Current |
|------|----------------|
| Active monitoring | ≤ 30mA |
| Standby | ≤ 500μA |

Detailed budget: `docs/POWER_BUDGET.md`

## Documentation

- `docs/README.zh-CN.md` — 简体中文文档 (Simplified Chinese)
- `docs/PROTOCOL.md` — BLE GATT protocol (Source of Truth)
- `docs/HARDWARE.md` — hardware design & pin allocation
- `docs/MODEL_ARCH.md` — model architecture & quantization pipeline
- `docs/BENCHMARK.md` — external validation benchmark (beat detection / motion robustness)
- `docs/MEMORY_LAYOUT.md` — memory layout
- `docs/POWER_BUDGET.md` — power budget
- `AGENTS.md` — AI-assisted development guidelines

## License

MIT License