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

void drawFace(int centerX, int centerY, int scale, uint32_t eyeColor, uint32_t mouthColor, int mouthType) {
    tft.fillCircle(centerX - 30 * scale, centerY - 20 * scale, 15 * scale, eyeColor);
    tft.fillCircle(centerX + 30 * scale, centerY - 20 * scale, 15 * scale, eyeColor);

    switch (mouthType) {
        case 1:
            tft.fillCircle(centerX, centerY + 25 * scale, 25 * scale, mouthColor);
            break;
        case 2:
            tft.fillRoundRect(centerX - 20 * scale, centerY + 15 * scale, 40 * scale, 12 * scale, 6 * scale, mouthColor);
            break;
        case 3:
            tft.fillRoundRect(centerX - 25 * scale, centerY + 15 * scale, 50 * scale, 15 * scale, 4 * scale, mouthColor);
            break;
        default:
            tft.fillRoundRect(centerX - 20 * scale, centerY + 20 * scale, 40 * scale, 6 * scale, 3 * scale, mouthColor);
            break;
    }
}

void drawProgressArc(int centerX, int centerY, int radius, int thickness, int progress, uint32_t bgColor, uint32_t fgColor) {
    tft.drawArc(centerX, centerY, radius, radius - thickness, 135, 270, bgColor, bgColor, true);
    int angle = (progress * 270) / 100;
    if (angle > 0) {
        tft.drawArc(centerX, centerY, radius, radius - thickness, 135, 135 + angle, fgColor, bgColor, true);
    }
}

void drawConfigScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.drawCentreString("Setup Required", 160, 20, 1);

    drawFace(160, 100, 1, TFT_WHITE, TFT_DARKGREY, 0);

    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY);
    tft.drawCentreString("Connect to WiFi", 160, 170, 1);
    tft.drawCentreString("\"ElegooMonitor\"", 160, 185, 1);
    tft.drawCentreString("Then open browser", 160, 200, 1);
    tft.drawCentreString("to http://192.168.4.1", 160, 215, 1);
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

void drawMonitorScreen() {
    tft.fillScreen(TFT_BLACK);

    uint32_t mouthColor = TFT_DARKGREY;
    int mouthType = 0;

    switch (printerStatus.state) {
        case STATE_PRINTING:
            mouthColor = TFT_ORANGE;
            mouthType = 2;
            break;
        case STATE_PREHEATING:
            mouthColor = TFT_YELLOW;
            mouthType = 2;
            break;
        case STATE_ERROR:
            mouthColor = TFT_RED;
            mouthType = 3;
            break;
        case STATE_IDLE:
        default:
            mouthColor = TFT_DARKGREY;
            mouthType = 0;
            break;
    }

    drawFace(160, 80, 1, TFT_WHITE, mouthColor, mouthType);
    drawProgressArc(160, 80, 55, 8, printerStatus.progress, 0x2d2d44, TFT_BLUE);

    tft.setTextSize(3);
    tft.setTextColor(TFT_WHITE);
    if (printerStatus.state == STATE_PREHEATING) {
        tft.drawCentreString("HEAT", 160, 155, 1);
    } else if (printerStatus.state == STATE_IDLE) {
        tft.drawCentreString("IDLE", 160, 155, 1);
    } else if (printerStatus.state == STATE_ERROR) {
        tft.drawCentreString("ERR", 160, 155, 1);
    } else {
        tft.drawCentreString(String(printerStatus.progress) + "%", 160, 155, 1);
    }

    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY);
    String statusStr;
    switch (printerStatus.state) {
        case STATE_PRINTING: statusStr = printerStatus.file_name.length() > 0 ? printerStatus.file_name : "Printing"; break;
        case STATE_PREHEATING: statusStr = "Preheating..."; break;
        case STATE_ERROR: statusStr = "Error"; break;
        case STATE_IDLE: statusStr = "Ready"; break;
        default: statusStr = "Offline";
    }
    tft.drawCentreString(statusStr, 160, 10, 1);

    tft.fillRoundRect(50, 195, 220, 35, 8, 0x1a1a2e);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("BED: " + String((int)printerStatus.bed_temp) + "/" + String((int)printerStatus.bed_target) + "C", 60, 205, 1);
    tft.drawRightString("NOZ: " + String((int)printerStatus.nozzle_temp) + "/" + String((int)printerStatus.nozzle_target) + "C", 260, 205, 1);

    tft.setTextSize(1);
    tft.setTextColor(TFT_GREY);
    if (printerStatus.estimated_time > 0) {
        int remaining = printerStatus.estimated_time;
        int hours = remaining / 3600;
        int mins = (remaining % 3600) / 60;
        char buf[16];
        snprintf(buf, sizeof(buf), "ETA: %d:%02d", hours, mins);
        tft.drawCentreString(buf, 160, 175, 1);
    } else {
        tft.drawCentreString("ETA: --:--", 160, 175, 1);
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
    DynamicJsonDocument doc(1024);
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

        DynamicJsonDocument doc(4096);
        DeserializationError error = deserializeJson(doc, payload);

        if (!error) {
            JsonObject status = doc["status"];
            if (status.containsKey("state")) {
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

            if (status.containsKey("progress")) {
                printerStatus.progress = status["progress"];
            }

            JsonObject print = doc["print"];
            if (print.containsKey("file_name")) {
                printerStatus.file_name = print["file_name"].as<String>();
            }
            if (print.containsKey("estimated_time")) {
                printerStatus.estimated_time = print["estimated_time"];
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

    nvs_flash_init();

    tft.init();
    tft.setRotation(0);
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
        dnsServer.processNextClient();
        server.handleClient();
    } else if (currentState == UI_MONITOR) {
        if (millis() - lastStatusUpdate > 2000) {
            lastStatusUpdate = millis();
            fetchPrinterStatus();
            drawMonitorScreen();
        }
    }

    delay(10);
}
