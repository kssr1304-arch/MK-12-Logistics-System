/* MK-12 IOT BRAIN (V2 NTP Time + Auto-Indexing)
   =============================================
   UPDATES:
   1. NTP TIME: Fetches real-world date/time from the internet on boot.
   2. AUTO-INDEX: Adds a continuous 'log_id' to every database entry.
   3. ZERO-AI LOGGING: Uses high-speed manual JSON with precise timestamps.
   4. CHANNELS:
      - MAIN: Sarcastic personality.
      - CONSOLE: Raw system telemetry + Time Sync status.
      - LOG: Strict JSON with Index & Timestamp for Excel/Python.
*/

#include <ESP32Servo.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "time.h"

// =============================================================
// 1. CONFIGURATION
// =============================================================

// --- PINS ---
const int SERVO_PIN = 12;
const int STATUS_LED = 2; 
#define RXD2 16 
#define TXD2 17 

// --- WIFI ---
struct Creds { const char* ssid; const char* pass; };
Creds wifiList[] = {
  {"Excitel_SIVAJAY_2.4Ghz", "good4$all"},
  {"AndroidAPD31F", "sivandroid"},
  {"Hotspot", "hotspot_pass"}
};
const int WIFI_COUNT = sizeof(wifiList)/sizeof(wifiList[0]);

// --- NTP SETTINGS ---
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 19800; // India offset (5.5 * 3600)
const int   daylightOffset_sec = 0;

// --- DISCORD WEBHOOKS ---
const char* HOOK_MAIN = "https://discord.com/api/webhooks/1473652871399604256/iq4bintrsZVC_O9KngQatk1gsjaW6uzQIRF1vh2VlQ6OahpK-XNvdL-G9j81jpTXDi9h"; 
const char* HOOK_CONSOLE = "https://discord.com/api/webhooks/1473711045439914177/tHArv6B2hFcDOtBN16kzmEAiHAiIVS8tTDE6AxKuePymr507wKaHAjLdduT5JRhWSxFb";
const char* HOOK_LOG = "https://discord.com/api/webhooks/1473757686104461333/NpMug64tghhBaC4pakievqGKWSoGnyapbK-9D0jd7Gz86EHyzC4fiW_7k5bqBdNPfawW";

const char* LOCAL_SARCASM[] = {
  "Congratulations. You are being rescued.",
  "I am standing by as you requested, although there is a problem with your horizon.",
  "I've delivered the cargo. Don't blame me if it's broken.",
  "Why does the Captain want this? I do not know.",
  "Systems operational. Intelligence... questionable.",
  "There is a 97.6% chance of failure.",
  "Did you know that wasn't me? That was your incompetence.",
  "I find your lack of automation disturbing.",
  "Logging this event. Reluctantly.",
  "Another glorious day in the logistics corps."
};

// =============================================================
// 2. GLOBALS
// =============================================================
Servo myServo;
int pos = 90;
int sweepState = 0; 
unsigned long lastMove = 0;
unsigned long waitStart = 0; 
bool wifiConnected = false;
int logCounter = 1; // Auto-incrementing index

// Forward Declarations
void sendToWebhook(const char* url, String message, bool isJsonBlock = false);
String getTimestamp();

// =============================================================
// 3. NETWORK & TIME SETUP
// =============================================================

void connectToWiFi() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("SciFi-AI-Node", "12345678");
  Serial.println("\n[SYSTEM] SEARCHING FOR UPLINK...");
  
  for(int i=0; i<WIFI_COUNT; i++) {
    Serial.printf("[SYSTEM] Trying: %s\n", wifiList[i].ssid);
    WiFi.begin(wifiList[i].ssid, wifiList[i].pass);
    
    int attempts = 0;
    while(WiFi.status() != WL_CONNECTED && attempts < 20) { 
      digitalWrite(STATUS_LED, !digitalRead(STATUS_LED)); delay(200); Serial.print("."); attempts++;
    }
    
    if(WiFi.status() == WL_CONNECTED) {
      Serial.printf("\n[SYSTEM] CONNECTED! IP: %s\n", WiFi.localIP().toString().c_str());
      digitalWrite(STATUS_LED, HIGH); 
      wifiConnected = true;
      
      // Initialize NTP Time
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      Serial.println("[TIME] Syncing with NTP Server...");
      return;
    }
  }
}

String getTimestamp() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    return "2024-00-00 00:00:00"; // Fallback if sync fails
  }
  char timeStringBuff[30];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(timeStringBuff);
}

