/* SCI-FI DELIVERY BOT - MK-5 FINAL (Cleaned & Verified)
   ================================================
   1. MISSION: "TAG ANY" Button Added.
   2. REMOVED: "Hold-to-Follow" logic completely scrapped.
   3. LOGIC: Smart RFID + Soft-Steer + Active Braking.
*/

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <MFRC522.h>

// =============================================================
// 1. PIN DEFINITIONS
// =============================================================
const int PIN_ENA = 25; const int PIN_IN1 = 26; const int PIN_IN2 = 27; // Right
const int PIN_ENB = 14; const int PIN_IN3 = 21; const int PIN_IN4 = 13; // Left
const int IR_L = 32; const int IR_M = 33; const int IR_R = 34; // Sensors
const int TRIG = 4; const int ECHO = 15; const int BUZZER = 2; // Sonar/Buzz
const int SS_PIN = 5; const int RST_PIN = 22; // RFID

// =============================================================
// 2. SETTINGS & GLOBALS
// =============================================================

// --- MOTOR TUNING ---
float trimL = 1.0; 
float trimR = 1.0;

// --- SPEED SETTINGS ---
const int SPD_FWD = 150;        
const int SPD_TURN_FAST = 180; 
const int SPD_TURN_SLOW = -40;  // Active Braking
const int SPD_MANUAL = 255;     // Max power for manual

// --- CLIMATE SETTINGS ---
const float TEMP_C = 26.0; 
const float SOUND_SPEED = (331.3 + (0.606 * TEMP_C)) / 10000.0; 

AsyncWebServer server(80);
MFRC522 rfid(SS_PIN, RST_PIN);

// System State
String lastLog = "SYSTEM READY";
String currentMissionTag = ""; 
int targetAction = 0; 

// Sensor Data
float sonarDist = 0.0;
int valL = 0; int valM = 0; int valR = 0;
unsigned long rfidWatchdogTimer = 0;
unsigned long lastRfidCheck = 0; 

// Flags
bool autoModeActive = false; 

