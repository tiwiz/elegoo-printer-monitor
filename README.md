# ESP32 Printer Monitor

A companion display for Elegoo 3D printers using ESP32-C3 and LVGL. Features a BuddyLab-style UI with animated expressions on a round display.

## Hardware

- ESP32-C3 Super Mini
- 1.28" Round TFT GC9A01

## Wiring

| GC9A01 | ESP32-C3 |
|--------|----------|
| SCL    | GPIO6    |
| SDA    | GPIO7    |
| CS     | GPIO10   |
| DC     | GPIO4    |
| RES    | GPIO5    |
| BL     | GPIO8    |
| VCC    | 3.3V     |
| GND    | GND      |

## Instructions

### First-Time Setup

1. **Power on the device** - On first boot, the device will create an open WiFi access point.

2. **Connect to the device** - Using your phone or computer, connect to the WiFi network named **ElegooMonitor** (no password required).

3. **Open the configuration page** - Open a web browser and navigate to:
   ```
   http://192.168.4.1
   ```

4. **Configure your settings**:
   - **WiFi Network**: Enter your home WiFi network name (SSID)
   - **Password**: Enter your WiFi password
   - **Printer Type**: Select your Elegoo printer model
   - **Printer IP Address**: Enter your printer's IP address
   - **Printer Port**: Leave as 8888 (default)

5. **Save configuration** - Click "Save & Connect"

6. **Device restarts** - The device will save your settings and restart in normal mode, connecting to your WiFi network and printer.

### Supported Printers

- Elegoo Neptune 4
- Elegoo Neptune 4 Pro
- Elegoo Neptune 4 Max
- Elegoo Centauri Carbon
- Elegoo Centauri Carbon 2
- Elegoo OrangeStorm Giga
- Generic (Moonraker protocol)

### Finding Your Printer's IP Address

1. On your printer's display, go to Settings > Network > WiFi
2. Look for "IP Address" or "IP" - it will be something like `192.168.1.100`

### Reconfiguring

To reset and reconfigure the device:

1. Press and hold the reset button for 10 seconds
2. The device will erase saved settings and restart in AP mode
3. Follow the First-Time Setup steps again

### Building the Firmware

```bash
cd esp32-printer-monitor
idf.py set-target esp32c3
idf.py menuconfig
idf.py build
idf.py flash
idf.py monitor
```

## Features

- Round display with animated face expressions
- Progress arc showing print completion
- Bed and nozzle temperature display
- Estimated time remaining
- WiFi status indicator
- Access point configuration portal (open network)
- Settings stored in NVS flash

## UI States

| Printer State | Display |
|---------------|---------|
| Idle | Neutral face, "IDLE" |
| Printing | Working face, progress % |
| Preheating | Neutral face, "HEAT" |
| Exception | Error face, "ERR" |
| Connected | Green WiFi indicator |
| Disconnected | Gray WiFi indicator |

## Protocol

Uses the Elegoo Link protocol for communication:
- HTTP for status queries
- WebSocket for real-time updates
