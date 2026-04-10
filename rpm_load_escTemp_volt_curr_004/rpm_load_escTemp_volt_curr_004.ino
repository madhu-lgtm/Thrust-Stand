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

// --- DAQ FILTER VARIABLES ---
float calibration_factor = 42.5; 
const float rpmAlpha = 0.15;        // RPM Smoothing factor
float smoothedRPM = 0;

const float powerAlpha = 0.15;      // Voltage/Current Smoothing factor
float smoothedVoltage = 0;
float smoothedCurrent = 0;

// --- RPM INTERRUPT VARIABLES ---
volatile unsigned long firstPulseTime = 0;
volatile unsigned long lastPulseTime = 0;
volatile unsigned int pulseCount = 0;

// --- TIMING VARIABLES ---
unsigned long previousPrintMillis = 0;
const unsigned long printInterval = 1000; 

void setup() {
  Serial.begin(115200); 
  while (!Serial) { ; } 
  
  Serial.println("INITIALIZING STANDALONE MASTER DAQ...");
  
  // 1. Tell Arduino to use the physical AREF pin (locked to 3.3V)
  analogReference(AR_EXTERNAL);
  
  // 2. Enable 14-bit ADC resolution
  analogReadResolution(14);
  
  // Initialize Load Cell
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibration_factor);
  scale.tare(); 
  
  // Initialize RPM
  pinMode(rpmPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(rpmPin), measurePulse, FALLING);

  // Initialize Temp
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
    // 4. GATHER VOLTAGE & CURRENT (HEAVY FILTER)
    // ==========================================
    long sumV = 0;
    long sumI = 0;
    const int numSamples = 50; // Increased to 50 rapid snapshots

    for (int i = 0; i < numSamples; i++) {
      sumV += analogRead(voltagePin);
      sumI += analogRead(currentPin);
      delay(1); 
    }

    float avgRawV = (float)sumV / numSamples;
    float avgRawI = (float)sumI / numSamples;
    
    float instantVoltage = (avgRawV / 16383.0) * 3.3 * VOLTAGE_MULTIPLIER;
    float instantCurrent = (avgRawI / 16383.0) * 3.3 * CURRENT_MULTIPLIER;

    // Apply the Exponential Moving Average filter
    if (smoothedVoltage == 0) smoothedVoltage = instantVoltage;
    else smoothedVoltage = (powerAlpha * instantVoltage) + ((1.0 - powerAlpha) * smoothedVoltage);

    if (smoothedCurrent == 0) smoothedCurrent = instantCurrent;
    else smoothedCurrent = (powerAlpha * instantCurrent) + ((1.0 - powerAlpha) * smoothedCurrent);
    
    // Phantom Amp Filter
    if (smoothedCurrent < 0.5) {
      smoothedCurrent = 0.0;
    }
    
    float powerWatts = smoothedVoltage * smoothedCurrent;

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
    Serial.print(smoothedVoltage, 2);
    Serial.print(" V  |  Amps: ");
    Serial.print(smoothedCurrent, 2);
    Serial.print(" A  |  Power: ");
    Serial.print(powerWatts, 0);
    Serial.println(" W");

    previousPrintMillis = currentMillis;
  }
}

// ==========================================
// --- THE RPM INTERRUPT FUNCTION ---
// ==========================================
void measurePulse() {
  unsigned long currentTime = micros();
  if (currentTime - lastPulseTime > 5000) {
    if (pulseCount == 0) firstPulseTime = currentTime; 
    lastPulseTime = currentTime;    
    pulseCount++;
  }
}