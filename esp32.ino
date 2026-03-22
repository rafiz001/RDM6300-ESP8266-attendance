#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// ========== PIN DEFINITIONS ==========
// RDM6300 on Hardware Serial2
#define RDM6300_RX 16  // GPIO16 (RX2)
#define RDM6300_TX 17  // GPIO17 (TX2) — not used but required for Serial2.begin()

// OLED — 1.3" SH1106 via I2C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_ADDR     0x3C
// ESP32 default I2C: SDA=21, SCL=22 (no need to call Wire.begin with pins)

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ========== WIFI CREDENTIALS ==========
const char* ssid = "Huzaifa";
const char* password = "Huzaifa@1234";

// ========== API CONFIGURATION ==========
const char* serverUrl = "https://goldenschoolbd.com/app/i/attendance/iot.php";

// ========== VARIABLES ==========
String cardData = "";
bool reading = false;
unsigned long lastCharTime = 0;
const unsigned long timeout = 100;

// State management
bool isProcessing = false;
String currentUID = "";
unsigned long processingStartTime = 0;
const unsigned long processingTimeout = 15000;

// Duplicate prevention
String lastProcessedUID = "";
unsigned long lastProcessedTime = 0;
const unsigned long duplicateCooldown = 5000;

WiFiClientSecure secureClient;

// ========== FLUSH RDM6300 BUFFER ==========
void flushRDM6300() {
  unsigned long flushStart = millis();
  while (millis() - flushStart < 200) {
    while (Serial2.available()) Serial2.read();
    delay(10);
  }
}

// ========== EXTRACT UID ==========
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
  display.setCursor(20, 10);
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
  display.setCursor(0, 0);
  display.println("Error!");
  display.drawLine(0, 10, 128, 10, SH110X_WHITE);
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
    const char* value = doc["value"];

    Serial.print("Value from server: ");
    Serial.println(value);

    unsigned long uidDecimal = strtoul(uid.c_str(), NULL, 16);
    Serial.print("UID (decimal): ");
    Serial.println(uidDecimal);

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Card Processed!");
    display.drawLine(0, 10, 128, 10, SH110X_WHITE);
    display.setCursor(0, 20);
    display.print("UID: ");
    display.println(uid);
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
  http.begin(secureClient, serverUrl);
  http.addHeader("Content-Type", "application/json");
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(10000);

  String payload = "{\"uid\":\"" + uid + "\",\"check\":\"Astagfirullah^^^\"}";

  Serial.println("\n========== API REQUEST ==========");
  Serial.print("URL: ");      Serial.println(serverUrl);
  Serial.print("UID (hex): "); Serial.println(uid);
  Serial.print("UID (dec): "); Serial.println(strtoul(uid.c_str(), NULL, 16));
  Serial.print("Payload: ");  Serial.println(payload);

  int httpCode = http.POST(payload);
  String response = http.getString();

  Serial.print("HTTP Code: "); Serial.println(httpCode);
  Serial.println("Response:");  Serial.println(response);
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

void setup() {
  Serial.begin(115200);

  // I2C uses default ESP32 pins: SDA=21, SCL=22
  Wire.begin();

  if (!display.begin(OLED_ADDR, true)) {  // true = reset
    Serial.println("SH1106 OLED init failed!");
  }
  display.setTextColor(SH110X_WHITE);

  // Hardware Serial2 for RDM6300
  Serial2.begin(9600, SERIAL_8N1, RDM6300_RX, RDM6300_TX);

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(15, 10);
  display.println("RFID Attendance");
  display.setCursor(25, 30);
  display.println("Connecting...");
  display.display();

  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected");
    secureClient.setInsecure();

    display.clearDisplay();
    display.setCursor(10, 20);
    display.println("WiFi Connected");
    display.setCursor(15, 40);
    display.println("Ready for cards");
    display.display();
  } else {
    Serial.println("\nWiFi failed!");
    display.clearDisplay();
    display.setCursor(0, 20);
    display.println("WiFi Failed!");
    display.setCursor(0, 40);
    display.println("Check credentials");
    display.display();
  }

  delay(2000);
  showWaitingScreen();
}

void loop() {
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
          bool isDuplicate = (uid == lastProcessedUID &&
                             (millis() - lastProcessedTime < duplicateCooldown));

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

    // Timeout for incomplete reads
    if (reading && (millis() - lastCharTime > timeout)) {
      reading = false;
      cardData = "";
    }
  }

  delay(10);
}