// =============================================================
// 3. WEB INTERFACE
// =============================================================
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no">
  <title>MK-5 MISSION</title>
  <style>
    @import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono&display=swap');
    
    body { 
      background-color: #050505; color: #00f3ff; 
      font-family: 'Share Tech Mono', monospace; 
      margin: 0; padding: 10px; user-select: none; 
      background-image: linear-gradient(rgba(0, 243, 255, 0.05) 1px, transparent 1px), 
                        linear-gradient(90deg, rgba(0, 243, 255, 0.05) 1px, transparent 1px); 
      background-size: 30px 30px; 
    }
    
    header { border-bottom: 2px solid #00f3ff; padding-bottom: 5px; margin-bottom: 15px; display:flex; justify-content:space-between; align-items:center;}
    #pingDisplay { color: #00ff88; font-weight: bold; font-size: 14px; }

    /* --- LAYOUT GRID --- */
    .grid-container { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; max-width: 600px; margin: 0 auto; }
    
    .panel { 
      background: rgba(10, 20, 30, 0.8); border: 1px solid #00f3ff; padding: 15px; 
      position: relative; 
      clip-path: polygon(15px 0, 100% 0, 100% calc(100% - 15px), calc(100% - 15px) 100%, 0 100%, 0 15px); 
      box-shadow: 0 0 15px rgba(0, 243, 255, 0.1); 
    }
    .panel-title { font-size: 12px; color: #00ff88; position: absolute; top: 2px; right: 15px; opacity: 0.8; }

    /* --- CONTROLS --- */
    .d-pad { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; margin-top: 10px; }
    .btn { 
      background: rgba(0, 243, 255, 0.1); color: #00f3ff; 
      border: 1px solid #00f3ff; border-radius: 5px; height: 50px; font-size: 24px; 
      display: flex; align-items: center; justify-content: center; 
    }
    .btn:active { background: #00f3ff; color: #000; box-shadow: 0 0 15px #00f3ff; }
    .stop-btn { border-color: #ff0055; color: #ff0055; background: rgba(255, 0, 85, 0.1); }
    .stop-btn:active { background: #ff0055; color: #fff; box-shadow: 0 0 15px #ff0055; }
    .honk-btn { width: 100%; margin-top: 10px; background: rgba(255, 235, 59, 0.1); color: #ffeb3b; border: 1px solid #ffeb3b; height: 40px; font-weight: bold; }
    .honk-btn:active { background: #ffeb3b; color: #000; }
    .reset-btn { width: 100%; padding: 15px; font-family: 'Share Tech Mono'; font-size: 16px; font-weight: bold; border: 2px solid #ff0055; color: #ff0055; background: rgba(255, 0, 85, 0.1); cursor: pointer; }
    .reset-btn:active { background: #ff0055; color: white; box-shadow: 0 0 20px #ff0055; }

    /* --- MISSION BUTTONS --- */
    .mission-row { display: flex; gap: 5px; margin-bottom: 10px; flex-wrap: wrap; }
    .m-btn { flex: 1; min-width: 60px; padding: 10px; border: 1px solid #555; background: #111; color: #555; font-family: inherit; font-size: 14px; cursor: pointer; }
    .m-btn.selected { border-color: #ff9800; color: #ff9800; background: rgba(255, 152, 0, 0.2); box-shadow: 0 0 10px #ff9800; }
    .act-btn.selected { border-color: #00f3ff; color: #00f3ff; background: rgba(0, 243, 255, 0.2); box-shadow: 0 0 10px #00f3ff; }

    .auto-toggle { width: 100%; padding: 15px; font-family: 'Share Tech Mono', monospace; font-size: 16px; border: 2px solid #333; background: #111; color: #555; cursor: pointer; clip-path: polygon(10px 0, 100% 0, 100% calc(100% - 10px), calc(100% - 10px) 100%, 0 100%, 0 10px); }
    .auto-on { border-color: #00ff88; color: #00ff88; background: rgba(0, 255, 136, 0.1); box-shadow: 0 0 20px rgba(0, 255, 136, 0.2); }
    
    .console { grid-column: 1 / -1; height: 60px; background: #000; border: 1px solid #333; padding: 10px; font-size: 12px; color: #aaa; display: flex; flex-direction: column-reverse; overflow: hidden;}
    .log-line:first-child { color: #00f3ff; text-shadow: 0 0 5px #00f3ff; }
    
    #chartContainer { width: 100%; height: 50px; background: #111; border: 1px solid #333; margin: 5px 0 15px 0; }
    
    /* LED MATRIX */
    .led-row { display: flex; justify-content: center; gap: 15px; margin-bottom: 15px; }
    .led { 
      width: 40px; height: 30px; 
      border: 2px solid #555; background: #222; color: #555; 
      display: flex; align-items: center; justify-content: center; 
      font-size: 14px; font-weight: bold; border-radius: 4px; 
    }
    .led.active { background: #00f3ff; color: #000; box-shadow: 0 0 15px #00f3ff; border-color: #fff; }
    
    .mission-status { font-size: 11px; color: #888; text-align: center; margin-bottom: 5px; border-top: 1px solid #333; padding-top: 5px; }
    .hl { color: #fff; font-weight: bold; }
  </style>
</head>
<body>
  <div style="display:flex; justify-content:space-between;">
    <div id="pingDisplay">PING: --ms</div>
    <div id="rssiDisplay" style="font-size:12px; color:#666;">AP: SciFi-Bot</div>
  </div>
  
  <header><h1>MK-5 MISSION</h1></header>
  
  <div class="grid-container">
    <!-- 1. TOP LEFT: TELEMETRY -->
    <div class="panel">
      <div class="panel-title">SENSOR ARRAY</div>
      <div class="led-row">
        <div id="ledL" class="led">L</div>
        <div id="ledM" class="led">M</div>
        <div id="ledR" class="led">R</div>
      </div>
      <canvas id="chartContainer"></canvas>
      <div style="text-align:center; font-size:12px;">SONAR: <span id="distVal">--</span> cm</div>
    </div>

    <!-- 2. TOP RIGHT: MANUAL CONTROL -->
    <div class="panel">
      <div class="panel-title">MANUAL</div>
      <div class="d-pad">
        <div></div><button class="btn" onmousedown="dr('F')" onmouseup="dr('S')" ontouchstart="dr('F')" ontouchend="dr('S')">▲</button><div></div>
        <button class="btn" onmousedown="dr('L')" onmouseup="dr('S')" ontouchstart="dr('L')" ontouchend="dr('S')">◀</button>
        <button class="btn stop-btn" onclick="dr('S')">■</button>
        <button class="btn" onmousedown="dr('R')" onmouseup="dr('S')" ontouchstart="dr('R')" ontouchend="dr('S')">▶</button>
        <div></div><button class="btn" onmousedown="dr('B')" onmouseup="dr('S')" ontouchstart="dr('B')" ontouchend="dr('S')">▼</button><div></div>
      </div>
      <button class="btn honk-btn" onclick="fetch('/honk')">AUDIO SIGNAL</button>
    </div>

    <!-- 3. BOTTOM: MISSION CONTROL -->
    <div class="panel" style="grid-column: 1 / -1;">
      <div class="panel-title">MISSION SETUP</div>
      
      <div style="font-size:12px; margin-bottom:5px;">1. SELECT DESTINATION</div>
      <div class="mission-row">
        <!-- UPDATE YOUR TAG IDS HERE -->
        <button id="btnA" class="m-btn" onclick="selTag('A', '7255FE03')">TAG A</button>
        <button id="btnB" class="m-btn" onclick="selTag('B', '827EFC03')">TAG B</button>
        <button id="btnC" class="m-btn" onclick="selTag('C', '99F5FB03')">TAG C</button>
        <button id="btnAny" class="m-btn" onclick="selTag('Any', 'ANY')">TAG ANY</button>
      </div>

      <div style="font-size:12px; margin-bottom:5px;">2. SELECT ARRIVAL ACTION</div>
      <div class="mission-row">
        <button id="actL" class="m-btn act-btn" onclick="selAct(1)">TURN LEFT</button>
        <button id="actS" class="m-btn act-btn selected" onclick="selAct(0)">STOP</button>
        <button id="actR" class="m-btn act-btn" onclick="selAct(2)">TURN RIGHT</button>
      </div>
      
      <div class="mission-status">
        CONFIRMED: <span id="confTag" class="hl">NONE</span> >> <span id="confAct" class="hl">STOP</span>
      </div>
      
      <button id="autoBtn" class="auto-toggle" onclick="engageAuto()">ENGAGE AUTO-PILOT</button>
    </div>

    <button class="reset-btn" onclick="if(confirm('REBOOT?')) fetch('/reset')" style="grid-column:1/-1;">⚠ SYSTEM REBOOT ⚠</button>
    <div class="panel console" id="console"><div class="log-line">> SYSTEM READY</div></div>
  </div>

  <script>
    var selTagID = "";
    var selAction = 0; 
    var isDriving = false;
    
    var distHistory = new Array(50).fill(0);
    var canvas = document.getElementById('chartContainer');
    var ctx = canvas.getContext('2d');

    function selTag(lbl, uid) {
      selTagID = uid;
      document.querySelectorAll('.m-btn').forEach(b => b.classList.remove('selected'));
      document.getElementById('btn'+lbl).classList.add('selected');
      selAct(selAction); 
    }

    function selAct(id) {
      selAction = id;
      document.querySelectorAll('.act-btn').forEach(b => b.classList.remove('selected'));
      if(id==0) document.getElementById('actS').classList.add('selected');
      if(id==1) document.getElementById('actL').classList.add('selected');
      if(id==2) document.getElementById('actR').classList.add('selected');
    }

    function engageAuto() {
      if(selTagID === "") { alert("ERROR: SELECT A DESTINATION TAG FIRST!"); return; }
      fetch("/setMission?tag=" + selTagID + "&act=" + selAction).then(() => {
        fetch("/toggleAuto");
      });
    }

    function drawGraph() {
      canvas.width = canvas.clientWidth; canvas.height = canvas.clientHeight;
      let w = canvas.width; let h = canvas.height;
      ctx.clearRect(0,0,w,h); ctx.beginPath(); ctx.strokeStyle='#00ff88'; ctx.lineWidth=2;
      let step = w/distHistory.length;
      for(let i=0; i<distHistory.length; i++) {
        let val = Math.min(50, distHistory[i]);
        let y = h - (val/50*h);
        if(i==0) ctx.moveTo(0,y); else ctx.lineTo(i*step,y);
      }
      ctx.stroke();
    }

    function poll() {
      if(isDriving) { setTimeout(poll, 500); return; }
      let t = Date.now();
      
      fetch("/status").then(r=>r.json()).then(d=>{
        document.getElementById('pingDisplay').innerText = "PING: " + (Date.now()-t) + "ms";
        document.getElementById('distVal').innerText = d.dist;
        distHistory.shift(); distHistory.push(d.dist); drawGraph();
        
        document.getElementById('ledL').className = d.l ? 'led active' : 'led';
        document.getElementById('ledM').className = d.m ? 'led active' : 'led';
        document.getElementById('ledR').className = d.r ? 'led active' : 'led';

        // Update Mission Confirmation
        document.getElementById('confTag').innerText = d.target == "" ? "NONE" : (d.target=="ANY" ? "ANY TAG" : d.target.substring(0,4)+"...");
        let aStr = "STOP";
        if(d.act == 1) aStr = "LEFT";
        if(d.act == 2) aStr = "RIGHT";
        document.getElementById('confAct').innerText = aStr;

        let btn = document.getElementById('autoBtn');
        if(d.mode == "AUTO") { btn.innerText = "ABORT MISSION"; btn.classList.add("auto-on"); }
        else { btn.innerText = "ENGAGE AUTO-PILOT"; btn.classList.remove("auto-on"); }

        let c = document.getElementById('console');
        if(c.firstChild.innerText !== "> " + d.log) {
          let l = document.createElement("div"); l.className="log-line"; l.innerText="> "+d.log;
          c.prepend(l); if(c.children.length>5) c.lastChild.remove();
        }
        setTimeout(poll, 250);
      });
    }
    poll(); 

    function dr(d) { fetch('/drive?dir='+d); }
  </script>
</body>
</html>
)rawliteral";


// =============================================================
// 4. MOTOR CONTROLLER
// =============================================================
void setMotors(int l, int r) {
  l = (float)l * trimL; r = (float)r * trimR;
  l = constrain(l, -255, 255); r = constrain(r, -255, 255);
  if(l>0) { digitalWrite(PIN_IN3,LOW); digitalWrite(PIN_IN4,HIGH); ledcWrite(PIN_ENB,l); }
  else    { digitalWrite(PIN_IN3,HIGH); digitalWrite(PIN_IN4,LOW); ledcWrite(PIN_ENB,abs(l)); }
  if(r>0) { digitalWrite(PIN_IN1,LOW); digitalWrite(PIN_IN2,HIGH); ledcWrite(PIN_ENA,r); }
  else    { digitalWrite(PIN_IN1,HIGH); digitalWrite(PIN_IN2,LOW); ledcWrite(PIN_ENA,abs(r)); }
}

// =============================================================
// 5. SETUP
// =============================================================
void setup() {
  Serial.begin(115200);
  
  pinMode(PIN_IN1,OUTPUT); pinMode(PIN_IN2,OUTPUT); pinMode(PIN_IN3,OUTPUT); pinMode(PIN_IN4,OUTPUT);
  ledcAttach(PIN_ENA, 1000, 8); ledcAttach(PIN_ENB, 1000, 8);
  pinMode(IR_L,INPUT); pinMode(IR_M,INPUT); pinMode(IR_R,INPUT);
  pinMode(TRIG,OUTPUT); pinMode(ECHO,INPUT); pinMode(BUZZER,OUTPUT);

  SPI.begin(); rfid.PCD_Init();
  delay(50); rfid.PCD_SetAntennaGain(rfid.RxGain_max); 
  
  WiFi.softAP("SciFi-Bot", "12345678");
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req){ req->send(200, "text/html", htmlPage); });
  
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *req){
    String json = "{";
    json += "\"dist\":" + String((int)sonarDist) + ",";
    json += "\"l\":" + String(valL) + ",\"m\":" + String(valM) + ",\"r\":" + String(valR) + ",";
    json += "\"mode\":\"" + String(autoModeActive?"AUTO":"MANUAL") + "\",";
    json += "\"target\":\"" + currentMissionTag + "\",";
    json += "\"act\":" + String(targetAction) + ",";
    json += "\"log\":\"" + lastLog + "\"}";
    req->send(200, "application/json", json);
  });

  server.on("/drive", HTTP_GET, [](AsyncWebServerRequest *req){
    if(autoModeActive) return req->send(200);
    String d = req->getParam("dir")->value();
    
    // D-Pad uses MANUAL Speed (Max Power)
    if(d=="F") setMotors(SPD_MANUAL, SPD_MANUAL);
    else if(d=="B") setMotors(-SPD_MANUAL, -SPD_MANUAL);
    else if(d=="L") setMotors(-SPD_MANUAL, SPD_MANUAL);
    else if(d=="R") setMotors(SPD_MANUAL, -SPD_MANUAL);
    else setMotors(0, 0);
    
    req->send(200, "text/plain", "OK");
  });

  server.on("/setMission", HTTP_GET, [](AsyncWebServerRequest *req){
    if(req->hasParam("tag") && req->hasParam("act")) {
      currentMissionTag = req->getParam("tag")->value();
      targetAction = req->getParam("act")->value().toInt();
      lastLog = "TARGET SET: " + currentMissionTag;
    }
    req->send(200, "text/plain", "OK");
  });

  server.on("/toggleAuto", HTTP_GET, [](AsyncWebServerRequest *req){
    autoModeActive = !autoModeActive;
    if(!autoModeActive) setMotors(0,0);
    lastLog = autoModeActive ? "AUTO ENGAGED" : "MANUAL";
    req->send(200, "text/plain", "OK");
  });
  
  server.on("/honk", HTTP_GET, [](AsyncWebServerRequest *req){
    digitalWrite(BUZZER,HIGH); delay(200); digitalWrite(BUZZER,LOW);
    req->send(200, "text/plain", "OK");
  });

  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *req){
    req->send(200, "text/plain", "OK"); delay(100); ESP.restart();
  });

  server.begin();
  digitalWrite(BUZZER, HIGH); delay(100); digitalWrite(BUZZER, LOW); delay(80);
  digitalWrite(BUZZER, HIGH); delay(100); digitalWrite(BUZZER, LOW);
}

// =============================================================
// 6. MAIN LOOP
// =============================================================
void loop() {
  valL = digitalRead(IR_L); valM = digitalRead(IR_M); valR = digitalRead(IR_R);

  // Sonar - 3000us timeout (3ms)
  static unsigned long lastSonar = 0;
  if (millis() - lastSonar > 100) {
    digitalWrite(TRIG, LOW); delayMicroseconds(2); digitalWrite(TRIG, HIGH); delayMicroseconds(10); digitalWrite(TRIG, LOW);
    long d = pulseIn(ECHO, HIGH, 3000); 
    sonarDist = (d == 0) ? 999 : (d * SOUND_SPEED) / 2.0; 
    lastSonar = millis();
  }

  // RFID Watchdog
  if (millis() - rfidWatchdogTimer > 5000) {
    byte v = rfid.PCD_ReadRegister(rfid.VersionReg);
    if (v == 0x00 || v == 0xFF) { rfid.PCD_Init(); rfid.PCD_SetAntennaGain(rfid.RxGain_max); }
    rfidWatchdogTimer = millis();
  }

  // RFID Logic (Optimized to check every 20ms to allow Line Follower CPU time)
  if (millis() - lastRfidCheck > 20) {
    lastRfidCheck = millis();
    
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      String scannedTag = "";
      for (byte i = 0; i < rfid.uid.size; i++) {
        if(rfid.uid.uidByte[i] < 0x10) scannedTag += "0";
        scannedTag += String(rfid.uid.uidByte[i], HEX);
      }
      scannedTag.toUpperCase();
      
      // --- AUTO MODE MISSION LOGIC ---
      if (autoModeActive) {
        // If Target is "ANY", treat match as True. Otherwise check specific ID.
        if (currentMissionTag == "ANY" || scannedTag == currentMissionTag) {
          
          setMotors(0,0); // OPTIMIZATION: Immediate Stop prevents overshoot
          lastLog = "TARGET REACHED!";
          digitalWrite(BUZZER, HIGH); delay(800); digitalWrite(BUZZER, LOW);
          
          if (targetAction == 0) { // STOP
            autoModeActive = false; // MISSION COMPLETE -> OFF
          }
          else if (targetAction == 1) { // LEFT
            lastLog = "TURNING LEFT...";
            setMotors(-180, 180); delay(350); 
            setMotors(0,0); autoModeActive = false; // TURN COMPLETE -> OFF
          }
          else if (targetAction == 2) { // RIGHT
            lastLog = "TURNING RIGHT...";
            setMotors(180, -180); delay(350); 
            setMotors(0,0); autoModeActive = false; // TURN COMPLETE -> OFF
          }
        } else {
          lastLog = "IGNORE TAG: " + scannedTag;
          // Optimized Wrong Tag Warning: Faster blips to prevent line loss
          for(int i=0;i<3;i++){ digitalWrite(BUZZER,HIGH); delay(20); digitalWrite(BUZZER,LOW); delay(20); }
        }
      } 
      // --- MANUAL SCAN ---
      else {
        lastLog = "TAG: " + scannedTag;
        digitalWrite(BUZZER,HIGH); delay(100); digitalWrite(BUZZER,LOW);
      }
      
      rfid.PICC_HaltA(); rfid.PCD_StopCrypto1();
    }
  }

  // Line Follow Logic
  if (autoModeActive) {
    // RE-READ SENSORS for freshest data immediately before steering
    valL = digitalRead(IR_L); valM = digitalRead(IR_M); valR = digitalRead(IR_R);

    if (sonarDist < 15 && sonarDist > 0) { setMotors(0,0); return; }

    if (valM==1 && valL==0 && valR==0) setMotors(SPD_FWD, SPD_FWD);
    else if (valL==1) setMotors(SPD_TURN_SLOW, SPD_TURN_FAST);
    else if (valR==1) setMotors(SPD_TURN_FAST, SPD_TURN_SLOW);
    else if (valL==1 && valM==1) setMotors(-50, SPD_TURN_FAST);
    else if (valR==1 && valM==1) setMotors(SPD_TURN_FAST, -50);
    else if (valL==0 && valM==0 && valR==0) setMotors(0,0); 
    else setMotors(SPD_FWD, SPD_FWD);
  }
}