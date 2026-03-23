#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>

// ========== PIN DEFINITIONS ==========
#define RDM6300_RX 16
#define RDM6300_TX 17

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR 0x3C

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ========== DEFAULT / FALLBACK CREDENTIALS ==========
const char* DEFAULT_SSID     = "fiz";
const char* DEFAULT_PASSWORD = "fizRafiz";
const char* DEFAULT_API_URL  = "https://your-endpoint-here";

// ========== RUNTIME CONFIG (loaded from NVS) ==========
String cfg_ssid     = "";
String cfg_password = "";
String cfg_apiUrl   = "";

// ========== NVS + WEB SERVER ==========
Preferences prefs;
WebServer server(80);

// ========== VARIABLES ==========
String cardData = "";
bool reading = false;
unsigned long lastCharTime = 0;
const unsigned long timeout = 100;

bool isProcessing = false;
String currentUID = "";
unsigned long processingStartTime = 0;
const unsigned long processingTimeout = 15000;

String lastProcessedUID = "";
unsigned long lastProcessedTime = 0;
const unsigned long duplicateCooldown = 5000;

WiFiClientSecure secureClient;

// ========== CONFIG WEBPAGE HTML ==========
const char CONFIG_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>RFID Attendance — Config</title>
<style>
  @import url('https://fonts.googleapis.com/css2?family=DM+Mono:wght@400;500&family=Syne:wght@700;800&display=swap');

  *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

  :root {
    --bg: #0a0a0f;
    --surface: #13131a;
    --border: #2a2a3a;
    --accent: #00e5a0;
    --accent-dim: #00e5a022;
    --text: #e8e8f0;
    --muted: #6b6b80;
    --danger: #ff4d6d;
  }

  html, body {
    min-height: 100vh;
    background: var(--bg);
    color: var(--text);
    font-family: 'DM Mono', monospace;
  }

  body {
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 24px;
    background-image:
      radial-gradient(ellipse 60% 50% at 50% -10%, #00e5a012, transparent),
      repeating-linear-gradient(0deg, transparent, transparent 39px, #1a1a2a 39px, #1a1a2a 40px),
      repeating-linear-gradient(90deg, transparent, transparent 39px, #1a1a2a 39px, #1a1a2a 40px);
  }

  .card {
    width: 100%;
    max-width: 480px;
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 16px;
    overflow: hidden;
    box-shadow: 0 0 80px #00e5a010, 0 24px 48px #00000060;
  }

  .header {
    padding: 28px 32px 24px;
    border-bottom: 1px solid var(--border);
    background: linear-gradient(135deg, #13131a 0%, #0d1a14 100%);
    position: relative;
  }

  .header::before {
    content: '';
    position: absolute;
    top: 0; left: 0; right: 0;
    height: 2px;
    background: linear-gradient(90deg, transparent, var(--accent), transparent);
  }

  .badge {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    font-size: 10px;
    letter-spacing: 0.15em;
    text-transform: uppercase;
    color: var(--accent);
    background: var(--accent-dim);
    border: 1px solid #00e5a030;
    padding: 4px 10px;
    border-radius: 99px;
    margin-bottom: 14px;
  }

  .badge-dot {
    width: 6px; height: 6px;
    background: var(--accent);
    border-radius: 50%;
    animation: pulse 2s infinite;
  }

  @keyframes pulse {
    0%, 100% { opacity: 1; transform: scale(1); }
    50% { opacity: 0.4; transform: scale(0.8); }
  }

  h1 {
    font-family: 'Syne', sans-serif;
    font-size: 26px;
    font-weight: 800;
    color: #fff;
    letter-spacing: -0.5px;
    line-height: 1.1;
  }

  h1 span { color: var(--accent); }

  .subtitle {
    font-size: 11px;
    color: var(--muted);
    margin-top: 6px;
    letter-spacing: 0.05em;
  }

  form { padding: 28px 32px 32px; }

  .section-label {
    font-size: 9px;
    letter-spacing: 0.2em;
    text-transform: uppercase;
    color: var(--muted);
    margin-bottom: 16px;
    padding-bottom: 8px;
    border-bottom: 1px solid var(--border);
  }

  .field {
    margin-bottom: 18px;
  }

  label {
    display: block;
    font-size: 11px;
    color: var(--muted);
    letter-spacing: 0.1em;
    text-transform: uppercase;
    margin-bottom: 7px;
  }

  input {
    width: 100%;
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 8px;
    color: var(--text);
    font-family: 'DM Mono', monospace;
    font-size: 13px;
    padding: 11px 14px;
    transition: border-color 0.2s, box-shadow 0.2s;
    outline: none;
  }

  input:focus {
    border-color: var(--accent);
    box-shadow: 0 0 0 3px var(--accent-dim);
  }

  input::placeholder { color: var(--muted); }

  .divider {
    height: 1px;
    background: var(--border);
    margin: 24px 0;
  }

  .btn {
    width: 100%;
    padding: 14px;
    background: var(--accent);
    color: #000;
    border: none;
    border-radius: 8px;
    font-family: 'Syne', sans-serif;
    font-size: 14px;
    font-weight: 700;
    letter-spacing: 0.05em;
    cursor: pointer;
    transition: opacity 0.2s, transform 0.1s;
    position: relative;
    overflow: hidden;
  }

  .btn:hover { opacity: 0.9; }
  .btn:active { transform: scale(0.99); }

  .btn::after {
    content: '';
    position: absolute;
    inset: 0;
    background: linear-gradient(180deg, rgba(255,255,255,0.15) 0%, transparent 100%);
  }

  .warning {
    margin-top: 16px;
    padding: 12px 14px;
    background: #ff4d6d10;
    border: 1px solid #ff4d6d30;
    border-radius: 8px;
    font-size: 11px;
    color: #ff8099;
    text-align: center;
    letter-spacing: 0.04em;
  }

  .current-ip {
    font-size: 10px;
    color: var(--muted);
    text-align: center;
    margin-top: 16px;
  }

  .current-ip span { color: var(--accent); }

  /* Toast */
  #toast {
    display: none;
    position: fixed;
    bottom: 28px;
    left: 50%;
    transform: translateX(-50%);
    background: var(--accent);
    color: #000;
    font-family: 'Syne', sans-serif;
    font-weight: 700;
    font-size: 13px;
    padding: 12px 24px;
    border-radius: 99px;
    box-shadow: 0 8px 32px #00000060;
    z-index: 100;
    animation: slideUp 0.3s ease;
  }

  @keyframes slideUp {
    from { opacity: 0; transform: translateX(-50%) translateY(10px); }
    to { opacity: 1; transform: translateX(-50%) translateY(0); }
  }
</style>
</head>
<body>
<div class="card">
  <div class="header">
    <div class="badge"><span class="badge-dot"></span>ESP32 Config Panel</div>
    <h1>RFID <span>Attendance</span></h1>
    <p class="subtitle">Configure WiFi credentials and API endpoint below</p>
  </div>
  <form id="configForm">
    <p class="section-label">// WiFi Settings</p>

    <div class="field">
      <label>Network SSID</label>
      <input type="text" name="ssid" id="ssid" placeholder="Enter WiFi name" required>
    </div>
    <div class="field">
      <label>Password</label>
      <input type="password" name="password" id="password" placeholder="Enter WiFi password">
    </div>

    <div class="divider"></div>
    <p class="section-label">// API Configuration</p>

    <div class="field">
      <label>API Endpoint URL</label>
      <input type="url" name="apiUrl" id="apiUrl" placeholder="https://your-api.example.com/endpoint" required>
    </div>

    <button type="submit" class="btn">SAVE &amp; REBOOT</button>
    <p class="warning">⚠ Device will reboot after saving. Reconnect to the new network if SSID changes.</p>
    <p class="current-ip" id="currentIp"></p>
  </form>
</div>

<div id="toast">✓ Saved! Rebooting device...</div>

<script>
  // Load current config
  fetch('/config')
    .then(r => r.json())
    .then(d => {
      document.getElementById('ssid').value = d.ssid || '';
      document.getElementById('apiUrl').value = d.apiUrl || '';
      document.getElementById('currentIp').innerHTML =
        'Device IP: <span>' + window.location.hostname + '</span>';
    });

  document.getElementById('configForm').addEventListener('submit', function(e) {
    e.preventDefault();
    const data = {
      ssid: document.getElementById('ssid').value,
      password: document.getElementById('password').value,
      apiUrl: document.getElementById('apiUrl').value
    };

    fetch('/save', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(data)
    }).then(r => r.text()).then(() => {
      const toast = document.getElementById('toast');
      toast.style.display = 'block';
      setTimeout(() => { toast.style.display = 'none'; }, 4000);
    });
  });
</script>
</body>
</html>
)rawliteral";

// ========== WEB SERVER HANDLERS ==========

void handleRoot() {
  server.send(200, "text/html", CONFIG_PAGE);
}

void handleGetConfig() {
  // Return current config (never return password for security)
  String json = "{\"ssid\":\"" + cfg_ssid + "\",\"apiUrl\":\"" + cfg_apiUrl + "\"}";
  server.send(200, "application/json", json);
}

void handleSave() {
  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, body) == DeserializationError::Ok) {
      String newSSID     = doc["ssid"]     | "";
      String newPassword = doc["password"] | "";
      String newApiUrl   = doc["apiUrl"]   | "";

      if (newSSID.length() > 0 && newApiUrl.length() > 0) {
        prefs.begin("wifi-config", false);
        prefs.putString("ssid",     newSSID);
        prefs.putString("password", newPassword);
        prefs.putString("apiUrl",   newApiUrl);
        prefs.end();

        server.send(200, "text/plain", "OK");

        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(10, 20);
        display.println("Config Saved!");
        display.setCursor(10, 40);
        display.println("Rebooting...");
        display.display();
        delay(2000);
        ESP.restart();
        return;
      }
    }
  }
  server.send(400, "text/plain", "Bad Request");
}

