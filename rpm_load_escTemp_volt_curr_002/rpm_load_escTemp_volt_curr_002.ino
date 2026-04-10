#include "HX711.h"
#include <OneWire.h>
#include <DallasTemperature.h>

// ==========================================
// --- USER SETTINGS: MASTER TOGGLES ---
// ==========================================
// Change to 'true' to print Raw RPM. Change to 'false' to print Filtered RPM.
const bool printRawRPM = false; 

// ==========================================
// --- MAUCH SENSOR CALIBRATION ---
// ⚠️ Update these to match the numbers on your specific Mauch paper card!
// ==========================================
const float VOLTAGE_MULTIPLIER = 19.02; //21.0
const float CURRENT_MULTIPLIER = 60.6; 

// ==========================================
// --- PIN DEFINITIONS ---
// ==========================================
const int rpmPin = 3;            // RPM Sensor (Optocoupler OUT)
const int LOADCELL_DOUT_PIN = 4; // HX711 Data Pin
const int LOADCELL_SCK_PIN = 5;  // HX711 Clock Pin
const int ONE_WIRE_BUS = 6;      // DS18B20 Data Pin
const int voltagePin = A1;       // Mauch PL Sensor Voltage (V_OUT)
const int currentPin = A0;       // Mauch PL Sensor Current (I_OUT)

// --- SENSOR OBJECTS ---
HX711 scale;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature tempSensor(&oneWire);

// --- THRUST & FILTER VARIABLES ---
float calibration_factor = 42.5; // Your custom grams calibration
const float alpha = 0.15;        // RPM Smoothing factor
float smoothedRPM = 0;

// --- RPM INTERRUPT VARIABLES ---
volatile unsigned long firstPulseTime = 0;
volatile unsigned long lastPulseTime = 0;
volatile unsigned int pulseCount = 0;

// --- TIMING VARIABLES ---
unsigned long previousPrintMillis = 0;
const unsigned long printInterval = 1000; // Update Serial Monitor every 1 second

void setup() {
  Serial.begin(115200); 
  while (!Serial) { ; } // Wait for the Serial Monitor to open
  
  Serial.println("INITIALIZING MASTER DAQ...");
  
  // ==========================================
  // --- ADVANCED ADC SETUP (UNO R4 WIFI) ---
  // ==========================================
  // 1. Tell the Arduino to use the physical AREF pin (locked to 3.3V via your jumper)
  analogReference(AR_EXTERNAL);
  
  // 2. Enable 14-bit ADC resolution (16,384 steps of extreme precision!)
  analogReadResolution(14);
  
  // ==========================================
  // --- SENSOR INITIALIZATION ---
  // ==========================================
  Serial.println("Zeroing scale... Please keep the bench still.");
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibration_factor);
  scale.tare(); 
  
  pinMode(rpmPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(rpmPin), measurePulse, FALLING);

  tempSensor.begin();
  
  Serial.println("Ready for Motor Test!");
  Serial.println("-------------------------------------------------------------------------------------------------");
}

void loop() {
  unsigned long currentMillis = millis();

  // Run this data gathering block exactly once per second
  if (currentMillis - previousPrintMillis >= printInterval) {
    
    // ==========================================
    // 1. GATHER RPM DATA
    // ==========================================
    noInterrupts(); // Pause interrupts safely
    unsigned int currentPulses = pulseCount;
    unsigned long startT = firstPulseTime;
    unsigned long endT = lastPulseTime;
    pulseCount = 0; // Reset for the next 1-second window
    interrupts();   // Resume interrupts

    float rawRPM = 0; 

    // We need at least 2 pulses to calculate a time span
    if (currentPulses > 1) {
      unsigned long timeSpan = endT - startT;
      rawRPM = ((currentPulses - 1) * 60000000.0) / timeSpan;
      
      // Apply the Exponential Moving Average DAQ Filter
      if (smoothedRPM == 0) {
        smoothedRPM = rawRPM; 
      } else {
        smoothedRPM = (alpha * rawRPM) + ((1.0 - alpha) * smoothedRPM);
      }
    } else {
      rawRPM = 0; 
      smoothedRPM = 0; // Motor is stopped
    }

    // ==========================================
    // 2. GATHER THRUST DATA
    // ==========================================
    // Average 3 readings to prevent blocking the RPM interrupt
    float currentThrust = scale.get_units(3); 
    if (currentThrust < 0) currentThrust = 0; // Prevent floating negative zeros

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
    
    // The Arduino is now using your 3.3V jumper wire as the perfect mathematical ruler!
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

// ==========================================
// --- THE RPM INTERRUPT FUNCTION ---
// ==========================================
void measurePulse() {
  unsigned long currentTime = micros();
  
  // 5,000 microsecond debounce (Ignores optical bounce, allows up to 12,000 RPM)
  if (currentTime - lastPulseTime > 5000) {
    if (pulseCount == 0) {
      firstPulseTime = currentTime; 
    }
    lastPulseTime = currentTime;    
    pulseCount++;
  }
}