const int irPin = 3; 

// Volatile variables changed inside the interrupt
volatile unsigned long lastPulseTime = 0; 
volatile unsigned long pulsePeriod = 0;   // The exact time between two pulses

// Timer for printing to the Serial Monitor
unsigned long previousPrintMillis = 0;
const unsigned long printInterval = 1000; // Print every 1 second

void setup() {
  Serial.begin(115200); 
  pinMode(irPin, INPUT);
  
  // Trigger on the FALLING edge, just like your successful test
  attachInterrupt(digitalPinToInterrupt(irPin), measurePulse, FALLING);
  
  while (!Serial) { ; }
  Serial.println("Microsecond Precision Tachometer Started...");
}

void loop() {
  unsigned long currentMillis = millis();

  // Print the results once every second
  if (currentMillis - previousPrintMillis >= printInterval) {
    
    // Safely grab the most recent microsecond data from the interrupt
    noInterrupts();
    unsigned long currentPeriod = pulsePeriod;
    unsigned long timeSinceLastPulse = micros() - lastPulseTime;
    interrupts();

    // TIMEOUT CHECK: 
    // If it has been more than 500,000 microseconds (0.5 seconds) since the last 
    // piece of tape was seen, the motor is spinning at less than 120 RPM or has stopped.
    if (timeSinceLastPulse > 500000) {
      Serial.println("Motor stopped or waiting...");
    } 
    // If we have a valid period, calculate the exact RPM
    else if (currentPeriod > 0) {
      
      // Calculate exact RPM using floating-point math
      float exactRPM = 60000000.0 / currentPeriod;
      
      Serial.print("Exact RPM: ");
      Serial.println(exactRPM, 1); // Print with 1 decimal place of precision
    }

    previousPrintMillis = currentMillis;
  }
}

// The Interrupt Function
void measurePulse() {
  unsigned long currentTime = micros();
  unsigned long period = currentTime - lastPulseTime;
  
  // SOFTWARE DEBOUNCE:
  // Still strictly ignoring any noise faster than 2000 microseconds
  if (period > 2000) {
    pulsePeriod = period;       // Save the duration of this rotation
    lastPulseTime = currentTime; // Reset the clock for the next rotation
  }
}