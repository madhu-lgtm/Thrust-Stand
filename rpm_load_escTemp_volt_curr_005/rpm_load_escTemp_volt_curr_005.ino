#include "HX711.h"
#include <OneWire.h>
#include <DallasTemperature.h>

// ==========================================
// --- USER SETTINGS: MASTER TOGGLES ---
// ==========================================
const bool printRawRPM = false; 

// ==========================================
// --- MAUCH SENSOR CALIBRATION ---
// ==========================================
const float VOLTAGE_MULTIPLIER = 19.02; 
const float CURRENT_MULTIPLIER = 60.6;  

// ==========================================
// --- PIN DEFINITIONS ---
// ==========================================
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

// --- RPM INTERRUPT VARIABLES ---
volatile unsigned long firstPulseTime = 0;
volatile unsigned long lastPulseTime = 0;
volatile unsigned int pulseCount = 0;

// --- TIMING VARIABLES ---
unsigned long previousPrintMillis = 0;
// Note: If you want the screen to update faster, change 1000 to 500 (half-second) or 250
const unsigned long printInterval = 1000; 

void setup() {
  Serial.begin(115200); 
  while (!Serial) { ; } 
  
  Serial.println("INITIALIZING STANDALONE MASTER DAQ (ZERO-LAG MODE)...");
  
  analogReference(AR_EXTERNAL);
  analogReadResolution(14);
  
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibration_factor);
  scale.tare(); 
  
  pinMode(rpmPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(rpmPin), measurePulse, FALLING);

  tempSensor.begin();
  
  Serial.println("Ready for Motor Test!");
  Serial.println("------------------------------------------------------------------");
}

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - previousPrintMillis >= printInterval) {
    
    // ==========================================
    // 1. GATHER RPM
    // ==========================================
    noInterrupts(); 
    unsigned int currentPulses = pulseCount;
    unsigned long startT = firstPulseTime;
    unsigned long endT = lastPulseTime;
    pulseCount = 0; 
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
    // 4. GATHER VOLTAGE & CURRENT (INSTANTANEOUS)
    // ==========================================
    long sumV = 0;
    long sumI = 0;
    const int numSamples = 10; 

    // Micro-burst to clear ADC glitches, zero artificial delay
    for (int i = 0; i < numSamples; i++) {
      sumV += analogRead(voltagePin);
      sumI += analogRead(currentPin);
    }

    float avgRawV = (float)sumV / numSamples;
    float avgRawI = (float)sumI / numSamples;
    
    float batteryVoltage = (avgRawV / 16383.0) * 3.3 * VOLTAGE_MULTIPLIER;
    float batteryCurrent = (avgRawI / 16383.0) * 3.3 * CURRENT_MULTIPLIER;

    // Hard cutoff for background sensor noise
    if (batteryCurrent < 0.5) {
      batteryCurrent = 0.0;
    }
    
    float powerWatts = batteryVoltage * batteryCurrent;

    // ==========================================
    // 5. PRINT DATA
    // ==========================================
    Serial.print("RPM: ");
    if (printRawRPM) Serial.print(rawRPM, 1);
    else Serial.print(smoothedRPM, 1);
    
    Serial.print("  |  Thrust: ");
    Serial.print(currentThrust, 0); 
    Serial.print(" g  |  Temp: ");
    
    if (tempC == DEVICE_DISCONNECTED_C) Serial.print("ERR"); 
    else Serial.print(tempC, 1); 
    
    Serial.print(" °C  |  Volts: ");
    Serial.print(batteryVoltage, 2);
    Serial.print(" V  |  Amps: ");
    Serial.print(batteryCurrent, 2);
    Serial.print(" A  |  Power: ");
    Serial.print(powerWatts, 0);
    Serial.println(" W");

    previousPrintMillis = currentMillis;
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