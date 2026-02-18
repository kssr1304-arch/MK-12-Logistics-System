/* SCI-FI DELIVERY BOT - MK-12 COMMANDER (Logistics Database Edition)
   ============================================================
   UPDATES:
   1. MANUAL LOGGING: Now triggers delivery logs even in Manual Mode
      if a Cargo Name is active.
   2. LOGIC: Differentiates between (AUTO) and (MANUAL) deliveries.
   3. WEB UI: Keeps the database input fields.
*/

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <MFRC522.h>

// =============================================================
// 1. PIN DEFINITIONS (COMMANDER BOARD)
// =============================================================
// Motors
const int PIN_ENA = 25; const int PIN_IN1 = 26; const int PIN_IN2 = 27;
const int PIN_ENB = 14; const int PIN_IN3 = 21; const int PIN_IN4 = 13;
// Line Sensors
const int IR_L = 32; const int IR_M = 33; const int IR_R = 34;
// Sonar (Back on Main Board)
const int TRIG = 4; const int ECHO = 15;
// Audio
const int BUZZER = 2;
// Cargo
const int CARGO_PIN = 35;
// RFID
const int SS_PIN = 5; const int RST_PIN = 22;

// =============================================================
// 2. SETTINGS & GLOBALS
// =============================================================
// --- WIFI ---
const char* AP_SSID = "AIML-Bot";
const char* AP_PASS = "12345678";

// --- TUNING ---
float trimL = 1.0; float trimR = 1.0;

// --- SPEEDS (Safety Tuned) ---
const int SPD_FWD = 150;        
const int SPD_TURN_FAST = 180;
const int SPD_TURN_SLOW = -40;  
const int SPD_MANUAL = 200;

// --- CLIMATE (Bangalore) ---
const float SOUND_SPEED = 0.0347;
const int SONAR_TIMEOUT = 6000;

AsyncWebServer server(80);
MFRC522 rfid(SS_PIN, RST_PIN);

// System State
String lastLog = "SYSTEM READY";
String currentMissionTag = "";
String currentCargoName = "EMPTY"; // "EMPTY" means no active delivery
int targetAction = 0;
int cargoMode = 0; // 0=NIL, 1=LOAD, 2=UNLOAD

// Sensor Data
float sonarDist = 0.0;
int valL = 0; int valM = 0; int valR = 0;
int cargoStatus = 1; // 1=Empty

// Timers
unsigned long rfidWatchdogTimer = 0;
unsigned long lastRfidCheck = 0;
unsigned long lastSonarTime = 0;
unsigned long lastCmdTime = 0;

// Flags
bool autoModeActive = false;
bool obstacleLockout = false;
bool aiObstacleReported = false; // Prevents AI Spam

// =============================================================
// 3. AI COMMUNICATIONS
// =============================================================
void sendToAI(String msg) {
  lastLog = msg;        // Update local Web UI
  // SAFETY CHECK: Only send if the buffer has space.
  if(Serial2.availableForWrite() > 20) {
      Serial2.println(msg); 
  }
}

