/* SCI-FI DELIVERY BOT - MK-8 FINAL (Logic Patched)
   ================================================
   1. PATCH: "Side-Glance" Bug fixed. Safety Stop only triggers
      if the obstacle is actually IN FRONT (Servo 60-120 deg).
   2. UI: Restored Original MK-6 Layout.
   3. LOGIC: Non-blocking Servo + Smart Cargo + Audio FX.
*/

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <MFRC522.h>

// =============================================================
// 1. PIN DEFINITIONS
// =============================================================
const int PIN_ENA = 25; const int PIN_IN1 = 26; const int PIN_IN2 = 27; 
const int PIN_ENB = 14; const int PIN_IN3 = 21; const int PIN_IN4 = 13; 
const int IR_L = 32; const int IR_M = 33; const int IR_R = 34; 
const int TRIG = 4; const int ECHO = 15; 
const int BUZZER = 2; 
const int SS_PIN = 5; const int RST_PIN = 22; 
const int SERVO_PIN = 12; 
const int CARGO_PIN = 35; // Note: GPIO 35 has no internal Pull-Up. Sensor must drive HIGH/LOW.

// =============================================================
// 2. SETTINGS & GLOBALS
// =============================================================

// --- MOTOR TUNING ---
float trimL = 1.0; float trimR = 1.0;

// --- SPEEDS ---
const int SPD_FWD = 150;        
const int SPD_TURN_FAST = 180; 
const int SPD_TURN_SLOW = -40;  
const int SPD_MANUAL = 255;     

// --- CLIMATE ---
const float TEMP_C = 26.0; 
const float SOUND_SPEED = (331.3 + (0.606 * TEMP_C)) / 10000.0; 
const int SONAR_TIMEOUT = 6000; // ~1m limit

AsyncWebServer server(80);
MFRC522 rfid(SS_PIN, RST_PIN);

// System State
String lastLog = "SYSTEM READY";
String currentMissionTag = ""; 
int targetAction = 0; 
int cargoMode = 0; // 0=NIL, 1=LOAD, 2=UNLOAD

// Sensor Data
float sonarDist = 0.0;
int valL=0, valM=0, valR=0;
int servoAngle = 90;

// Timers
unsigned long rfidWatchdogTimer = 0;
unsigned long lastRfidCheck = 0; 
unsigned long lastSonarTime = 0;
unsigned long lastServoTime = 0;

// Flags
bool autoModeActive = false; 
bool obstacleLockout = false; 

