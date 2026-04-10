#include "HX711.h"

// --- PIN DEFINITIONS ---
const int irPin = 3;             // RPM Sensor Signal Pin
const int LOADCELL_DOUT_PIN = 4; // HX711 Data Pin
const int LOADCELL_SCK_PIN = 5;  // HX711 Clock Pin

// --- THRUST VARIABLES ---
HX711 scale;
float calibration_factor = 42.5; // Your custom grams calibration

// --- RPM INTERRUPT VARIABLES ---
volatile unsigned long firstPulseTime = 0;
volatile unsigned long lastPulseTime = 0;
volatile unsigned int pulseCount = 0;

// --- TIMING & FILTER VARIABLES ---
unsigned long previousPrintMillis = 0;
const unsigned long printInterval = 1000; // Print data every 1 second
float smoothedRPM = 0;
const float alpha = 0.15; // RPM Smoothing factor

void setup() {
  Serial.begin(115200); 
  
  // 1. Initialize the Load Cell
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibration_factor);
  scale.tare(); // Zero the scale on startup
  
  // 2. Initialize the RPM Sensor
  pinMode(irPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(irPin), measurePulse, FALLING);
  
  while (!Serial) { ; } // Wait for serial connection
  Serial.println("MASTER DAQ INITIALIZED.");
  Serial.println("Zeroing scale... Please keep the bench still.");
  delay(2000);
  Serial.println("Ready for Motor Test!");
  Serial.println("--------------------------------------------------");
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
    pulseCount = 0; // Reset for next second
    interrupts();   // Resume interrupts

    if (currentPulses > 1) {
      unsigned long timeSpan = endT - startT;
      float exactRPM = ((currentPulses - 1) * 60000000.0) / timeSpan;
      
      // Apply the EMA Filter
      if (smoothedRPM == 0) smoothedRPM = exactRPM;
      else smoothedRPM = (alpha * exactRPM) + ((1.0 - alpha) * smoothedRPM);
    } else {
      smoothedRPM = 0; // Motor stopped
    }

    // ==========================================
    // 2. GATHER THRUST DATA
    // ==========================================
    // We average 3 readings to keep it fast and prevent blocking the RPM interrupt
    float currentThrust = scale.get_units(3); 
    
    // Prevent negative numbers drifting around zero when idle
    if (currentThrust < 0) currentThrust = 0;

    // ==========================================
    // 3. PRINT THE PAIRED DATA
    // ==========================================
    Serial.print("RPM: ");
    if (smoothedRPM > 0) {
      Serial.print(smoothedRPM, 1);
    } else {
      Serial.print("0.0    "); // Keep formatting neat
    }
    
    Serial.print("   |   Thrust: ");
    Serial.print(currentThrust, 0); // Print with 0 decimal places
    Serial.println(" g");

    previousPrintMillis = currentMillis;
  }
}

// --- THE RPM INTERRUPT FUNCTION ---
void measurePulse() {
  unsigned long currentTime = micros();
  
  // 5,000 microsecond debounce (Allows up to 12,000 RPM)
  if (currentTime - lastPulseTime > 5000) {
    if (pulseCount == 0) {
      firstPulseTime = currentTime;
    }
    lastPulseTime = currentTime;
    pulseCount++;
  }
}
