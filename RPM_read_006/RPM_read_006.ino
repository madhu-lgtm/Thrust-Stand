// --- PIN DEFINITIONS ---
const int rpmPin = 3; 

// --- INTERRUPT VARIABLES ---
volatile unsigned long firstPulseTime = 0;
volatile unsigned long lastPulseTime = 0;
volatile unsigned int pulseCount = 0;

// --- TIMING VARIABLES ---
unsigned long previousPrintMillis = 0;
const unsigned long printInterval = 1000; // Update every 1 second

// --- FILTER VARIABLES ---
float smoothedRPM = 0;
const float alpha = 0.15; // Smoothing factor for physical mechanical jitter

void setup() {
  Serial.begin(115200);
  while (!Serial) { ; } // Wait for the Serial Monitor to open

  Serial.println("-------------------------------------------");
  Serial.println("Industrial RPM Sensor Test (Optocoupler)");
  Serial.println("-------------------------------------------");
  
  // INPUT_PULLUP guarantees the pin stays at 5V until the optocoupler triggers it
  pinMode(rpmPin, INPUT_PULLUP);
  
  // Trigger on the FALLING edge (when the optocoupler pulls the pin to GND)
  attachInterrupt(digitalPinToInterrupt(rpmPin), measurePulse, FALLING);
  
  Serial.println("Waiting for motor to spin...");
}

void loop() {
  unsigned long currentMillis = millis();

  // Run the math exactly once per second
  if (currentMillis - previousPrintMillis >= printInterval) {
    
    // 1. Safely copy the data from the interrupt
    noInterrupts();
    unsigned int currentPulses = pulseCount;
    unsigned long startT = firstPulseTime;
    unsigned long endT = lastPulseTime;
    pulseCount = 0; // Reset counter for the next window
    interrupts();

    float rawRPM = 0;

    // 2. We need at least 2 pulses to measure a time span
    if (currentPulses > 1) {
      
      unsigned long timeSpan = endT - startT;
      
      // Exact RPM formula: ((Revolutions) * 60,000,000) / Total Microseconds
      rawRPM = ((currentPulses - 1) * 60000000.0) / timeSpan;
      
      // 3. Apply the DAQ Filter
      if (smoothedRPM == 0) {
        smoothedRPM = rawRPM; // Instantly lock onto the first valid reading
      } else {
        smoothedRPM = (alpha * rawRPM) + ((1.0 - alpha) * smoothedRPM);
      }
      
      // 4. Print the results
      Serial.print("Raw RPM: ");
      Serial.print(rawRPM, 1); 
      Serial.print("  |  Filtered RPM: ");
      Serial.println(smoothedRPM, 1);
      
    } else {
      smoothedRPM = 0; // Reset the filter when the motor stops
      Serial.println("Motor stopped or waiting...");
    }

    previousPrintMillis = currentMillis;
  }
}

// --- THE INTERRUPT FUNCTION ---
void measurePulse() {
  unsigned long currentTime = micros();
  
  // SOFTWARE DEBOUNCE: 5,000 microseconds (5ms)
  // This ignores optical bounce but is fast enough to read up to 12,000 RPM.
  if (currentTime - lastPulseTime > 5000) {
    
    if (pulseCount == 0) {
      firstPulseTime = currentTime;
    }
    
    lastPulseTime = currentTime; 
    pulseCount++;                
  }
}