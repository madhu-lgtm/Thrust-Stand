#include "HX711.h"
#include <OneWire.h>
#include <DallasTemperature.h>

// ==========================================
// --- USER SETTINGS: MASTER TOGGLES ---
// ==========================================
const bool printRawRPM = false; 

// --- PIN DEFINITIONS ---
const int rpmPin = 3;            
const int LOADCELL_DOUT_PIN = 4; 
const int LOADCELL_SCK_PIN = 5;  
const int ONE_WIRE_BUS = 6;      
const int currentPin = A1;       // Mauch PL Sensor Current (3.3V Max)
const int voltagePin = A0;       // Mauch PL Sensor Voltage (3.3V Max)

// ==========================================
// --- MAUCH SENSOR CALIBRATION ---
// ⚠️ IMPORTANT: Change these to match your specific Mauch Calibration Card!
// ==========================================
const float VOLTAGE_MULTIPLIER = 21.0; 
const float CURRENT_MULTIPLIER = 60.6; 

// --- SENSOR OBJECTS ---
HX711 scale;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature tempSensor(&oneWire);

// --- CALIBRATION & FILTER VARIABLES ---
float calibration_factor = 42.5; 
const float alpha = 0.15;        
float smoothedRPM = 0;

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
  
  Serial.println("INITIALIZING MASTER DAQ (3.3V MAUCH POWER SENSE ACTIVE)...");
  
  // Enable 14-bit ADC resolution on Uno R4 (16,384 steps of precision!)
  analogReadResolution(14);
  
  // 1. Initialize the Load Cell
  Serial.println("Zeroing scale... Please keep the bench still.");
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibration_factor);
  scale.tare(); 
  
  // 2. Initialize the RPM Sensor
  pinMode(rpmPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(rpmPin), measurePulse, FALLING);

  // 3. Initialize the DS18B20 Temp Sensor
  tempSensor.begin();
  
  Serial.println("Ready for Motor Test!");
  Serial.println("------------------------------------------------------------------");
}

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - previousPrintMillis >= printInterval) {
    
    // ==========================================
    // 1. GATHER RPM DATA
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
      
      if (smoothedRPM == 0) {
        smoothedRPM = rawRPM; 
      } else {
        smoothedRPM = (alpha * rawRPM) + ((1.0 - alpha) * smoothedRPM);
      }
    } else {
      rawRPM = 0; 
      smoothedRPM = 0;
    }

    // ==========================================
    // 2. GATHER THRUST DATA
    // ==========================================
    float currentThrust = scale.get_units(3); 
    if (currentThrust < 0) currentThrust = 0;

    // ==========================================
    // 3. GATHER TEMPERATURE DATA
    // ==========================================
    tempSensor.requestTemperatures(); 
    float tempC = tempSensor.getTempCByIndex(0); 

    // ==========================================
    // 4. GATHER VOLTAGE & CURRENT (MAUCH 3.3V)
    // ==========================================
    // Read the 14-bit analog values (0 to 16383)
    int rawV = analogRead(voltagePin);
    int rawI = analogRead(currentPin);
    
    // Corrected Math: The signal maxes out at 3.3V, not 5.0V!
    float batteryVoltage = (rawV / 16383.0) * 3.3 * VOLTAGE_MULTIPLIER;
    float batteryCurrent = (rawI / 16383.0) * 3.3 * CURRENT_MULTIPLIER;
    float powerWatts = batteryVoltage * batteryCurrent;

    // ==========================================
    // 5. PRINT THE PAIRED DATA
    // ==========================================
    Serial.print("RPM: ");
    if (printRawRPM == true) {
      Serial.print(rawRPM, 1);
    } else {
      Serial.print(smoothedRPM, 1);
    }
    
    Serial.print("  |  Thrust: ");
    Serial.print(currentThrust, 0); 
    Serial.print(" g");

    Serial.print("  |  Temp: ");
    if (tempC == DEVICE_DISCONNECTED_C) {
      Serial.print("ERR"); 
    } else {
      Serial.print(tempC, 1); 
    }
    Serial.print(" °C");

    Serial.print("  |  Volts: ");
    Serial.print(batteryVoltage, 2);
    Serial.print(" V  |  Amps: ");
    Serial.print(batteryCurrent, 2);
    Serial.print(" A  |  Power: ");
    Serial.print(powerWatts, 0);
    Serial.println(" W");

    previousPrintMillis = currentMillis;
  }
}

// --- THE RPM INTERRUPT FUNCTION ---
void measurePulse() {
  unsigned long currentTime = micros();
  
  if (currentTime - lastPulseTime > 5000) {
    if (pulseCount == 0) {
      firstPulseTime = currentTime; 
    }
    lastPulseTime = currentTime;    
    pulseCount++;
  }
}