/* SCI-FI DELIVERY BOT - MK-12 COMMANDER (Polished UI & Safety)
   ============================================================
   1. SAFETY: Manual Mode now respects Obstacle Stop (Priority #1).
   2. UI: Added "UAV Tactical Map" visualization & "Heartbeat".
   3. SERVO: Instructions updated for limited 60-120 degree sweep.
   4. LOGIC: All sensors local (MCU A). MCU B is dummy servo only.
   5. AUDIO: "Hyper-Car" Start sequence (Crank, Revs, Spool).
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

// --- SPEEDS ---
const int SPD_FWD = 150;        
const int SPD_TURN_FAST = 180; 
const int SPD_TURN_SLOW = -40;  
const int SPD_MANUAL = 255; 

// --- CLIMATE (Bangalore) ---
const float SOUND_SPEED = 0.0347; 
const int SONAR_TIMEOUT = 6000; // 6ms (~1m)

AsyncWebServer server(80);
MFRC522 rfid(SS_PIN, RST_PIN);

// System State
String lastLog = "SYSTEM READY";
String currentMissionTag = ""; 
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

// Flags
bool autoModeActive = false; 
bool obstacleLockout = false;

// =============================================================
// 3. WEB INTERFACE (MK-12 SCI-FI)
// =============================================================
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no">
  <title>MK-12 COMMAND</title>
  <style>
    @import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono&display=swap');
    body { background-color: #050505; color: #00f3ff; font-family: 'Share Tech Mono', monospace; margin: 0; padding: 10px; user-select: none; background-image: linear-gradient(rgba(0, 243, 255, 0.05) 1px, transparent 1px), linear-gradient(90deg, rgba(0, 243, 255, 0.05) 1px, transparent 1px); background-size: 30px 30px; }
    
    header { border-bottom: 2px solid #00f3ff; padding-bottom: 5px; margin-bottom: 15px; display:flex; justify-content:space-between; align-items:center; text-shadow: 0 0 10px #00f3ff; }
    #pingDisplay { color: #00ff88; font-weight: bold; font-size: 14px; text-shadow: 0 0 5px #00ff88; }
    
    .grid-container { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; max-width: 600px; margin: 0 auto; }
    .panel { background: rgba(10, 20, 30, 0.8); border: 1px solid #00f3ff; padding: 15px; position: relative; clip-path: polygon(15px 0, 100% 0, 100% calc(100% - 15px), calc(100% - 15px) 100%, 0 100%, 0 15px); box-shadow: 0 0 15px rgba(0, 243, 255, 0.1); transition: box-shadow 0.3s; }
    .panel-title { font-size: 12px; color: #00ff88; position: absolute; top: 2px; right: 15px; opacity: 0.8; letter-spacing: 1px; }
    
    .d-pad { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; margin-top: 10px; }
    .btn { background: rgba(0, 243, 255, 0.1); color: #00f3ff; border: 1px solid #00f3ff; border-radius: 5px; height: 50px; font-size: 24px; display: flex; align-items: center; justify-content: center; }
    .btn:active { background: #00f3ff; color: #000; box-shadow: 0 0 20px #00f3ff; }
    .stop-btn { border-color: #ff0055; color: #ff0055; background: rgba(255, 0, 85, 0.1); }
    .stop-btn:active { background: #ff0055; color: #fff; box-shadow: 0 0 20px #ff0055; }
    .honk-btn { width: 100%; margin-top: 10px; background: rgba(255, 235, 59, 0.1); color: #ffeb3b; border: 1px solid #ffeb3b; height: 40px; font-weight: bold; }
    .honk-btn:active { background: #ffeb3b; color: #000; }
    .reset-btn { width: 100%; padding: 15px; font-family: 'Share Tech Mono'; font-size: 16px; font-weight: bold; border: 2px solid #ff0055; color: #ff0055; background: rgba(255, 0, 85, 0.1); cursor: pointer; margin-top: 10px; }
    .reset-btn:active { background: #ff0055; color: white; box-shadow: 0 0 20px #ff0055; }
    
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
        <div></div><button class="btn" onmousedown="dr('F')" onmouseup="dr('S')" ontouchstart="dr('F')" ontouchend="dr('S')">▲</button><div></div>
        <button class="btn" onmousedown="dr('L')" onmouseup="dr('S')" ontouchstart="dr('L')" ontouchend="dr('S')">◀</button>
        <button class="btn stop-btn" onclick="dr('S')">■</button>
        <button class="btn" onmousedown="dr('R')" onmouseup="dr('S')" ontouchstart="dr('R')" ontouchend="dr('S')">▶</button>
        <div></div><button class="btn" onmousedown="dr('B')" onmouseup="dr('S')" ontouchstart="dr('B')" ontouchend="dr('S')">▼</button><div></div>
      </div>
      <button class="btn honk-btn" onclick="fetch('/honk')">AUDIO SIGNAL</button>
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
      <button id="autoBtn" class="auto-toggle" onclick="engageAuto()">ENGAGE AUTO-PILOT</button>
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

    function sel(type, id, val) {
      if(type=='t') { selTag=val; document.querySelectorAll('.m-btn').forEach(b => { if(b.id.startsWith('btn')) b.classList.remove('selected'); }); document.getElementById('btn'+id).classList.add('selected'); }
      if(type=='a') { selAct=id; document.querySelectorAll('.act-btn').forEach(b => b.classList.remove('selected')); if(id==0)document.getElementById('actS').classList.add('selected'); if(id==1)document.getElementById('actL').classList.add('selected'); if(id==2)document.getElementById('actR').classList.add('selected'); if(id==3)document.getElementById('actU').classList.add('selected'); }
      if(type=='c') { selCar=id; document.querySelectorAll('.car-btn').forEach(b => b.classList.remove('selected')); if(id==0)document.getElementById('cN').classList.add('selected'); if(id==1)document.getElementById('cL').classList.add('selected'); if(id==2)document.getElementById('cU').classList.add('selected'); }
      if(type=='t') fetch("/setMis?tag="+val+"&act="+selAct+"&car="+selCar); else fetch("/setMis?tag="+selTag+"&act="+selAct+"&car="+selCar);
    }
    function engageAuto() { if(selTag === "") { alert("SELECT DESTINATION!"); return; } fetch("/setMis?tag="+selTag+"&act="+selAct+"&car="+selCar).then(() => fetch("/toggleAuto")); }
    
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
        if(d.mode == "AUTO") { btn.innerText = "ABORT"; btn.classList.add("auto-on"); } else { btn.innerText = "ENGAGE AUTO-PILOT"; btn.classList.remove("auto-on"); }
        let c = document.getElementById('console');
        if(c.firstChild.innerText !== "> " + d.log) { let l = document.createElement("div"); l.className="log-line"; l.innerText="> "+d.log; c.prepend(l); if(c.children.length>5) c.lastChild.remove(); }
        histData.shift(); histData.push(d.dist); drawHist();
        setTimeout(poll, 200);
      }).catch(e => setTimeout(poll, 200));
    }
    poll(); 
    function dr(d) { fetch('/drive?dir='+d); }
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
  
  // 1. Starter Motor Crank (Chug... Chug...) - 4x
  for(int i=0; i<4; i++) {
     digitalWrite(BUZZER, HIGH); delay(40); // Sound pulse
     digitalWrite(BUZZER, LOW); 
     setMotors(80, 80); delay(40); // Motor pulse (Haptic)
     setMotors(0,0); delay(150); // Pause
  }
  
  // 2. Ignition Catch (Frequency Sweep Up)
  for(int f=80; f<600; f+=20) {
     int delayTime = 1000000/f/2; // Half period in microseconds
     digitalWrite(BUZZER, HIGH); delayMicroseconds(delayTime);
     digitalWrite(BUZZER, LOW); delayMicroseconds(delayTime);
  }
  
  // 3. Rev 1 (VROOM!)
  for(int f=300; f<1000; f+=15) {
     int delayTime = 1000000/f/2;
     digitalWrite(BUZZER, HIGH); delayMicroseconds(delayTime);
     digitalWrite(BUZZER, LOW); delayMicroseconds(delayTime);
  }
  for(int f=1000; f>400; f-=20) { // Fall back
     int delayTime = 1000000/f/2;
     digitalWrite(BUZZER, HIGH); delayMicroseconds(delayTime);
     digitalWrite(BUZZER, LOW); delayMicroseconds(delayTime);
  }
  delay(100);
  
  // 4. Rev 2 (LOUDER/HIGHER!)
  for(int f=400; f<1500; f+=20) {
     int delayTime = 1000000/f/2;
     digitalWrite(BUZZER, HIGH); delayMicroseconds(delayTime);
     digitalWrite(BUZZER, LOW); delayMicroseconds(delayTime);
  }
  
  // 5. Swoosh / Systems Online
  for(int f=2000; f>100; f-=10) { // Fast drop
     digitalWrite(BUZZER, HIGH); delayMicroseconds(100); 
     digitalWrite(BUZZER, LOW); delayMicroseconds(200);
  }
}

void setup() {
  Serial.begin(115200);
  
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
    lastLog = "CFG: " + currentMissionTag;
    req->send(200, "text/plain", "OK");
  });

  server.on("/toggleAuto", HTTP_GET, [](AsyncWebServerRequest *req){
    if(!autoModeActive && cargoMode == 1) { 
      if(digitalRead(CARGO_PIN) == 1) { // 1 = Empty
        lastLog = "ERR: LOAD CARGO FIRST!";
        digitalWrite(BUZZER, HIGH); delay(200); digitalWrite(BUZZER, LOW);
        req->send(200); return;
      }
    }
    autoModeActive = !autoModeActive;
    if(!autoModeActive) setMotors(0,0);
    lastLog = autoModeActive ? "AUTO ENGAGED" : "MANUAL";
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
}

void loop() {
  valL = digitalRead(IR_L); valM = digitalRead(IR_M); valR = digitalRead(IR_R);

  // Sonar - 6ms timeout (~1m range)
  static unsigned long lastSonar = 0;
  if (millis() - lastSonar > 60) {
    digitalWrite(TRIG, LOW); delayMicroseconds(2); digitalWrite(TRIG, HIGH); delayMicroseconds(10); digitalWrite(TRIG, LOW);
    long d = pulseIn(ECHO, HIGH, SONAR_TIMEOUT); 
    sonarDist = (d == 0) ? 999 : (d * SOUND_SPEED) / 2.0; 
    
    // Safety - Now applies to Manual Forward too!
    if (sonarDist < 15.0 && sonarDist > 0) {
      setMotors(0,0); obstacleLockout = true; lastLog="OBSTACLE!";
      
      // Proximity Low Beep Pulse (Non-blocking-ish)
      // Generates a ~100Hz growl
      for(int k=0; k<10; k++) { 
         digitalWrite(BUZZER, HIGH); delay(2); digitalWrite(BUZZER, LOW); delay(8);
      }
    } else {
      obstacleLockout = false;
    }
    lastSonar = millis();
  }

  // Note: Obstacle Lockout check is now inside specific logic blocks (Manual/Auto)

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
      
      if (autoModeActive) {
        if (currentMissionTag == "ANY" || tag == currentMissionTag) {
          setMotors(0,0);
          lastLog = "TARGET REACHED!";
          digitalWrite(BUZZER, HIGH); delay(500); digitalWrite(BUZZER, LOW);
          
          if (cargoMode == 2) { // UNLOAD
             lastLog = "WAITING FOR UNLOAD...";
             unsigned long waitStart = millis();
             while(digitalRead(CARGO_PIN) == 0) { // Wait for empty
               delay(100);
               if(millis() - waitStart > 30000) break; 
             }
             lastLog = "ITEM TAKEN. RETURNING...";
             delay(2000); 
             setMotors(180, -180); delay(750); // U-Turn
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