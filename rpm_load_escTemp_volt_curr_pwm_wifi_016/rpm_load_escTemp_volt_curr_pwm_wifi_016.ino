#include "HX711.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFiS3.h> 

// ==========================================
// --- WIFI & DASHBOARD SETTINGS ---
// ==========================================
char ssid[] = "Marut_Thrust_Stand_001"; 
char pass[] = "Marut@123";              
int WIFI_CHANNEL = 1;                   

WiFiServer server(80); 
String latestCSV = ""; 

// ==========================================
// --- USER SETTINGS: MASTER TOGGLES ---
// ==========================================
const bool printRawRPM = false; 

// ==========================================
// --- PWM & THROTTLE CALIBRATION ---
// ==========================================
const int MIN_PWM = 1000; 
const int MAX_PWM = 2000; 

// ==========================================
// --- POWER SENSOR CALIBRATION (NOW EDITABLE) ---
// ==========================================
float VOLTAGE_MULTIPLIER = 19.02; 
float CURRENT_MULTIPLIER = 60.6;  

// ==========================================
// --- PIN DEFINITIONS ---
// ==========================================
const int pwmInputPin = 2;       
const int rpmPin = 3;            
const int LOADCELL_DOUT_PIN = 4; 
const int LOADCELL_SCK_PIN = 5;  
const int ONE_WIRE_BUS = 6;      
const int currentPin = A0;       
const int voltagePin = A1;       

// --- SENSOR OBJECTS ---
HX711 scale;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature tempSensor(&oneWire);

// --- DAQ CALIBRATION VARIABLES ---
float calibration_factor = 42.5; 
const float rpmAlpha = 0.15;        
float smoothedRPM = 0;

// --- INTERRUPT VARIABLES (RPM & PWM) ---
volatile unsigned long firstPulseTime = 0;
volatile unsigned long lastPulseTime = 0;
volatile unsigned int pulseCount = 0;

volatile unsigned long pwmStartTime = 0;
volatile unsigned int sharedPWM = 0; 

// --- TIMING & LOGGING VARIABLES ---
unsigned long previousPrintMillis = 0;
const unsigned long printInterval = 500; 
unsigned long logIndex = 1; 

// --- DYNAMIC RESET VARIABLES ---
bool lastSerialState = false;
unsigned long startTimeOffset = 0;

// ==========================================
// --- FUNCTION PROTOTYPES ---
// ==========================================
void measurePulse();
void measurePWM();

void setup() {
  Serial.begin(115200); 
  
  analogReference(AR_EXTERNAL);
  analogReadResolution(14);
  
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibration_factor);
  scale.tare(); 
  
  pinMode(rpmPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(rpmPin), measurePulse, FALLING);

  pinMode(pwmInputPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(pwmInputPin), measurePWM, CHANGE);

  tempSensor.begin();
  tempSensor.setResolution(9);
  tempSensor.setWaitForConversion(false);

  Serial.println("\n---------------------------------------------------------");
  Serial.println("STARTING WIFI ACCESS POINT...");
  
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    while (true); 
  }

  int status = WiFi.beginAP(ssid, pass, WIFI_CHANNEL);
  if (status != WL_AP_LISTENING) {
    Serial.println("Creating Access Point failed");
    while (true);
  }

  delay(2000);
  server.begin();

  Serial.println("ACCESS POINT ONLINE!");
  Serial.println("http://192.168.4.1");
  Serial.println("---------------------------------------------------------\n");
}

