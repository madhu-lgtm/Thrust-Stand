const int irPin = 3; 

volatile unsigned long pulseCount = 0; 
volatile unsigned long lastInterruptTime = 0; 

unsigned long previousMillis = 0;
const unsigned long sampleWindow = 1000; // Strictly 1 second

// --- FILTER VARIABLES ---
float smoothedRPM = 0;
// 'alpha' determines how much filtering is applied.
// 1.0 = No filtering (raw data). 
// 0.1 = Very heavy filtering (very smooth, but slow to react to throttle changes).
// 0.3 is a great starting point for motor RPM.
const float alpha = 0.3; 

void setup() {
  Serial.begin(115200); 
  pinMode(irPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(irPin), countPulse, FALLING);
  
  while (!Serial) { ; }
  Serial.println("1-Second EMA Filter Started...");
}

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= sampleWindow) {
    
    noInterrupts(); 
    unsigned long currentPulses = pulseCount;
    pulseCount = 0; 
    interrupts();   

    // 1. Calculate the raw, jumpy RPM
    unsigned long rawRPM = currentPulses * 60;

    // 2. Apply the EMA Filter
    if (rawRPM > 0) {
      if (smoothedRPM == 0) {
        smoothedRPM = rawRPM; // Lock onto the first reading instantly
      } else {
        // Blend the new raw reading with the historical smoothed reading
        smoothedRPM = (alpha * rawRPM) + ((1.0 - alpha) * smoothedRPM);
      }
      
      Serial.print("Raw: ");
      Serial.print(rawRPM);
      Serial.print("  |  Smoothed RPM: ");
      Serial.println((int)smoothedRPM); // Print as a clean whole number
      
    } else {
      smoothedRPM = 0; // Reset filter if motor stops completely
      Serial.println("Motor stopped or waiting...");
    }

    previousMillis = currentMillis;
  }
}

void countPulse() {
  unsigned long interruptTime = micros();
  // Keep the 2ms debounce to reject false optical noise
  if (interruptTime - lastInterruptTime > 2000) {
    pulseCount++;
    lastInterruptTime = interruptTime;
  }
}