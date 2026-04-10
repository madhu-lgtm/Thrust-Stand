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

  // ==========================================
  // --- INITIALIZE WIFI ACCESS POINT ---
  // ==========================================
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
    bool isDataRequest = false;
    unsigned long timeout = millis(); 
    
    while (client.connected() && millis() - timeout < 100) {
      if (client.available()) {
        char c = client.read();
        if (c == '\n') {
          if (currentLine.length() == 0) {
            
            if (isDataRequest) {
              client.println("HTTP/1.1 200 OK");
              client.println("Content-Type: text/plain");
              client.println("Access-Control-Allow-Origin: *");
              client.println("Connection: close");
              client.println();
              client.print(latestCSV);
            } else {
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
  <title>Marut Thrust Stand</title>
  <style>
    body { font-family: 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; background-color: #f4f7f6; color: #333; margin: 0; padding: 20px; }
    h2 { text-align: center; color: #2c3e50; font-weight: 600; margin-bottom: 30px; letter-spacing: 1px; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 20px; max-width: 1200px; margin: 0 auto; }
    .card { background: #fff; padding: 25px; border-radius: 16px; box-shadow: 0 4px 15px rgba(0,0,0,0.05); text-align: center; transition: transform 0.2s; }
    .card:hover { transform: translateY(-2px); }
    h3 { margin: 0; color: #7f8c8d; font-size: 14px; text-transform: uppercase; letter-spacing: 1.5px; }
    .val-container { margin: 15px 0; }
    .val { font-size: 42px; font-weight: 700; color: #2c3e50; }
    .unit { font-size: 18px; color: #95a5a6; margin-left: 5px; font-weight: 600; }
    canvas { width: 100%; height: 70px; margin-top: 10px; }
    .status { text-align: center; margin-top: 30px; font-size: 14px; color: #7f8c8d; }
  </style>
</head>
<body>

  <h2>MARUT THRUST STAND 001</h2>

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

  <div class="status" id="status_text">Connecting to Telemetry Stream...</div>

  <script>
    const maxPts = 50;
    let d = { thr:[], pwm:[], rpm:[], tht:[], amp:[], vol:[], pow:[], tmp:[] };

    function drawSparkline(id, arr, col) {
      let c = document.getElementById('c_' + id);
      let ctx = c.getContext('2d');
      c.width = c.clientWidth; 
      c.height = c.clientHeight;
      ctx.clearRect(0, 0, c.width, c.height);
      
      if(arr.length < 2) return;
      
      let min = Math.min(...arr);
      let max = Math.max(...arr);
      if(max === min) { min -= 1; max += 1; }
      
      ctx.beginPath(); 
      ctx.strokeStyle = col; 
      ctx.lineWidth = 3;
      ctx.lineJoin = 'round';
      
      for(let i = 0; i < arr.length; i++) {
        let ptX = (i / (maxPts - 1)) * c.width;
        let ptY = c.height - ((arr[i] - min) / (max - min)) * c.height;
        ptY = Math.max(2, Math.min(c.height - 2, ptY)); 
        if(i === 0) ctx.moveTo(ptX, ptY); 
        else ctx.lineTo(ptX, ptY);
      }
      ctx.stroke();
    }

    setInterval(() => {
      fetch('/data').then(r => r.text()).then(t => {
        if(!t) return;
        let p = t.split(',');
        if(p.length < 9) return;
        
        document.getElementById('status_text').innerText = "Live Data Active | Uptime: " + p[1].split(':')[1].trim() + "s";
        
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
          d.tmp.push(v_tmp); if(d.tmp.length > maxPts) d.tmp.shift(); drawSparkline('tmp', d.tmp, '#c0392b'); // Red
        }

        d.thr.push(v_thr); if(d.thr.length > maxPts) d.thr.shift(); drawSparkline('thr', d.thr, '#3498db'); // Blue
        d.pwm.push(v_pwm); if(d.pwm.length > maxPts) d.pwm.shift(); drawSparkline('pwm', d.pwm, '#00bcd4'); // Cyan
        d.rpm.push(v_rpm); if(d.rpm.length > maxPts) d.rpm.shift(); drawSparkline('rpm', d.rpm, '#9b59b6'); // Purple
        d.tht.push(v_tht); if(d.tht.length > maxPts) d.tht.shift(); drawSparkline('tht', d.tht, '#e74c3c'); // Red-Orange
        d.amp.push(v_amp); if(d.amp.length > maxPts) d.amp.shift(); drawSparkline('amp', d.amp, '#f39c12'); // Yellow-Orange
        d.vol.push(v_vol); if(d.vol.length > maxPts) d.vol.shift(); drawSparkline('vol', d.vol, '#2ecc71'); // Green
        d.pow.push(v_pow); if(d.pow.length > maxPts) d.pow.shift(); drawSparkline('pow', d.pow, '#e67e22'); // Dark Orange
        
      }).catch(e => {
        document.getElementById('status_text').innerText = "Connection Lost. Reconnecting...";
      });
    }, 500); 
  </script>
</body>
</html>
)=====");
              // -----------------------------------------------------------------
            }
            break; 
          } else {
            if (currentLine.startsWith("GET /data")) {
              isDataRequest = true;
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

    // ==========================================
    // --- 3. CONSTRUCT THE CSV STRING ---
    // ==========================================
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

// ==========================================
// --- INTERRUPT FUNCTIONS ---
// ==========================================
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