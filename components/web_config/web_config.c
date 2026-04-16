#include "web_config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_netif.h"
#include "esp_netif_types.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"

static const char *TAG = "web_config";

struct web_config_ctx {
    httpd_handle_t server;
    bool running;
    TaskHandle_t task;
};

static const char html_page[] = R"rawliteral(
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
        h1 {
            color: #fff;
            text-align: center;
            margin-bottom: 10px;
            font-size: 24px;
        }
        .subtitle {
            color: #94a3b8;
            text-align: center;
            margin-bottom: 30px;
            font-size: 14px;
        }
        .section {
            background: #1a1a2e;
            border-radius: 12px;
            padding: 20px;
            margin-bottom: 20px;
        }
        .section-title {
            color: #3b82f6;
            font-size: 12px;
            text-transform: uppercase;
            letter-spacing: 1px;
            margin-bottom: 15px;
        }
        label {
            display: block;
            color: #94a3b8;
            font-size: 12px;
            margin-bottom: 6px;
        }
        input, select {
            width: 100%;
            padding: 12px 15px;
            background: #0f0f23;
            border: 1px solid #2d2d44;
            border-radius: 8px;
            color: #fff;
            font-size: 14px;
            margin-bottom: 12px;
            transition: border-color 0.3s;
        }
        input:focus, select:focus {
            outline: none;
            border-color: #3b82f6;
        }
        input::placeholder { color: #6b7280; }
        button {
            width: 100%;
            padding: 15px;
            background: #3b82f6;
            color: #fff;
            border: none;
            border-radius: 10px;
            font-size: 16px;
            font-weight: 600;
            cursor: pointer;
            transition: background 0.3s, transform 0.1s;
        }
        button:hover { background: #2563eb; }
        button:active { transform: scale(0.98); }
        button:disabled { background: #4b5563; cursor: not-allowed; }
        .spinner {
            display: inline-block;
            width: 20px;
            height: 20px;
            border: 2px solid #fff;
            border-radius: 50%;
            border-top-color: transparent;
            animation: spin 1s linear infinite;
            margin-right: 8px;
            vertical-align: middle;
        }
        @keyframes spin { to { transform: rotate(360deg); } }
        .message {
            padding: 12px;
            border-radius: 8px;
            margin-top: 15px;
            text-align: center;
            font-size: 14px;
            display: none;
        }
        .message.success { background: #065f46; color: #6ee7b7; }
        .message.error { background: #7f1d1d; color: #fca5a5; }
        .scan-list {
            max-height: 150px;
            overflow-y: auto;
            border: 1px solid #2d2d44;
            border-radius: 8px;
            margin-bottom: 10px;
        }
        .scan-item {
            padding: 10px 15px;
            color: #fff;
            cursor: pointer;
            border-bottom: 1px solid #2d2d44;
            transition: background 0.2s;
        }
        .scan-item:hover { background: #2d2d44; }
        .scan-item:last-child { border-bottom: none; }
        .scan-item.selected { background: #3b82f6; }
        .scan-btn {
            width: 100%;
            padding: 10px;
            background: #374151;
            color: #fff;
            border: none;
            border-radius: 8px;
            cursor: pointer;
            margin-bottom: 10px;
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
            } catch (e) {
                showMessage('Scan failed: ' + e.message, 'error');
            }

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
            const net = scanResults[index];
            document.getElementById('ssid').value = net.ssid;
            document.querySelectorAll('.scan-item').forEach((el, i) => {
                el.classList.toggle('selected', i === index);
            });
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
                    body: JSON.stringify({
                        ssid, password, printerType, printerHost, printerPort
                    })
                });

                if (res.ok) {
                    showMessage('Configuration saved! Device will connect...', 'success');
                    btn.textContent = 'Saved!';
                } else {
                    throw new Error('Server error: ' + res.status);
                }
            } catch (e) {
                showMessage('Error: ' + e.message, 'error');
                btn.textContent = 'Save & Connect';
                btn.disabled = false;
            }
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

static esp_err_t http_get_handler(httpd_req_t *req) {
    if (strcmp(req->uri, "/") == 0 || strcmp(req->uri, "/index.html") == 0) {
        httpd_resp_send(req, html_page, strlen(html_page));
        return ESP_OK;
    }

    if (strcmp(req->uri, "/scan") == 0) {
        wifi_scan_config_t scan_config = {
            .show_hidden = false,
            .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        };

        uint16_t max_ap = 20;
        wifi_ap_record_t ap_info[20];
        memset(ap_info, 0, sizeof(ap_info));

        esp_err_t err = esp_wifi_scan_start(&scan_config, true);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
        }

        esp_wifi_scan_get_ap_records(&max_ap, ap_info);

        cJSON *root = cJSON_CreateObject();
        cJSON *networks = cJSON_CreateArray();

        for (int i = 0; i < max_ap; i++) {
            if (strlen((char *)ap_info[i].ssid) > 0) {
                cJSON *net = cJSON_CreateObject();
                cJSON_AddStringToObject(net, "ssid", (char *)ap_info[i].ssid);
                cJSON_AddNumberToObject(net, "rssi", ap_info[i].rssi);
                cJSON_AddItemToArray(networks, net);
            }
        }

        cJSON_AddItemToObject(root, "networks", networks);
        char *json_str = cJSON_Print(root);

        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_str, strlen(json_str));

        free(json_str);
        cJSON_Delete(root);
        return ESP_OK;
    }

    httpd_resp_send_404(req);
    return ESP_OK;
}

static esp_err_t http_post_handler(httpd_req_t *req) {
    if (strcmp(req->uri, "/save") != 0) {
        httpd_resp_send_404(req);
        return ESP_OK;
    }

    char buf[1024];
    int ret = httpd_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        httpd_resp_send_400(req);
        return ESP_FAIL;
    }

    web_config_data_t config = {0};

    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *password = cJSON_GetObjectItem(root, "password");
    cJSON *printerType = cJSON_GetObjectItem(root, "printerType");
    cJSON *printerHost = cJSON_GetObjectItem(root, "printerHost");
    cJSON *printerPort = cJSON_GetObjectItem(root, "printerPort");

    if (ssid && cJSON_IsString(ssid)) {
        strncpy(config.wifi_ssid, ssid->valuestring, sizeof(config.wifi_ssid) - 1);
    }
    if (password && cJSON_IsString(password)) {
        strncpy(config.wifi_password, password->valuestring, sizeof(config.wifi_password) - 1);
    }
    if (printerHost && cJSON_IsString(printerHost)) {
        strncpy(config.printer_host, printerHost->valuestring, sizeof(config.printer_host) - 1);
    }
    if (printerPort && cJSON_IsNumber(printerPort)) {
        config.printer_port = printerPort->valueint;
    } else {
        config.printer_port = 8888;
    }

    if (printerType && cJSON_IsString(printerType)) {
        config.printer_type = web_config_string_to_printer_type(printerType->valuestring);
    }

    cJSON_Delete(root);

    esp_err_t err = web_config_save(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save config: %s", esp_err_to_name(err));
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", 14);

    vTaskDelay(pdMS_TO_TICKS(500));
    httpd_stop(req_get_global_transport_infos(req)->hd);

    return ESP_OK;
}

static void start_webserver(httpd_handle_t *server) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WEB_CONFIG_SERVER_PORT;
    config.stack_size = 8192;
    config.uri_match_fn = NULL;

    if (httpd_start(server, &config) == ESP_OK) {
        httpd_register_uri_handler(*server, &(httpd_uri_t){
            .uri = "/",
            .method = HTTP_GET,
            .handler = http_get_handler,
            .user_ctx = NULL
        });
        httpd_register_uri_handler(*server, &(httpd_uri_t){
            .uri = "/scan",
            .method = HTTP_GET,
            .handler = http_get_handler,
            .user_ctx = NULL
        });
        httpd_register_uri_handler(*server, &(httpd_uri_t){
            .uri = "/save",
            .method = HTTP_POST,
            .handler = http_post_handler,
            .user_ctx = NULL
        });
        ESP_LOGI(TAG, "HTTP server started on port %d", WEB_CONFIG_SERVER_PORT);
    }
}

const char* web_config_printer_type_to_string(printer_type_t type) {
    switch (type) {
        case PRINTER_TYPE_NEPTUNE4: return "neptune4";
        case PRINTER_TYPE_NEPTUNE4PRO: return "neptune4pro";
        case PRINTER_TYPE_NEPTUNE4MAX: return "neptune4max";
        case PRINTER_TYPE_CENTAURI_CARBON: return "centauri_carbon";
        case PRINTER_TYPE_CENTAURI_CARBON2: return "centauri_carbon2";
        case PRINTER_TYPE_ORANGESTORM_GIGA: return "orangestorm_giga";
        case PRINTER_TYPE_GENERIC: return "generic";
        default: return "neptune4";
    }
}

printer_type_t web_config_string_to_printer_type(const char *str) {
    if (strcmp(str, "neptune4pro") == 0) return PRINTER_TYPE_NEPTUNE4PRO;
    if (strcmp(str, "neptune4max") == 0) return PRINTER_TYPE_NEPTUNE4MAX;
    if (strcmp(str, "centauri_carbon") == 0) return PRINTER_TYPE_CENTAURI_CARBON;
    if (strcmp(str, "centauri_carbon2") == 0) return PRINTER_TYPE_CENTAURI_CARBON2;
    if (strcmp(str, "orangestorm_giga") == 0) return PRINTER_TYPE_ORANGESTORM_GIGA;
    if (strcmp(str, "generic") == 0) return PRINTER_TYPE_GENERIC;
    return PRINTER_TYPE_NEPTUNE4;
}

esp_err_t web_config_init(web_config_ctx_t **out_ctx) {
    web_config_ctx_t *ctx = calloc(1, sizeof(web_config_ctx_t));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    *out_ctx = ctx;
    return ESP_OK;
}

esp_err_t web_config_start_ap(web_config_ctx_t *ctx) {
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (ap_netif == NULL) {
        return ESP_FAIL;
    }

    wifi_config_t ap_config = {
        .ap = {
            .ssid = WEB_CONFIG_AP_SSID,
            .ssid_len = strlen(WEB_CONFIG_AP_SSID),
            .channel = WEB_CONFIG_AP_CHANNEL,
            .authmode = WIFI_AUTH_OPEN,
            .max_connection = 4,
            .pmf_cfg = { .required = false },
        },
    };

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP config: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "AP started: SSID=%s (Open)", WEB_CONFIG_AP_SSID);
    ESP_LOGI(TAG, "Connect to %s and open http://192.168.4.1", WEB_CONFIG_AP_SSID);

    start_webserver(&ctx->server);
    ctx->running = true;

    return ESP_OK;
}

