#include "HX711.h"
#include <OneWire.h>
#include <DallasTemperature.h>

// ==========================================
// --- USER SETTINGS: MASTER TOGGLES ---
// ==========================================
const bool printRawRPM = false; 

// ==========================================
// --- PWM & THROTTLE CALIBRATION ---
// Adjust these to match your ESC or Radio Transmitter's exact endpoints!
// ==========================================
const int MIN_PWM = 1000; // PWM value for 0% Throttle
const int MAX_PWM = 2000; // PWM value for 100% Throttle

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

// ⚠️ CHANGE THIS NUMBER TO CONTROL LOGGING SPEED
// 1000 = 1 second, 500 = 0.5 seconds, 250 = 0.25 seconds
const unsigned long printInterval = 500; 

unsigned long logIndex = 1; // Serial number counter for data logging

void setup() {
  Serial.begin(115200); 
  while (!Serial) { ; } 
  
  Serial.println("INITIALIZING STANDALONE MASTER DAQ (0.5s LOGGING)...");
  
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
  
  Serial.println("Ready for Motor Test!");
  Serial.println("------------------------------------------------------------------");
}

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - previousPrintMillis >= printInterval) {
    
    // ==========================================
    // 1. GATHER INTERRUPT DATA
    // ==========================================
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

    // ==========================================
    // 2. GATHER THRUST
    // ==========================================
    float currentThrust = scale.get_units(3); 
    if (currentThrust < 0) currentThrust = 0;

    // ==========================================
    // 3. GATHER TEMP
    // ==========================================
    tempSensor.requestTemperatures(); 
    float tempC = tempSensor.getTempCByIndex(0); 

    // ==========================================
    // 4. GATHER VOLTAGE & CURRENT 
    // ==========================================
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

    if (batteryCurrent < 0.5) {
      batteryCurrent = 0.0;
    }
    float powerWatts = batteryVoltage * batteryCurrent;

    // --- Calculate Run Time (Stopwatch) ---
    float runTimeSeconds = currentMillis / 1000.0;

    // ==========================================
    // 5. PRINT DATA (CUSTOM CSV FORMAT)
    // ==========================================
    Serial.print("S.No : ");
    Serial.print(logIndex);
    
    Serial.print(" , Time(s) : ");
    Serial.print(runTimeSeconds, 1);
    
    Serial.print(" , Throttle(%) : ");
    Serial.print(throttlePercent, 0); 
    
    Serial.print(" , PWM(us) : ");
    Serial.print(currentPWM);
    
    Serial.print(" , RPM : ");
    if (printRawRPM) {
      Serial.print(rawRPM, 1);
    } else {
      Serial.print(smoothedRPM, 1);
    }
    
    Serial.print(" , Thrust (g) : ");
    Serial.print(currentThrust, 0); 
    
    Serial.print(" , Current(A) : ");
    Serial.print(batteryCurrent, 2);
    
    Serial.print(" , Voltage(V) : ");
    Serial.print(batteryVoltage, 2);
    
    Serial.print(" , Power(W) : ");
    Serial.print(powerWatts, 0);
    
    Serial.print(" , Temp (°C): ");
    if (tempC == DEVICE_DISCONNECTED_C) {
      Serial.println("ERR"); 
    } else {
      Serial.println(tempC, 1); 
    }

    // Increment counters
    previousPrintMillis = currentMillis;
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