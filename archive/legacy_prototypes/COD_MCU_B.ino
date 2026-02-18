/* MK-11 IOT SCOUT (Real AI Edition)
   =================================
   ROLE: 
     1. "Sentient" Logging using Google Gemini API.
     2. Servo Animation (Visuals).
     3. Multi-WiFi Auto-Connect.
     4. Cloud Upload (Discord/Serial).
   
   HARDWARE: Servo on Pin 12.
   CONNECTION: TX Pin 17 (MCU B) <-> RX Pin 16 (MCU A).
*/

#include <ESP32Servo.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// --- CONFIG ---
const int SERVO_PIN = 12;
#define RXD2 16 // Listen to Commander

// --- MULTI-WIFI CREDENTIALS ---
struct Creds { const char* ssid; const char* pass; };
Creds wifiList[] = {
  {"Excitel_SIVAJAY_2.4Ghz", "good4$all"},
  {"AndroidAPD31F", "sivandroid"},
  {"Hotspot", "hotspot_pass"}
};
const int WIFI_COUNT = sizeof(wifiList)/sizeof(wifiList[0]);

// --- AI CONFIGURATION (THE BRAIN) ---
// 1. Get Key: https://aistudio.google.com/
const char* GEMINI_API_KEY = "AIzaSyD5bi3-A4MLY6m6_ff6P7sAZVTs4xRvT_c"; 

// 2. Discord Webhook (Optional - leave empty to just log to Serial)
const char* DISCORD_WEBHOOK = "https://discord.com/api/webhooks/1473652871399604256/iq4bintrsZVC_O9KngQatk1gsjaW6uzQIRF1vh2VlQ6OahpK-XNvdL-G9j81jpTXDi9h"; 

// 3. The Robot's Personality (Prompt Engineering)
const String AI_SYSTEM_PROMPT = "You are MK-11, a sarcastic but efficient sci-fi logistics droid. Write a very short (max 15 words) status log for this event: ";

Servo myServo;

// Variables
int pos = 90;
int sweepState = 0; 
unsigned long lastMove = 0;
unsigned long waitStart = 0;
bool wifiConnected = false;

void connectToWiFi() {
  WiFi.mode(WIFI_AP_STA);
  
  // Debug Hotspot
  WiFi.softAP("SciFi-AI-Node", "12345678");
  Serial.print("AI NODE IP: "); Serial.println(WiFi.softAPIP());

  Serial.println("SEARCHING FOR INTERNET UPLINK...");
  
  for(int i=0; i<WIFI_COUNT; i++) {
    Serial.print("Connecting to: "); Serial.print(wifiList[i].ssid);
    WiFi.begin(wifiList[i].ssid, wifiList[i].pass);
    
    int attempts = 0;
    while(WiFi.status() != WL_CONNECTED && attempts < 15) { 
      delay(200); Serial.print("."); attempts++;
    }
    
    if(WiFi.status() == WL_CONNECTED) {
      Serial.println("\n[ONLINE] UPLINK ESTABLISHED.");
      wifiConnected = true;
      return;
    }
    Serial.println(" Failed.");
  }
  Serial.println("[OFFLINE] AI SYSTEMS DISABLED.");
}

void setup() {
  Serial.begin(115200);
  
  // Listen to Commander (RX only needed here technically, but using Serial2 standard)
  Serial2.begin(115200, SERIAL_8N1, RXD2, 17); 

  myServo.attach(SERVO_PIN, 500, 2400);
  myServo.write(90);

  connectToWiFi();
}

// --- REAL AI GENERATION ---
String generateAIResponse(String event) {
  if(!wifiConnected) return "Offline. Event: " + event;
  if(String(GEMINI_API_KEY).length() < 10) return "No API Key. Event: " + event;

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure(); // Skip cert check for speed
  
  // Gemini 1.5 Flash Endpoint (Fast & Cheap/Free)
  String url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent?key=" + String(GEMINI_API_KEY);
  
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  
  // JSON Payload for Gemini
  String payload = "{ \"contents\": [{ \"parts\": [{ \"text\": \"" + AI_SYSTEM_PROMPT + event + "\" }] }] }";
  
  int httpCode = http.POST(payload);
  String result = "AI Error";
  
  if(httpCode == 200) {
    String response = http.getString();
    // Simple parsing to extract the text (Avoiding heavy JSON library)
    int textStart = response.indexOf("\"text\": \"");
    if(textStart > 0) {
      textStart += 9;
      int textEnd = response.indexOf("\"", textStart);
      result = response.substring(textStart, textEnd);
      // Clean up newlines which JSON might escape
      result.replace("\\n", " "); 
    }
  } else {
    Serial.printf("AI Request Failed: %d\n", httpCode);
    result = "Connection Error during AI processing.";
  }
  
  http.end();
  return result;
}

// --- DISCORD UPLOAD ---
void sendToDiscord(String message) {
  if(!wifiConnected || String(DISCORD_WEBHOOK) == "") return;
  
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  
  http.begin(client, DISCORD_WEBHOOK);
  http.addHeader("Content-Type", "application/json");
  
  String json = "{\"content\": \"🤖 **MK-11 LOG:** " + message + "\"}";
  http.POST(json);
  http.end();
}

void processEvent(String msg) {
  Serial.println("\n[EVENT DETECTED]: " + msg);
  
  // 1. Generate AI Response (This will block for ~1-2s, creating a "Thinking" pause)
  String aiResponse = generateAIResponse(msg);
  Serial.println("[AI]: " + aiResponse);
  
  // 2. Upload to Discord (Optional)
  sendToDiscord(aiResponse);
}

void loop() {
  // 1. LISTEN FOR COMMANDER
  if(Serial2.available()) {
    String msg = Serial2.readStringUntil('\n');
    msg.trim();
    if(msg.length() > 0) {
      processEvent(msg);
    }
  }

  // 2. SERVO ANIMATION 
  // (Will pause naturally during AI generation, adding realism)
  if(sweepState != 2 && millis() - lastMove > 20) {
    lastMove = millis();
    
    if (sweepState == 0) { // Scan Left
      pos++;
      if (pos >= 130) sweepState = 1;
    } 
    else if (sweepState == 1) { // Scan Right
      pos--;
      if (pos <= 50) sweepState = 3;
    }
    else if (sweepState == 3) { // Center
      pos++;
      if (pos >= 90) {
        sweepState = 2; 
        waitStart = millis();
      }
    }
    myServo.write(pos);
  }
  
  // 3. WAIT STATE
  if (sweepState == 2) {
    if (millis() - waitStart > 3000) {
      sweepState = 0; 
    }
  }
}