// ========== LOAD CONFIG FROM NVS ==========
void loadConfig() {
  prefs.begin("wifi-config", true);  // read-only
  cfg_ssid     = prefs.getString("ssid",     DEFAULT_SSID);
  cfg_password = prefs.getString("password", DEFAULT_PASSWORD);
  cfg_apiUrl   = prefs.getString("apiUrl",   DEFAULT_API_URL);
  prefs.end();

  Serial.println("Loaded config:");
  Serial.println("  SSID: " + cfg_ssid);
  Serial.println("  API:  " + cfg_apiUrl);
}

// ========== UTILITY FUNCTIONS (unchanged) ==========

void flushRDM6300() {
  unsigned long flushStart = millis();
  while (millis() - flushStart < 200) {
    while (Serial2.available()) Serial2.read();
    delay(10);
  }
}

String extractUID(String data) {
  if (data.length() < 12) return "";
  String tagHex = data.substring(2, 10);
  tagHex.toUpperCase();
  for (int i = 0; i < tagHex.length(); i++) {
    char c = tagHex[i];
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) {
      Serial.println("Invalid hex in UID, discarding.");
      return "";
    }
  }
  return tagHex;
}

void showWaitingScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(15, 10);
  display.println("Attendance System");
  display.drawLine(0, 25, 128, 25, SH110X_WHITE);
  display.setCursor(0, 35);
  display.println("Scan your card");
  display.setCursor(0, 50);
  display.print(WiFi.status() == WL_CONNECTED ? "WiFi: Connected" : "WiFi: Disconnected");
  display.display();
  Serial.println("Ready for next card...");
}

