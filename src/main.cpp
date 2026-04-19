#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <nvs_flash.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <UUID.h>

#define AP_SSID "ElegooMonitor"
#define AP_IP 192, 168, 4, 1
#define AP_GATEWAY 192, 168, 4, 1
#define AP_SUBNET 255, 255, 255, 0

#define NVS_NAMESPACE "elegoo_cfg"
#define NVS_KEY_WIFI_SSID "wifi_ssid"
#define NVS_KEY_WIFI_PASS "wifi_pass"
#define NVS_KEY_PRINTER_HOST "printer_host"
#define NVS_KEY_PRINTER_PORT "printer_port"
#define NVS_KEY_PRINTER_TYPE "printer_type"
#define NVS_KEY_CONFIGURED "configured"
#define NVS_KEY_MAINBOARD_ID "mainboard_id"

#define SDCP_PORT 3030
#define SDCP_WS_PATH "/websocket"
#define SDCP_DISCOVERY_PORT 3000
#define SDCP_DISCOVERY_MSG "M99999"
#define SDCP_KEEPALIVE_INTERVAL 30000
#define SDCP_STATUS_INTERVAL 2000
#define SDCP_RECONNECT_DELAY 5000
#define SDCP_CONNECTION_TIMEOUT 10000
#define SDCP_MAX_RECONNECT_ATTEMPTS 10

TFT_eSPI tft = TFT_eSPI();
WebServer server(80);
DNSServer dnsServer;
UUID uuid;

// --- MOCK SETTINGS ---
const bool MOCK_UI_MODE = false;
const int MOCK_CURRENT_STATE = 3; // 3 = STATE_ERROR

// --- MD3 COLORS (RGB565) ---
#define MD3_BG 0x1042         // #121212
#define MD3_CARD 0x18E3       // #1E1E1E
#define MD3_TEXT 0xFFFF       // #FFFFFF
#define MD3_TEXT_SEC 0xC658   // #CACACA
#define MD3_PRIMARY 0xD5F9    // #D0BCFF
#define MD3_ACCENT 0x3E68     // #38A169

enum PrinterType {
    PRINTER_NEPTUNE4,
    PRINTER_NEPTUNE4PRO,
    PRINTER_NEPTUNE4MAX,
    PRINTER_CENTAURI_CARBON,
    PRINTER_CENTAURI_CARBON2,
    PRINTER_ORANGESTORM_GIGA,
    PRINTER_GENERIC
};

enum PrinterState {
    STATE_IDLE,
    STATE_PRINTING,
    STATE_PREHEATING,
    STATE_ERROR,
    STATE_PAUSED,
    STATE_OFFLINE,
    STATE_CONNECTING
};

struct Config {
    String wifi_ssid;
    String wifi_password;
    String printer_host;
    int printer_port = SDCP_PORT;
    PrinterType printer_type = PRINTER_NEPTUNE4;
    bool configured = false;
};

struct PrinterStatus {
    PrinterState state = STATE_OFFLINE;
    int progress = 0;
    float bed_temp = 0;
    float bed_target = 0;
    float nozzle_temp = 0;
    float nozzle_target = 0;
    float enclosure_temp = 0;
    float enclosure_target = 0;
    String file_name;
    int estimated_time = 0;
    int print_duration = 0;
    int current_layer = 0;
    int total_layers = 0;
    String current_action = "Idle";
    String mainboard_id;
    String printer_name;
    unsigned long last_update = 0;
};

struct SDCPMessage {
    String id;
    String cmd;
    JsonObject data;
    String request_id;
    String mainboard_id;
    unsigned long timestamp;
    int from;
    String topic;
};

Config config;
PrinterStatus printerStatus;
PrinterStatus cachedStatus;
bool wifiConnected = false;
bool printerConnected = false;
unsigned long lastStatusUpdate = 0;
unsigned long lastKeepalive = 0;
unsigned long lastUIDraw = 0;
int reconnectAttempts = 0;

WebSocketsClient sdcpClient;
bool sdcpConnected = false;
bool awaitingResponse = false;
unsigned long lastCommandTime = 0;

enum UIState {
    UI_CONFIG,
    UI_CONNECTING,
    UI_MONITOR
};

