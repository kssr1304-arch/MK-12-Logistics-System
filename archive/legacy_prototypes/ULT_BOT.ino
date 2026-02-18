/*
  Smart Delivery Bot - GOLD MASTER (Robust RFID Edition)
  ======================================================
  - FIX: SPI Speed lowered to 4MHz to fix "0xEE/0xB2" errors.
  - FIX: Startup Health Check loop (Retries connection).
  - FEATURE: Built-in RFID Hardware Check.
  - HARDWARE: Buzzer on Pin 2. RFID on Pin 5.
*/

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <MFRC522.h>

// =====================
// 1. PIN DEFINITIONS
// =====================

const int ENA = 25; const int IN1 = 26; const int IN2 = 27; 
const int ENB = 14; const int IN3 = 21; const int IN4 = 13; 
const int IR_LEFT_PIN = 32; const int IR_MID_PIN = 33; const int IR_RIGHT_PIN = 34; 
const int ULTRASONIC_TRIG = 4; const int ULTRASONIC_ECHO = 15;
const int BUZZER_PIN = 2; 

// RFID
const int RFID_SDA_PIN = 5; 
const int RFID_RST_PIN = 22;

// =====================
// 2. CONFIGURATION
// =====================

// TAG UIDs (Your specific tags)
const String TAG_A = "7255FE03";
const String TAG_B = "827EFC03";
const String TAG_C = "99F5FB03";

const bool INVERT_LEFT_MOTOR  = false; 
const bool INVERT_RIGHT_MOTOR = false; 
const bool INVERT_SENSORS = true; 

// REMOVED 'const' so sliders can change it
int AUTO_BASE_SPEED = 110; 

const int AUTO_TURN_SPEED = 140; 
const int PROX_SPEED = 140;
const int DRIVE_SPEED = 200; 
const int TURN_SPEED  = 180; 
const float OBSTACLE_DIST = 15.0;

// PID Defaults
float Kp = 60.0; 
float Kd = 35.0; 

// =====================
// 3. GLOBALS
// =====================
AsyncWebServer server(80);
MFRC522 rfid(RFID_SDA_PIN, RFID_RST_PIN);

bool autoMode = false;        
bool proxMode = false;        
bool manualLineFollow = false; 

// Navigation State
String currentTarget = ""; 
String lastSeenTag = "";      // To prevent re-reading same tag instantly
unsigned long lastTagTime = 0; // Debounce timer
int lastTurnDir = 0;
int lastError = 0;
unsigned long penaltyEndTime = 0; 

// Telemetry
String webLog = "SYSTEM READY";
float lastDistReading = 0.0;

const int PWM_FREQ = 1000;
const int PWM_RES  = 8;