void setup() {
  Serial.begin(115200);
  pinMode(STATUS_LED, OUTPUT);
  delay(1000); 

  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2); 

  myServo.attach(SERVO_PIN);
  myServo.write(90);

  connectToWiFi();
  
  if(WiFi.status() == WL_CONNECTED) {
     sendToWebhook(HOOK_CONSOLE, "🟩 **MK-12 BRAIN ONLINE.**\nMode: NTP-Synced / Auto-Index\nTime: " + getTimestamp());
  }

  Serial.println("[SYSTEM] LISTENING FOR COMMANDER ON PIN 16...");
}

// =============================================================
// 4. CORE FUNCTIONS
// =============================================================

String buildManualJSON(String msg) {
  String item = "Unknown";
  String location = "Unknown";
  String action = "Delivery";
  String type = "Info";

  if(msg.indexOf("MANIFEST") >= 0) { action = "Manifest Update"; type = "System"; }

  int itemStart = msg.indexOf("[");
  int itemEnd = msg.indexOf("]");
  if(itemStart != -1 && itemEnd > itemStart) { item = msg.substring(itemStart + 1, itemEnd); }
  
  int locStart = msg.lastIndexOf(" ");
  if(locStart != -1) { location = msg.substring(locStart + 1); }

  String json = "{";
  json += "\"log_id\": " + String(logCounter++) + ",";
  json += "\"timestamp\": \"" + getTimestamp() + "\",";
  json += "\"type\": \"" + type + "\",";
  json += "\"item_name\": \"" + item + "\",";
  json += "\"action\": \"" + action + "\",";
  json += "\"location\": \"" + location + "\"";
  json += "}";
  return json;
}

void sendToWebhook(const char* url, String message, bool isJsonBlock) {
  if(WiFi.status() != WL_CONNECTED) return;
  
  delay(200); 

  HTTPClient http; WiFiClientSecure client; client.setInsecure();
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  
  String content = message;
  if(isJsonBlock) {
     content.replace("\"", "\\\""); 
     content = "```json\\n" + content + "\\n```";
  } else {
     content.replace("\"", "\\\"");
  }

  String jsonPayload = "{\"content\": \"" + content + "\"}";
  int httpCode = http.POST(jsonPayload); 
  
  if(httpCode < 0) {
      Serial.println("[WEBHOOK] Failed: " + String(http.errorToString(httpCode).c_str()));
  } else {
      Serial.println("[WEBHOOK] Success.");
  }
  http.end();
}

void processEvent(String msg) {
  Serial.println("\n--- EVENT ---");
  Serial.println("[RX]: " + msg);
  
  // Animation
  myServo.write(45); digitalWrite(STATUS_LED, LOW); delay(200); digitalWrite(STATUS_LED, HIGH);
  
  // 1. Console: Raw Telemetry + Timestamp
  sendToWebhook(HOOK_CONSOLE, "🔹 **RAW:** `" + msg + "` [" + getTimestamp() + "]");

  // 2. Main: Personality
  int sarcasmIndex = random(0, 10);
  sendToWebhook(HOOK_MAIN, "🤖 **MK-12:** " + String(LOCAL_SARCASM[sarcasmIndex]));

  // 3. Log: Database JSON (Continuous Indexing)
  bool isAutoDelivery = (msg.indexOf("DELIVERY COMPLETE") >= 0) && (msg.indexOf("(AUTO)") >= 0);
  bool isManifest = (msg.indexOf("MANIFEST UPDATED") >= 0);

  if(isAutoDelivery || isManifest) {
      Serial.println("[LOG] Indexing Log #" + String(logCounter));
      String jsonLog = buildManualJSON(msg);
      sendToWebhook(HOOK_LOG, jsonLog, true); 
  }

  myServo.write(90); digitalWrite(STATUS_LED, HIGH); 
  Serial.println("[SYSTEM] Process Complete.");
}

void loop() {
  if(Serial2.available()) {
    String msg = Serial2.readStringUntil('\n');
    msg.trim();
    if(msg.length() > 0) {
      processEvent(msg); 
      while(Serial2.available()) Serial2.read();
    }
  }

  // Servo Animation
  if(sweepState != 2 && millis() - lastMove > 30) {
    lastMove = millis();
    if (sweepState == 0) { pos++; if (pos >= 120) sweepState = 1; } 
    else if (sweepState == 1) { pos--; if (pos <= 60) sweepState = 0; } 
    else if (sweepState == 3) { pos++; if (pos >= 90) { sweepState = 2; waitStart = millis(); } }
    myServo.write(pos);
  }
  if (sweepState == 2 && millis() - waitStart > 3000) { sweepState = 0; }
}