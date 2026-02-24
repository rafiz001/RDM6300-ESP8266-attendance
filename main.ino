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
const char* serverUrl = "https://...................php";

// ========== VARIABLES ==========
String cardData = "";
bool reading = false;
unsigned long lastCharTime = 0;
const unsigned long timeout = 100;

// State management
bool isProcessing = false;        // True when waiting for API response
String currentUID = "";            // UID of current card being processed
unsigned long processingStartTime = 0;
const unsigned long processingTimeout = 15000; // 15 seconds max wait for API

// Duplicate prevention
String lastProcessedUID = "";      // Last UID that was successfully processed
unsigned long lastProcessedTime = 0;
const unsigned long duplicateCooldown = 5000; // 5 seconds cooldown for same card

WiFiClientSecure secureClient;

void setup() {
  Serial.begin(115200);
  
  Wire.begin(OLED_SDA, OLED_SCL);
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED failed!");
  }
  
  rdm6300.begin(9600);
  
  // Show splash screen
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(15, 10);
  display.println("RFID Attendance");
  display.setCursor(25, 30);
  display.println("Connecting...");
  display.display();
  
  // Connect to WiFi
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
  // Check for processing timeout
  if (isProcessing && (millis() - processingStartTime > processingTimeout)) {
    Serial.println("Processing timeout - resetting");
    isProcessing = false;
    currentUID = "";
    showWaitingScreen();
  }
  
  // Only read new cards if NOT processing
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
          // Check if this is a duplicate of last processed card
          bool isDuplicate = (uid == lastProcessedUID && 
                             (millis() - lastProcessedTime < duplicateCooldown));
          
          if (!isDuplicate) {
            Serial.print("Processing new card: ");
            Serial.println(uid);
            
            // Start processing this card
            isProcessing = true;
            currentUID = uid;
            processingStartTime = millis();
            
            // Send to API
            sendToAPI(uid);
          } else {
            Serial.println("Duplicate card ignored (cooldown period)");
            
            // Show quick message about duplicate
            display.clearDisplay();
            display.setCursor(0, 0);
            display.println("Duplicate Card");
            display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
            display.setCursor(0, 30);
            display.println("Please wait...");
            display.display();
            delay(1500);
            showWaitingScreen();
          }
        }
        
        cardData = "";
      }
      else if (reading) {
        cardData += c;
      }
    }
    
    // Timeout protection for reading
    if (reading && (millis() - lastCharTime > timeout)) {
      reading = false;
      cardData = "";
    }
  }
  
  delay(10);
}

String extractUID(String data) {
  if (data.length() < 10) {
    return "";
  }
  
  String uidHex = "";
  for (int i = 0; i < 5; i++) {
    byte b = data[i];
    if (b < 0x10) uidHex += "0";
    uidHex += String(b, HEX);
  }
  uidHex.toUpperCase();
  
  return uidHex;
}

void sendToAPI(String uid) {
  // Show sending on OLED
  display.clearDisplay();
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
  Serial.print("UID: ");
  Serial.println(uid);
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
  
  // Process the response
  if (httpCode >= 200 && httpCode < 300) {
    // Parse JSON to get "value" key
    bool success = processJSONResponse(response, uid);
    
    if (success) {
      // Store this UID as successfully processed
      lastProcessedUID = uid;
      lastProcessedTime = millis();
    }
  } else {
    // Handle HTTP error
    showError("HTTP Error: " + String(httpCode), uid);
  }
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
  
  // Check if "value" key exists
  if (doc.containsKey("value")) {
    const char* value = doc["value"];
    
    Serial.print("Value from server: ");
    Serial.println(value);
    
    // Display the value on OLED
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Card Processed!");
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
    display.setCursor(0, 20);
    display.print("UID: ");
    display.println(uid);
    
    // Show the value prominently
    display.setCursor(0, 40);
    display.setTextSize(2);
    
    String valueStr = String(value);
    if (valueStr.length() > 8) {
      display.setTextSize(1);
    }
    display.println(valueStr);
    
    display.display();
    
    // Also print to serial
    Serial.println("=== RESULT ===");
    Serial.print("Value: ");
    Serial.println(value);
    Serial.println("==============");
    
    // Wait for 3 seconds showing the result
    delay(3000);
    
    // Reset processing state
    isProcessing = false;
    currentUID = "";
    showWaitingScreen();
    
    return true;
    
  } else {
    // No "value" key found
    Serial.println("No 'value' key in response");
    showError("No value in response", uid);
    return false;
  }
}

void showError(String errorMsg, String uid) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Error!");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
  display.setCursor(0, 20);
  display.print("UID: ");
  display.println(uid);
  display.setCursor(0, 40);
  display.setTextSize(1);
  
  // Truncate long error messages
  if (errorMsg.length() > 20) {
    display.println(errorMsg.substring(0, 18) + "...");
  } else {
    display.println(errorMsg);
  }
  
  display.display();
  
  Serial.print("ERROR: ");
  Serial.println(errorMsg);
  
  // Wait 3 seconds showing error
  delay(3000);
  
  // Reset and go back to waiting
  isProcessing = false;
  currentUID = "";
  showWaitingScreen();
}

void showWaitingScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(20, 10);
  display.println("Attendance System");
  display.drawLine(0, 25, 128, 25, SSD1306_WHITE);
  display.setCursor(0, 35);
  display.println("Scan your card");
  
  // Show WiFi status
  display.setCursor(0, 50);
  if (WiFi.status() == WL_CONNECTED) {
    display.print("WiFi: Connected");
  } else {
    display.print("WiFi: Disconnected");
  }
  
  display.display();
  
  Serial.println("Ready for next card...");
}
