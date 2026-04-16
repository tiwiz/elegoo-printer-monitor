#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <nvs_flash.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

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

TFT_eSPI tft = TFT_eSPI();
WebServer server(80);
DNSServer dnsServer;

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
    STATE_OFFLINE
};

struct Config {
    String wifi_ssid;
    String wifi_password;
    String printer_host;
    int printer_port = 8888;
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
    String file_name;
    int estimated_time = 0;
    int print_duration = 0;
};

Config config;
PrinterStatus printerStatus;
bool wifiConnected = false;
bool printerConnected = false;
unsigned long lastStatusUpdate = 0;

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
            <input type="text" id="printerHost" placeholder="192.168.1.100">
            <label for="printerPort">Printer Port</label>
            <input type="number" id="printerPort" placeholder="8888" value="8888" min="1" max="65535">
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
            const printerPort = parseInt(document.getElementById('printerPort').value) || 8888;

            if (!ssid) { showMessage('Please enter or select a network', 'error'); return; }
            if (!printerHost) { showMessage('Please enter printer IP address', 'error'); return; }

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

// ==========================================
// EMOJI HELPERS
// ==========================================
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
    // Tears
    tft.fillTriangle(cx - 30 * scale, cy + 5 * scale, cx - 36 * scale, cy + 18 * scale, cx - 24 * scale, cy + 18 * scale, TFT_CYAN);
    tft.fillCircle(cx - 30 * scale, cy + 18 * scale, 6 * scale, TFT_CYAN);
    
    tft.fillTriangle(cx + 30 * scale, cy + 5 * scale, cx + 36 * scale, cy + 18 * scale, cx + 24 * scale, cy + 18 * scale, TFT_CYAN);
    tft.fillCircle(cx + 30 * scale, cy + 18 * scale, 6 * scale, TFT_CYAN);
    
    // Sad mouth
    tft.drawArc(cx, cy + 40 * scale, 30 * scale, 22 * scale, 310, 410, TFT_RED, TFT_BLACK, true);
}

// ==========================================
// [ SETUP / CONNECTING SCREENS ]
// ==========================================
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
        tft.drawCentreString(line2, 160, 130, 1);
    }
}

// ==========================================
// [ STATE: OFFLINE ]
// ==========================================
void drawStateOffline() {
    tft.fillScreen(TFT_BLACK);
    drawSadFace(160, 80, 2);
    tft.setTextSize(3);
    tft.setTextColor(TFT_DARKGREY);
    tft.drawCentreString("Printer is", 160, 160, 1);
    tft.drawCentreString("offline", 160, 195, 1);
}

// ==========================================
// [ STATE: IDLE ]
// ==========================================
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

// ==========================================
// [ STATE: PRINTING ]
// ==========================================
void drawStatePrinting() {
    tft.fillScreen(TFT_BLACK);
    
    // Header
    tft.setTextSize(2);
    tft.setTextColor(TFT_ORANGE);
    String title = printerStatus.file_name.length() > 0 ? printerStatus.file_name : "Printing...";
    tft.drawCentreString(title, 160, 15, 1);
    
    // Grid Layout for Stats
    tft.fillRoundRect(10, 60, 145, 80, 8, 0x1a1a2e);
    tft.fillRoundRect(165, 60, 145, 80, 8, 0x1a1a2e);
    
    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY);
    tft.drawCentreString("ELAPSED", 82, 75, 1);
    tft.drawCentreString("ETA", 237, 75, 1);
    
    tft.setTextSize(3);
    tft.setTextColor(TFT_WHITE);
    // Format elapsed
    int e_hours = printerStatus.print_duration / 3600;
    int e_mins = (printerStatus.print_duration % 3600) / 60;
    char e_buf[16];
    snprintf(e_buf, sizeof(e_buf), "%d:%02d", e_hours, e_mins);
    tft.drawCentreString(e_buf, 82, 100, 1);
    
    // Format ETA
    int eta_hours = printerStatus.estimated_time / 3600;
    int eta_mins = (printerStatus.estimated_time % 3600) / 60;
    char eta_buf[16];
    if (printerStatus.estimated_time > 0) {
        snprintf(eta_buf, sizeof(eta_buf), "%d:%02d", eta_hours, eta_mins);
    } else {
        snprintf(eta_buf, sizeof(eta_buf), "--:--");
    }
    tft.drawCentreString(eta_buf, 237, 100, 1);
    
    // Percentage & Progress Bar
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