UIState currentState = UI_CONFIG;

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Elegoo Printer Monitor - Setup</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%);
            min-height: 100vh;
            display: flex;
            justify-content: center;
            align-items: center;
            padding: 20px;
        }
        .container {
            background: #0f0f23;
            border-radius: 20px;
            padding: 40px;
            width: 100%;
            max-width: 500px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.5);
        }
        h1 { color: #fff; text-align: center; margin-bottom: 10px; font-size: 24px; }
        .subtitle { color: #94a3b8; text-align: center; margin-bottom: 30px; font-size: 14px; }
        .section { background: #1a1a2e; border-radius: 12px; padding: 20px; margin-bottom: 20px; }
        .section-title { color: #3b82f6; font-size: 12px; text-transform: uppercase; letter-spacing: 1px; margin-bottom: 15px; }
        label { display: block; color: #94a3b8; font-size: 12px; margin-bottom: 6px; }
        input, select {
            width: 100%; padding: 12px 15px; background: #0f0f23; border: 1px solid #2d2d44;
            border-radius: 8px; color: #fff; font-size: 14px; margin-bottom: 12px; transition: border-color 0.3s;
        }
        input:focus, select:focus { outline: none; border-color: #3b82f6; }
        input::placeholder { color: #6b7280; }
        button {
            width: 100%; padding: 15px; background: #3b82f6; color: #fff; border: none;
            border-radius: 10px; font-size: 16px; font-weight: 600; cursor: pointer; transition: background 0.3s;
        }
        button:hover { background: #2563eb; }
        button:disabled { background: #4b5563; cursor: not-allowed; }
        .spinner {
            display: inline-block; width: 20px; height: 20px; border: 2px solid #fff;
            border-radius: 50%; border-top-color: transparent; animation: spin 1s linear infinite;
            margin-right: 8px; vertical-align: middle;
        }
        @keyframes spin { to { transform: rotate(360deg); } }
        .message { padding: 12px; border-radius: 8px; margin-top: 15px; text-align: center; font-size: 14px; display: none; }
        .message.success { background: #065f46; color: #6ee7b7; }
        .message.error { background: #7f1d1d; color: #fca5a5; }
        .scan-list { max-height: 150px; overflow-y: auto; border: 1px solid #2d2d44; border-radius: 8px; margin-bottom: 10px; }
        .scan-item { padding: 10px 15px; color: #fff; cursor: pointer; border-bottom: 1px solid #2d2d44; }
        .scan-item:hover { background: #2d2d44; }
        .scan-item.selected { background: #3b82f6; }
        .scan-item:last-child { border-bottom: none; }
        .scan-btn {
            width: 100%; padding: 10px; background: #374151; color: #fff; border: none;
            border-radius: 8px; cursor: pointer; margin-bottom: 10px;
        }
        .scan-btn:hover { background: #4b5563; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Elegoo Printer Monitor</h1>
        <p class="subtitle">Configure your device</p>

        <div class="section">
            <div class="section-title">WiFi Network</div>
            <button class="scan-btn" onclick="scanNetworks()">Scan for Networks</button>
            <div class="scan-list" id="scanList"></div>
            <label for="ssid">Network Name (SSID)</label>
            <input type="text" id="ssid" placeholder="Enter WiFi network name" maxlength="31">
            <label for="password">Password</label>
            <input type="password" id="password" placeholder="Enter WiFi password" maxlength="63">
        </div>

        <div class="section">
            <div class="section-title">Printer Settings</div>
            <label for="printerType">Printer Type</label>
            <select id="printerType">
                <option value="neptune4">Elegoo Neptune 4</option>
                <option value="neptune4pro">Elegoo Neptune 4 Pro</option>
                <option value="neptune4max">Elegoo Neptune 4 Max</option>
                <option value="centauri_carbon">Elegoo Centauri Carbon</option>
                <option value="centauri_carbon2">Elegoo Centauri Carbon 2</option>
                <option value="orangestorm_giga">Elegoo OrangeStorm Giga</option>
                <option value="generic">Generic (Moonraker)</option>
            </select>
            <label for="printerHost">Printer IP Address</label>
            <input type="text" id="printerHost" placeholder="192.168.1.100 (Optional - Auto Discovery)">
            <label for="printerPort">Printer Port</label>
            <input type="number" id="printerPort" placeholder="3030" value="3030" min="1" max="65535">
        </div>

        <button onclick="saveConfig()" id="saveBtn">Save & Connect</button>
        <div class="message" id="message"></div>
    </div>

    <script>
        let scanResults = [];

        async function scanNetworks() {
            const btn = document.querySelector('.scan-btn');
            btn.textContent = 'Scanning...';
            btn.disabled = true;
            try {
                const res = await fetch('/scan');
                const data = await res.json();
                scanResults = data.networks || [];
                displayScanResults();
            } catch (e) { showMessage('Scan failed: ' + e.message, 'error'); }
            btn.textContent = 'Scan for Networks';
            btn.disabled = false;
        }

        function displayScanResults() {
            const list = document.getElementById('scanList');
            if (scanResults.length === 0) {
                list.innerHTML = '<div class="scan-item">No networks found</div>';
                return;
            }
            list.innerHTML = scanResults.map((n, i) =>
                '<div class="scan-item" onclick="selectNetwork(' + i + ')">' + n.ssid + ' (' + n.rssi + ' dBm)</div>'
            ).join('');
        }

        function selectNetwork(index) {
            document.getElementById('ssid').value = scanResults[index].ssid;
            document.querySelectorAll('.scan-item').forEach((el, i) => el.classList.toggle('selected', i === index));
        }

        async function saveConfig() {
            const btn = document.getElementById('saveBtn');
            const ssid = document.getElementById('ssid').value.trim();
            const password = document.getElementById('password').value;
            const printerType = document.getElementById('printerType').value;
            const printerHost = document.getElementById('printerHost').value.trim();
            const printerPort = parseInt(document.getElementById('printerPort').value) || 3030;

            if (!ssid) { showMessage('Please enter or select a network', 'error'); return; }

            btn.disabled = true;
            btn.innerHTML = '<span class="spinner"></span>Saving...';
            try {
                const res = await fetch('/save', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ ssid, password, printerType, printerHost, printerPort })
                });
                if (res.ok) {
                    showMessage('Configuration saved! Device will restart...', 'success');
                    btn.textContent = 'Saved!';
                } else { throw new Error('Server error'); }
            } catch (e) { showMessage('Error: ' + e.message, 'error'); btn.textContent = 'Save & Connect'; btn.disabled = false; }
        }

        function showMessage(text, type) {
            const msg = document.getElementById('message');
            msg.textContent = text;
            msg.className = 'message ' + type;
            msg.style.display = 'block';
        }
    </script>
</body>
</html>
)rawliteral";

void drawBaseEyes(int cx, int cy, int scale) {
    tft.fillCircle(cx - 30 * scale, cy - 20 * scale, 15 * scale, TFT_WHITE);
    tft.fillCircle(cx + 30 * scale, cy - 20 * scale, 15 * scale, TFT_WHITE);
}

void drawSadFace(int cx, int cy, int scale) {
    drawBaseEyes(cx, cy, scale);
    // Sad mouth arc
    tft.drawArc(cx, cy + 25 * scale, 20 * scale, 15 * scale, 135, 225, MD3_TEXT_SEC, MD3_BG, true);
}

void drawHappyFace(int cx, int cy, int scale) {
    drawBaseEyes(cx, cy, scale);
    // Draw a smile arc (using two segments to safely bridge 0 degree, 300->360 and 0->60)
    tft.drawArc(cx, cy - 5 * scale, 32 * scale, 24 * scale, 300, 360, MD3_ACCENT, MD3_BG, true);
    tft.drawArc(cx, cy - 5 * scale, 32 * scale, 24 * scale, 0, 60, MD3_ACCENT, MD3_BG, true);
}

void drawCryingFace(int cx, int cy, int scale) {
    drawBaseEyes(cx, cy, scale);
    // Tears
    tft.fillCircle(cx - 30 * scale, cy + 8 * scale, 4 * scale, 0x55BF); // Soft Blue
    tft.fillCircle(cx + 30 * scale, cy + 8 * scale, 4 * scale, 0x55BF);
    // Sad mouth arc
    tft.drawArc(cx, cy + 35 * scale, 25 * scale, 18 * scale, 120, 240, 0xF248, MD3_BG, true); // MD3 Error Red-ish
}

void drawThinkingFace(int cx, int cy, int scale) {
    drawBaseEyes(cx, cy, scale);
    // Mouth: a flat line or small round ellipse to look "thinking"
    tft.fillRoundRect(cx - 15 * scale, cy + 15 * scale, 30 * scale, 6 * scale, 3 * scale, MD3_TEXT_SEC);
}

void drawConfigScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.drawCentreString("Setup Required", 160, 20, 1);
    drawBaseEyes(160, 90, 1);
    tft.fillRect(140, 110, 40, 6, TFT_DARKGREY);
    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY);
    tft.drawCentreString("Connect to WiFi", 160, 150, 1);
    tft.drawCentreString("\"ElegooMonitor\"", 160, 165, 1);
    tft.drawCentreString("Then open browser", 160, 180, 1);
    tft.drawCentreString("to http://192.168.4.1", 160, 195, 1);
}

void drawConnectingScreen(const String& line1, const String& line2) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE);
    tft.drawCentreString(line1, 160, 100, 1);
    if (line2.length() > 0) {
        tft.setTextColor(TFT_LIGHTGREY);
        tft.setTextSize(1);
        tft.drawCentreString(line2, 160, 130, 1);
    }
}

void drawStateOffline() {
    tft.fillScreen(MD3_BG);
    drawSadFace(160, 80, 1.5);
    tft.setTextSize(2);
    tft.setTextColor(MD3_TEXT_SEC);
    tft.drawCentreString("Printer is", 160, 160, 1);
    tft.setTextColor(MD3_TEXT);
    tft.drawCentreString("offline", 160, 185, 1);
}

void drawStateConnecting() {
    tft.fillScreen(MD3_BG);
    drawThinkingFace(160, 80, 1.5);
    tft.setTextSize(2);
    tft.setTextColor(MD3_TEXT);
    tft.drawCentreString("Connecting...", 160, 160, 1);
    if (config.printer_host.length() > 0) {
        tft.setTextSize(1);
        tft.setTextColor(MD3_TEXT_SEC);
        tft.drawCentreString(config.printer_host, 160, 185, 1);
    }
}

static bool lastShowBed = false;
static float lastTempC = -1;

void drawStateIdle(bool fullRedraw) {
    bool showBed = (millis() / 5000) % 2 == 0;
    float currentTempC = showBed ? printerStatus.bed_temp : printerStatus.nozzle_temp;
    
    if (fullRedraw) {
        tft.fillScreen(MD3_BG);
        drawHappyFace(160, 80, 1);
        
        int rectX = 20, rectY = 150, rectW = 280, rectH = 70;
        tft.fillRoundRect(rectX, rectY, rectW, rectH, 16, MD3_CARD);
        
        lastShowBed = !showBed; // force text update
        lastTempC = -1;
    }
    
    if (showBed != lastShowBed || currentTempC != lastTempC) {
        lastShowBed = showBed;
        lastTempC = currentTempC;
        
        int rectY = 150;
        tft.setTextSize(2);
        tft.setTextColor(MD3_TEXT, MD3_CARD);
        tft.setTextPadding(260); // clear any previous text
        
        if (showBed) {
            int tempC = (int)printerStatus.bed_temp;
            int tempF = (int)(printerStatus.bed_temp * 9.0/5.0 + 32.0);
            tft.drawCentreString("Bed Temperature", 160, rectY + 15, 1);
            tft.setTextColor(MD3_TEXT_SEC, MD3_CARD);
            tft.drawCentreString(String(tempC) + " C  /  " + String(tempF) + " F", 160, rectY + 40, 1);
        } else {
            int tempC = (int)printerStatus.nozzle_temp;
            int tempF = (int)(printerStatus.nozzle_temp * 9.0/5.0 + 32.0);
            tft.drawCentreString("Nozzle Temperature", 160, rectY + 15, 1);
            tft.setTextColor(MD3_TEXT_SEC, MD3_CARD);
            tft.drawCentreString(String(tempC) + " C  /  " + String(tempF) + " F", 160, rectY + 40, 1);
        }
        tft.setTextPadding(0);
    }
}

static int lastProgress = -1;
static unsigned long lastPrintDuration = 0xFFFFFFFF;
static String lastAction = "";

void drawStatePrinting(bool fullRedraw) {
    if (fullRedraw) {
        tft.fillScreen(MD3_BG);
        drawHappyFace(160, 80, 1);
        
        // Progress bar background (Pill shape)
        int barX = 15, barY = 150, barW = 220, barH = 24;
        tft.fillRoundRect(barX, barY, barW, barH, barH/2, MD3_CARD);
        
        // Bottom info card
        tft.fillRoundRect(20, 185, 280, 45, 12, MD3_CARD);
        
        lastProgress = -1;
        lastPrintDuration = 0xFFFFFFFF;
        lastAction = "";
    }

    if (printerStatus.current_action != lastAction) {
        tft.setTextSize(1);
        tft.setTextColor(MD3_TEXT_SEC, MD3_BG);
        tft.setTextPadding(280);
        tft.drawCentreString(printerStatus.current_action, 160, 122, 1);
        tft.setTextPadding(0);
        lastAction = printerStatus.current_action;
    }

    // Update progress bar
    if (printerStatus.progress != lastProgress) {
        int barX = 15, barY = 150, barW = 220, barH = 24;
        int fillW = (printerStatus.progress * barW) / 100;
        if (fillW < barH) fillW = barH; // Ensure rounded ends
        
        tft.fillRoundRect(barX, barY, fillW, barH, barH/2, MD3_PRIMARY);
        
        // Progress text outside
        tft.setTextSize(2);
        tft.setTextColor(MD3_PRIMARY, MD3_BG); 
        tft.setTextPadding(80); 
        tft.drawRightString(String(printerStatus.progress) + "%", 310, barY + 4, 1);
        tft.setTextPadding(0);
        
        lastProgress = printerStatus.progress;
    }

    // Update time and file info
    if (printerStatus.print_duration != lastPrintDuration) {
        tft.setTextSize(1);
        tft.setTextColor(MD3_TEXT_SEC, MD3_CARD);
        tft.setTextPadding(260);

        int e_hours = printerStatus.print_duration / 3600;
        int e_mins = (printerStatus.print_duration % 3600) / 60;
        int eta_hours = printerStatus.estimated_time / 3600;
        int eta_mins = (printerStatus.estimated_time % 3600) / 60;

        char buf[64];
        snprintf(buf, sizeof(buf), "Elapsed: %d:%02d  |  ETA: %d:%02d", e_hours, e_mins, eta_hours, eta_mins);
        tft.drawCentreString(buf, 160, 192, 1);

        String title = printerStatus.file_name;
        if (title.length() > 30) title = title.substring(0, 27) + "...";
        tft.drawCentreString(title, 160, 210, 1);
        
        tft.setTextPadding(0);
        lastPrintDuration = printerStatus.print_duration;
    }
}

void drawStatePaused(bool fullRedraw) {
    if (fullRedraw) {
        tft.fillScreen(MD3_BG);
        drawThinkingFace(160, 80, 1);
        
        // Progress bar background (Pill shape)
        int barX = 20, barY = 150, barW = 280, barH = 24;
        tft.fillRoundRect(barX, barY, barW, barH, barH/2, MD3_CARD);
        
        // Bottom info card
        tft.fillRoundRect(20, 185, 280, 45, 12, MD3_CARD);
        
        lastProgress = -1;
        lastPrintDuration = 0xFFFFFFFF;
    }
    
    // Update progress bar (Amber/Orange for paused)
    if (printerStatus.progress != lastProgress) {
        int barX = 20, barY = 150, barW = 280, barH = 24;
        int fillW = (printerStatus.progress * barW) / 100;
        if (fillW < barH) fillW = barH;
        
        tft.fillRoundRect(barX, barY, fillW, barH, barH/2, 0xFD20); // Amber/Orange
        
        tft.setTextSize(1);
        tft.setTextColor(MD3_BG);
        tft.drawCentreString(String(printerStatus.progress) + "% (Paused)", barX + (barW/2), barY + 4, 1);
        
        lastProgress = printerStatus.progress;
    }

    if (printerStatus.print_duration != lastPrintDuration) {
        tft.setTextSize(1);
        tft.setTextColor(MD3_TEXT_SEC, MD3_CARD);
        tft.setTextPadding(260);
        tft.drawCentreString("Print status: PAUSED", 160, 192, 1);
        
        String title = printerStatus.file_name;
        if (title.length() > 30) title = title.substring(0, 27) + "...";
        tft.drawCentreString(title, 160, 210, 1);
        
        tft.setTextPadding(0);
        lastPrintDuration = printerStatus.print_duration;
    }
}

void drawStateError(bool fullRedraw) {
    if (fullRedraw) {
        tft.fillScreen(MD3_BG);
        drawCryingFace(160, 80, 1);
        
        // Large Error Card (Red background in MD3)
        int rectX = 20, rectY = 150, rectW = 280, rectH = 70;
        tft.fillRoundRect(rectX, rectY, rectW, rectH, 16, 0x9000); // Darker Red for MD3 context
        
        tft.setTextSize(2);
        tft.setTextColor(0xFFFF); // White
        tft.drawCentreString("CRITICAL ERROR", 160, rectY + 15, 1);
        
        tft.setTextSize(1);
        tft.setTextColor(0xFD08); // Light red/pinkish
        tft.drawCentreString("Check the printer hardware", 160, rectY + 45, 1);
    }
}

static PrinterState lastStateDrawn = (PrinterState)-1;

void drawCurrentState() {
    bool forceFullRedraw = false;
    if (lastStateDrawn != printerStatus.state) {
        forceFullRedraw = true;
        lastStateDrawn = printerStatus.state;
    }

    switch (printerStatus.state) {
        case STATE_OFFLINE:
            if (forceFullRedraw) drawStateOffline();
            break;
        case STATE_CONNECTING:
            if (forceFullRedraw) drawStateConnecting();
            break;
        case STATE_IDLE:
        case STATE_PREHEATING:
            drawStateIdle(forceFullRedraw);
            break;
        case STATE_PRINTING:
            drawStatePrinting(forceFullRedraw);
            break;
        case STATE_PAUSED:
            drawStatePaused(forceFullRedraw);
            break;
        case STATE_ERROR:
            drawStateError(forceFullRedraw);
            break;
    }
}

void saveConfigToNVS(const Config& cfg) {
    nvs_handle_t nvs;
    nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    nvs_set_str(nvs, NVS_KEY_WIFI_SSID, cfg.wifi_ssid.c_str());
    nvs_set_str(nvs, NVS_KEY_WIFI_PASS, cfg.wifi_password.c_str());
    nvs_set_str(nvs, NVS_KEY_PRINTER_HOST, cfg.printer_host.c_str());
    nvs_set_i32(nvs, NVS_KEY_PRINTER_PORT, cfg.printer_port);
    nvs_set_i32(nvs, NVS_KEY_PRINTER_TYPE, cfg.printer_type);
    nvs_set_str(nvs, NVS_KEY_MAINBOARD_ID, printerStatus.mainboard_id.c_str());
    nvs_set_i8(nvs, NVS_KEY_CONFIGURED, 1);
    nvs_commit(nvs);
    nvs_close(nvs);
}

bool loadConfigFromNVS(Config& cfg) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) return false;

    char buffer[64];
    size_t len = sizeof(buffer);

    if (nvs_get_str(nvs, NVS_KEY_WIFI_SSID, buffer, &len) == ESP_OK) {
        cfg.wifi_ssid = buffer;
    } else {
        nvs_close(nvs);
        return false;
    }

    len = sizeof(buffer);
    if (nvs_get_str(nvs, NVS_KEY_WIFI_PASS, buffer, &len) == ESP_OK) {
        cfg.wifi_password = buffer;
    }

    len = sizeof(buffer);
    if (nvs_get_str(nvs, NVS_KEY_PRINTER_HOST, buffer, &len) == ESP_OK) {
        cfg.printer_host = buffer;
    }

    nvs_get_i32(nvs, NVS_KEY_PRINTER_PORT, (int32_t*)&cfg.printer_port);
    nvs_get_i32(nvs, NVS_KEY_PRINTER_TYPE, (int32_t*)&cfg.printer_type);

    len = sizeof(buffer);
    if (nvs_get_str(nvs, NVS_KEY_MAINBOARD_ID, buffer, &len) == ESP_OK) {
        cachedStatus.mainboard_id = buffer;
    }

    int8_t configured = 0;
    nvs_get_i8(nvs, NVS_KEY_CONFIGURED, &configured);
    cfg.configured = (configured == 1);

    nvs_close(nvs);
    return cfg.configured;
}

PrinterType parsePrinterType(const String& str) {
    if (str == "neptune4pro") return PRINTER_NEPTUNE4PRO;
    if (str == "neptune4max") return PRINTER_NEPTUNE4MAX;
    if (str == "centauri_carbon") return PRINTER_CENTAURI_CARBON;
    if (str == "centauri_carbon2") return PRINTER_CENTAURI_CARBON2;
    if (str == "orangestorm_giga") return PRINTER_ORANGESTORM_GIGA;
    if (str == "generic") return PRINTER_GENERIC;
    return PRINTER_NEPTUNE4;
}

void handleRoot() {
    server.send_P(200, "text/html", INDEX_HTML);
}

void handleScan() {
    int n = WiFi.scanNetworks();
    String json = "{\"networks\":[";
    for (int i = 0; i < n; ++i) {
        if (i > 0) json += ",";
        json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    json += "]}";
    WiFi.scanDelete();
    server.send(200, "application/json", json);
}

void handleSave() {
    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "Bad Request");
        return;
    }

    String body = server.arg("plain");
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body);
    if (error) {
        server.send(400, "text/plain", "JSON Error");
        return;
    }

    config.wifi_ssid = doc["ssid"].as<String>();
    config.wifi_password = doc["password"].as<String>();
    config.printer_host = doc["printerHost"].as<String>();
    config.printer_port = doc["printerPort"].as<int>();
    config.printer_type = parsePrinterType(doc["printerType"].as<String>());
    config.configured = true;

    saveConfigToNVS(config);

    server.send(200, "application/json", "{\"success\":true}");

    delay(1000);
    ESP.restart();
}

void setupAP() {
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(AP_IP), IPAddress(AP_IP), IPAddress(AP_SUBNET));
    WiFi.softAP(AP_SSID);

    dnsServer.start(53, "*", IPAddress(AP_IP));

    server.on("/", handleRoot);
    server.on("/scan", handleScan);
    server.on("/save", HTTP_POST, handleSave);
    server.begin();
}

void connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(config.wifi_ssid.c_str(), config.wifi_password.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        attempts++;
    }

    wifiConnected = (WiFi.status() == WL_CONNECTED);
}

bool autoDiscoverPrinter() {
    drawConnectingScreen("Scanning for Printer...", "SDCP Discovery");
    WiFiUDP udp;
    udp.begin(0);
    IPAddress broadcastIp(255, 255, 255, 255);
    
    unsigned long startTime = millis();
    unsigned long lastBroadcast = 0;
    while (millis() - startTime < 8000) {
        if (millis() - lastBroadcast > 2000) {
            lastBroadcast = millis();
            udp.beginPacket(broadcastIp, SDCP_DISCOVERY_PORT);
            udp.write((const uint8_t*)SDCP_DISCOVERY_MSG, strlen(SDCP_DISCOVERY_MSG));
            udp.endPacket();
        }
        
        int packetSize = udp.parsePacket();
        if (packetSize) {
            char buf[512];
            int len = udp.read(buf, 511);
            if (len > 0) {
                buf[len] = 0;
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, buf);
                if (!error) {
                    String name = "Elegoo Printer";
                    String mbId = "";
                    if (doc["Data"]["Attributes"]["Name"].is<const char*>()) {
                        name = doc["Data"]["Attributes"]["Name"].as<String>();
                    }
                    if (doc["Data"]["Attributes"]["MainboardID"].is<const char*>()) {
                        mbId = doc["Data"]["Attributes"]["MainboardID"].as<String>();
                    }
                    
                    config.printer_host = udp.remoteIP().toString();
                    printerStatus.mainboard_id = mbId;
                    printerStatus.printer_name = name;
                    
                    drawConnectingScreen("Printer Found!", name);
                    delay(1000);
                    
                    drawConnectingScreen("Probing SDCP...", "Port 3030");
                    
                    WiFiClient testClient;
                    if (testClient.connect(config.printer_host.c_str(), SDCP_PORT, 2000)) {
                        config.printer_port = SDCP_PORT;
                        testClient.stop();
                        drawConnectingScreen("SDCP Available!", config.printer_host);
                        delay(1000);
                        saveConfigToNVS(config);
                        return true;
                    }
                    
                    drawConnectingScreen("Port 3030 Failed", "Trying 8888...");
                    if (testClient.connect(config.printer_host.c_str(), 8888, 2000)) {
                        config.printer_port = 8888;
                        testClient.stop();
                        drawConnectingScreen("Alt Port OK!", config.printer_host);
                        delay(1000);
                        saveConfigToNVS(config);
                        return true;
                    }
                    
                    config.printer_port = SDCP_PORT;
                    saveConfigToNVS(config);
                    return true;
                }
            }
        }
        delay(10);
    }
    return false;
}

String generateUUID() {
    uuid.seed(micros());
    uuid.generate();
    return uuid.toCharArray();
}

void sendSDCPCommand(int cmd, JsonObject data = JsonObject()) {
    if (!sdcpConnected || config.printer_host.length() == 0) return;
    
    JsonDocument doc;
    JsonObject msgData = doc["Data"].to<JsonObject>();
    
    msgData["Cmd"] = cmd;
    msgData["From"] = 1;
    msgData["TimeStamp"] = millis();
    msgData["RequestID"] = generateUUID();
    msgData["MainboardID"] = printerStatus.mainboard_id.length() > 0 ? printerStatus.mainboard_id : "";
    
    if (!data.isNull()) {
        msgData["Data"] = data;
    } else {
        msgData["Data"] = JsonObject();
    }
    
    doc["Id"] = generateUUID();
    doc["Topic"] = "sdcp/request/" + (printerStatus.mainboard_id.length() > 0 ? printerStatus.mainboard_id : "");
    
    String output;
    serializeJson(doc, output);
    sdcpClient.sendTXT(output);
    
    lastCommandTime = millis();
}

void parseSDCPStatus(const JsonObject& status) {
    if (status["TempOfHotbed"].is<float>() || status["TempOfHotbed"].is<int>()) {
        printerStatus.bed_temp = status["TempOfHotbed"].as<float>();
    }
    if (status["TempTargetHotbed"].is<float>() || status["TempTargetHotbed"].is<int>()) {
        printerStatus.bed_target = status["TempTargetHotbed"].as<float>();
    }
    if (status["TempOfNozzle"].is<float>() || status["TempOfNozzle"].is<int>()) {
        printerStatus.nozzle_temp = status["TempOfNozzle"].as<float>();
    }
    if (status["TempTargetNozzle"].is<float>() || status["TempTargetNozzle"].is<int>()) {
        printerStatus.nozzle_target = status["TempTargetNozzle"].as<float>();
    }
    if (status["TempOfBox"].is<float>() || status["TempOfBox"].is<int>()) {
        printerStatus.enclosure_temp = status["TempOfBox"].as<float>();
    }
    if (status["TempTargetBox"].is<float>() || status["TempTargetBox"].is<int>()) {
        printerStatus.enclosure_target = status["TempTargetBox"].as<float>();
    }
    
    JsonObject printInfo = status["PrintInfo"];
    if (!printInfo.isNull()) {
        if (printInfo.containsKey("Status")) {
            int printState = printInfo["Status"].as<int>();
            
            if (printState == 1 || printState == 16 || printState == 21) {
                printerStatus.current_action = "Preparing";
                printerStatus.state = STATE_PREHEATING;
            } else if (printState == 13 || printState == 20) {
                printerStatus.current_action = "Printing";
                printerStatus.state = STATE_PRINTING;
            } else if (printState == 9) {
                printerStatus.current_action = "Print Complete";
                printerStatus.state = STATE_IDLE;
            } else if (printState == 0) {
                printerStatus.current_action = "Idle";
                printerStatus.state = STATE_IDLE;
            } else if (printState == 5 || printState == 10) {
                printerStatus.current_action = "Paused";
                printerStatus.state = STATE_PAUSED;
            } else if (printState == 8) {
                printerStatus.current_action = "Preheating";
                printerStatus.state = STATE_PREHEATING;
            } else {
                printerStatus.current_action = "Unknown (" + String(printState) + ")";
            }
        }
        
        if (printInfo["Progress"].is<int>()) {
            printerStatus.progress = printInfo["Progress"].as<int>();
        }
        
        if (printInfo["Filename"].is<const char*>()) {
            printerStatus.file_name = printInfo["Filename"].as<String>();
        }
        
        if (printInfo.containsKey("CurrentTicks")) {
            printerStatus.print_duration = printInfo["CurrentTicks"].as<long>();
        }
        
        if (printInfo.containsKey("TotalTicks")) {
            long totalTicks = printInfo["TotalTicks"].as<long>();
            printerStatus.estimated_time = totalTicks - printerStatus.print_duration;
            if (printerStatus.estimated_time < 0) printerStatus.estimated_time = 0;
        }
        
        if (printInfo["CurrentLayer"].is<int>()) {
            printerStatus.current_layer = printInfo["CurrentLayer"].as<int>();
        }
        
        if (printInfo["TotalLayer"].is<int>()) {
            printerStatus.total_layers = printInfo["TotalLayer"].as<int>();
        }
    }
    
    printerStatus.last_update = millis();
    cachedStatus = printerStatus;
}

void parseSDCPResponse(const String& payload) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
        Serial.println("JSON parse error: " + String(error.c_str()));
        return;
    }
    
    JsonObject root = doc.as<JsonObject>();
    
    if (root["Data"]["MainboardID"].is<const char*>()) {
        String mbId = root["Data"]["MainboardID"].as<String>();
        if (printerStatus.mainboard_id.length() == 0) {
            printerStatus.mainboard_id = mbId;
            Serial.println("Mainboard ID: " + mbId);
        }
    }
    
    if (root["Status"].is<JsonObject>()) {
        parseSDCPStatus(root["Status"].as<JsonObject>());
    }
    
    if (root["Attributes"].is<JsonObject>()) {
        JsonObject attrs = root["Attributes"].as<JsonObject>();
        if (attrs["Name"].is<const char*>()) {
            printerStatus.printer_name = attrs["Name"].as<String>();
        }
    }
}

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            Serial.println("[SDCP] Disconnected");
            sdcpConnected = false;
            printerConnected = false;
            break;
            
        case WStype_CONNECTED:
            Serial.println("[SDCP] Connected");
            sdcpConnected = true;
            reconnectAttempts = 0;
            sendSDCPCommand(1);
            delay(100);
            sendSDCPCommand(0);
            break;
            
        case WStype_TEXT:
            Serial.println("[SDCP] Received: " + String((char*)payload));
            parseSDCPResponse(String((char*)payload));
            break;
            
        case WStype_ERROR:
            Serial.println("[SDCP] Error");
            break;
            
        case WStype_PING:
            Serial.println("[SDCP] Ping received");
            break;
            
        case WStype_PONG:
            Serial.println("[SDCP] Pong received");
            break;
            
        default:
            break;
    }
}

