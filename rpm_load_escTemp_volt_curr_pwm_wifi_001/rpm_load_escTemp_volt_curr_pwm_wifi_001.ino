#include "HX711.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFiS3.h> // NEW: The Uno R4 WiFi Library!

// ==========================================
// --- WIFI & DASHBOARD SETTINGS ---
// ==========================================
char ssid[] = "Marut_Thrust_Stand_001"; // Change for next stand
char pass[] = "Marut@123";              // Keep the same for convenience
int WIFI_CHANNEL = 1;                   // Stand 1=CH 1, Stand 2=CH 6, Stand 3=CH 11

WiFiServer server(80); 
String latestCSV = ""; // Holds the live data for the web browser

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
const unsigned long printInterval = 500; // 500ms = 0.5 seconds
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
    while (true); // Freeze if WiFi hardware is dead
  }

  // Start the Access Point
  int status = WiFi.beginAP(ssid, pass, WIFI_CHANNEL);
  if (status != WL_AP_LISTENING) {
    Serial.println("Creating Access Point failed");
    while (true);
  }

  // Wait a moment for AP to stabilize, then start web server
  delay(2000);
  server.begin();

  Serial.println("ACCESS POINT ONLINE!");
  Serial.print("Network Name: ");
  Serial.println(ssid);
  Serial.print("Password: ");
  Serial.println(pass);
  Serial.println("To view live data, connect to this WiFi and open your browser to:");
  Serial.println("http://192.168.4.1");
  Serial.println("---------------------------------------------------------\n");
}

void loop() {
  unsigned long currentMillis = millis();
  bool currentSerialState = Serial;

  // --- DYNAMIC SERIAL MONITOR RESET ---
  if (currentSerialState && !lastSerialState) {
    logIndex = 1;
    startTimeOffset = currentMillis;
    previousPrintMillis = currentMillis; 
    Serial.println("\n--- NEW DAQ LOGGING SESSION STARTED ---");
  }
  lastSerialState = currentSerialState;

  // ==========================================
  // --- 1. HANDLE WEB DASHBOARD CLIENTS ---
  // (We do this extremely fast so it doesn't block the sensors)
  // ==========================================
  WiFiClient client = server.available();
  if (client) {
    String currentLine = "";
    bool isDataRequest = false;
    unsigned long timeout = millis(); // Anti-freeze protection
    
    while (client.connected() && millis() - timeout < 100) {
      if (client.available()) {
        char c = client.read();
        if (c == '\n') {
          if (currentLine.length() == 0) {
            // End of headers! Send the response.
            if (isDataRequest) {
              // Send the raw CSV data to the Javascript
              client.println("HTTP/1.1 200 OK");
              client.println("Content-Type: text/plain");
              client.println("Access-Control-Allow-Origin: *");
              client.println("Connection: close");
              client.println();
              client.print(latestCSV);
            } else {
              // Send the Beautiful HTML/JS Dashboard
              client.println("HTTP/1.1 200 OK");
              client.println("Content-Type: text/html");
              client.println("Connection: close");
              client.println();
              client.print("<!DOCTYPE html><html><head><title>Marut DAQ Dashboard</title></head>");
              client.print("<body style='background-color:#1e1e1e; color:#00ff00; font-family:monospace; padding:20px;'>");
              client.print("<h2>");
              client.print(ssid);
              client.print(" - LIVE TELEMETRY</h2>");
              client.print("<button onclick=\"document.getElementById('log').innerHTML=''\" style='padding:10px; margin-bottom:10px; background:#333; color:#fff; border:1px solid #555; cursor:pointer;'>Clear Log</button>");
              client.print("<div id='log' style='white-space:pre-wrap; height:80vh; overflow-y:auto; border:1px solid #444; padding:10px; background:#000;'></div>");
              client.print("<script>");
              client.print("let lastData = '';");
              client.print("setInterval(() => {");
              client.print("fetch('/data').then(r => r.text()).then(t => {");
              client.print("if(t !== '' && t !== lastData) {");
              client.print("lastData = t;");
              client.print("let logDiv = document.getElementById('log');");
              client.print("logDiv.innerHTML += t + '\\n';");
              client.print("logDiv.scrollTop = logDiv.scrollHeight;");
              client.print("}");
              client.print("}).catch(e => console.log(e));");
              client.print("}, 250);"); // Web browser checks for new data 4 times a second
              client.print("</script></body></html>");
            }
            break; // Break out of the while loop
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
    client.stop(); // Close connection to keep loop flying fast
  }

  // ==========================================
  // --- 2. MAIN DAQ LOGGING LOOP ---
  // ==========================================
  if (currentMillis - previousPrintMillis >= printInterval) {
    
    // GATHER INTERRUPT DATA
    noInterrupts(); 
    unsigned int currentPulses = pulseCount;
    unsigned long startT = firstPulseTime;
    unsigned long endT = lastPulseTime;
    pulseCount = 0; 
    unsigned int currentPWM = sharedPWM;
    interrupts(); 

    // --- Calculate RPM ---
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

    // --- Calculate Throttle Percentage ---
    float throttlePercent = 0.0;
    if (currentPWM > 800) { 
      int safePWM = constrain(currentPWM, MIN_PWM, MAX_PWM);
      throttlePercent = ((float)(safePWM - MIN_PWM) / (MAX_PWM - MIN_PWM)) * 100.0;
    }

    // GATHER THRUST 
    float currentThrust = scale.get_units(1); 
    if (currentThrust < 0) currentThrust = 0;

    // GATHER TEMP
    float tempC = tempSensor.getTempCByIndex(0); 
    tempSensor.requestTemperatures(); 

    // GATHER VOLTAGE & CURRENT 
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

    // --- ANTI-DRIFT ZERO-BASED TIME ---
    previousPrintMillis += printInterval; 
    float runTimeSeconds = (previousPrintMillis - startTimeOffset) / 1000.0;

    // ==========================================
    // --- 3. CONSTRUCT THE CSV STRING ---
    // ==========================================
    String tempStr = (tempC <= -127.0) ? "ERR" : String(tempC, 1);
    String rpmStr = printRawRPM ? String(rawRPM, 1) : String(smoothedRPM, 1);

    // Build the string piece by piece (safest way for memory)
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

    // Print to Serial Monitor (for testing/USB users)
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