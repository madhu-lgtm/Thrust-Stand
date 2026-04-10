// --- PIN DEFINITIONS ---
const int irPin = 3; 

// --- INTERRUPT VARIABLES ---
// 'volatile' is required for variables modified inside an interrupt function
volatile unsigned long firstPulseTime = 0;
volatile unsigned long lastPulseTime = 0;
volatile unsigned int pulseCount = 0;

// --- TIMING VARIABLES ---
unsigned long previousPrintMillis = 0;
const unsigned long printInterval = 1000; // Update the Serial Monitor every 1 second

// --- DAQ FILTER VARIABLES ---
float smoothedRPM = 0;
// Alpha controls the smoothing strength. 
// 0.15 is heavy filtering (perfect for the KY-032's jitter).
// If it reacts too slowly to throttle changes, increase this to 0.3 or 0.4.
const float alpha = 0.15; 

void setup() {
  Serial.begin(115200); 
  pinMode(irPin, INPUT);
  
  // Trigger on the FALLING edge (when the sensor sees the white tape)
  attachInterrupt(digitalPinToInterrupt(irPin), measurePulse, FALLING);
  
  // Wait for the native USB serial port to connect
  while (!Serial) { ; }
  Serial.println("Ultimate DAQ Filtered RPM Started...");
  Serial.println("Waiting for motor...");
}

void loop() {
  unsigned long currentMillis = millis();

  // Every 1 second, calculate and print the RPM
  if (currentMillis - previousPrintMillis >= printInterval) {
    
    // 1. Safely copy the data from the interrupt
    noInterrupts();
    unsigned int currentPulses = pulseCount;
    unsigned long startT = firstPulseTime;
    unsigned long endT = lastPulseTime;
    pulseCount = 0; // Reset counter for the next 1-second window
    interrupts();

    // 2. We need at least 2 pulses to measure a time span
    if (currentPulses > 1) {
      
      // Calculate the exact time span between the first and last pulse
      unsigned long timeSpan = endT - startT;
      
      // Calculate raw exact RPM: ((Revolutions) * 60,000,000) / Total Microseconds
      float exactRPM = ((currentPulses - 1) * 60000000.0) / timeSpan;
      
      // 3. Apply the Exponential Moving Average (EMA) Filter
      if (smoothedRPM == 0) {
        smoothedRPM = exactRPM; // Instantly lock onto the first valid reading
      } else {
        // Blend the jumpy hardware reading with the smoothed history
        smoothedRPM = (alpha * exactRPM) + ((1.0 - alpha) * smoothedRPM);
      }
      
      // 4. Print the results
      Serial.print("Raw: ");
      Serial.print(exactRPM, 1); 
      Serial.print("  |  Filtered Stable RPM: ");
      Serial.println(smoothedRPM, 1); 
      
    } else {
      // If the motor stops, reset the filter so it doesn't get stuck on an old value
      smoothedRPM = 0; 
      Serial.println("Motor stopped or waiting...");
    }

    // Reset the 1-second timer
    previousPrintMillis = currentMillis;
  }
}

// --- THE INTERRUPT FUNCTION ---
// This runs automatically every time the sensor sees the white tape
void measurePulse() {
  unsigned long currentTime = micros();
  
  // SOFTWARE DEBOUNCE: 5,000 microseconds (5ms)
  // This ignores optical bounce but is fast enough to read up to 12,000 RPM.
  if (currentTime - lastPulseTime > 5000) {
    
    // If this is the very first pulse of the new 1-second window, record its start time
    if (pulseCount == 0) {
      firstPulseTime = currentTime;
    }
    
    lastPulseTime = currentTime; // Always update the time of the last seen pulse
    pulseCount++;                // Count the rotation
  }
}