void connectSDCP() {
    if (config.printer_host.length() == 0) return;
    
    printerStatus.state = STATE_CONNECTING;
    
    sdcpClient.disconnect();
    
    String wsUrl = "ws://" + config.printer_host + ":" + String(config.printer_port) + SDCP_WS_PATH;
    Serial.println("[SDCP] Connecting to: " + wsUrl);
    
    sdcpClient.begin(config.printer_host.c_str(), config.printer_port, SDCP_WS_PATH);
    sdcpClient.onEvent(webSocketEvent);
    sdcpClient.setReconnectInterval(SDCP_RECONNECT_DELAY);
    sdcpClient.enableHeartbeat(15000, 3000, 2);
}

void disconnectSDCP() {
    sdcpClient.disconnect();
    sdcpConnected = false;
    printerConnected = false;
}

void sdcpLoop() {
    sdcpClient.loop();
    
    if (sdcpConnected && printerStatus.mainboard_id.length() > 0) {
        if (millis() - lastCommandTime > SDCP_STATUS_INTERVAL) {
            sendSDCPCommand(0);
        }
        
        if (millis() - lastKeepalive > SDCP_KEEPALIVE_INTERVAL) {
            sendSDCPCommand(0);
            lastKeepalive = millis();
        }
        
        printerConnected = true;
    }
    
    if (!sdcpConnected && config.printer_host.length() > 0) {
        if (reconnectAttempts < SDCP_MAX_RECONNECT_ATTEMPTS) {
            if (millis() - lastStatusUpdate > SDCP_RECONNECT_DELAY) {
                Serial.println("[SDCP] Reconnecting... Attempt " + String(reconnectAttempts + 1));
                reconnectAttempts++;
                connectSDCP();
                lastStatusUpdate = millis();
            }
        } else {
            printerStatus.state = STATE_OFFLINE;
        }
    }
    
    if (sdcpConnected && (millis() - printerStatus.last_update > 30000)) {
        printerStatus.state = STATE_OFFLINE;
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("Elegoo Printer Monitor Starting...");
    
    pinMode(17, OUTPUT);
    digitalWrite(17, HIGH);

    // Explicitly power backlight on pin 21
    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH);

    nvs_flash_init();

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(MD3_BG);
    tft.setTextColor(MD3_TEXT);
    tft.setTextSize(2);
    tft.drawCentreString("Elegoo Monitor", 160, 100, 1);
    delay(1000);

    if (MOCK_UI_MODE) {
        currentState = UI_MONITOR;
        printerStatus.state = (PrinterState)MOCK_CURRENT_STATE;
        printerStatus.bed_temp = 23.0;
        printerStatus.nozzle_temp = 25.0;
        printerStatus.progress = 42;
        printerStatus.print_duration = 3600;
        printerStatus.estimated_time = 7200;
        printerStatus.file_name = "Calibration_Cube.gcode";
        Serial.println("Skipping network/printer setup - MOCK_UI_MODE enabled");
        return;
    }

    if (loadConfigFromNVS(config)) {
        cachedStatus = printerStatus;
        currentState = UI_CONNECTING;
        drawConnectingScreen("Connecting to WiFi...", config.wifi_ssid);

        connectWiFi();

        if (wifiConnected) {
            drawConnectingScreen("WiFi Connected", WiFi.localIP().toString());
            delay(500);

            if (config.printer_host == "") {
                if (!autoDiscoverPrinter()) {
                    drawConnectingScreen("Discovery Failed", "Check printer / LAN");
                    delay(3000);
                    config.configured = false;
                }
            }

            if (config.printer_host != "") {
                drawConnectingScreen("Connecting to SDCP...", config.printer_host);
                connectSDCP();
                currentState = UI_MONITOR;
            }
        } else {
            drawConnectingScreen("WiFi Failed", "Check credentials");
            delay(3000);
            config.configured = false;
        }
    }

    if (!config.configured) {
        currentState = UI_CONFIG;
        tft.fillScreen(TFT_BLACK);
        drawConfigScreen();
        setupAP();
    }
}

void loop() {
    if (MOCK_UI_MODE) {
        if (millis() - lastUIDraw > 200) {
            lastUIDraw = millis();
            drawCurrentState();
        }
        delay(10);
        return;
    }

    if (currentState == UI_CONFIG) {
        dnsServer.processNextRequest();
        server.handleClient();
    } else if (currentState == UI_MONITOR) {
        sdcpLoop();
        
        if (millis() - lastUIDraw > 1000) {
            lastUIDraw = millis();
            drawCurrentState();
        }
    }

    delay(10);
}