// ==========================================
// [ STATE: ERROR ]
// ==========================================
void drawStateError() {
    tft.fillScreen(TFT_BLACK);
    drawCryingFace(160, 80, 2);
    tft.setTextSize(3);
    tft.setTextColor(TFT_RED);
    tft.drawCentreString("Check the printer!", 160, 180, 1);
}

// ==========================================
// STATE DISPATCHER
// ==========================================
void drawCurrentState() {
    switch (printerStatus.state) {
        case STATE_OFFLINE:
            drawStateOffline();
            break;
        case STATE_IDLE:
        case STATE_PREHEATING:
            drawStateIdle();
            break;
        case STATE_PRINTING:
            drawStatePrinting();
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

void fetchPrinterStatus() {
    HTTPClient http;
    String url = "http://" + config.printer_host + ":" + String(config.printer_port) + "/api/v1/printer";

    http.begin(url);
    http.setTimeout(5000);

    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        printerConnected = true;

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (!error) {
            JsonObject status = doc["status"];
            if (status["state"].is<const char*>()) {
                const char* state = status["state"];
                if (strcmp(state, "printing") == 0) {
                    printerStatus.state = STATE_PRINTING;
                } else if (strcmp(state, "idle") == 0) {
                    printerStatus.state = STATE_IDLE;
                } else if (strcmp(state, "preheating") == 0) {
                    printerStatus.state = STATE_PREHEATING;
                } else if (strcmp(state, "exception") == 0) {
                    printerStatus.state = STATE_ERROR;
                } else {
                    printerStatus.state = STATE_IDLE;
                }
            }

            if (status["progress"].is<int>()) {
                printerStatus.progress = status["progress"];
            }

            JsonObject print = doc["print"];
            if (print["file_name"].is<const char*>()) {
                printerStatus.file_name = print["file_name"].as<String>();
            }
            if (print["estimated_time"].is<int>()) {
                printerStatus.estimated_time = print["estimated_time"];
            }
            if (print["print_duration"].is<int>()) {
                printerStatus.print_duration = print["print_duration"];
            }

            JsonObject temp = doc["temperature"];
            JsonObject bed = temp["bed"];
            if (!bed.isNull()) {
                printerStatus.bed_temp = bed["actual"];
                printerStatus.bed_target = bed["target"];
            }
            JsonObject nozzle = temp["nozzle"];
            if (!nozzle.isNull()) {
                printerStatus.nozzle_temp = nozzle["actual"];
                printerStatus.nozzle_target = nozzle["target"];
            }
        }
    } else {
        printerConnected = false;
        printerStatus.state = STATE_OFFLINE;
    }

    http.end();
}

void setup() {
    Serial.begin(115200);

    // CYD Hardware Fixes (derived from GymTimer.ino)
    // 1. Turn off the annoying rear RGB LED (Blue) which is active low on pin 17
    pinMode(17, OUTPUT);
    digitalWrite(17, HIGH);
    
    ledcSetup(0, 4000, 8);
    ledcAttachPin(1, 0);
    ledcWrite(0, 0); 

    // 3. Explicitly power backlight on pin 21
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
        currentState = UI_CONNECTING;
        drawConnectingScreen("Connecting to WiFi...", config.wifi_ssid);

        connectWiFi();

        if (wifiConnected) {
            drawConnectingScreen("WiFi Connected", WiFi.localIP().toString());
            delay(500);

            drawConnectingScreen("Connecting to Printer...", config.printer_host);

            fetchPrinterStatus();
            if (printerConnected) {
                currentState = UI_MONITOR;
            } else {
                tft.fillScreen(TFT_BLACK);
                tft.setTextColor(TFT_ORANGE);
                tft.drawCentreString("Printer Offline", 160, 100, 1);
                tft.setTextColor(TFT_LIGHTGREY);
                tft.drawCentreString("Will retry...", 160, 130, 1);
                delay(2000);
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
        if (millis() - lastStatusUpdate > 2000) {
            lastStatusUpdate = millis();
            fetchPrinterStatus();
            drawCurrentState();
        }
    }

    delay(10);
}