// =============================================================
// 3. WEB INTERFACE (MK-6 LAYOUT)
// =============================================================
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no">
  <title>MK-8 LOGISTICS</title>
  <style>
    @import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono&display=swap');
    body { background-color: #050505; color: #00f3ff; font-family: 'Share Tech Mono', monospace; margin: 0; padding: 10px; user-select: none; background-image: linear-gradient(rgba(0, 243, 255, 0.05) 1px, transparent 1px), linear-gradient(90deg, rgba(0, 243, 255, 0.05) 1px, transparent 1px); background-size: 30px 30px; }
    
    header { border-bottom: 2px solid #00f3ff; padding-bottom: 5px; margin-bottom: 15px; display:flex; justify-content:space-between; align-items:center;}
    #pingDisplay { color: #00ff88; font-weight: bold; font-size: 14px; }

    .grid-container { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; max-width: 600px; margin: 0 auto; }
    
    .panel { background: rgba(10, 20, 30, 0.8); border: 1px solid #00f3ff; padding: 15px; position: relative; clip-path: polygon(15px 0, 100% 0, 100% calc(100% - 15px), calc(100% - 15px) 100%, 0 100%, 0 15px); box-shadow: 0 0 15px rgba(0, 243, 255, 0.1); }
    .panel-title { font-size: 12px; color: #00ff88; position: absolute; top: 2px; right: 15px; opacity: 0.8; }

    /* Controls */
    .d-pad { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; margin-top: 10px; }
    .btn { background: rgba(0, 243, 255, 0.1); color: #00f3ff; border: 1px solid #00f3ff; border-radius: 5px; height: 50px; font-size: 24px; display: flex; align-items: center; justify-content: center; }
    .btn:active { background: #00f3ff; color: #000; box-shadow: 0 0 15px #00f3ff; }
    .stop-btn { border-color: #ff0055; color: #ff0055; background: rgba(255, 0, 85, 0.1); }
    .stop-btn:active { background: #ff0055; color: #fff; box-shadow: 0 0 15px #ff0055; }
    .honk-btn { width: 100%; margin-top: 10px; background: rgba(255, 235, 59, 0.1); color: #ffeb3b; border: 1px solid #ffeb3b; height: 40px; font-weight: bold; }
    .honk-btn:active { background: #ffeb3b; color: #000; }
    .reset-btn { width: 100%; padding: 15px; font-family: 'Share Tech Mono'; font-size: 16px; font-weight: bold; border: 2px solid #ff0055; color: #ff0055; background: rgba(255, 0, 85, 0.1); cursor: pointer; }
    .reset-btn:active { background: #ff0055; color: white; box-shadow: 0 0 20px #ff0055; }

    /* Mission */
    .mission-row { display: flex; gap: 5px; margin-bottom: 10px; flex-wrap: wrap; }
    .m-btn { flex: 1; min-width: 60px; padding: 8px; border: 1px solid #555; background: #111; color: #555; font-family: inherit; font-size: 12px; cursor: pointer; }
    .m-btn.selected { border-color: #ff9800; color: #ff9800; background: rgba(255, 152, 0, 0.2); box-shadow: 0 0 10px #ff9800; }
    .act-btn.selected { border-color: #00f3ff; color: #00f3ff; background: rgba(0, 243, 255, 0.2); box-shadow: 0 0 10px #00f3ff; }
    .cargo-btn.selected { border-color: #d0f; color: #d0f; background: rgba(200, 0, 255, 0.2); box-shadow: 0 0 10px #d0f; }

    .auto-toggle { width: 100%; padding: 15px; font-family: 'Share Tech Mono', monospace; font-size: 16px; border: 2px solid #333; background: #111; color: #555; cursor: pointer; clip-path: polygon(10px 0, 100% 0, 100% calc(100% - 10px), calc(100% - 10px) 100%, 0 100%, 0 10px); }
    .auto-on { border-color: #00ff88; color: #00ff88; background: rgba(0, 255, 136, 0.1); box-shadow: 0 0 20px rgba(0, 255, 136, 0.2); }
    
    .console { grid-column: 1 / -1; height: 60px; background: #000; border: 1px solid #333; padding: 10px; font-size: 12px; color: #aaa; display: flex; flex-direction: column-reverse; overflow: hidden;}
    .log-line:first-child { color: #00f3ff; text-shadow: 0 0 5px #00f3ff; }
    
    #radarCanvas { background: #001100; border-radius: 50%; border: 2px solid #005500; box-shadow: inset 0 0 20px #005500; }
    #histCanvas { background: #000; border: 1px solid #333; width: 100%; height: 30px; margin-top:5px; }
    
    .led-row { display: flex; justify-content: center; gap: 15px; margin-bottom: 15px; }
    .led { width: 40px; height: 30px; border: 2px solid #555; background: #222; color: #555; display: flex; align-items: center; justify-content: center; font-size: 14px; font-weight: bold; border-radius: 4px; }
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
  
  <header><h1>MK-8 LOGISTICS</h1></header>
  
  <div class="grid-container">
    <!-- 1. TOP LEFT: SENSORS & RADAR -->
    <div class="panel">
      <div class="panel-title">SENSOR ARRAY</div>
      <div class="led-row">
        <div id="ledL" class="led">L</div>
        <div id="ledM" class="led">M</div>
        <div id="ledR" class="led">R</div>
      </div>
      
      <div style="display:flex; justify-content:center;">
        <canvas id="radarCanvas" width="140" height="140"></canvas>
      </div>
      
      <div style="text-align:center; font-size:12px;">
        SONAR: <span id="distVal">--</span> cm
        <canvas id="histCanvas"></canvas>
      </div>
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
      
      <!-- Destinations -->
      <div style="font-size:10px; color:#888;">DESTINATION</div>
      <div class="mission-row">
        <button id="btnA" class="m-btn" onclick="sel('t','A','7255FE03')">TAG A</button>
        <button id="btnB" class="m-btn" onclick="sel('t','B','827EFC03')">TAG B</button>
        <button id="btnC" class="m-btn" onclick="sel('t','C','99F5FB03')">TAG C</button>
        <button id="btnAny" class="m-btn" onclick="sel('t','Any','ANY')">ANY</button>
      </div>

      <!-- Actions -->
      <div style="font-size:10px; color:#888;">ACTION</div>
      <div class="mission-row">
        <button id="actS" class="m-btn act-btn selected" onclick="sel('a',0,0)">STOP</button>
        <button id="actL" class="m-btn act-btn" onclick="sel('a',1,0)">LEFT</button>
        <button id="actR" class="m-btn act-btn" onclick="sel('a',2,0)">RIGHT</button>
        <button id="actU" class="m-btn act-btn" onclick="sel('a',3,0)">U-TURN</button>
      </div>

      <!-- Cargo -->
      <div style="font-size:10px; color:#888;">CARGO LOGIC</div>
      <div class="mission-row">
        <button id="carN" class="m-btn cargo-btn selected" onclick="sel('c',0,0)">NIL</button>
        <button id="carL" class="m-btn cargo-btn" onclick="sel('c',1,0)">LOAD</button>
        <button id="carU" class="m-btn cargo-btn" onclick="sel('c',2,0)">UNLOAD</button>
      </div>
      
      <div class="mission-status">
        STATUS: <span id="confTag" class="hl">NONE</span> >> <span id="confAct" class="hl">STOP</span>
      </div>
      
      <button id="autoBtn" class="auto-toggle" onclick="engageAuto()">ENGAGE AUTO-PILOT</button>
    </div>

    <button class="reset-btn" onclick="if(confirm('REBOOT?')) fetch('/reset')" style="grid-column:1/-1;">⚠ SYSTEM REBOOT ⚠</button>
    <div class="panel console" id="console"><div class="log-line">> SYSTEM READY</div></div>
  </div>

  <script>
    var selTagID="", selAction=0, selCargo=0;
    var isDriving = false;
    var radarAngle = 0; var radarDir = 2;
    var lastDist = 999;
    var histData = new Array(30).fill(0);
    
    var rCanvas = document.getElementById('radarCanvas');
    var rCtx = rCanvas.getContext('2d');
    var hCanvas = document.getElementById('histCanvas');
    var hCtx = hCanvas.getContext('2d');

    function sel(type, id, val) {
      if(type=='t') { 
        selTagID = val; 
        document.querySelectorAll('.m-btn').forEach(b => { if(b.id.startsWith('btn')) b.classList.remove('selected'); });
        document.getElementById('btn'+id).classList.add('selected');
      }
      if(type=='a') { 
        selAction = id; 
        document.querySelectorAll('.act-btn').forEach(b => b.classList.remove('selected'));
        if(id==0) document.getElementById('actS').classList.add('selected');
        if(id==1) document.getElementById('actL').classList.add('selected');
        if(id==2) document.getElementById('actR').classList.add('selected');
        if(id==3) document.getElementById('actU').classList.add('selected');
      }
      if(type=='c') {
        selCargo = id;
        document.querySelectorAll('.cargo-btn').forEach(b => b.classList.remove('selected'));
        if(id==0) document.getElementById('carN').classList.add('selected');
        if(id==1) document.getElementById('carL').classList.add('selected');
        if(id==2) document.getElementById('carU').classList.add('selected');
      }
    }

    function engageAuto() {
      if(selTagID === "") { alert("ERROR: SELECT DESTINATION!"); return; }
      fetch("/setMis?tag=" + selTagID + "&act=" + selAction + "&car=" + selCargo).then(() => {
        fetch("/toggleAuto");
      });
    }

    function drawRadar(srvAng) {
      let w=140, h=140, cx=70, cy=70;
      rCtx.fillStyle='rgba(0,10,0,0.15)'; rCtx.fillRect(0,0,w,h); // Trails
      
      rCtx.strokeStyle='#003300'; rCtx.lineWidth=1;
      rCtx.beginPath(); rCtx.arc(cx,cy,30,0,2*Math.PI); rCtx.stroke();
      rCtx.beginPath(); rCtx.arc(cx,cy,60,0,2*Math.PI); rCtx.stroke();
      
      let displayAngle = srvAng; 
      let rad = (180 - displayAngle) * (Math.PI / 180);
      
      let lx = cx + 65 * Math.cos(rad);
      let ly = cy - 65 * Math.sin(rad);
      rCtx.strokeStyle='#00ff00'; rCtx.beginPath(); rCtx.moveTo(cx,cy); rCtx.lineTo(lx,ly); rCtx.stroke();
      
      if(lastDist < 100) {
        let px = (lastDist / 100) * 60;
        let bx = cx + px * Math.cos(rad);
        let by = cy - px * Math.sin(rad);
        rCtx.fillStyle='#ff0000'; rCtx.beginPath(); rCtx.arc(bx,by,3,0,2*Math.PI); rCtx.fill();
      }
    }
    
    function drawHist() {
      hCanvas.width = hCanvas.clientWidth; hCanvas.height = hCanvas.clientHeight;
      let w=hCanvas.width, h=hCanvas.height;
      hCtx.clearRect(0,0,w,h); hCtx.strokeStyle='#00ff88'; hCtx.beginPath();
      let step = w/histData.length;
      for(let i=0; i<histData.length; i++) {
        let y = h - (Math.min(100, histData[i])/100 * h);
        if(i==0) hCtx.moveTo(0,y); else hCtx.lineTo(i*step, y);
      }
      hCtx.stroke();
    }

    function poll() {
      if(isDriving) { setTimeout(poll, 500); return; }
      let t = Date.now();
      
      fetch("/stat").then(r=>r.json()).then(d=>{
        document.getElementById('pingDisplay').innerText = "PING: " + (Date.now()-t) + "ms";
        document.getElementById('distVal').innerText = d.dist;
        lastDist = d.dist;
        histData.shift(); histData.push(d.dist); drawHist();
        
        drawRadar(d.ang); 
        
        document.getElementById('ledL').className = d.l ? 'led active' : 'led';
        document.getElementById('ledM').className = d.m ? 'led active' : 'led';
        document.getElementById('ledR').className = d.r ? 'led active' : 'led';

        document.getElementById('confTag').innerText = d.target == "" ? "NONE" : (d.target=="ANY" ? "ANY" : d.target.substring(0,4)+"...");
        let aStr = ["STOP","LEFT","RIGHT","U-TURN"][d.act] || "STOP";
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
// 4. SERVO FUNCTION (Hardware PWM)
// =============================================================
void setupServo() {
  ledcAttach(SERVO_PIN, 50, 16); // 50Hz, 16-bit resolution
}

void moveServo(int angle) {
  int duty = map(angle, 0, 180, 1638, 7864); 
  ledcWrite(SERVO_PIN, duty);
}

// =============================================================
// 5. MOTOR & SOUND
// =============================================================
void setMotors(int l, int r) {
  l = constrain(l * trimL, -255, 255); r = constrain(r * trimR, -255, 255);
  if(l>0) { digitalWrite(PIN_IN3,LOW); digitalWrite(PIN_IN4,HIGH); ledcWrite(PIN_ENB,l); }
  else    { digitalWrite(PIN_IN3,HIGH); digitalWrite(PIN_IN4,LOW); ledcWrite(PIN_ENB,abs(l)); }
  if(r>0) { digitalWrite(PIN_IN1,LOW); digitalWrite(PIN_IN2,HIGH); ledcWrite(PIN_ENA,r); }
  else    { digitalWrite(PIN_IN1,HIGH); digitalWrite(PIN_IN2,LOW); ledcWrite(PIN_ENA,abs(r)); }
}

void playBootTune() {
  // Robotic Rising Scale + Motor Haptics
  setMotors(80, 80); digitalWrite(BUZZER, HIGH); delay(150); 
  setMotors(0, 0); digitalWrite(BUZZER, LOW); delay(50);
  setMotors(-80, -80); digitalWrite(BUZZER, HIGH); delay(150); 
  setMotors(0, 0); digitalWrite(BUZZER, LOW); delay(50);
  digitalWrite(BUZZER, HIGH); delay(400); digitalWrite(BUZZER, LOW);
}

// =============================================================
// 6. SETUP
// =============================================================
void setup() {
  Serial.begin(115200);
  
  pinMode(PIN_IN1,OUTPUT); pinMode(PIN_IN2,OUTPUT); pinMode(PIN_IN3,OUTPUT); pinMode(PIN_IN4,OUTPUT);
  ledcAttach(PIN_ENA, 1000, 8); ledcAttach(PIN_ENB, 1000, 8);
  pinMode(IR_L,INPUT); pinMode(IR_M,INPUT); pinMode(IR_R,INPUT);
  pinMode(TRIG,OUTPUT); pinMode(ECHO,INPUT); pinMode(BUZZER,OUTPUT);
  pinMode(CARGO_PIN, INPUT); 
  
  setupServo();
  moveServo(90); // Center

  SPI.begin(); rfid.PCD_Init(); delay(50); rfid.PCD_SetAntennaGain(rfid.RxGain_max);
  
  WiFi.softAP("SciFi-Bot", "12345678");
  
  // ROUTES
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req){ req->send(200, "text/html", htmlPage); });
  
  server.on("/stat", HTTP_GET, [](AsyncWebServerRequest *req){
    String json = "{";
    json += "\"d\":" + String((int)sonarDist) + ",";
    json += "\"l\":" + String(valL) + ",\"m\":" + String(valM) + ",\"r\":" + String(valR) + ",";
    json += "\"c\":" + String(digitalRead(CARGO_PIN)) + ",";
    json += "\"auto\":" + String(autoModeActive) + ",";
    json += "\"ang\":" + String(servoAngle) + ",";
    json += "\"target\":\"" + currentMissionTag + "\",";
    json += "\"act\":" + String(targetAction) + ",";
    json += "\"log\":\"" + lastLog + "\"}";
    req->send(200, "application/json", json);
  });

  server.on("/drive", HTTP_GET, [](AsyncWebServerRequest *req){
    // Override Lockout for Stop Command
    String d = req->getParam("d")->value();
    if (d == "S") { setMotors(0,0); autoModeActive=false; obstacleLockout=false; req->send(200, "text/plain", "OK"); return; }
    
    if(autoModeActive || obstacleLockout) return req->send(200);
    
    if(d=="F") setMotors(SPD_MANUAL, SPD_MANUAL);
    else if(d=="B") setMotors(-SPD_MANUAL, -SPD_MANUAL);
    else if(d=="L") setMotors(-SPD_MANUAL, SPD_MANUAL);
    else if(d=="R") setMotors(SPD_MANUAL, -SPD_MANUAL);
    else setMotors(0, 0);
    req->send(200, "text/plain", "OK");
  });

  server.on("/setMis", HTTP_GET, [](AsyncWebServerRequest *req){
    currentMissionTag = req->getParam("tag")->value();
    targetAction = req->getParam("act")->value().toInt();
    cargoMode = req->getParam("car")->value().toInt();
    lastLog = "CFG SET: " + currentMissionTag;
    req->send(200, "text/plain", "OK");
  });

  server.on("/toggleAuto", HTTP_GET, [](AsyncWebServerRequest *req){
    // CARGO LOAD CHECK
    if(!autoModeActive && cargoMode == 1) { // Load Mode
      if(digitalRead(CARGO_PIN) == 1) { // 1 = Empty
        lastLog = "ERR: LOAD CARGO!";
        digitalWrite(BUZZER, HIGH); delay(100); digitalWrite(BUZZER, LOW); delay(100);
        digitalWrite(BUZZER, HIGH); delay(100); digitalWrite(BUZZER, LOW);
        req->send(200, "text/plain", "ERR"); return;
      }
    }
    
    autoModeActive = !autoModeActive;
    if(autoModeActive) { moveServo(90); } // Lock Servo Fwd
    else { setMotors(0,0); }
    req->send(200, "text/plain", "OK");
  });
  
  server.on("/honk", HTTP_GET, [](AsyncWebServerRequest *req){
    digitalWrite(BUZZER, HIGH); delay(200); digitalWrite(BUZZER, LOW);
    req->send(200);
  });

  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *req){
    req->send(200); delay(100); ESP.restart();
  });

  server.begin();
  playBootTune();
}

// =============================================================
// 7. MAIN LOOP
// =============================================================
// Globals for Servo Sweep
int servoDir = 15; 

void loop() {
  valL = digitalRead(IR_L); valM = digitalRead(IR_M); valR = digitalRead(IR_R);

  // --- SERVO SWEEP (MANUAL ONLY) ---
  if (!autoModeActive && millis() - lastServoTime > 250) {
    servoAngle += servoDir;
    if (servoAngle >= 180 || servoAngle <= 0) servoDir = -servoDir;
    moveServo(servoAngle);
    lastServoTime = millis();
    // Trigger Sonar Sync (Updates sonarDist)
    digitalWrite(TRIG, LOW); delayMicroseconds(2); digitalWrite(TRIG, HIGH); delayMicroseconds(10); digitalWrite(TRIG, LOW);
    long d = pulseIn(ECHO, HIGH, SONAR_TIMEOUT);
    sonarDist = (d == 0) ? 999 : (d * SOUND_SPEED) / 2.0; 
  }
  // --- FIXED SONAR (AUTO MODE) ---
  else if (autoModeActive && millis() - lastServoTime > 60) {
    digitalWrite(TRIG, LOW); delayMicroseconds(2); digitalWrite(TRIG, HIGH); delayMicroseconds(10); digitalWrite(TRIG, LOW);
    long d = pulseIn(ECHO, HIGH, SONAR_TIMEOUT);
    sonarDist = (d == 0) ? 999 : (d * SOUND_SPEED) / 2.0; 
    lastServoTime = millis();
  }

  // --- GLOBAL OBSTACLE SAFETY (PATCHED: Angle Aware) ---
  // Only stop if obstacle is < 15cm AND servo is facing forward (60-120 deg)
  // Or if in Auto Mode (Head is always fixed forward)
  bool isLookingForward = (servoAngle >= 60 && servoAngle <= 120);
  
  if (sonarDist < 15.0 && sonarDist > 0 && (autoModeActive || isLookingForward)) {
    setMotors(0,0); obstacleLockout = true; lastLog="OBSTACLE!";
  } else {
    obstacleLockout = false;
  }

  if (obstacleLockout) return;

  // --- RFID LOGIC ---
  if (millis() - lastRfidCheck > 30) {
    lastRfidCheck = millis();
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      String tag = "";
      for (byte i=0; i<rfid.uid.size; i++) { if(rfid.uid.uidByte[i]<0x10) tag+="0"; tag+=String(rfid.uid.uidByte[i], HEX); }
      tag.toUpperCase();
      
      if (autoModeActive) {
        if (currentMissionTag == "ANY" || tag == currentMissionTag) {
          setMotors(0,0);
          lastLog = "ARRIVED: " + tag;
          digitalWrite(BUZZER, HIGH); delay(500); digitalWrite(BUZZER, LOW);
          
          if (cargoMode == 2) { 
             lastLog = "WAITING FOR UNLOAD...";
             while(digitalRead(CARGO_PIN) == 0) { 
               delay(100); 
               if(!autoModeActive) break; // Safety Break
             }
             lastLog = "ITEM TAKEN. RETURNING...";
             delay(3000); 
             setMotors(180, -180); delay(750); 
             setMotors(0,0);
             currentMissionTag = "ANY"; targetAction = 0; 
          }
          else if (targetAction == 0) autoModeActive = false; 
          else if (targetAction == 1) { setMotors(-180, 180); delay(350); setMotors(0,0); autoModeActive=false; }
          else if (targetAction == 2) { setMotors(180, -180); delay(350); setMotors(0,0); autoModeActive=false; }
          else if (targetAction == 3) { setMotors(180, -180); delay(750); setMotors(0,0); autoModeActive=false; }
        }
      } 
      rfid.PICC_HaltA(); rfid.PCD_StopCrypto1();
    }
  }

  // --- LINE FOLLOWER ---
  if (autoModeActive) {
    if (valM==1 && valL==0 && valR==0) setMotors(SPD_FWD, SPD_FWD);
    else if (valL==1) setMotors(SPD_TURN_SLOW, SPD_TURN_FAST);
    else if (valR==1) setMotors(SPD_TURN_FAST, SPD_TURN_SLOW);
    else if (valL==1 && valM==1) setMotors(-50, SPD_TURN_FAST);
    else if (valR==1 && valM==1) setMotors(SPD_TURN_FAST, -50);
    else if (valL==0 && valM==0 && valR==0) setMotors(0,0); 
    else setMotors(SPD_FWD, SPD_FWD);
  }
}