# esp32-firmware

ESP-IDF firmware for an ESP32 CRSF node. It **reuses the same `crsf-core`
library** as the desktop backend — no fork, no copy.

## How the reuse works

`components/crsf-core/CMakeLists.txt` registers the source/header files from
`../../crsf-core` by relative path:

```
<repo root>/
├── crsf-core/                      <-- the one and only implementation
└── esp32-firmware/
    └── components/crsf-core/       <-- IDF component that points back at it
```

Edit `crsf-core` once; both the Linux backend and this firmware rebuild against
it. (If the firmware ever moves to its own repo, swap the relative path for a
git submodule — same files either way.)

## Layout

| File | Role |
|------|------|
| `main/app_main.cpp`   | Entry point; brings up UART, spawns the RX + telemetry tasks |
| `main/uart_driver.*`  | ESP-IDF UART wrapper (UART2, 420000 8N1) |
| `main/control.*`      | Portable control logic (no ESP deps; host-testable) |

The split mirrors the project rule: `crsf-core` and `control` are pure logic;
`uart_driver` / `app_main` are the IO + RTOS adapter.

## Wiring (defaults)

| ESP32 UART2 | CRSF |
|-------------|------|
| `RX_PIN` = GPIO16 | <- receiver/handset CRSF TX (data in) |
| `TX_PIN` = GPIO17 | -> receiver/handset CRSF RX (telemetry out) |

Adjust the `kTxPin` / `kRxPin` / `kPort` constants in `app_main.cpp`.

## Build & flash

Requires a configured ESP-IDF environment (`. $IDF_PATH/export.sh`):

```sh
cd esp32-firmware
idf.py set-target esp32      # or esp32s3, esp32c3, ...
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Tasks

- **crsf_rx_task** — reads UART bytes, feeds `crsf::CRSFParser`, dispatches
  completed packets to `Control` (RC channels + link stats, with a failsafe
  timer).
- **telemetry_task** — encodes an example battery frame via the shared
  `CRSFPacker` and writes it back upstream once a second, logging link/failsafe
  status. Replace the placeholder reading with a real sensor.
