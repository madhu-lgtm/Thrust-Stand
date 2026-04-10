#include "HX711.h"
#include <OneWire.h>
#include <DallasTemperature.h>

// --- PIN DEFINITIONS ---
const int irPin = 3;             // RPM Sensor Signal Pin
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

// --- RPM INTERRUPT VARIABLES ---
volatile unsigned long firstPulseTime = 0;
volatile unsigned long lastPulseTime = 0;
volatile unsigned int pulseCount = 0;

// --- TIMING VARIABLES ---
unsigned long previousPrintMillis = 0;
const unsigned long printInterval = 1000; // Print data every 1 second
float smoothedRPM = 0;

void setup() {
  Serial.begin(115200); 
  
  // 1. Initialize the Load Cell
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibration_factor);
  scale.tare(); 
  
  // 2. Initialize the RPM Sensor
  pinMode(irPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(irPin), measurePulse, FALLING);

  // 3. Initialize the DS18B20 Temp Sensor
  tempSensor.begin();
  
  while (!Serial) { ; } 
  Serial.println("MASTER DAQ: RPM + THRUST + TEMP INITIALIZED.");
  Serial.println("Zeroing scale... Please keep the bench still.");
  delay(2000);
  Serial.println("Ready for Motor Test!");
  Serial.println("---------------------------------------------------------------------------------");
}

void loop() {
  unsigned long currentMillis = millis();

  // Run this block exactly once per second
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
      
      if (smoothedRPM == 0) smoothedRPM = rawRPM;
      else smoothedRPM = (alpha * rawRPM) + ((1.0 - alpha) * smoothedRPM);
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
    // Send the command to get temperatures
    tempSensor.requestTemperatures(); 
    // We use index 0 since you only have one DS18B20 on the wire
    float tempC = tempSensor.getTempCByIndex(0); 

    // ==========================================
    // 4. PRINT THE PAIRED DATA
    // ==========================================
    Serial.print("Raw RPM: ");
    Serial.print(rawRPM, 1);
    
    Serial.print("  |  Filtered RPM: ");
    Serial.print(smoothedRPM, 1);
    
    Serial.print("  |  Thrust: ");
    Serial.print(currentThrust, 0); 
    Serial.print(" g");

    Serial.print("  |  Temp: ");
    // The library returns -127 if the wiring is bad or the 4.7k resistor is missing
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