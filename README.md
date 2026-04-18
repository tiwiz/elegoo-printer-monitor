# ESP32 Printer Monitor

A companion display for Elegoo 3D printers using the ESP32-Cheap-Yellow-Display (CYD). Features a BuddyLab-style UI with animated expressions.

## Hardware

**ESP32-Cheap-Yellow-Display (CYD)**
- ESP32 (WiFi + Bluetooth)
- 320 x 240 2.8" LCD Display
- Resistive Touch Screen
- USB-C for power/programming
- SD Card Slot
- RGB LED

More info: [ESP32-Cheap-Yellow-Display](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) by @witnessmenow

## Wiring

No wiring required - all components are built into the CYD!

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

### Building the Firmware

**Requirements:**
- [PlatformIO](https://platformio.org/) (or use VS Code with PlatformIO extension)

**Build and Upload:**

```bash
# Clone the repository
git clone <your-repo-url>
cd esp32-printer-monitor

# Install dependencies and build
pio run

# Upload to device (connect via USB)
pio run --target upload

# View serial output
pio device monitor
```

**Or with VS Code:**
1. Install PlatformIO extension
2. Open the project folder
3. Click "Upload" in the bottom toolbar

### Finding Your Printer's IP Address

1. On your printer's display, go to Settings > Network > WiFi
2. Look for "IP Address" or "IP" - it will be something like `192.168.1.100`

### Reconfiguring

To reset and reconfigure the device:

1. Press and hold the BOOT button while powering on the device
2. The device will enter configuration mode
3. Follow the First-Time Setup steps again

## Supported Printers

Based on the Elegoo Link protocol:
- Elegoo Neptune 4
- Elegoo Neptune 4 Pro
- Elegoo Neptune 4 Max
- Elegoo Centauri Carbon
- Elegoo Centauri Carbon 2
- Elegoo OrangeStorm Giga
- Generic (Moonraker protocol)

## Features

- 2.8" LCD display with animated face expressions
- Progress arc showing print completion
- Bed and nozzle temperature display
- Estimated time remaining
- WiFi status indicator
- Access point configuration portal (open network)
- Settings stored in NVS flash
- Touch screen ready for future UI interactions

## UI States

| Printer State | Display |
|---------------|---------|
| Idle | Neutral face, "IDLE" |
| Printing | Working face, progress % |
| Preheating | Working face, "HEAT" |
| Error | Error face, "ERR" |
| Offline | Gray face, "Offline" |

## Protocol

Uses the Elegoo Link protocol for communication via HTTP:
- Status queries to `/api/v1/printer`
- JSON response parsing

## Display Configuration

The CYD uses the following pins (built-in):

| Feature | Pin |
|---------|-----|
| TFT DC | GPIO 2 |
| TFT MISO | GPIO 12 |
| TFT MOSI | GPIO 13 |
| TFT SCK | GPIO 14 |
| TFT CS | GPIO 15 |
| TFT Backlight | GPIO 21 |
| Touch CLK | GPIO 25 |
| Touch MOSI | GPIO 32 |
| Touch CS | GPIO 33 |
| Touch IRQ | GPIO 36 |
| Touch MISO | GPIO 39 |

## Additional resources

- [ESP32-Cheap-Yellow-Display](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display)
- [Elegoo Link protocol](https://github.com/elegoo/Elegoo-Link-Protocol)
- [CYD Projects](https://github.com/bitbank2/CYD_Projects)

## License

MIT License
