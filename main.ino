#include <SoftwareSerial.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// ========== PIN DEFINITIONS ==========
#define RDM6300_RX 4  // GPIO4 (D2)
SoftwareSerial rdm6300(RDM6300_RX, 12);

// OLED connections
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR 0x3C
#define OLED_SDA 2   // GPIO2 (D4)
#define OLED_SCL 0   // GPIO0 (D3)

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ========== WIFI CREDENTIALS ==========
const char* ssid = "---";
const char* password = "---";

// ========== API CONFIGURATION ==========
const char* serverUrl = "https://---------------------.php";

// ========== VARIABLES ==========
String cardData = "";
bool reading = false;
unsigned long lastCharTime = 0;
const unsigned long timeout = 100;

// State management
bool isProcessing = false;
String currentUID = "";
unsigned long processingStartTime = 0;
const unsigned long processingTimeout = 15000; // 15 seconds max wait for API

// Duplicate prevention
String lastProcessedUID = "";
unsigned long lastProcessedTime = 0;
const unsigned long duplicateCooldown = 5000; // 5 seconds cooldown for same card

WiFiClientSecure secureClient;

// ========== FLUSH RDM6300 BUFFER ==========
// Drains any leftover bytes from the RDM6300 serial buffer.
// Call this after processing a card so buffered duplicate packets
// don't trigger a second scan the moment isProcessing clears.
void flushRDM6300() {
  unsigned long flushStart = millis();
  while (millis() - flushStart < 200) {
    while (rdm6300.available()) rdm6300.read();
    delay(10);
  }
}

// ========== EXTRACT UID ==========
// The RDM6300 frame between STX(0x02) and ETX(0x03) contains 12 ASCII hex chars:
//   [2 version chars][8 tag chars][2 checksum chars]
// The number printed on the card corresponds to the 8 tag chars (bytes 2-9).
// We read them as-is (they are already ASCII hex), which matches the card number.
String extractUID(String data) {
  if (data.length() < 12) {
    return "";
  }

  // Skip 2-char version prefix, take 8-char tag ID
  String tagHex = data.substring(2, 10);
  tagHex.toUpperCase();

  // Validate it's proper hex
  for (int i = 0; i < tagHex.length(); i++) {
    char c = tagHex[i];
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) {
      Serial.println("Invalid hex in UID, discarding.");
      return "";
    }
  }

  return tagHex; // e.g. "00AABBCC" — matches the number printed on the card
}

void showWaitingScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(20, 10);
  display.println("Attendance System");
  display.drawLine(0, 25, 128, 25, SSD1306_WHITE);
  display.setCursor(0, 35);
  display.println("Scan your card");
  display.setCursor(0, 50);
  if (WiFi.status() == WL_CONNECTED) {
    display.print("WiFi: Connected");
  } else {
    display.print("WiFi: Disconnected");
  }
  display.display();
  Serial.println("Ready for next card...");
}

void showError(String errorMsg, String uid) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Error!");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
  display.setCursor(0, 20);
  display.print("UID: ");
  display.println(uid);
  display.setCursor(0, 40);
  if (errorMsg.length() > 20) {
    display.println(errorMsg.substring(0, 18) + "...");
  } else {
    display.println(errorMsg);
  }
  display.display();

  Serial.print("ERROR: ");
  Serial.println(errorMsg);

  delay(3000);

  // Flush buffer BEFORE clearing isProcessing so no buffered duplicates slip through
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

    // Also print decimal equivalent of UID (matches most card label formats)
    unsigned long uidDecimal = strtoul(uid.c_str(), NULL, 16);
    Serial.print("UID (decimal): ");
    Serial.println(uidDecimal);

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Card Processed!");
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
    display.setCursor(0, 20);
    display.print("UID: ");
    display.println(uid);

    display.setCursor(0, 40);
    String valueStr = String(value);
    if (valueStr.length() > 8) {
      display.setTextSize(1);
    } else {
      display.setTextSize(2);
    }
    display.println(valueStr);
    display.display();

    Serial.println("=== RESULT ===");
    Serial.print("Value: ");
    Serial.println(value);
    Serial.println("==============");

    delay(3000);

    // Flush buffer BEFORE clearing isProcessing
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
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
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
  Serial.print("URL: ");
  Serial.println(serverUrl);
  Serial.print("UID (hex): ");
  Serial.println(uid);
  Serial.print("UID (decimal): ");
  Serial.println(strtoul(uid.c_str(), NULL, 16));
  Serial.print("Payload: ");
  Serial.println(payload);

  int httpCode = http.POST(payload);
  String response = http.getString();

  Serial.print("HTTP Code: ");
  Serial.println(httpCode);
  Serial.println("Response:");
  Serial.println(response);
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

  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED failed!");
  }

  rdm6300.begin(9600);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
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
  // Timeout guard: if API never responds, reset after processingTimeout
  if (isProcessing && (millis() - processingStartTime > processingTimeout)) {
    Serial.println("Processing timeout - resetting");
    flushRDM6300();
    isProcessing = false;
    currentUID = "";
    showWaitingScreen();
  }

  // Only read new cards if NOT currently processing one
  if (!isProcessing) {
    while (rdm6300.available()) {
      char c = rdm6300.read();
      lastCharTime = millis();

      if (c == 0x02) {
        cardData = "";
        reading = true;
      }
      else if (c == 0x03 && reading) {
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
            display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
            display.setCursor(0, 30);
            display.println("Please wait...");
            display.display();

            delay(1500);
            flushRDM6300(); // flush duplicates that built up during the delay
            showWaitingScreen();
          }
        }

        cardData = "";
      }
      else if (reading) {
        cardData += c;
      }
    }

    // Timeout protection for incomplete reads
    if (reading && (millis() - lastCharTime > timeout)) {
      reading = false;
      cardData = "";
    }
  }

  delay(10);
}