void showError(String errorMsg, String uid) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(55, 0);
  display.println("X");
  display.drawLine(0, 13, 128, 13, SH110X_WHITE);
  display.setCursor(0, 20);
  display.print("UID: ");
  display.println(uid);
  display.setCursor(0, 40);
  display.println(errorMsg.length() > 20 ? errorMsg.substring(0, 18) + "..." : errorMsg);
  display.display();
  Serial.print("ERROR: ");
  Serial.println(errorMsg);
  delay(3000);
  flushRDM6300();
  isProcessing = false;
  currentUID = "";
  showWaitingScreen();
}

bool processJSONResponse(String jsonResponse, String uid) {
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, jsonResponse);
  if (error) {
    Serial.print("JSON parse failed: ");
    Serial.println(error.c_str());
    showError("Invalid Response", uid);
    return false;
  }

  if (doc.containsKey("value")) {
    const char* value  = doc["value"];
    const char* status = doc["status"];

    Serial.print("Value from server: ");
    Serial.println(value);

    unsigned long uidDecimal = strtoul(uid.c_str(), NULL, 16);
    Serial.print("UID (decimal): ");
    Serial.println(uidDecimal);

    display.clearDisplay();
    static const uint8_t check_bmp[] = {
      0b00000000, 0b00000000,
      0b00000000, 0b00000000,
      0b00000000, 0b01000000,
      0b00000000, 0b10000000,
      0b00000001, 0b00000000,
      0b00000010, 0b00000000,
      0b01000100, 0b00000000,
      0b00101000, 0b00000000,
      0b00010000, 0b00000000,
      0b00000000, 0b00000000
    };
    display.setTextSize(1);
    if (String(status) == "success") {
      display.drawBitmap(55, 0, check_bmp, 10, 10, SH110X_WHITE);
    } else {
      display.setCursor(55, 0);
      display.print("X");
    }
    display.drawLine(0, 15, 128, 15, SH110X_WHITE);
    display.setCursor(0, 20);
    display.print("UID: ");
    display.print(uid);
    display.setCursor(0, 40);
    String valueStr = String(value);
    display.setTextSize(valueStr.length() > 8 ? 1 : 2);
    display.println(valueStr);
    display.display();

    Serial.println("=== RESULT ===");
    Serial.print("Value: ");
    Serial.println(value);
    Serial.println("==============");

    delay(3000);
    flushRDM6300();
    isProcessing = false;
    currentUID = "";
    showWaitingScreen();
    return true;
  } else {
    Serial.println("No 'value' key in response");
    showError("No value in response", uid);
    return false;
  }
}

