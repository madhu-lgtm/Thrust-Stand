#include "HX711.h"
#include <Wire.h>
#include <Adafruit_MLX90614.h>

// --- PIN DEFINITIONS ---
const int irPin = 3;             // RPM Sensor Signal Pin
const int LOADCELL_DOUT_PIN = 4; // HX711 Data Pin
const int LOADCELL_SCK_PIN = 5;  // HX711 Clock Pin

// --- SENSOR OBJECTS ---
HX711 scale;
Adafruit_MLX90614 mlx = Adafruit_MLX90614();

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

  // 3. Initialize the IR Temp Sensor
  if (!mlx.begin()) {
    Serial.println("Error connecting to MLX90614. Check wiring!");
    while (1); // Halt if sensor isn't found
  }
  
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
    // readObjectTempC() gets the target temperature. readAmbientTempC() gets the sensor's own board temp.
    float motorTempC = mlx.readObjectTempC();

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

    Serial.print("  |  Motor Temp: ");
    Serial.print(motorTempC, 1); 
    Serial.println(" °C");

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