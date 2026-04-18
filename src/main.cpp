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
    tft.fillRoundRect(cx - 20 * scale, cy + 15 * scale, 40 * scale, 6 * scale, 3 * scale, TFT_DARKGREY);
}

void drawHappyFace(int cx, int cy, int scale) {
    drawBaseEyes(cx, cy, scale);
    tft.drawArc(cx, cy - 10 * scale, 40 * scale, 32 * scale, 135, 225, TFT_GREEN, TFT_BLACK, true);
}

void drawCryingFace(int cx, int cy, int scale) {
    drawBaseEyes(cx, cy, scale);
    tft.fillTriangle(cx - 30 * scale, cy + 5 * scale, cx - 36 * scale, cy + 18 * scale, cx - 24 * scale, cy + 18 * scale, TFT_CYAN);
    tft.fillCircle(cx - 30 * scale, cy + 18 * scale, 6 * scale, TFT_CYAN);
    tft.fillTriangle(cx + 30 * scale, cy + 5 * scale, cx + 36 * scale, cy + 18 * scale, cx + 24 * scale, cy + 18 * scale, TFT_CYAN);
    tft.fillCircle(cx + 30 * scale, cy + 18 * scale, 6 * scale, TFT_CYAN);
    tft.drawArc(cx, cy + 40 * scale, 30 * scale, 22 * scale, 310, 410, TFT_RED, TFT_BLACK, true);
}

void drawThinkingFace(int cx, int cy, int scale) {
    drawBaseEyes(cx, cy, scale);
    tft.fillCircle(cx - 35 * scale, cy - 10 * scale, 8 * scale, TFT_LIGHTGREY);
    tft.fillCircle(cx + 35 * scale, cy - 10 * scale, 8 * scale, TFT_LIGHTGREY);
    tft.fillRoundRect(cx - 15 * scale, cy + 15 * scale, 30 * scale, 6 * scale, 3 * scale, TFT_DARKGREY);
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
    tft.fillScreen(TFT_BLACK);
    drawSadFace(160, 80, 2);
    tft.setTextSize(3);
    tft.setTextColor(TFT_DARKGREY);
    tft.drawCentreString("Printer is", 160, 160, 1);
    tft.drawCentreString("offline", 160, 195, 1);
}

void drawStateConnecting() {
    tft.fillScreen(TFT_BLACK);
    drawThinkingFace(160, 80, 1);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE);
    tft.drawCentreString("Connecting...", 160, 160, 1);
    if (config.printer_host.length() > 0) {
        tft.setTextSize(1);
        tft.setTextColor(TFT_LIGHTGREY);
        tft.drawCentreString(config.printer_host, 160, 185, 1);
    }
}

void drawStateIdle() {
    tft.fillScreen(TFT_BLACK);
    drawHappyFace(160, 80, 1);
    bool showFahrenheit = (millis() / 5000) % 2 == 1;
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE);
    tft.fillRoundRect(30, 150, 260, 60, 10, 0x1a1a2e);
    if (showFahrenheit) {
        int bTempF = (int)(printerStatus.bed_temp * 9/5 + 32);
        int bTargF = (int)(printerStatus.bed_target * 9/5 + 32);
        int nTempF = (int)(printerStatus.nozzle_temp * 9/5 + 32);
        int nTargF = (int)(printerStatus.nozzle_target * 9/5 + 32);
        tft.drawString("BED: " + String(bTempF) + "/" + String(bTargF) + "F", 45, 170, 1);
        tft.drawRightString("NOZ: " + String(nTempF) + "/" + String(nTargF) + "F", 275, 170, 1);
    } else {
        tft.drawString("BED: " + String((int)printerStatus.bed_temp) + "/" + String((int)printerStatus.bed_target) + "C", 45, 170, 1);
        tft.drawRightString("NOZ: " + String((int)printerStatus.nozzle_temp) + "/" + String((int)printerStatus.nozzle_target) + "C", 275, 170, 1);
    }
}