void sendToAPI(String uid) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Card Detected!");
  display.drawLine(0, 10, 128, 10, SH110X_WHITE);
  display.setCursor(0, 25);
  display.print("UID: ");
  display.println(uid);
  display.setCursor(0, 45);
  display.println("Contacting server...");
  display.display();

  if (WiFi.status() != WL_CONNECTED) {
    showError("WiFi Error", uid);
    return;
  }

  HTTPClient http;
  http.begin(secureClient, cfg_apiUrl);   // <-- uses dynamic URL
  http.addHeader("Content-Type", "application/json");
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(10000);

  String payload = "{\"uid\":\"" + uid + "\",\"check\":\"Astagfirullah^^^\"}";

  Serial.println("\n========== API REQUEST ==========");
  Serial.print("URL: ");
  Serial.println(cfg_apiUrl);
  Serial.print("UID (hex): ");
  Serial.println(uid);
  Serial.print("Payload: ");
  Serial.println(payload);

  int httpCode = http.POST(payload);
  String response = http.getString();

  Serial.print("HTTP Code: ");
  Serial.println(httpCode);
  Serial.println("Response: " + response);
  Serial.println("=================================\n");

  http.end();

  if (httpCode >= 200 && httpCode < 300) {
    bool success = processJSONResponse(response, uid);
    if (success) {
      lastProcessedUID = uid;
      lastProcessedTime = millis();
    }
  } else {
    showError("HTTP Error: " + String(httpCode), uid);
  }
}

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  Wire.begin();

  if (!display.begin(OLED_ADDR, true)) {
    Serial.println("SH1106 OLED init failed!");
  }
  display.setTextColor(SH110X_WHITE);
  Serial2.begin(9600, SERIAL_8N1, RDM6300_RX, RDM6300_TX);

  loadConfig();

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(15, 10);
  display.println("RFID Attendance");
  display.setCursor(25, 30);
  display.println("Connecting...");
  display.display();

  // ── ATTEMPT 1: NVS credentials ──────────────────────────────
  bool connected = false;

  if (cfg_ssid.length() > 0) {
    Serial.println("Trying NVS credentials: " + cfg_ssid);
    WiFi.begin(cfg_ssid.c_str(), cfg_password.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {  // ~10s
      delay(500);
      Serial.print(".");
      attempts++;
    }
    connected = (WiFi.status() == WL_CONNECTED);
  }

  // ── ATTEMPT 2: Fallback to hardcoded credentials ─────────────
  if (!connected) {
    Serial.println("\nNVS credentials failed. Trying fallback...");

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 10);
    display.println("NVS WiFi Failed!");
    display.setCursor(0, 30);
    display.println("Trying fallback...");
    display.display();

    WiFi.disconnect(true);
    delay(500);
    WiFi.begin(DEFAULT_SSID, DEFAULT_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {  // ~10s
      delay(500);
      Serial.print(".");
      attempts++;
    }
    connected = (WiFi.status() == WL_CONNECTED);

    if (connected) {
      // Temporarily override runtime config so API calls still work
      // (API URL keeps its NVS value; only WiFi fell back)
      Serial.println("\nConnected via fallback credentials.");
    }
  }

  // ── Result ───────────────────────────────────────────────────
  if (connected) {
    Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());
    secureClient.setInsecure();

    display.clearDisplay();
    display.setCursor(10, 10);
    display.println("WiFi Connected");
    display.setCursor(0, 30);
    display.print("IP: ");
    display.println(WiFi.localIP().toString());
    display.setCursor(0, 50);
    display.println("Config: /config");
    display.display();

    server.on("/",       HTTP_GET,  handleRoot);
    server.on("/config", HTTP_GET,  handleGetConfig);
    server.on("/save",   HTTP_POST, handleSave);
    server.begin();
    Serial.println("Web config server started.");
  } else {
    Serial.println("\nAll WiFi attempts failed!");
    display.clearDisplay();
    display.setCursor(0, 10);
    display.println("WiFi Failed!");
    display.setCursor(0, 30);
    display.println("Both NVS &");
    display.setCursor(0, 45);
    display.println("fallback failed!");
    display.display();
  }

  delay(2000);
  showWaitingScreen();
}

