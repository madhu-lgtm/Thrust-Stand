//Added RPM FILTER
const int irPin = 3; 

volatile unsigned long pulseCount = 0; 
volatile unsigned long lastInterruptTime = 0; // Used to filter out noise

unsigned long previousMillis = 0;

// Set your fixed reading time here! (e.g., 3000 = 3 seconds)
const unsigned long sampleWindow = 1000; //3000 -> 1000

void setup() {
  Serial.begin(115200); 
  pinMode(irPin, INPUT);

  // Using FALLING on Pin 3, just as you successfully tested
  attachInterrupt(digitalPinToInterrupt(irPin), countPulse, FALLING);
  
  while (!Serial) { ; }
  Serial.println("RPM Averaging Started. Waiting for motor...");
}

void loop() {
  unsigned long currentMillis = millis();

  // Wait until the fixed sample window has passed
  if (currentMillis - previousMillis >= sampleWindow) {
    
    noInterrupts(); // Pause interrupts to safely read the count
    unsigned long currentPulses = pulseCount;
    pulseCount = 0; // Reset counter for the next window
    interrupts();   // Resume interrupts

    // Calculate Average RPM
    // Formula: (Total Pulses / Seconds Sampled) * 60 seconds
    unsigned long rpm = currentPulses * (60000 / sampleWindow);

    // Print the formatted results
    if (rpm > 0) {
      Serial.print("Average RPM (over ");
      Serial.print(sampleWindow / 1000);
      Serial.print(" sec): ");
      Serial.println(rpm);
    } else {
      Serial.println("Motor stopped or waiting...");
    }

    previousMillis = currentMillis;
  }
}

// The Interrupt Function
void countPulse() {
  unsigned long interruptTime = micros();
  
  // SOFTWARE DEBOUNCE:
  // Only count a pulse if it has been at least 2000 microseconds (2ms) since the last one.
  // This ignores the rapid "bouncing" noise at the edge of the tape.
  // (2ms allows for reading up to 30,000 RPM, which is plenty for your X6 motor).
  if (interruptTime - lastInterruptTime > 2000) {
    pulseCount++;
    lastInterruptTime = interruptTime;
  }
}