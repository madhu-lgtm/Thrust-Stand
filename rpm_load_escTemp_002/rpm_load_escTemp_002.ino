#include "HX711.h"
#include <OneWire.h>
#include <DallasTemperature.h>

// --- PIN DEFINITIONS ---
const int rpmPin = 3;            // RPM Sensor (Optocoupler OUT)
const int LOADCELL_DOUT_PIN = 4; // HX711 Data Pin
const int LOADCELL_SCK_PIN = 5;  // HX711 Clock Pin
const int ONE_WIRE_BUS = 6;      // DS18B20 Data Pin

// --- SENSOR OBJECTS ---
HX711 scale;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature tempSensor(&oneWire);

// --- CALIBRATION VARIABLES ---
float calibration_factor = 42.5; // Your custom grams calibration

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
  // INPUT_PULLUP keeps the pin HIGH until the optocoupler pulls it LOW
  pinMode(rpmPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(rpmPin), measurePulse, FALLING);

  // 3. Initialize the DS18B20 Temp Sensor
  tempSensor.begin();
  
  Serial.println("Ready for Motor Test!");
  Serial.println("------------------------------------------------------------------");
}

void loop() {
  unsigned long currentMillis = millis();

  // Run this block exactly once per second
  if (currentMillis - previousPrintMillis >= printInterval) {
    
    // ==========================================
    // 1. GATHER RPM DATA
    // ==========================================
    noInterrupts(); // Pause interrupts safely
    unsigned int currentPulses = pulseCount;
    unsigned long startT = firstPulseTime;
    unsigned long endT = lastPulseTime;
    pulseCount = 0; // Reset for next 1-second window
    interrupts();   // Resume interrupts

    float rawRPM = 0; 

    // We need at least 2 pulses to measure a time span
    if (currentPulses > 1) {
      unsigned long timeSpan = endT - startT;
      // Exact RPM formula
      rawRPM = ((currentPulses - 1) * 60000000.0) / timeSpan;
    } else {
      rawRPM = 0; // Motor stopped
    }

    // ==========================================
    // 2. GATHER THRUST DATA
    // ==========================================
    // Average 3 readings to keep it fast and prevent blocking the RPM interrupt
    float currentThrust = scale.get_units(3); 
    
    // Prevent negative numbers drifting around zero when idle
    if (currentThrust < 0) currentThrust = 0;

    // ==========================================
    // 3. GATHER TEMPERATURE DATA
    // ==========================================
    // Send the command to read the DS18B20
    tempSensor.requestTemperatures(); 
    float tempC = tempSensor.getTempCByIndex(0); 

    // ==========================================
    // 4. PRINT THE PAIRED DATA
    // ==========================================
    Serial.print("RPM: ");
    Serial.print(rawRPM, 1);
    
    Serial.print("   |   Thrust: ");
    Serial.print(currentThrust, 0); 
    Serial.print(" g");

    Serial.print("   |   Temp: ");
    if (tempC == DEVICE_DISCONNECTED_C) {
      Serial.println("Wiring Error"); // Checks if 4.7k resistor is missing
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
  
  // 5,000 microsecond debounce (Ignores optical bounce, allows up to 12,000 RPM)
  if (currentTime - lastPulseTime > 5000) {
    if (pulseCount == 0) {
      firstPulseTime = currentTime; // Mark the start of the rotation window
    }
    lastPulseTime = currentTime;    // Mark the end of the rotation window
    pulseCount++;
  }
}