// ========== LOOP ==========
void loop() {
  server.handleClient();  // <-- handles web requests in background

  // Timeout guard
  if (isProcessing && (millis() - processingStartTime > processingTimeout)) {
    Serial.println("Processing timeout - resetting");
    flushRDM6300();
    isProcessing = false;
    currentUID = "";
    showWaitingScreen();
  }

  if (!isProcessing) {
    while (Serial2.available()) {
      char c = Serial2.read();
      lastCharTime = millis();

      if (c == 0x02) {
        cardData = "";
        reading = true;
      } else if (c == 0x03 && reading) {
        reading = false;
        String uid = extractUID(cardData);

        if (uid != "") {
          bool isDuplicate = (uid == lastProcessedUID && (millis() - lastProcessedTime < duplicateCooldown));

          if (!isDuplicate) {
            Serial.print("Processing new card: ");
            Serial.println(uid);
            isProcessing = true;
            currentUID = uid;
            processingStartTime = millis();
            sendToAPI(uid);
          } else {
            Serial.println("Duplicate card ignored (cooldown active)");
            display.clearDisplay();
            display.setTextSize(1);
            display.setCursor(0, 0);
            display.println("Duplicate Card");
            display.drawLine(0, 10, 128, 10, SH110X_WHITE);
            display.setCursor(0, 30);
            display.println("Please wait...");
            display.display();
            delay(1500);
            flushRDM6300();
            showWaitingScreen();
          }
        }
        cardData = "";
      } else if (reading) {
        cardData += c;
      }
    }

    if (reading && (millis() - lastCharTime > timeout)) {
      reading = false;
      cardData = "";
    }
  }

  delay(10);
}
