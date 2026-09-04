# TianshangPulse

> **Edge AI smartwatch firmware on ESP32-P4.** Forms a fully offline health monitoring ecosystem with [TianshangHealth](https://github.com/Tianshang301/TianshangHealth) (Android APP).
>
> **简体中文**: [docs/README.zh-CN.md](docs/README.zh-CN.md)

## Highlights

- **On-device AI**: 750K-parameter INT8 LSTM inference, latency <10ms, zero cloud dependency
- **Sensors**: MAX30102 (PPG heart rate / SpO2) + MPU6886 (accelerometer / gyroscope)
- **Connectivity**: BLE GATT Server — real-time heart rate / SpO2 / anomaly notifications, offline event caching with batch upload
- **UI**: LVGL with customizable theme

## Tech Stack

| Category | Technology |
|----------|------------|
| Platform | ESP32-P4 (400MHz dual-core RISC-V, 16MB PSRAM) |
| Framework | ESP-IDF v5.4+ (C/C++) + FreeRTOS |
| Inference | TensorFlow Lite Micro (`esp-tflite-micro`) |
| UI | LVGL v9 |
| BLE | NimBLE GATT Server |

## Repository Layout

```
firmware/                     # ESP-IDF project
├── main/
│   ├── config.h              # Key constants (arena / buffers / MTU / sample rates)
│   ├── main.c                # Entry point + sensor/inference tasks
│   ├── tflite/               # Inference engine (init/run/deinit, PSRAM arena)
│   ├── sensors/              # Unified sensor interface + MAX30102 + MPU6886
│   ├── ble/                  # GATT Server + offline event cache
│   ├── ui/                   # LVGL UI
│   └── power/                # Power management (active / light-sleep / standby)
├── sdkconfig.defaults        # Target chip / PSRAM / BLE / I2C defaults
└── main/idf_component.yml    # Component dependencies (lvgl, esp-tflite-micro)
```

Other directories: `docs/` (protocol / hardware / power / model / memory layout), `scripts/` (model conversion & verification), `model/`, `data/`, `hardware/`

## Build

### Prerequisites

- [ESP-IDF v5.4](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/get-started/)
- Chip: ESP32-P4 (16MB PSRAM variant required)

### First build

```bash
# Load ESP-IDF environment (PowerShell)
. $env:IDF_PATH\export.ps1

cd firmware
idf.py set-target esp32p4
idf.py build
```

On the first build, the ESP Component Manager automatically fetches the `lvgl` and `esp-tflite-micro` components.

### Flash & monitor

```bash
idf.py flash monitor
```

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
- `docs/MEMORY_LAYOUT.md` — memory layout
- `docs/POWER_BUDGET.md` — power budget
- `AGENTS.md` — AI-assisted development guidelines

## License

MIT License