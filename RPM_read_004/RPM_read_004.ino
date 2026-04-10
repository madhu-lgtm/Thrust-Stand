const int irPin = 3; 

// Variables to track the span of time
volatile unsigned long firstPulseTime = 0;
volatile unsigned long lastPulseTime = 0;
volatile unsigned int pulseCount = 0;

unsigned long previousPrintMillis = 0;
const unsigned long printInterval = 1000; 

void setup() {
  Serial.begin(115200); 
  pinMode(irPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(irPin), measurePulse, FALLING);
  
  while (!Serial) { ; }
  Serial.println("Ultimate Precision RPM Started...");
}

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - previousPrintMillis >= printInterval) {
    
    // Pause interrupts to safely copy the data
    noInterrupts();
    unsigned int currentPulses = pulseCount;
    unsigned long startT = firstPulseTime;
    unsigned long endT = lastPulseTime;
    pulseCount = 0; // Reset for the next 1-second window
    interrupts();

    // We need at least 2 pulses to measure a time span
    if (currentPulses > 1) {
      
      // Calculate the exact time span between the first and last pulse
      unsigned long timeSpan = endT - startT;
      
      // Formula: ((Total Revolutions) * 60,000,000) / Total Microseconds
      // Total revolutions is (currentPulses - 1)
      float exactRPM = ((currentPulses - 1) * 60000000.0) / timeSpan;
      
      Serial.print("Stable RPM: ");
      Serial.println(exactRPM, 1); 
      
    } else {
      Serial.println("Motor stopped or waiting...");
    }

    previousPrintMillis = currentMillis;
  }
}

// The Interrupt Function
void measurePulse() {
  unsigned long currentTime = micros();
  
  // MASSIVE DEBOUNCE: 20,000 microseconds (20ms)
  // This physically prevents the sensor from reading the "wrong" edge of your tape.
  if (currentTime - lastPulseTime > 5000) {  // 5000us allows up to 12,000 RPM
    
    // If this is the first pulse of the new window, record its start time
    if (pulseCount == 0) {
      firstPulseTime = currentTime;
    }
    
    lastPulseTime = currentTime;
    pulseCount++;
  }
}