// =============================================================
// 4. WEB INTERFACE (WAREHOUSE EDITION)
// =============================================================
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no">
  <title>MK-12 WAREHOUSE</title>
  <style>
    @import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono&display=swap');
    body { background-color: #050505; color: #00f3ff; font-family: 'Share Tech Mono', monospace; margin: 0; padding: 10px; user-select: none; background-image: linear-gradient(rgba(0, 243, 255, 0.05) 1px, transparent 1px), linear-gradient(90deg, rgba(0, 243, 255, 0.05) 1px, transparent 1px); background-size: 30px 30px; }
    
    header { border-bottom: 2px solid #00f3ff; padding-bottom: 5px; margin-bottom: 15px; display:flex; justify-content:space-between; align-items:center; text-shadow: 0 0 10px #00f3ff; }
    #pingDisplay { color: #00ff88; font-weight: bold; font-size: 14px; text-shadow: 0 0 5px #00ff88; }
    
    .grid-container { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; max-width: 600px; margin: 0 auto; }
    .panel { background: rgba(10, 20, 30, 0.9); border: 1px solid #00f3ff; padding: 15px; position: relative; clip-path: polygon(15px 0, 100% 0, 100% calc(100% - 15px), calc(100% - 15px) 100%, 0 100%, 0 15px); box-shadow: 0 0 15px rgba(0, 243, 255, 0.1); }
    .panel-title { font-size: 12px; color: #00ff88; position: absolute; top: 2px; right: 15px; opacity: 0.8; letter-spacing: 1px; }
    
    .d-pad { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; margin-top: 10px; }
    .btn { background: rgba(0, 243, 255, 0.1); color: #00f3ff; border: 1px solid #00f3ff; border-radius: 5px; height: 50px; font-size: 24px; display: flex; align-items: center; justify-content: center; }
    .btn:active { background: #00f3ff; color: #000; box-shadow: 0 0 20px #00f3ff; }
    .stop-btn { border-color: #ff0055; color: #ff0055; background: rgba(255, 0, 85, 0.1); }
    .stop-btn:active { background: #ff0055; color: #fff; box-shadow: 0 0 20px #ff0055; }
    .honk-btn { width: 100%; margin-top: 10px; background: rgba(255, 235, 59, 0.1); color: #ffeb3b; border: 1px solid #ffeb3b; height: 40px; font-weight: bold; }
    .honk-btn:active { background: #ffeb3b; color: #000; }
    
    /* INPUT FIELD STYLING */
    input[type=text] { width: 95%; background: #000; border: 1px solid #00ff88; color: #00ff88; padding: 10px; font-family: inherit; font-size: 16px; margin-bottom: 5px; margin-top: 15px; }
    
    .mission-row { display: flex; gap: 5px; margin-bottom: 10px; flex-wrap: wrap; }
    .m-btn { flex: 1; min-width: 60px; padding: 10px; border: 1px solid #555; background: #111; color: #555; font-family: inherit; font-size: 14px; cursor: pointer; transition: all 0.2s; }
    .m-btn.selected { border-color: #ff9800; color: #ff9800; background: rgba(255, 152, 0, 0.2); box-shadow: 0 0 15px #ff9800; }
    .act-btn.selected { border-color: #00f3ff; color: #00f3ff; background: rgba(0, 243, 255, 0.2); box-shadow: 0 0 15px #00f3ff; }
    .car-btn.selected { border-color: #d0f; color: #d0f; background: rgba(200, 0, 255, 0.2); box-shadow: 0 0 15px #d0f; }
    
    .auto-toggle { width: 100%; padding: 15px; font-family: 'Share Tech Mono', monospace; font-size: 16px; border: 2px solid #333; background: #111; color: #555; cursor: pointer; clip-path: polygon(10px 0, 100% 0, 100% calc(100% - 10px), calc(100% - 10px) 100%, 0 100%, 0 10px); transition: all 0.3s; }
    .auto-on { border-color: #00ff88; color: #00ff88; background: rgba(0, 255, 136, 0.1); box-shadow: 0 0 25px rgba(0, 255, 136, 0.2); text-shadow: 0 0 5px #00ff88; }
    
    .console { grid-column: 1 / -1; height: 60px; background: #000; border: 1px solid #333; padding: 10px; font-size: 12px; color: #aaa; display: flex; flex-direction: column-reverse; overflow: hidden; box-shadow: inset 0 0 10px #000;}
    .log-line:first-child { color: #00f3ff; text-shadow: 0 0 5px #00f3ff; }
    
    .led-row { display: flex; justify-content: center; gap: 15px; margin-bottom: 5px; }
    .led { width: 40px; height: 10px; border: 1px solid #555; background: #222; transition: background 0.1s; }
    .led.active { background: #00f3ff; box-shadow: 0 0 15px #00f3ff; border-color: #fff; }
    
    /* GRAPHS & ANIMATIONS */
    #histCanvas { width: 100%; height: 50px; background: #050505; border: 1px solid #333; margin: 5px 0 5px 0; }
    #beatCanvas { width: 100%; height: 20px; background: #000; border-top: 1px dashed #004400; margin-bottom: 5px; opacity: 0.7; }
    #uavCanvas { width: 100%; height: 80px; background: #001100; border: 1px solid #005500; margin-top: 5px; box-shadow: inset 0 0 10px #003300; }
  </style>
</head>
<body>
  <div style="display:flex; justify-content:space-between;">
    <div id="pingDisplay">PING: --ms</div>
    <div id="rssiDisplay" style="font-size:12px; color:#666;">AP: AIML-Bot</div>
  </div>
  <header><h1>MK-12 LOGISTICS</h1></header>
  <div class="grid-container">
    
    <!-- DATABASE INPUT -->
    <div class="panel" style="grid-column: 1 / -1;">
      <div class="panel-title">MANIFEST ENTRY</div>
      <input type="text" id="cargoName" placeholder="Enter Product Name (e.g. Vaccines)">
      <div style="font-size: 10px; color: #666; text-align:right;">LEAVE EMPTY TO AUTO-GENERATE ID</div>
    </div>

    <!-- TELEMETRY -->
    <div class="panel">
      <div class="panel-title">LIVE SENSORS</div>
      <div class="led-row">
        <div id="ledL" class="led"></div>
        <div id="ledM" class="led"></div>
        <div id="ledR" class="led"></div>
      </div>
      <canvas id="histCanvas"></canvas>
      <canvas id="beatCanvas"></canvas>
      <div style="text-align:center; font-size:12px;">
        DIST: <span id="distVal" style="color:#fff; font-weight:bold; font-size:14px;">--</span>cm | CARGO: <span id="cSt" style="color:#555">--</span>
      </div>
      <canvas id="uavCanvas"></canvas> <!-- FAKE UAV MAP -->
    </div>
    <!-- MANUAL -->
    <div class="panel">
      <div class="panel-title">MANUAL</div>
      <div class="d-pad">
        <div></div><button class="btn" onmousedown="startDr('F')" onmouseup="stopDr()" ontouchstart="startDr('F')" ontouchend="stopDr()">▲</button><div></div>
        <button class="btn" onmousedown="startDr('L')" onmouseup="stopDr()" ontouchstart="startDr('L')" ontouchend="stopDr()">◀</button>
        <button class="btn stop-btn" onclick="stopDr()">■</button>
        <button class="btn" onmousedown="startDr('R')" onmouseup="stopDr()" ontouchstart="startDr('R')" ontouchend="stopDr()">▶</button>
        <div></div><button class="btn" onmousedown="startDr('B')" onmouseup="stopDr()" ontouchstart="startDr('B')" ontouchend="stopDr()">▼</button><div></div>
      </div>
      <button class="btn honk-btn" onclick="fetch('/honk')">AUDIO SIGNAL</button>
      <button class="btn honk-btn" style="border-color:#ff9800; color:#ff9800; margin-top:5px;" onclick="fetch('/testAI')">TEST CONNECTION</button>
    </div>
    <!-- MISSION -->
    <div class="panel" style="grid-column: 1 / -1;">
      <div class="panel-title">MISSION CONFIG</div>
      <div class="mission-row">
        <button id="btnA" class="m-btn" onclick="sel('t','A','7255FE03')">TAG A</button>
        <button id="btnB" class="m-btn" onclick="sel('t','B','827EFC03')">TAG B</button>
        <button id="btnC" class="m-btn" onclick="sel('t','C','99F5FB03')">TAG C</button>
        <button id="btnAny" class="m-btn" onclick="sel('t','Any','ANY')">ANY</button>
      </div>
      <div class="mission-row">
        <button id="actL" class="m-btn act-btn" onclick="sel('a',1,0)">LEFT</button>
        <button id="actS" class="m-btn act-btn selected" onclick="sel('a',0,0)">STOP</button>
        <button id="actR" class="m-btn act-btn" onclick="sel('a',2,0)">RIGHT</button>
        <button id="actU" class="m-btn act-btn" onclick="sel('a',3,0)">U-TURN</button>
      </div>
      <div class="mission-row">
        <button id="cN" class="m-btn car-btn selected" onclick="sel('c',0,0)">NIL</button>
        <button id="cL" class="m-btn car-btn" onclick="sel('c',1,0)">LOAD</button>
        <button id="cU" class="m-btn car-btn" onclick="sel('c',2,0)">UNLOAD</button>
      </div>
      <div style="font-size:10px; text-align:center; color:#666;">CONFIRMED: <span id="confMsg">NONE</span></div>
      <button id="autoBtn" class="auto-toggle" onclick="engageAuto()">ENGAGE DELIVERY</button>
    </div>
    <button class="reset-btn" onclick="if(confirm('REBOOT?')) fetch('/reset')" style="grid-column:1/-1;">⚠ SYSTEM REBOOT ⚠</button>
    <div class="panel console" id="console"><div class="log-line">> SYSTEM READY</div></div>
  </div>
  <script>
    var selTag="", selAct=0, selCar=0;
    var isDriving = false;
    var histData = new Array(50).fill(0);
    var hCtx = document.getElementById('histCanvas').getContext('2d');
    var bCtx = document.getElementById('beatCanvas').getContext('2d');
    var uCtx = document.getElementById('uavCanvas').getContext('2d');
    var beatX = 0;
    var uavDots = [];
    var driveInterval = null;

    function sel(type, id, val) {
      if(type=='t') { selTag=val; document.querySelectorAll('.m-btn').forEach(b => { if(b.id.startsWith('btn')) b.classList.remove('selected'); }); document.getElementById('btn'+id).classList.add('selected'); }
      if(type=='a') { selAct=id; document.querySelectorAll('.act-btn').forEach(b => b.classList.remove('selected')); if(id==0)document.getElementById('actS').classList.add('selected'); if(id==1)document.getElementById('actL').classList.add('selected'); if(id==2)document.getElementById('actR').classList.add('selected'); if(id==3)document.getElementById('actU').classList.add('selected'); }
      if(type=='c') { selCar=id; document.querySelectorAll('.car-btn').forEach(b => b.classList.remove('selected')); if(id==0)document.getElementById('cN').classList.add('selected'); if(id==1)document.getElementById('cL').classList.add('selected'); if(id==2)document.getElementById('cU').classList.add('selected'); }
      // Don't send immediately on selection anymore, wait for ENGAGE
    }
    
    function engageAuto() { 
      if(selTag === "") { alert("SELECT DESTINATION!"); return; } 
      
      let prod = document.getElementById('cargoName').value;
      // Encode URI to handle spaces in names like "Medical Kit"
      fetch("/setMis?tag="+selTag+"&act="+selAct+"&car="+selCar+"&prod="+encodeURIComponent(prod))
        .then(() => fetch("/toggleAuto")); 
    }
    
    function drawHist() {
      hCtx.width = hCtx.canvas.clientWidth; hCtx.height = hCtx.canvas.clientHeight;
      let w=hCtx.canvas.width, h=hCtx.canvas.height;
      hCtx.clearRect(0,0,w,h); 
      
      // Glow Effect
      hCtx.shadowBlur = 10; hCtx.shadowColor = "#00ff00";
      hCtx.strokeStyle='#00ff00'; hCtx.lineWidth = 2; hCtx.beginPath();
      
      let step = w/histData.length;
      for(let i=0; i<histData.length; i++) {
        let val = Math.min(100, histData[i]);
        let y = h - (val/100*h);
        if(i==0) hCtx.moveTo(0,y); else hCtx.lineTo(i*step, y);
      }
      hCtx.stroke();
      hCtx.shadowBlur = 0; // Reset
    }
    
    function drawBeat() {
      let w = bCtx.canvas.clientWidth; let h = bCtx.canvas.clientHeight;
      bCtx.clearRect(0,0,w,h);
      bCtx.fillStyle = '#003300'; bCtx.fillRect(0,0,w,h);
      bCtx.fillStyle = '#00ff88';
      beatX += 4; if(beatX > w) beatX = 0;
      bCtx.fillRect(beatX, 0, 10, h);
      
      // UAV MAP ANIMATION
      w = uCtx.canvas.clientWidth; h = uCtx.canvas.clientHeight;
      uCtx.clearRect(0,0,w,h);
      
      // Grid
      uCtx.strokeStyle = '#004400'; uCtx.lineWidth = 1;
      uCtx.beginPath();
      for(let x=0; x<w; x+=20) { uCtx.moveTo(x,0); uCtx.lineTo(x,h); }
      for(let y=0; y<h; y+=20) { uCtx.moveTo(0,y); uCtx.lineTo(w,y); }
      uCtx.stroke();
      
      // Random Dots
      if(Math.random() > 0.95) uavDots.push({x:Math.random()*w, y:Math.random()*h, a:1});
      
      uCtx.fillStyle = '#00ff00';
      for(let i=0; i<uavDots.length; i++) {
        let d = uavDots[i];
        uCtx.globalAlpha = d.a;
        uCtx.beginPath(); uCtx.arc(d.x, d.y, 2, 0, Math.PI*2); uCtx.fill();
        d.a -= 0.02;
      }
      uavDots = uavDots.filter(d => d.a > 0);
      uCtx.globalAlpha = 1;
      
      requestAnimationFrame(drawBeat);
    }
    drawBeat();

    function poll() {
      if(isDriving) { setTimeout(poll, 500); return; }
      let t = Date.now();
      fetch("/status").then(r=>r.json()).then(d=>{
        document.getElementById('pingDisplay').innerText = "PING: " + (Date.now()-t) + "ms";
        document.getElementById('distVal').innerText = d.dist;
        document.getElementById('cSt').innerText = d.c ? "EMPTY" : "LOADED";
        document.getElementById('cSt').style.color = d.c ? "#555" : "#d0f";
        document.getElementById('ledL').className = d.l ? 'led active' : 'led';
        document.getElementById('ledM').className = d.m ? 'led active' : 'led';
        document.getElementById('ledR').className = d.r ? 'led active' : 'led';
        document.getElementById('confMsg').innerText = (d.target==""?"NONE":d.tag.substring(0,4)) + " > " + ["STOP","L","R","U"][d.act];
        let btn = document.getElementById('autoBtn');
        if(d.mode == "AUTO") { btn.innerText = "ABORT"; btn.classList.add("auto-on"); } else { btn.innerText = "ENGAGE DELIVERY"; btn.classList.remove("auto-on"); }
        let c = document.getElementById('console');
        if(c.firstChild.innerText !== "> " + d.log) { let l = document.createElement("div"); l.className="log-line"; l.innerText="> "+d.log; c.prepend(l); if(c.children.length>5) c.lastChild.remove(); }
        histData.shift(); histData.push(d.dist); drawHist();
        setTimeout(poll, 200);
      }).catch(e => setTimeout(poll, 200));
    }
    poll(); 
    
    // HEARTBEAT DRIVE FUNCTION (Fixes stuck button issues)
    function startDr(d) {
      if(driveInterval) clearInterval(driveInterval);
      fetch('/drive?dir='+d); // Immediate fire
      driveInterval = setInterval(() => {
        fetch('/drive?dir='+d);
      }, 200); // Repeat every 200ms
    }
    
    function stopDr() {
      if(driveInterval) clearInterval(driveInterval);
      driveInterval = null;
      fetch('/drive?dir=S');
    }
  </script>
</body>
</html>
)rawliteral";

void setMotors(int l, int r) {
  l = constrain(l * trimL, -255, 255); r = constrain(r * trimR, -255, 255);
  if(l>0) { digitalWrite(PIN_IN3,LOW); digitalWrite(PIN_IN4,HIGH); ledcWrite(PIN_ENB,l); }
  else    { digitalWrite(PIN_IN3,HIGH); digitalWrite(PIN_IN4,LOW); ledcWrite(PIN_ENB,abs(l)); }
  if(r>0) { digitalWrite(PIN_IN1,LOW); digitalWrite(PIN_IN2,HIGH); ledcWrite(PIN_ENA,r); }
  else    { digitalWrite(PIN_IN1,HIGH); digitalWrite(PIN_IN2,LOW); ledcWrite(PIN_ENA,abs(r)); }
}

void playEngineStart() {
  // LAMBO START SEQUENCE
  
  // 1. Starter Motor Crank
  for(int i=0; i<4; i++) {
      digitalWrite(BUZZER, HIGH); delay(40); 
      digitalWrite(BUZZER, LOW); 
      setMotors(80, 80); delay(40); 
      setMotors(0,0); delay(150); 
  }
  
  // 2. Ignition Catch
  for(int f=80; f<600; f+=20) {
      int delayTime = 1000000/f/2; 
      digitalWrite(BUZZER, HIGH); delayMicroseconds(delayTime);
      digitalWrite(BUZZER, LOW); delayMicroseconds(delayTime);
  }
  
  // 3. Rev 1
  for(int f=300; f<1000; f+=15) {
      int delayTime = 1000000/f/2;
      digitalWrite(BUZZER, HIGH); delayMicroseconds(delayTime);
      digitalWrite(BUZZER, LOW); delayMicroseconds(delayTime);
  }
  for(int f=1000; f>400; f-=20) { 
      int delayTime = 1000000/f/2;
      digitalWrite(BUZZER, HIGH); delayMicroseconds(delayTime);
      digitalWrite(BUZZER, LOW); delayMicroseconds(delayTime);
  }
  delay(100);
  
  // 4. Rev 2
  for(int f=400; f<1500; f+=20) {
      int delayTime = 1000000/f/2;
      digitalWrite(BUZZER, HIGH); delayMicroseconds(delayTime);
      digitalWrite(BUZZER, LOW); delayMicroseconds(delayTime);
  }
  
  // 5. Swoosh
  for(int f=2000; f>100; f-=10) { 
      digitalWrite(BUZZER, HIGH); delayMicroseconds(100); 
      digitalWrite(BUZZER, LOW); delayMicroseconds(200);
  }
}

void setup() {
  Serial.begin(115200);
  // COMM LINK TO MCU B (AI NODE)
  // TX=17 on this board connects to RX=16 on the AI board
  Serial2.begin(115200, SERIAL_8N1, 16, 17);
  
  pinMode(PIN_IN1,OUTPUT); pinMode(PIN_IN2,OUTPUT); pinMode(PIN_IN3,OUTPUT); pinMode(PIN_IN4,OUTPUT);
  ledcAttach(PIN_ENA, 1000, 8); ledcAttach(PIN_ENB, 1000, 8);
  pinMode(IR_L,INPUT); pinMode(IR_M,INPUT); pinMode(IR_R,INPUT);
  pinMode(TRIG,OUTPUT); pinMode(ECHO,INPUT); 
  pinMode(BUZZER,OUTPUT);
  pinMode(CARGO_PIN, INPUT);

  SPI.begin(); rfid.PCD_Init(); delay(50); rfid.PCD_SetAntennaGain(rfid.RxGain_max);
  
  WiFi.softAP(AP_SSID, AP_PASS);
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req){ req->send(200, "text/html", htmlPage); });
  
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *req){
    String json = "{";
    json += "\"dist\":" + String((int)sonarDist) + ",";
    json += "\"l\":" + String(valL) + ",\"m\":" + String(valM) + ",\"r\":" + String(valR) + ",";
    json += "\"c\":" + String(digitalRead(CARGO_PIN)) + ",";
    json += "\"mode\":\"" + String(autoModeActive?"AUTO":"MANUAL") + "\",";
    json += "\"target\":\"" + currentMissionTag + "\",";
    json += "\"act\":" + String(targetAction) + ",";
    json += "\"tag\":\"" + currentMissionTag + "\","; 
    json += "\"log\":\"" + lastLog + "\"}";
    req->send(200, "application/json", json);
  });

  server.on("/drive", HTTP_GET, [](AsyncWebServerRequest *req){
    String d = req->getParam("dir")->value();
    
    // Update Watchdog Timer
    lastCmdTime = millis();

    // SAFETY UPDATE: Manual Stop allowed even if locked.
    if (d == "S") { setMotors(0,0); autoModeActive=false; obstacleLockout=false; req->send(200, "text/plain", "STOPPED"); return; }
    
    // SAFETY UPDATE: Manual Mode now respects obstacle stop (except Reverse)
    if(obstacleLockout && d == "F") return req->send(200); 
    if(autoModeActive) return req->send(200);
    
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
    
    // --- DATABASE LOGIC UPDATE ---
    if(req->hasParam("prod")) {
        currentCargoName = req->getParam("prod")->value();
    }
    // Auto-generate ID if empty
    if(currentCargoName == "") {
        currentCargoName = "PKG-" + String(random(1000, 9999));
    }
    
    sendToAI("MANIFEST UPDATED: ITEM [" + currentCargoName + "] -> ZONE " + currentMissionTag);
    req->send(200, "text/plain", "OK");
  });

  server.on("/testAI", HTTP_GET, [](AsyncWebServerRequest *req){
    sendToAI("MANUAL LINK TEST. ITEM: " + (currentCargoName==""?"SYSTEM":currentCargoName));
    req->send(200, "text/plain", "OK");
  });

  server.on("/toggleAuto", HTTP_GET, [](AsyncWebServerRequest *req){
    if(!autoModeActive && cargoMode == 1) { 
      if(digitalRead(CARGO_PIN) == 1) { // 1 = Empty
        sendToAI("ERROR: CARGO BAY EMPTY. CANNOT START DELIVERY.");
        digitalWrite(BUZZER, HIGH); delay(200); digitalWrite(BUZZER, LOW);
        req->send(200); return;
      }
    }
    autoModeActive = !autoModeActive;
    if(!autoModeActive) { 
      setMotors(0,0);
      sendToAI("MANUAL OVERRIDE. DELIVERY PAUSED.");
    } else {
      sendToAI("DELIVERY INITIATED: ITEM [" + currentCargoName + "]");
    }
    req->send(200, "text/plain", "OK");
  });
  
  server.on("/honk", HTTP_GET, [](AsyncWebServerRequest *req){
    digitalWrite(BUZZER, HIGH); delay(200); digitalWrite(BUZZER, LOW);
    req->send(200);
  });

  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *req){
    req->send(200, "text/plain", "OK"); delay(100); ESP.restart();
  });

  server.begin();
  playEngineStart();
  sendToAI("SYSTEM ONLINE. WAREHOUSE MODE ACTIVE.");
}

void loop() {
  valL = digitalRead(IR_L); valM = digitalRead(IR_M); valR = digitalRead(IR_R);

  // Sonar - Faster Polling (30ms)
  if (millis() - lastSonarTime > 30) {
    digitalWrite(TRIG, LOW); delayMicroseconds(2); digitalWrite(TRIG, HIGH); delayMicroseconds(10); digitalWrite(TRIG, LOW);
    long d = pulseIn(ECHO, HIGH, SONAR_TIMEOUT); 
    sonarDist = (d == 0) ? 999 : (d * SOUND_SPEED) / 2.0; 
    
    // Safety - Trigger at 40cm
    if (sonarDist < 40.0 && sonarDist > 1.0) {
      setMotors(0,0); 
      obstacleLockout = true; 
      
      // Send to AI only once to avoid spamming the API
      if(!aiObstacleReported) {
         sendToAI("COLLISION ALERT! ITEM [" + currentCargoName + "] HALTED AT " + String((int)sonarDist) + "CM.");
         aiObstacleReported = true;
      }
      
      // Proximity Low Beep Pulse
      for(int k=0; k<5; k++) { 
          digitalWrite(BUZZER, HIGH); delay(1); digitalWrite(BUZZER, LOW); delay(5);
      }
    } else {
      obstacleLockout = false;
      aiObstacleReported = false; // Reset flag when clear
    }
    lastSonarTime = millis();
  }
  
  // SAFETY WATCHDOG
  if (!autoModeActive && (millis() - lastCmdTime > 400)) {
     static bool watchdogActive = false;
     if(!watchdogActive) {
        setMotors(0,0);
        watchdogActive = true; 
     }
  }

  // RFID Watchdog
  if (millis() - rfidWatchdogTimer > 5000) {
    byte v = rfid.PCD_ReadRegister(rfid.VersionReg);
    if (v == 0x00 || v == 0xFF) { rfid.PCD_Init(); rfid.PCD_SetAntennaGain(rfid.RxGain_max); }
    rfidWatchdogTimer = millis();
  }

  // RFID Logic
  if (millis() - lastRfidCheck > 30) {
    lastRfidCheck = millis();
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      String tag = "";
      for (byte i=0; i<rfid.uid.size; i++) { if(rfid.uid.uidByte[i]<0x10) tag+="0"; tag+=String(rfid.uid.uidByte[i], HEX); }
      tag.toUpperCase();
      
      // LOGIC UPDATE: Allows Manual Delivery Logging!
      // Checks: Is tag correct? AND (Is Auto Active? OR Do we have Cargo?)
      bool isValidDelivery = (currentMissionTag == "ANY" || tag == currentMissionTag);
      bool isAutoRun = autoModeActive;
      bool isManualRun = (!autoModeActive && currentCargoName != "EMPTY");

      if (isValidDelivery && (isAutoRun || isManualRun)) {
          setMotors(0,0);
          
          // Send formatted log based on mode
          String modeStr = isAutoRun ? "(AUTO)" : "(MANUAL)";
          sendToAI("DELIVERY COMPLETE " + modeStr + ". ITEM [" + currentCargoName + "] DEPOSITED AT " + tag);
          
          digitalWrite(BUZZER, HIGH); delay(500); digitalWrite(BUZZER, LOW);
          
          if (cargoMode == 2) { // UNLOAD
             sendToAI("INITIATING UNLOAD SEQUENCE. WAITING FOR REMOVAL.");
             unsigned long waitStart = millis();
             while(digitalRead(CARGO_PIN) == 0) { // Wait for empty
               delay(100);
               if(millis() - waitStart > 30000) break; // 30s Timeout prevents infinite loop
             }
             sendToAI("CARGO DELIVERED. RETURNING TO BASE.");
             delay(2000); 
             setMotors(180, -180); delay(750); // U-Turn
             setMotors(0,0);
             currentMissionTag = "ANY"; targetAction = 0; 
             currentCargoName = "EMPTY"; // Reset Inventory
          }
          else if (targetAction == 0) autoModeActive = false; 
          else if (targetAction == 1) { setMotors(-180, 180); delay(350); setMotors(0,0); autoModeActive=false; }
          else if (targetAction == 2) { setMotors(180, -180); delay(350); setMotors(0,0); autoModeActive=false; }
          else if (targetAction == 3) { setMotors(180, -180); delay(750); setMotors(0,0); autoModeActive=false; }
          
          // Ensure Auto Mode is cleared after arrival
          autoModeActive = false;
      } 
      rfid.PICC_HaltA(); rfid.PCD_StopCrypto1();
    }
  }

  // Line Follow
  if (autoModeActive && !obstacleLockout) {
    if (valM==1 && valL==0 && valR==0) setMotors(SPD_FWD, SPD_FWD);
    else if (valL==1) setMotors(SPD_TURN_SLOW, SPD_TURN_FAST);
    else if (valR==1) setMotors(SPD_TURN_FAST, SPD_TURN_SLOW);
    else if (valL==1 && valM==1) setMotors(-50, SPD_TURN_FAST);
    else if (valR==1 && valM==1) setMotors(SPD_TURN_FAST, -50);
    else if (valL==0 && valM==0 && valR==0) setMotors(0,0); 
    else setMotors(SPD_FWD, SPD_FWD);
  }
}