void drawStatePrinting() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_ORANGE);
    String title = printerStatus.file_name.length() > 0 ? printerStatus.file_name : "Printing...";
    if (title.length() > 18) {
        title = title.substring(0, 15) + "...";
    }
    tft.drawCentreString(title, 160, 15, 1);
    tft.fillRoundRect(10, 60, 145, 80, 8, 0x1a1a2e);
    tft.fillRoundRect(165, 60, 145, 80, 8, 0x1a1a2e);
    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY);
    tft.drawCentreString("ELAPSED", 82, 75, 1);
    tft.drawCentreString("ETA", 237, 75, 1);
    tft.setTextSize(3);
    tft.setTextColor(TFT_WHITE);
    int e_hours = printerStatus.print_duration / 3600;
    int e_mins = (printerStatus.print_duration % 3600) / 60;
    char e_buf[16];
    snprintf(e_buf, sizeof(e_buf), "%d:%02d", e_hours, e_mins);
    tft.drawCentreString(e_buf, 82, 100, 1);
    int eta_hours = printerStatus.estimated_time / 3600;
    int eta_mins = (printerStatus.estimated_time % 3600) / 60;
    char eta_buf[16];
    if (printerStatus.estimated_time > 0) {
        snprintf(eta_buf, sizeof(eta_buf), "%d:%02d", eta_hours, eta_mins);
    } else {
        snprintf(eta_buf, sizeof(eta_buf), "--:--");
    }
    tft.drawCentreString(eta_buf, 237, 100, 1);
    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN);
    tft.drawCentreString(String(printerStatus.progress) + "%", 160, 165, 1);
    int barW = 280;
    int barH = 20;
    int barX = 20;
    int barY = 195;
    tft.drawRect(barX, barY, barW, barH, TFT_DARKGREY);
    if (printerStatus.progress > 0) {
        int fillW = (printerStatus.progress * (barW - 4)) / 100;
        tft.fillRect(barX + 2, barY + 2, fillW, barH - 4, TFT_BLUE);
    }
}

void drawStatePaused() {
    tft.fillScreen(TFT_BLACK);
    drawThinkingFace(160, 80, 2);
    tft.setTextSize(2);
    tft.setTextColor(TFT_ORANGE);
    tft.drawCentreString("Print Paused", 160, 160, 1);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);
    tft.drawCentreString(String(printerStatus.progress) + "% complete", 160, 185, 1);
}

void drawStateError() {
    tft.fillScreen(TFT_BLACK);
    drawCryingFace(160, 80, 2);
    tft.setTextSize(3);
    tft.setTextColor(TFT_RED);
    tft.drawCentreString("Check the printer!", 160, 180, 1);
}

void drawCurrentState() {
    switch (printerStatus.state) {
        case STATE_OFFLINE:
            drawStateOffline();
            break;
        case STATE_CONNECTING:
            drawStateConnecting();
            break;
        case STATE_IDLE:
        case STATE_PREHEATING:
            drawStateIdle();
            break;
        case STATE_PRINTING:
            drawStatePrinting();
            break;
        case STATE_PAUSED:
            drawStatePaused();
            break;
        case STATE_ERROR:
            drawStateError();
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
        if (printInfo["Status"].is<int>()) {
            int printState = printInfo["Status"].as<int>();
            switch (printState) {
                case 0:
                    printerStatus.state = STATE_IDLE;
                    break;
                case 5:
                case 10:
                    printerStatus.state = STATE_PAUSED;
                    break;
                case 8:
                case 9:
                    printerStatus.state = STATE_PREHEATING;
                    break;
                case 13:
                    printerStatus.state = STATE_PRINTING;
                    break;
                case 20:
                    printerStatus.state = STATE_PRINTING;
                    break;
                default:
                    break;
            }
        }
        
        if (printInfo["Progress"].is<int>()) {
            printerStatus.progress = printInfo["Progress"].as<int>();
        }
        
        if (printInfo["Filename"].is<const char*>()) {
            printerStatus.file_name = printInfo["Filename"].as<String>();
        }
        
        if (printInfo["CurrentTicks"].is<int>()) {
            printerStatus.print_duration = printInfo["CurrentTicks"].as<int>();
        }
        
        if (printInfo["TotalTicks"].is<int>()) {
            int totalTicks = printInfo["TotalTicks"].as<int>();
            printerStatus.estimated_time = totalTicks - printerStatus.print_duration;
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
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.drawCentreString("Elegoo Monitor", 160, 100, 1);
    delay(1000);

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