esp_err_t web_config_stop(web_config_ctx_t *ctx) {
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (ctx->server != NULL) {
        httpd_stop(ctx->server);
        ctx->server = NULL;
    }

    esp_wifi_stop();
    ctx->running = false;

    return ESP_OK;
}

esp_err_t web_config_get_saved(web_config_data_t *out_data) {
    if (out_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    size_t len;

    len = sizeof(out_data->wifi_ssid);
    nvs_get_str(nvs, NVS_KEY_WIFI_SSID, out_data->wifi_ssid, &len);

    len = sizeof(out_data->wifi_password);
    nvs_get_str(nvs, NVS_KEY_WIFI_PASS, out_data->wifi_password, &len);

    len = sizeof(out_data->printer_host);
    nvs_get_str(nvs, NVS_KEY_PRINTER_HOST, out_data->printer_host, &len);

    nvs_get_i32(nvs, NVS_KEY_PRINTER_PORT, &out_data->printer_port);

    int8_t ptype = PRINTER_TYPE_NEPTUNE4;
    nvs_get_i8(nvs, NVS_KEY_PRINTER_TYPE, &ptype);
    out_data->printer_type = (printer_type_t)ptype;

    int8_t configured = 0;
    nvs_get_i8(nvs, NVS_KEY_CONFIGURED, &configured);
    out_data->configured = (configured != 0);

    nvs_close(nvs);

    return ESP_OK;
}

esp_err_t web_config_save(const web_config_data_t *data) {
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    if (strlen(data->wifi_ssid) > 0) {
        nvs_set_str(nvs, NVS_KEY_WIFI_SSID, data->wifi_ssid);
    }
    nvs_set_str(nvs, NVS_KEY_WIFI_PASS, data->wifi_password);
    nvs_set_str(nvs, NVS_KEY_PRINTER_HOST, data->printer_host);
    nvs_set_i32(nvs, NVS_KEY_PRINTER_PORT, data->printer_port);
    nvs_set_i8(nvs, NVS_KEY_PRINTER_TYPE, (int8_t)data->printer_type);
    nvs_set_i8(nvs, NVS_KEY_CONFIGURED, 1);

    err = nvs_commit(nvs);
    nvs_close(nvs);

    ESP_LOGI(TAG, "Configuration saved successfully");

    return err;
}

esp_err_t web_config_delete(void) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    nvs_erase_key(nvs, NVS_KEY_WIFI_SSID);
    nvs_erase_key(nvs, NVS_KEY_WIFI_PASS);
    nvs_erase_key(nvs, NVS_KEY_PRINTER_HOST);
    nvs_erase_key(nvs, NVS_KEY_PRINTER_PORT);
    nvs_erase_key(nvs, NVS_KEY_PRINTER_TYPE);
    nvs_erase_key(nvs, NVS_KEY_CONFIGURED);

    err = nvs_commit(nvs);
    nvs_close(nvs);

    return err;
}

esp_err_t web_config_is_configured(bool *configured) {
    if (configured == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        *configured = false;
        return ESP_OK;
    }

    int8_t val = 0;
    err = nvs_get_i8(nvs, NVS_KEY_CONFIGURED, &val);
    *configured = (val == 1);

    nvs_close(nvs);
    return ESP_OK;
}

esp_err_t web_config_delete_ctx(web_config_ctx_t *ctx) {
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    web_config_stop(ctx);
    free(ctx);
    return ESP_OK;
}