// =====================
// 4. HTML INTERFACE
// =====================
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no">
  <title>MK-4 COMMAND</title>
  <style>
    @import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono&display=swap');
    body { background-color: #050505; color: #00f3ff; font-family: 'Share Tech Mono', monospace; margin: 0; padding: 10px; user-select: none; background-image: linear-gradient(rgba(0, 243, 255, 0.05) 1px, transparent 1px), linear-gradient(90deg, rgba(0, 243, 255, 0.05) 1px, transparent 1px); background-size: 30px 30px; }
    
    header { display: flex; justify-content: space-between; align-items: center; border-bottom: 2px solid #00f3ff; padding-bottom: 5px; margin-bottom: 15px; }
    h1 { margin: 0; font-size: 24px; text-shadow: 0 0 10px #00f3ff; letter-spacing: 2px; }
    .blink { animation: blinker 1s linear infinite; color: #ff0055; font-weight: bold; }
    @keyframes blinker { 50% { opacity: 0; } }
    
    .grid-container { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; max-width: 600px; margin: 0 auto; }
    .panel { background: rgba(10, 20, 30, 0.8); border: 1px solid #00f3ff; padding: 15px; position: relative; clip-path: polygon(15px 0, 100% 0, 100% calc(100% - 15px), calc(100% - 15px) 100%, 0 100%, 0 15px); box-shadow: 0 0 15px rgba(0, 243, 255, 0.1); }
    .panel-title { font-size: 12px; color: #00ff88; letter-spacing: 1px; position: absolute; top: 2px; right: 15px; opacity: 0.8; }

    .d-pad { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; margin-top: 10px; }
    .btn { background: rgba(0, 243, 255, 0.1); color: #00f3ff; border: 1px solid #00f3ff; border-radius: 5px; height: 50px; font-size: 24px; display: flex; align-items: center; justify-content: center; }
    .btn:active { background: #00f3ff; color: #000; box-shadow: 0 0 15px #00f3ff; }
    .stop-btn { border-color: #ff0055; color: #ff0055; background: rgba(255, 0, 85, 0.1); }
    .stop-btn:active { background: #ff0055; color: #fff; box-shadow: 0 0 15px #ff0055; }
    
    .honk-btn { width: 100%; margin-top: 10px; background: rgba(255, 235, 59, 0.1); color: #ffeb3b; border: 1px solid #ffeb3b; height: 40px; font-weight: bold; }
    .honk-btn:active { background: #ffeb3b; color: #000; }
    .hold-btn { grid-column: 1 / -1; font-size: 14px; border-color: #00ff88; color: #00ff88; background: rgba(0, 255, 136, 0.1); }
    .hold-btn:active { background: #00ff88; color: #000; }

    .slider-box { margin-top: 10px; }
    .slider-label { font-size: 12px; display: flex; justify-content: space-between; }
    input[type=range] { width: 100%; -webkit-appearance: none; background: #333; height: 5px; border-radius: 5px; outline: none; }
    input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; width: 15px; height: 15px; background: #00ff88; border-radius: 50%; cursor: pointer; }

    .mission-btn { width: 100%; padding: 10px; margin-bottom: 5px; border: 1px solid #ff9800; color: #ff9800; background: rgba(255, 152, 0, 0.1); font-family: 'Share Tech Mono'; cursor: pointer; }
    .mission-btn:active { background: #ff9800; color: black; }

    .auto-toggle { width: 100%; padding: 15px; font-family: 'Share Tech Mono', monospace; font-size: 16px; border: 2px solid #333; background: #111; color: #555; cursor: pointer; margin-bottom: 10px; clip-path: polygon(10px 0, 100% 0, 100% calc(100% - 10px), calc(100% - 10px) 100%, 0 100%, 0 10px); }
    .auto-on { border-color: #00ff88; color: #00ff88; background: rgba(0, 255, 136, 0.1); box-shadow: 0 0 20px rgba(0, 255, 136, 0.2); }
    .prox-on { border-color: #00f3ff; color: #00f3ff; background: rgba(0, 243, 255, 0.1); box-shadow: 0 0 20px rgba(0, 243, 255, 0.2); }
    
    .console { grid-column: 1 / -1; height: 50px; background: #000; border: 1px solid #333; padding: 10px; font-size: 12px; color: #aaa; display: flex; flex-direction: column-reverse; }
    .log-line:first-child { color: #00f3ff; text-shadow: 0 0 5px #00f3ff; }
    
    .radar { width: 60px; height: 60px; border: 2px solid #00f3ff; border-radius: 50%; margin: 5px auto 15px auto; position: relative; background: radial-gradient(circle, rgba(0,243,255,0.2) 0%, transparent 70%); }
    .radar::after { content: ''; position: absolute; top: 50%; left: 50%; width: 50%; height: 2px; background: #00f3ff; transform-origin: 0 0; animation: scan 2s infinite linear; box-shadow: 0 0 10px #00f3ff; }
    @keyframes scan { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
    
    .telemetry-row { display: flex; justify-content: space-between; margin-bottom: 5px; font-size: 14px; }
    .val { color: #fff; font-weight: bold; }
    .dist-container { height: 10px; background: #111; border: 1px solid #333; margin-top: 5px; position: relative; overflow: hidden; }
    .dist-bar { height: 100%; width: 0%; background: linear-gradient(90deg, #00ff88, #ff0055); transition: width 0.2s; }
  </style>
</head>
<body>
  <div id="pingDisplay">PING: --ms</div>
  <header><h1>MK-4 COMMAND</h1></header>
  
  <div class="grid-container">
    <!-- CONTROLS -->
    <div class="panel">
      <div class="panel-title">MANUAL</div>
      <div class="d-pad">
        <div></div><div class="btn" ontouchstart="drive('F')" ontouchend="drive('S')" onmousedown="drive('F')" onmouseup="drive('S')">▲</div><div></div>
        <div class="btn" ontouchstart="drive('L')" ontouchend="drive('S')" onmousedown="drive('L')" onmouseup="drive('S')">◀</div>
        <div class="btn stop-btn" onclick="drive('S')">■</div>
        <div class="btn" ontouchstart="drive('R')" ontouchend="drive('S')" onmousedown="drive('R')" onmouseup="drive('S')">▶</div>
        <div></div><div class="btn" ontouchstart="drive('B')" ontouchend="drive('S')" onmousedown="drive('B')" onmouseup="drive('S')">▼</div><div></div>
        
        <div class="btn hold-btn" ontouchstart="drive('LF')" ontouchend="drive('S')" onmousedown="drive('LF')" onmouseup="drive('S')">HOLD: LINE TRACK</div>
      </div>
      <button class="btn honk-btn" onclick="honk()">AUDIO SIGNAL</button>
    </div>

    <!-- SENSOR & MODES -->
    <div class="panel">
      <div class="panel-title">SENSORS & MODES</div>
      <div class="radar"></div>
      
      <div class="telemetry-row"><span>PROXIMITY:</span><span class="val" id="distVal">-- cm</span></div>
      <div class="dist-container"><div class="dist-bar" id="distBar"></div></div><br>
      
      <button id="proxBtn" class="auto-toggle" onclick="toggleProx()">TARGET LOCK (HAND)</button>
      <button id="autoBtn" class="auto-toggle" onclick="toggleAuto()">ENGAGE AUTO-PILOT</button>
    </div>

    <!-- MISSION & PID -->
    <div class="panel" style="grid-column: 1 / -1;">
      <div class="panel-title">MISSION CONTROL</div>
      <div style="display:flex; gap:10px;">
        <button class="mission-btn" onclick="startMission('A')">TAG A</button>
        <button class="mission-btn" onclick="startMission('B')">TAG B</button>
        <button class="mission-btn" onclick="startMission('C')">TAG C</button>
      </div>
      
      <div class="slider-box">
        <div class="slider-label"><span>Kp (Turn Power): <span id="kpVal">60</span></span></div>
        <input type="range" min="0" max="100" value="60" oninput="updatePID('kp', this.value)">
      </div>
      <div class="slider-box">
        <div class="slider-label"><span>Kd (Stability): <span id="kdVal">35</span></span></div>
        <input type="range" min="0" max="100" value="35" oninput="updatePID('kd', this.value)">
      </div>
    </div>

    <div class="panel console" id="console"><div class="log-line">> SYSTEM READY</div></div>
  </div>

  <script>
    var isDriving = false;
    var lastPingTime = 0;

    function pollStatus() {
      if (isDriving) { setTimeout(pollStatus, 500); return; }
      
      lastPingTime = Date.now();
      var xhr = new XMLHttpRequest(); xhr.open("GET", "/status", true); xhr.timeout = 1000; 
      xhr.onload = function() {
        let latency = Date.now() - lastPingTime;
        document.getElementById('pingDisplay').innerText = "PING: " + latency + "ms";
        if (xhr.status === 200) updateTelemetry(JSON.parse(xhr.responseText));
        setTimeout(pollStatus, 250);
      };
      xhr.send();
    }
    pollStatus();

    function updateTelemetry(data) {
      document.getElementById('distVal').innerText = (data.dist > 500 ? ">500" : data.dist) + " cm";
      let percent = Math.max(0, Math.min(100, (50 - data.dist) * 2));
      document.getElementById('distBar').style.width = percent + "%";

      let aBtn = document.getElementById('autoBtn');
      let pBtn = document.getElementById('proxBtn');
      
      if (data.mode === "AUTO") { 
        aBtn.classList.add("auto-on"); pBtn.classList.remove("prox-on");
        aBtn.innerText = (data.target == "") ? "AUTO ENGAGED (FREE)" : "SEEKING TAG: " + data.target;
      } else if (data.mode === "PROXIMITY") {
        pBtn.classList.add("prox-on"); aBtn.classList.remove("auto-on");
      } else { 
        aBtn.classList.remove("auto-on"); pBtn.classList.remove("prox-on");
        aBtn.innerText = "ENGAGE AUTO-PILOT";
      }
      
      let consoleDiv = document.getElementById('console');
      if (consoleDiv.firstElementChild.innerText !== "> " + data.log) {
        let newLine = document.createElement("div"); newLine.className = "log-line"; newLine.innerText = "> " + data.log;
        consoleDiv.prepend(newLine); if (consoleDiv.children.length > 5) consoleDiv.removeChild(consoleDiv.lastChild);
      }
    }

    function startMission(tagID) {
      var xhr = new XMLHttpRequest(); xhr.open("GET", "/mission?id=" + tagID, true); xhr.send();
    }

    function updatePID(type, val) {
      if(type=='kp') document.getElementById('kpVal').innerText = val;
      if(type=='kd') document.getElementById('kdVal').innerText = val;
      var xhr = new XMLHttpRequest(); xhr.open("GET", "/setPID?type=" + type + "&val=" + val, true); xhr.send();
    }

    function drive(dir) { 
      var xhr = new XMLHttpRequest(); xhr.open("GET", "/drive?dir=" + dir, true); xhr.send(); 
    }
    
    function toggleAuto() { var xhr = new XMLHttpRequest(); xhr.open("GET", "/toggleAuto", true); xhr.send(); }
    function toggleProx() { var xhr = new XMLHttpRequest(); xhr.open("GET", "/toggleProx", true); xhr.send(); }
    function honk() { var xhr = new XMLHttpRequest(); xhr.open("GET", "/honk", true); xhr.send(); }
  </script>
</body>
</html>
)rawliteral";

// =====================
// 5. HELPER FUNCTIONS
// =====================

void setMotorRaw(int l, int r) {
  if (millis() < penaltyEndTime) { l = 0; r = 0; }
  l = constrain(l, -255, 255); r = constrain(r, -255, 255);
  if (INVERT_LEFT_MOTOR) l = -l; if (INVERT_RIGHT_MOTOR) r = -r;
  if(l>0){ digitalWrite(IN1,LOW); digitalWrite(IN2,HIGH); ledcWrite(ENA,l); } else { digitalWrite(IN1,HIGH); digitalWrite(IN2,LOW); ledcWrite(ENA,abs(l)); }
  if(r>0){ digitalWrite(IN3,LOW); digitalWrite(IN4,HIGH); ledcWrite(ENB,r); } else { digitalWrite(IN3,HIGH); digitalWrite(IN4,LOW); ledcWrite(ENB,abs(r)); }
}

void setMotorDirect(int l, int r) {
  if (INVERT_LEFT_MOTOR) l = -l; if (INVERT_RIGHT_MOTOR) r = -r;
  if(l>0){ digitalWrite(IN1,LOW); digitalWrite(IN2,HIGH); ledcWrite(ENA,l); } else { digitalWrite(IN1,HIGH); digitalWrite(IN2,LOW); ledcWrite(ENA,abs(l)); }
  if(r>0){ digitalWrite(IN3,LOW); digitalWrite(IN4,HIGH); ledcWrite(ENB,r); } else { digitalWrite(IN3,HIGH); digitalWrite(IN4,LOW); ledcWrite(ENB,abs(r)); }
}

void stopBot() { setMotorRaw(0,0); }

float getDist() {
  digitalWrite(ULTRASONIC_TRIG, LOW); delayMicroseconds(2); digitalWrite(ULTRASONIC_TRIG, HIGH); delayMicroseconds(10); digitalWrite(ULTRASONIC_TRIG, LOW);
  long d = pulseIn(ULTRASONIC_ECHO, HIGH, 25000); 
  float cm = (d==0)?999:(d*0.0343/2.0); lastDistReading = cm; return cm;
}

// =====================
// 6. LOGIC HANDLERS
// =====================

void checkRFID() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;
  String tag = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if(rfid.uid.uidByte[i] < 0x10) tag += "0";
    tag += String(rfid.uid.uidByte[i], HEX);
  }
  tag.toUpperCase();

  if (tag == lastSeenTag && millis() - lastTagTime < 2000) { rfid.PICC_HaltA(); rfid.PCD_StopCrypto1(); return; }
  lastSeenTag = tag; lastTagTime = millis();
  
  Serial.println("TAG FOUND: " + tag);

  if (currentTarget == "" || tag == currentTarget) {
    stopBot();
    autoMode = false; proxMode = false; manualLineFollow = false;
    webLog = "ARRIVED: " + tag;
    currentTarget = ""; 
    for(int i=0; i<3; i++) { digitalWrite(BUZZER_PIN, HIGH); delay(100); digitalWrite(BUZZER_PIN, LOW); delay(50); }
    penaltyEndTime = millis() + 3000; 
  } else {
    webLog = "PASSING: " + tag;
  }
  rfid.PICC_HaltA(); rfid.PCD_StopCrypto1();
}

void triggerPenalty() {
  stopBot(); webLog = "OBSTACLE! WAIT 2s";
  digitalWrite(BUZZER_PIN, HIGH); delay(100); digitalWrite(BUZZER_PIN, LOW);
  penaltyEndTime = millis() + 2000;
}

void runProximityFollow() {
  float d = getDist();
  if (d < 5.0) { webLog = "BREACH! BACK"; setMotorDirect(-PROX_SPEED, -PROX_SPEED); }
  else if (d >= 5.0 && d < 13.0) { webLog = "MAINTAINING"; setMotorDirect(-120, -120); }
  else if (d >= 13.0 && d <= 20.0) { webLog = "LOCKED"; stopBot(); }
  else if (d > 20.0 && d <= 30.0) { webLog = "CHASING"; setMotorDirect(PROX_SPEED, PROX_SPEED); }
  else { webLog = "SCANNING..."; int t = (millis() / 500) % 4; if(t==0) setMotorDirect(130, -130); else if(t==2) setMotorDirect(-130, 130); else stopBot(); }
}

void runAutoPilot() {
  if (millis() < penaltyEndTime) return;
  if (getDist() < OBSTACLE_DIST) { triggerPenalty(); delay(2000); webLog = "REVERSING..."; setMotorDirect(-150, -150); delay(500); setMotorDirect(180, -180); delay(600); return; }

  int l = digitalRead(IR_LEFT_PIN); int m = digitalRead(IR_MID_PIN); int r = digitalRead(IR_RIGHT_PIN);
  if (INVERT_SENSORS) { l = !l; m = !m; r = !r; }

  int error = 0;
  if (m && !l && !r) error = 0; else if (l && m) error = -1; else if (l && !m) error = -2; else if (r && m) error = 1; else if (r && !m) error = 2; else if (!l && !m && !r) { if(lastTurnDir == -1) error = -3; else error = 3; }
  if (error < 0) lastTurnDir = -1; if (error > 0) lastTurnDir = 1;

  int P = error; int D = error - lastError; int correction = (Kp * P) + (Kd * D); lastError = error;
  int leftSpeed = AUTO_BASE_SPEED + correction; int rightSpeed = AUTO_BASE_SPEED - correction;
  if (error == -3) { leftSpeed = -160; rightSpeed = 160; } else if (error == 3) { leftSpeed = 160; rightSpeed = -160; }
  setMotorRaw(leftSpeed, rightSpeed);
}

void checkManualSafety() {
  if (millis() < penaltyEndTime) return;
  if (getDist() < OBSTACLE_DIST) triggerPenalty();
}

// =====================
// 7. SETUP & LOOP
// =====================
void setup() {
  Serial.begin(115200);
  
  pinMode(IN1,OUTPUT); pinMode(IN2,OUTPUT); pinMode(IN3,OUTPUT); pinMode(IN4,OUTPUT);
  ledcAttach(ENA, PWM_FREQ, PWM_RES); ledcAttach(ENB, PWM_FREQ, PWM_RES);
  pinMode(IR_LEFT_PIN,INPUT); pinMode(IR_MID_PIN,INPUT); pinMode(IR_RIGHT_PIN,INPUT);
  pinMode(ULTRASONIC_TRIG,OUTPUT); pinMode(ULTRASONIC_ECHO,INPUT);
  pinMode(BUZZER_PIN,OUTPUT);
  
  SPI.begin();
  rfid.PCD_Init();
  rfid.PCD_SetAntennaGain(rfid.RxGain_max);
  // DIAGNOSTIC PRINT
  rfid.PCD_DumpVersionToSerial();
  
  // INIT CHECK LOOP (Retry until connected)
  for(int i=0; i<5; i++) {
    byte v = rfid.PCD_ReadRegister(rfid.VersionReg);
    Serial.print("Firmware Version: 0x"); Serial.println(v, HEX);
    if(v == 0x92 || v == 0x91 || v == 0x88) {
      Serial.println("RFID CONNECTED!");
      break; 
    }
    Serial.println("RFID NOT FOUND. Retrying...");
    delay(500);
    // Hard Reset via Software
    rfid.PCD_Init();
  }
  
  WiFi.softAP("Delivery-Bot-Net", "12345678");
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){ request->send(200, "text/html", htmlPage); });
  server.on("/drive", HTTP_GET, [](AsyncWebServerRequest *request){
    if (millis() < penaltyEndTime) { request->send(200, "text/plain", "LOCKED"); return; }
    if (autoMode || proxMode) { request->send(200, "text/plain", "Ignored"); return; }
    if (request->hasParam("dir")) {
      String dir = request->getParam("dir")->value();
      if (dir == "LF") { manualLineFollow = true; webLog = "HOLD: LINE TRACKING"; request->send(200, "text/plain", "OK"); return; }
      if (dir == "F" && getDist() < OBSTACLE_DIST) { triggerPenalty(); request->send(200, "text/plain", "BLOCKED"); return; } 
      if (dir == "F") { setMotorRaw(DRIVE_SPEED, DRIVE_SPEED); webLog = "FWD"; }
      else if (dir == "B") { setMotorRaw(-DRIVE_SPEED, -DRIVE_SPEED); webLog = "REV"; }
      else if (dir == "L") { setMotorRaw(TURN_SPEED, -TURN_SPEED); webLog = "LEFT"; }
      else if (dir == "R") { setMotorRaw(-TURN_SPEED, TURN_SPEED); webLog = "RIGHT"; }
      else if (dir == "S") { stopBot(); manualLineFollow = false; webLog = "STOP"; }
    }
    request->send(200, "text/plain", "OK");
  });
  
  server.on("/mission", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("id")) {
      String id = request->getParam("id")->value();
      if(id=="A") currentTarget = TAG_A; else if(id=="B") currentTarget = TAG_B; else if(id=="C") currentTarget = TAG_C;
      autoMode = true; proxMode = false; manualLineFollow = false;
      webLog = "MISSION START -> TAG " + id;
      Serial.println("MISSION STARTED: Target " + id);
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/setPID", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("type") && request->hasParam("val")) {
      String t = request->getParam("type")->value();
      int v = request->getParam("val")->value().toInt();
      if(t == "kp") Kp = v; if(t == "kd") Kd = v; if(t == "spd") AUTO_BASE_SPEED = v;
      webLog = "TUNED: " + t + "=" + String(v);
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){
    String modeStr = "MANUAL"; if (autoMode) modeStr = "AUTO"; if (proxMode) modeStr = "PROXIMITY";
    String targetLabel = ""; if (currentTarget == TAG_A) targetLabel = "A"; else if (currentTarget == TAG_B) targetLabel = "B"; else if (currentTarget == TAG_C) targetLabel = "C";
    String json = "{"; json += "\"mode\":\"" + modeStr + "\","; json += "\"dist\":" + String(lastDistReading) + ","; 
    json += "\"target\":\"" + targetLabel + "\","; json += "\"log\":\"" + webLog + "\""; json += "}";
    request->send(200, "application/json", json);
  });
  server.on("/toggleAuto", HTTP_GET, [](AsyncWebServerRequest *request){ autoMode = !autoMode; proxMode = false; currentTarget=""; stopBot(); webLog = autoMode ? "AUTO ENGAGED" : "MANUAL"; request->send(200, "text/plain", "OK"); });
  server.on("/toggleProx", HTTP_GET, [](AsyncWebServerRequest *request){ proxMode = !proxMode; autoMode = false; manualLineFollow = false; stopBot(); webLog = proxMode ? "TARGET LOCK ENGAGED" : "MANUAL"; request->send(200, "text/plain", "OK"); });
  server.on("/honk", HTTP_GET, [](AsyncWebServerRequest *request){ 
    webLog = "HONK!"; ledcAttach(BUZZER_PIN, 400, 8); ledcWrite(BUZZER_PIN, 127); delay(1000); ledcWrite(BUZZER_PIN, 0); ledcDetach(BUZZER_PIN); pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, LOW); 
    request->send(200, "text/plain", "HONK"); 
  });

  server.begin();
  digitalWrite(BUZZER_PIN, HIGH); delay(200); digitalWrite(BUZZER_PIN, LOW);
  Serial.println("SYSTEM READY.");
}

void loop() {
  checkRFID();
  if (autoMode) { runAutoPilot(); delay(1); }
  else if (proxMode) { runProximityFollow(); delay(20); }
  else if (manualLineFollow) { runAutoPilot(); delay(1); }
  else { checkManualSafety(); delay(5); }
}