void loop() {
  unsigned long currentMillis = millis();
  bool currentSerialState = Serial;

  if (currentSerialState && !lastSerialState) {
    logIndex = 1;
    startTimeOffset = currentMillis;
    previousPrintMillis = currentMillis; 
    Serial.println("\n--- NEW DAQ LOGGING SESSION STARTED ---");
  }
  lastSerialState = currentSerialState;

  // ==========================================
  // --- 1. HANDLE WEB DASHBOARD CLIENTS ---
  // ==========================================
  WiFiClient client = server.available();
  if (client) {
    String currentLine = "";
    int requestType = 0; 
    unsigned long timeout = millis(); 
    
    while (client.connected() && millis() - timeout < 100) {
      if (client.available()) {
        char c = client.read();
        if (c == '\n') {
          if (currentLine.length() == 0) {
            
            // --- ROUTE: FETCH DATA ---
            if (requestType == 1) {
              client.println("HTTP/1.1 200 OK");
              client.println("Content-Type: text/plain");
              client.println("Access-Control-Allow-Origin: *");
              client.println("Connection: close");
              client.println();
              client.print(latestCSV);
            } 
            // --- ROUTE: RESET LOGGING ---
            else if (requestType == 2) {
              logIndex = 1;
              startTimeOffset = millis();
              previousPrintMillis = millis();
              client.println("HTTP/1.1 200 OK");
              client.println("Connection: close");
              client.println();
              client.print("RESET_OK");
            } 
            // --- ROUTE: TARE LOAD CELL ---
            else if (requestType == 3) {
              scale.tare(); 
              client.println("HTTP/1.1 200 OK");
              client.println("Connection: close");
              client.println();
              client.print("TARE_OK");
            }
            // --- ROUTE: UPDATE CALIBRATION MULTIPLIERS ---
            else if (requestType == 4) {
              client.println("HTTP/1.1 200 OK");
              client.println("Connection: close");
              client.println();
              client.print("CAL_OK");
            }
            // --- ROUTE: SERVE HTML UI ---
            else {
              client.println("HTTP/1.1 200 OK");
              client.println("Content-Type: text/html");
              client.println("Connection: close");
              client.println();
              
              // -----------------------------------------------------------------
              // PURE HTML/JS/CSS PAYLOAD
              // -----------------------------------------------------------------
              client.print(R"=====(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>MTS35_V1 Dashboard</title>
  <style>
    body { font-family: 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; background-color: #f4f7f6; color: #333; margin: 0; display: flex; flex-direction: column; height: 100vh; overflow: hidden; }
    
    .navbar { background-color: #1e2b3c; padding: 15px 30px; display: flex; align-items: center; justify-content: space-between; box-shadow: 0 2px 10px rgba(0,0,0,0.15); z-index: 10; flex-shrink: 0; }
    .nav-left { display: flex; align-items: center; gap: 20px; width: 300px; } 
    
    .logo-container { background: #ffffff; padding: 8px 15px; border-radius: 8px; display: flex; align-items: center; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }
    .logo-container img { height: 35px; width: auto; display: block; }
    
    .nav-title { color: #ffffff; font-size: 18px; font-weight: 700; letter-spacing: 1.5px; padding-left: 20px; border-left: 2px solid #34495e; }
    
    /* --- SLEEK CENTERED TITLE --- */
    .nav-center { flex: 1; text-align: center; color: #ecf0f1; font-size: 20px; font-weight: 500; letter-spacing: 4px; text-transform: uppercase; }

    .nav-right { display: flex; align-items: center; justify-content: flex-end; gap: 12px; width: 450px; } 
    
    .btn { color: white; border: none; padding: 10px 16px; border-radius: 6px; cursor: pointer; font-weight: 600; font-size: 14px; transition: 0.2s; }
    .btn:hover { transform: translateY(-2px); }
    .btn-record-start { background: #2ecc71; }
    .btn-record-stop { background: #e74c3c; animation: pulse 1.5s infinite; }
    .btn-download { background: #3498db; }
    .btn-reset { background: #7f8c8d; }
    
    @keyframes pulse { 0% { box-shadow: 0 0 0 0 rgba(231, 76, 60, 0.7); } 70% { box-shadow: 0 0 0 10px rgba(231, 76, 60, 0); } 100% { box-shadow: 0 0 0 0 rgba(231, 76, 60, 0); } }

    .data-stats { background: #2c3e50; padding: 8px 15px; border-radius: 6px; color: #bdc3c7; font-size: 13px; display: flex; align-items: center; gap: 10px; font-weight: 600; }
    .data-stats span { font-size: 18px; color: #2ecc71; font-weight: bold; }
    
    .main-content { flex: 1; padding: 30px 40px; overflow-y: auto; }
    .header-bar { display: flex; justify-content: space-between; align-items: center; margin-bottom: 25px; border-bottom: 2px solid #e0e6ed; padding-bottom: 15px; }
    
    /* --- TAB MENU STYLES --- */
    .tabs-menu { display: flex; gap: 20px; }
    .tab-btn { background: none; border: none; font-size: 18px; font-weight: 600; color: #7f8c8d; cursor: pointer; padding: 5px 10px; border-bottom: 3px solid transparent; transition: 0.2s; }
    .tab-btn.active { color: #2c3e50; border-bottom: 3px solid #3498db; }
    .tab-btn:hover { color: #3498db; }
    
    /* --- DYNAMIC STATUS BADGE (GREEN/RED) --- */
    .status { font-size: 14px; font-weight: 700; padding: 8px 18px; border-radius: 20px; display: flex; align-items: center; gap: 8px; min-width: 155px; box-sizing: border-box; font-variant-numeric: tabular-nums; transition: 0.3s; }
    .status.ok { color: #27ae60; background: #eaffea; border: 1px solid #2ecc71; box-shadow: 0 2px 8px rgba(46, 204, 113, 0.2); }
    .status.ok::before { content: ''; display: block; width: 10px; height: 10px; border-radius: 50%; background-color: #2ecc71; animation: pulse-dot-ok 1.5s infinite; flex-shrink: 0; }
    .status.err { color: #c0392b; background: #ffeaea; border: 1px solid #e74c3c; box-shadow: 0 2px 8px rgba(231, 76, 60, 0.2); }
    .status.err::before { content: ''; display: block; width: 10px; height: 10px; border-radius: 50%; background-color: #e74c3c; animation: pulse-dot-err 1.5s infinite; flex-shrink: 0; }
    
    @keyframes pulse-dot-ok { 0% { box-shadow: 0 0 0 0 rgba(46, 204, 113, 0.6); } 70% { box-shadow: 0 0 0 6px rgba(46, 204, 113, 0); } 100% { box-shadow: 0 0 0 0 rgba(46, 204, 113, 0); } }
    @keyframes pulse-dot-err { 0% { box-shadow: 0 0 0 0 rgba(231, 76, 60, 0.6); } 70% { box-shadow: 0 0 0 6px rgba(231, 76, 60, 0); } 100% { box-shadow: 0 0 0 0 rgba(231, 76, 60, 0); } }
    
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); gap: 20px; }
    .card { background: #fff; padding: 20px 25px; border-radius: 12px; box-shadow: 0 4px 15px rgba(0,0,0,0.05); display: flex; flex-direction: column; height: 240px; box-sizing: border-box; }
    .card-header { display: flex; justify-content: space-between; align-items: flex-end; margin-bottom: 10px; }
    h3 { margin: 0; color: #7f8c8d; font-size: 14px; text-transform: uppercase; letter-spacing: 1.5px; padding-bottom: 4px; }
    .val-container { margin: 0; text-align: right; }
    .val { font-size: 38px; font-weight: 700; color: #2c3e50; line-height: 1; }
    .unit { font-size: 16px; color: #95a5a6; margin-left: 2px; font-weight: 600; }
    
    .canvas-wrapper { flex: 1; position: relative; width: 100%; margin-top: 10px;}
    canvas { position: absolute; top: 0; left: 0; width: 100%; height: 100%; }

    /* --- CALIBRATION VIEW STYLES --- */
    .calib-container { display: none; max-width: 800px; margin: 0 auto; }
    .calib-card { background: #fff; padding: 30px; border-radius: 12px; box-shadow: 0 4px 15px rgba(0,0,0,0.05); margin-bottom: 20px; }
    .calib-card h2 { margin-top: 0; color: #2c3e50; border-bottom: 1px solid #eee; padding-bottom: 10px; margin-bottom: 25px; }
    .input-group { margin-bottom: 20px; }
    .input-group label { display: block; margin-bottom: 8px; font-weight: 600; color: #7f8c8d; }
    .input-group input { width: 100%; max-width: 300px; padding: 12px; border: 1px solid #bdc3c7; border-radius: 6px; font-size: 16px; font-family: monospace; }
  </style>
</head>
<body>

  <div class="navbar">
    <div class="nav-left">
      <div class="logo-container">
        <img src="https://via.placeholder.com/130x35/FFFFFF/000000?text=MARUT+DRONES" alt="Marut Drones Logo">
      </div>
      <div class="nav-title">MTS35_V1</div>
    </div>
    
    <div class="nav-center">
      Marut Thrust Stand
    </div>
    
    <div class="nav-right">
      <button id="recordBtn" class="btn btn-record-start" onclick="toggleRecord()">▶ Start Recording</button>
      <button class="btn btn-download" onclick="downloadCSV()">⬇ Download Data</button>
      <button class="btn btn-reset" onclick="restartSession()">↺ Reset</button>
      <div class="data-stats">Data Points: <span id="log_count">0</span></div>
    </div>
  </div>

  <div class="main-content">
    
    <div class="header-bar">
      <div class="tabs-menu">
        <button class="tab-btn active" id="tab_dash" onclick="switchTab('dash')">Live Dashboard</button>
        <button class="tab-btn" id="tab_calib" onclick="switchTab('calib')">Sensor Calibration</button>
      </div>
      <div class="status ok" id="status_container"><span id="status_text">Connecting...</span></div>
    </div>

    <div class="grid" id="view_dash">
      <div class="card"><div class="card-header"><h3>Throttle</h3><div class="val-container"><span class="val" id="v_thr">0</span><span class="unit">%</span></div></div><div class="canvas-wrapper"><canvas id="c_thr"></canvas></div></div>
      <div class="card"><div class="card-header"><h3>PWM</h3><div class="val-container"><span class="val" id="v_pwm">1000</span><span class="unit">us</span></div></div><div class="canvas-wrapper"><canvas id="c_pwm"></canvas></div></div>
      <div class="card"><div class="card-header"><h3>RPM</h3><div class="val-container"><span class="val" id="v_rpm">0.0</span><span class="unit">rpm</span></div></div><div class="canvas-wrapper"><canvas id="c_rpm"></canvas></div></div>
      <div class="card"><div class="card-header"><h3>Thrust</h3><div class="val-container"><span class="val" id="v_tht">0</span><span class="unit">g</span></div></div><div class="canvas-wrapper"><canvas id="c_tht"></canvas></div></div>
      <div class="card"><div class="card-header"><h3>Current</h3><div class="val-container"><span class="val" id="v_amp">0.00</span><span class="unit">A</span></div></div><div class="canvas-wrapper"><canvas id="c_amp"></canvas></div></div>
      <div class="card"><div class="card-header"><h3>Voltage</h3><div class="val-container"><span class="val" id="v_vol">0.00</span><span class="unit">V</span></div></div><div class="canvas-wrapper"><canvas id="c_vol"></canvas></div></div>
      <div class="card"><div class="card-header"><h3>Power</h3><div class="val-container"><span class="val" id="v_pow">0</span><span class="unit">W</span></div></div><div class="canvas-wrapper"><canvas id="c_pow"></canvas></div></div>
      <div class="card"><div class="card-header"><h3>Temp</h3><div class="val-container"><span class="val" id="v_tmp">0.0</span><span class="unit">°C</span></div></div><div class="canvas-wrapper"><canvas id="c_tmp"></canvas></div></div>
    </div>

    <div class="calib-container" id="view_calib">
      <div class="calib-card">
        <h2>Load Cell Tare</h2>
        <button class="btn btn-download" id="btn_tare" onclick="tareLoadCell()">Tare to Zero</button>
      </div>

      <div class="calib-card">
        <h2>Voltage & Current Calibration</h2>
        
        <div class="input-group">
          <label>Voltage Divider</label>
          <input type="number" id="inp_vmult" value="19.02" step="0.01">
        </div>
        
        <div class="input-group">
          <label>Amps per Volt</label>
          <input type="number" id="inp_imult" value="60.6" step="0.1">
        </div>
        
        <button class="btn btn-record-start" id="btn_updcal" onclick="updateMultipliers()">Update Calibration</button>
      </div>
    </div>

  </div>

  <script>
    // --- TAB SWITCHING LOGIC ---
    function switchTab(tabId) {
      document.getElementById('view_dash').style.display = (tabId === 'dash') ? 'grid' : 'none';
      document.getElementById('view_calib').style.display = (tabId === 'calib') ? 'block' : 'none';
      document.getElementById('tab_dash').className = (tabId === 'dash') ? 'tab-btn active' : 'tab-btn';
      document.getElementById('tab_calib').className = (tabId === 'calib') ? 'tab-btn active' : 'tab-btn';
    }

    // --- CALIBRATION NETWORK COMMANDS ---
    function tareLoadCell() {
      let btn = document.getElementById('btn_tare');
      btn.innerText = "Taring Sensor...";
      fetch('/tare').then(r => r.text()).then(res => {
        btn.style.background = "#2ecc71";
        btn.innerText = "Tare Successful!";
        setTimeout(() => { btn.innerText = "Tare to Zero"; btn.style.background = "#3498db"; }, 2000);
      }).catch(e => {
        btn.style.background = "#e74c3c";
        btn.innerText = "Error - Retrying...";
        setTimeout(() => { btn.innerText = "Tare to Zero"; btn.style.background = "#3498db"; }, 2000);
      });
    }

    function updateMultipliers() {
      let v = document.getElementById('inp_vmult').value;
      let i = document.getElementById('inp_imult').value;
      let btn = document.getElementById('btn_updcal');
      btn.innerText = "Sending Data...";
      fetch('/cal?v=' + v + '&i=' + i).then(r => r.text()).then(res => {
        btn.innerText = "Multipliers Updated!";
        setTimeout(() => btn.innerText = "Update Calibration", 2000);
      }).catch(e => {
        btn.style.background = "#e74c3c";
        btn.innerText = "Update Failed!";
        setTimeout(() => { btn.innerText = "Update Calibration"; btn.style.background = "#2ecc71"; }, 2000);
      });
    }

    // --- LIVE DASHBOARD LOGIC ---
    const maxPts = 50;
    let d = { thr:[], pwm:[], rpm:[], tht:[], amp:[], vol:[], pow:[], tmp:[] };
    let isRecording = false; 
    let csvHeader = "S.No,Time(s),Throttle(%),PWM(us),RPM,Thrust(g),Current(A),Voltage(V),Power(W),Temp(C)";
    let csvLog = [csvHeader];
    let recordIndex = 1;
    let recordStartTime = -1;

    function drawSparkline(id, arr, col, fixedMin, fixedMax) {
      let c = document.getElementById('c_' + id);
      let ctx = c.getContext('2d');
      c.width = c.clientWidth; 
      c.height = c.clientHeight;
      ctx.clearRect(0, 0, c.width, c.height);
      
      ctx.lineWidth = 1;
      for(let j = 0; j <= 4; j++) {
        let y = (j / 4) * c.height;
        ctx.beginPath();
        ctx.strokeStyle = (j % 2 === 0) ? 'rgba(0,0,0,0.1)' : 'rgba(0,0,0,0.03)';
        ctx.moveTo(0, y);
        ctx.lineTo(c.width, y);
        ctx.stroke();
      }
      for(let j = 0; j <= 10; j++) {
        let x = (j / 10) * c.width;
        ctx.beginPath();
        ctx.strokeStyle = (j % 2 === 0) ? 'rgba(0,0,0,0.1)' : 'rgba(0,0,0,0.03)';
        ctx.moveTo(x, 0);
        ctx.lineTo(x, c.height);
        ctx.stroke();
      }

      if(arr.length < 2) return;
      let min = (fixedMin !== undefined) ? fixedMin : Math.min(...arr);
      let max = (fixedMax !== undefined) ? fixedMax : Math.max(...arr);
      if(max === min) { min -= 1; max += 1; }
      
      ctx.beginPath(); ctx.strokeStyle = col; ctx.lineWidth = 3; ctx.lineJoin = 'round';
      for(let i = 0; i < arr.length; i++) {
        let ptX = (i / (maxPts - 1)) * c.width;
        let ptY = c.height - ((arr[i] - min) / (max - min)) * c.height;
        ptY = Math.max(2, Math.min(c.height - 2, ptY)); 
        if(i === 0) ctx.moveTo(ptX, ptY); else ctx.lineTo(ptX, ptY);
      }
      ctx.stroke();
    }

    let fetchInterval = setInterval(fetchData, 500);

    function fetchData() {
      fetch('/data').then(r => r.text()).then(t => {
        if(!t) return;
        
        let statusBox = document.getElementById('status_container');
        if(statusBox.className.includes('err')) statusBox.className = 'status ok';

        let p = t.split(',');
        if(p.length < 9) return;
        
        let rawTime = parseFloat(p[1].split(':')[1].trim());
        document.getElementById('status_text').innerText = "Live | Uptime: " + rawTime.toFixed(1) + "s";
        
        if (isRecording) {
          if (recordStartTime === -1) recordStartTime = rawTime;
          let normalizedTime = (rawTime - recordStartTime).toFixed(1);
          let dataValues = p.map(item => item.split(':')[1].trim());
          dataValues[0] = recordIndex;
          dataValues[1] = normalizedTime;
          
          let cleanRow = dataValues.join(',');
          if(csvLog.length === 1 || cleanRow !== csvLog[csvLog.length - 1]) {
            csvLog.push(cleanRow);
            document.getElementById('log_count').innerText = csvLog.length - 1;
            recordIndex++; 
          }
        }
        
        let v_thr = parseFloat(p[2].split(':')[1]) || 0;
        let v_pwm = parseFloat(p[3].split(':')[1]) || 0;
        let v_rpm = parseFloat(p[4].split(':')[1]) || 0;
        let v_tht = parseFloat(p[5].split(':')[1]) || 0;
        let v_amp = parseFloat(p[6].split(':')[1]) || 0;
        let v_vol = parseFloat(p[7].split(':')[1]) || 0;
        let v_pow = parseFloat(p[8].split(':')[1]) || 0;
        let rawTemp = p[9].split(':')[1].trim();

        document.getElementById('v_thr').innerText = v_thr.toFixed(0);
        document.getElementById('v_pwm').innerText = v_pwm.toFixed(0);
        document.getElementById('v_rpm').innerText = v_rpm.toFixed(1);
        document.getElementById('v_tht').innerText = v_tht.toFixed(0);
        document.getElementById('v_amp').innerText = v_amp.toFixed(2);
        document.getElementById('v_vol').innerText = v_vol.toFixed(2);
        document.getElementById('v_pow').innerText = v_pow.toFixed(0);
        
        if (rawTemp === "ERR") {
          document.getElementById('v_tmp').innerText = "ERR";
        } else {
          let v_tmp = parseFloat(rawTemp) || 0;
          document.getElementById('v_tmp').innerText = v_tmp.toFixed(1);
          d.tmp.push(v_tmp); if(d.tmp.length > maxPts) d.tmp.shift(); 
          drawSparkline('tmp', d.tmp, '#c0392b', 0, 125); 
        }

        d.thr.push(v_thr); if(d.thr.length > maxPts) d.thr.shift(); drawSparkline('thr', d.thr, '#3498db', 0, 100);       
        d.pwm.push(v_pwm); if(d.pwm.length > maxPts) d.pwm.shift(); drawSparkline('pwm', d.pwm, '#00bcd4', 800, 2200);    
        d.rpm.push(v_rpm); if(d.rpm.length > maxPts) d.rpm.shift(); drawSparkline('rpm', d.rpm, '#9b59b6', 0, 10000);     
        d.tht.push(v_tht); if(d.tht.length > maxPts) d.tht.shift(); drawSparkline('tht', d.tht, '#e74c3c', 0, 35000);     
        d.amp.push(v_amp); if(d.amp.length > maxPts) d.amp.shift(); drawSparkline('amp', d.amp, '#f39c12', 0, 200);       
        d.vol.push(v_vol); if(d.vol.length > maxPts) d.vol.shift(); drawSparkline('vol', d.vol, '#2ecc71', 0, 60);        
        d.pow.push(v_pow); if(d.pow.length > maxPts) d.pow.shift(); drawSparkline('pow', d.pow, '#e67e22', 0, 12000);     
        
      }).catch(e => {
        let statusBox = document.getElementById('status_container');
        statusBox.className = 'status err';
        document.getElementById('status_text').innerText = "Connection Lost. Reconnecting...";
      });
    }

    function toggleRecord() {
      let btn = document.getElementById('recordBtn');
      if (!isRecording) {
        isRecording = true;
        btn.innerHTML = '⏹ Stop Recording';
        btn.className = 'btn btn-record-stop';
        csvLog = [csvHeader];
        recordIndex = 1;
        recordStartTime = -1;
        document.getElementById('log_count').innerText = "0";
      } else {
        isRecording = false;
        btn.innerHTML = '▶ Start Recording';
        btn.className = 'btn btn-record-start';
      }
    }

    function downloadCSV() {
      if(csvLog.length <= 1) {
        alert("No data collected yet! Click 'Start Recording' first.");
        return;
      }
      let blob = new Blob([csvLog.join('\n')], { type: 'text/csv' });
      let link = document.createElement('a');
      link.href = URL.createObjectURL(blob);
      link.download = 'MTS35_V1_Test_Data.csv';
      link.click();
    }

    function restartSession() {
      if(!confirm("Are you sure? This will reset the telemetry clocks and clear your current data.")) return;
      fetch('/reset').then(r => r.text()).then(res => {
        isRecording = false;
        let btn = document.getElementById('recordBtn');
        btn.innerHTML = '▶ Start Recording';
        btn.className = 'btn btn-record-start';
        csvLog = [csvHeader];
        document.getElementById('log_count').innerText = "0";
        d = { thr:[], pwm:[], rpm:[], tht:[], amp:[], vol:[], pow:[], tmp:[] };
        drawSparkline('thr', [], '#000', 0, 100);
        drawSparkline('pwm', [], '#000', 800, 2200);
        drawSparkline('rpm', [], '#000', 0, 10000);
        drawSparkline('tht', [], '#000', 0, 35000);
        drawSparkline('amp', [], '#000', 0, 200);
        drawSparkline('vol', [], '#000', 0, 60);
        drawSparkline('pow', [], '#000', 0, 12000);
        drawSparkline('tmp', [], '#000', 0, 125);
        document.getElementById('status_text').innerText = "Session Reset!";
      });
    }
  </script>
</body>
</html>
)=====");
              // -----------------------------------------------------------------
            }
            break; 
          } else {
            if (currentLine.startsWith("GET /data")) { requestType = 1; }
            else if (currentLine.startsWith("GET /reset")) { requestType = 2; }
            else if (currentLine.startsWith("GET /tare")) { requestType = 3; }
            else if (currentLine.startsWith("GET /cal?")) { 
              requestType = 4; 
              int vIdx = currentLine.indexOf("v=");
              int iIdx = currentLine.indexOf("&i=");
              int spaceIdx = currentLine.indexOf(" ", iIdx);
              if (vIdx != -1 && iIdx != -1 && spaceIdx != -1) {
                String vStr = currentLine.substring(vIdx + 2, iIdx);
                String iStr = currentLine.substring(iIdx + 3, spaceIdx);
                VOLTAGE_MULTIPLIER = vStr.toFloat();
                CURRENT_MULTIPLIER = iStr.toFloat();
              }
            }
            currentLine = "";
          }
        } else if (c != '\r') {
          currentLine += c;
        }
      }
    }
    client.stop(); 
  }

  // ==========================================
  // --- 2. MAIN DAQ LOGGING LOOP ---
  // ==========================================
  if (currentMillis - previousPrintMillis >= printInterval) {
    
    noInterrupts(); 
    unsigned int currentPulses = pulseCount;
    unsigned long startT = firstPulseTime;
    unsigned long endT = lastPulseTime;
    pulseCount = 0; 
    unsigned int currentPWM = sharedPWM;
    interrupts(); 

    float rawRPM = 0; 
    if (currentPulses > 1) {
      unsigned long timeSpan = endT - startT;
      rawRPM = ((currentPulses - 1) * 60000000.0) / timeSpan;
      if (smoothedRPM == 0) smoothedRPM = rawRPM; 
      else smoothedRPM = (rpmAlpha * rawRPM) + ((1.0 - rpmAlpha) * smoothedRPM);
    } else {
      rawRPM = 0; 
      smoothedRPM = 0; 
    }

    float throttlePercent = 0.0;
    if (currentPWM > 800) { 
      int safePWM = constrain(currentPWM, MIN_PWM, MAX_PWM);
      throttlePercent = ((float)(safePWM - MIN_PWM) / (MAX_PWM - MIN_PWM)) * 100.0;
    }

    float currentThrust = scale.get_units(1); 
    if (currentThrust < 0) currentThrust = 0;

    float tempC = tempSensor.getTempCByIndex(0); 
    tempSensor.requestTemperatures(); 

    long sumV = 0;
    long sumI = 0;
    const int numSamples = 10; 
    for (int i = 0; i < numSamples; i++) {
      sumV += analogRead(voltagePin);
      sumI += analogRead(currentPin);
    }
    float avgRawV = (float)sumV / numSamples;
    float avgRawI = (float)sumI / numSamples;
    
    float batteryVoltage = (avgRawV / 16383.0) * 3.3 * VOLTAGE_MULTIPLIER;
    float batteryCurrent = (avgRawI / 16383.0) * 3.3 * CURRENT_MULTIPLIER;

    if (batteryCurrent < 0.5) batteryCurrent = 0.0;
    float powerWatts = batteryVoltage * batteryCurrent;

    previousPrintMillis += printInterval; 
    float runTimeSeconds = (previousPrintMillis - startTimeOffset) / 1000.0;

    String tempStr = (tempC <= -127.0) ? "ERR" : String(tempC, 1);
    String rpmStr = printRawRPM ? String(rawRPM, 1) : String(smoothedRPM, 1);

    latestCSV = "S.No : " + String(logIndex) + 
                " , Time(s) : " + String(runTimeSeconds, 1) + 
                " , Throttle(%) : " + String(throttlePercent, 0) + 
                " , PWM(us) : " + String(currentPWM) + 
                " , RPM : " + rpmStr + 
                " , Thrust (g) : " + String(currentThrust, 0) + 
                " , Current(A) : " + String(batteryCurrent, 2) + 
                " , Voltage(V) : " + String(batteryVoltage, 2) + 
                " , Power(W) : " + String(powerWatts, 0) + 
                " , Temp (°C): " + tempStr;

    if (currentSerialState) {
      Serial.println(latestCSV);
    }

    logIndex++;
  }
}

void measurePulse() {
  unsigned long currentTime = micros();
  if (currentTime - lastPulseTime > 5000) {
    if (pulseCount == 0) firstPulseTime = currentTime; 
    lastPulseTime = currentTime;    
    pulseCount++;
  }
}

void measurePWM() {
  unsigned long currentTime = micros();
  if (digitalRead(pwmInputPin) == HIGH) {
    pwmStartTime = currentTime;
  } else {
    unsigned int pulseWidth = currentTime - pwmStartTime;
    if (pulseWidth > 800 && pulseWidth < 2200) {
      sharedPWM = pulseWidth;
    }
  }
}