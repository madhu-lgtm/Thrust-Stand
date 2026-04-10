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
// --- MAUCH SENSOR CALIBRATION ---
// ==========================================
const float VOLTAGE_MULTIPLIER = 19.02; 
const float CURRENT_MULTIPLIER = 60.6;  

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
            
            if (requestType == 1) {
              client.println("HTTP/1.1 200 OK");
              client.println("Content-Type: text/plain");
              client.println("Access-Control-Allow-Origin: *");
              client.println("Connection: close");
              client.println();
              client.print(latestCSV);
            } 
            else if (requestType == 2) {
              logIndex = 1;
              startTimeOffset = millis();
              previousPrintMillis = millis();
              client.println("HTTP/1.1 200 OK");
              client.println("Connection: close");
              client.println();
              client.print("RESET_OK");
            } 
            else {
              client.println("HTTP/1.1 200 OK");
              client.println("Content-Type: text/html");
              client.println("Connection: close");
              client.println();
              
              // -----------------------------------------------------------------
              // PURE HTML/JS/CSS PAYLOAD (WITH FIXED GRAPH BOUNDS & TALLER GRAPHS)
              // -----------------------------------------------------------------
              client.print(R"=====(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Marut DAQ Interface</title>
  <style>
    body { font-family: 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; background-color: #f4f7f6; color: #333; margin: 0; display: flex; height: 100vh; overflow: hidden; }
    
    .sidebar { width: 280px; background-color: #1e2b3c; color: white; display: flex; flex-direction: column; padding: 25px; box-shadow: 2px 0 10px rgba(0,0,0,0.1); z-index: 10; }
    
    .logo-container { 
      background: #ffffff; 
      margin-bottom: 40px; 
      padding: 15px 20px; 
      text-align: center; 
      border-radius: 12px; 
      box-shadow: 0 4px 15px rgba(0,0,0,0.15); 
    }
    .logo-container img { 
      max-width: 130px; 
      width: 100%; 
      height: auto; 
      display: block;
      margin: 0 auto;
    }
    
    .btn { color: white; border: none; padding: 14px 15px; margin-bottom: 15px; border-radius: 8px; cursor: pointer; font-weight: 600; font-size: 15px; transition: 0.2s; text-align: center; }
    .btn:hover { transform: translateY(-2px); }
    .btn-record-start { background: #2ecc71; }
    .btn-record-stop { background: #e74c3c; animation: pulse 1.5s infinite; }
    .btn-download { background: #3498db; }
    .btn-reset { background: #7f8c8d; }
    
    @keyframes pulse {
      0% { box-shadow: 0 0 0 0 rgba(231, 76, 60, 0.7); }
      70% { box-shadow: 0 0 0 10px rgba(231, 76, 60, 0); }
      100% { box-shadow: 0 0 0 0 rgba(231, 76, 60, 0); }
    }

    .data-stats { margin-top: auto; background: #2c3e50; padding: 15px; border-radius: 8px; text-align: center; font-size: 14px; color: #bdc3c7;}
    .data-stats span { display: block; font-size: 24px; color: #2ecc71; font-weight: bold; margin-top: 5px;}
    
    .main-content { flex: 1; padding: 40px; overflow-y: auto; background-color: #f4f7f6; }
    .header-bar { display: flex; justify-content: space-between; align-items: center; margin-bottom: 30px; }
    .header-bar h1 { margin: 0; color: #2c3e50; font-size: 26px; }
    .status { font-size: 14px; color: #7f8c8d; font-weight: 600; background: #fff; padding: 8px 15px; border-radius: 20px; box-shadow: 0 2px 5px rgba(0,0,0,0.05);}
    
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(240px, 1fr)); gap: 20px; }
    .card { background: #fff; padding: 25px; border-radius: 16px; box-shadow: 0 4px 15px rgba(0,0,0,0.05); text-align: center; }
    h3 { margin: 0; color: #7f8c8d; font-size: 13px; text-transform: uppercase; letter-spacing: 1.5px; }
    .val-container { margin: 15px 0; }
    .val { font-size: 42px; font-weight: 700; color: #2c3e50; }
    .unit { font-size: 18px; color: #95a5a6; margin-left: 5px; font-weight: 600; }
    
    /* GRAPHS ARE NOW 35% TALLER */
    canvas { width: 100%; height: 95px; margin-top: 15px; }
  </style>
</head>
<body>

  <div class="sidebar">
    <div class="logo-container">
      <img src="data:image/png;base64,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">
    </div>
    
    <button id="recordBtn" class="btn btn-record-start" onclick="toggleRecord()">
      ▶ Start Recording
    </button>

    <button class="btn btn-download" onclick="downloadCSV()">
      ⬇ Download Test Data
    </button>
    
    <button class="btn btn-reset" onclick="restartSession()">
      ↺ Restart New Data
    </button>
    
    <div class="data-stats">
      Recorded Data Points
      <span id="log_count">0</span>
    </div>
  </div>

  <div class="main-content">
    <div class="header-bar">
      <h1>Thrust Stand Data</h1>
      <div class="status" id="status_text">Connecting...</div>
    </div>

    <div class="grid">
      <div class="card"><h3>Throttle</h3><div class="val-container"><span class="val" id="v_thr">0</span><span class="unit">%</span></div><canvas id="c_thr"></canvas></div>
      <div class="card"><h3>PWM</h3><div class="val-container"><span class="val" id="v_pwm">1000</span><span class="unit">us</span></div><canvas id="c_pwm"></canvas></div>
      <div class="card"><h3>RPM</h3><div class="val-container"><span class="val" id="v_rpm">0.0</span><span class="unit">rpm</span></div><canvas id="c_rpm"></canvas></div>
      <div class="card"><h3>Thrust</h3><div class="val-container"><span class="val" id="v_tht">0</span><span class="unit">g</span></div><canvas id="c_tht"></canvas></div>
      <div class="card"><h3>Current</h3><div class="val-container"><span class="val" id="v_amp">0.00</span><span class="unit">A</span></div><canvas id="c_amp"></canvas></div>
      <div class="card"><h3>Voltage</h3><div class="val-container"><span class="val" id="v_vol">0.00</span><span class="unit">V</span></div><canvas id="c_vol"></canvas></div>
      <div class="card"><h3>Power</h3><div class="val-container"><span class="val" id="v_pow">0</span><span class="unit">W</span></div><canvas id="c_pow"></canvas></div>
      <div class="card"><h3>Temp</h3><div class="val-container"><span class="val" id="v_tmp">0.0</span><span class="unit">°C</span></div><canvas id="c_tmp"></canvas></div>
    </div>
  </div>

  <script>
    const maxPts = 50;
    let d = { thr:[], pwm:[], rpm:[], tht:[], amp:[], vol:[], pow:[], tmp:[] };
    
    let isRecording = false; 
    let csvHeader = "S.No,Time(s),Throttle(%),PWM(us),RPM,Thrust(g),Current(A),Voltage(V),Power(W),Temp(C)";
    let csvLog = [csvHeader];
    
    let recordIndex = 1;
    let recordStartTime = -1;

    // --- UPDATED: Sparkline function now accepts fixed Min and Max limits! ---
    function drawSparkline(id, arr, col, fixedMin, fixedMax) {
      let c = document.getElementById('c_' + id);
      let ctx = c.getContext('2d');
      c.width = c.clientWidth; 
      c.height = c.clientHeight;
      ctx.clearRect(0, 0, c.width, c.height);
      if(arr.length < 2) return;
      
      // Use fixed limits, or fallback to auto-scaling if none provided
      let min = (fixedMin !== undefined) ? fixedMin : Math.min(...arr);
      let max = (fixedMax !== undefined) ? fixedMax : Math.max(...arr);
      if(max === min) { min -= 1; max += 1; } // Prevent divide by zero
      
      ctx.beginPath(); ctx.strokeStyle = col; ctx.lineWidth = 3; ctx.lineJoin = 'round';
      for(let i = 0; i < arr.length; i++) {
        let ptX = (i / (maxPts - 1)) * c.width;
        let ptY = c.height - ((arr[i] - min) / (max - min)) * c.height;
        // Cap the line at the top/bottom of the canvas so it never breaks the UI
        ptY = Math.max(2, Math.min(c.height - 2, ptY)); 
        if(i === 0) ctx.moveTo(ptX, ptY); else ctx.lineTo(ptX, ptY);
      }
      ctx.stroke();
    }

    let fetchInterval = setInterval(fetchData, 500);

    function fetchData() {
      fetch('/data').then(r => r.text()).then(t => {
        if(!t) return;
        let p = t.split(',');
        if(p.length < 9) return;
        
        let rawTime = parseFloat(p[1].split(':')[1].trim());
        document.getElementById('status_text').innerText = "Live | Uptime: " + rawTime + "s";
        
        if (isRecording) {
          if (recordStartTime === -1) {
            recordStartTime = rawTime;
          }
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
          drawSparkline('tmp', d.tmp, '#c0392b', 0, 125); // TEMP: 0 to 125 C
        }

        // --- HARDCODED GRAPH LIMITS APPLIED HERE ---
        // Format: drawSparkline('id', data_array, 'hex_color', MIN_LIMIT, MAX_LIMIT);
        
        d.thr.push(v_thr); if(d.thr.length > maxPts) d.thr.shift(); 
        drawSparkline('thr', d.thr, '#3498db', 0, 100);       // THROTTLE: 0 to 100%
        
        d.pwm.push(v_pwm); if(d.pwm.length > maxPts) d.pwm.shift(); 
        drawSparkline('pwm', d.pwm, '#00bcd4', 800, 2200);    // PWM: 800 to 2200 us
        
        d.rpm.push(v_rpm); if(d.rpm.length > maxPts) d.rpm.shift(); 
        drawSparkline('rpm', d.rpm, '#9b59b6', 0, 10000);     // RPM: 0 to 10,000
        
        d.tht.push(v_tht); if(d.tht.length > maxPts) d.tht.shift(); 
        drawSparkline('tht', d.tht, '#e74c3c', 0, 50000);     // THRUST: 0 to 50,000g (50kg)
        
        d.amp.push(v_amp); if(d.amp.length > maxPts) d.amp.shift(); 
        drawSparkline('amp', d.amp, '#f39c12', 0, 200);       // CURRENT: 0 to 200A
        
        d.vol.push(v_vol); if(d.vol.length > maxPts) d.vol.shift(); 
        drawSparkline('vol', d.vol, '#2ecc71', 0, 60);        // VOLTAGE: 0 to 60V
        
        d.pow.push(v_pow); if(d.pow.length > maxPts) d.pow.shift(); 
        drawSparkline('pow', d.pow, '#e67e22', 0, 12000);     // POWER: 0 to 12,000W
        
      }).catch(e => {
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
      link.download = 'Marut_Test_Data.csv';
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
        
        // Reset empty graphs with limits
        drawSparkline('thr', [], '#000', 0, 100);
        drawSparkline('pwm', [], '#000', 800, 2200);
        drawSparkline('rpm', [], '#000', 0, 10000);
        drawSparkline('tht', [], '#000', 0, 50000);
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