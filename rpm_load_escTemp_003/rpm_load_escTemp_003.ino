#include "HX711.h"
#include <OneWire.h>
#include <DallasTemperature.h>

// ==========================================
// --- USER SETTINGS: MASTER TOGGLES ---
// ==========================================
// Change to 'true' to print Raw RPM. Change to 'false' to print Filtered RPM.
const bool printRawRPM = true; 

// --- PIN DEFINITIONS ---
const int rpmPin = 3;            // RPM Sensor (Optocoupler OUT)
const int LOADCELL_DOUT_PIN = 4; // HX711 Data Pin
const int LOADCELL_SCK_PIN = 5;  // HX711 Clock Pin
const int ONE_WIRE_BUS = 6;      // DS18B20 Data Pin

// --- SENSOR OBJECTS ---
HX711 scale;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature tempSensor(&oneWire);

// --- CALIBRATION & FILTER VARIABLES ---
float calibration_factor = 42.5; // Your custom grams calibration
const float alpha = 0.15;        // RPM Smoothing factor
float smoothedRPM = 0;

// --- RPM INTERRUPT VARIABLES ---
volatile unsigned long firstPulseTime = 0;
volatile unsigned long lastPulseTime = 0;
volatile unsigned int pulseCount = 0;

// --- TIMING VARIABLES ---
unsigned long previousPrintMillis = 0;
const unsigned long printInterval = 1000; // Print data every 1 second

void setup() {
  Serial.begin(115200); 
  while (!Serial) { ; } // Wait for serial monitor to open
  
  Serial.println("INITIALIZING MASTER DAQ...");
  
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
      
      // Calculate DAQ Filter
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
    // 4. PRINT THE PAIRED DATA
    // ==========================================
    Serial.print("RPM: ");
    
    // The Master Toggle Logic
    if (printRawRPM == true) {
      Serial.print(rawRPM, 1);
    } else {
      Serial.print(smoothedRPM, 1);
    }
    
    Serial.print("   |   Thrust: ");
    Serial.print(currentThrust, 0); 
    Serial.print(" g");

    Serial.print("   |   Temp: ");
    if (tempC == DEVICE_DISCONNECTED_C) {
      Serial.println("Wiring Error"); 
    } else {
      Serial.print(tempC, 1); 
      Serial.println(" °C");
    }

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