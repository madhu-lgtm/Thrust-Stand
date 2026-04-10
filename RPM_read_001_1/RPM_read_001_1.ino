// Define the pin connected to the IR Sensor
const int irPin = 3; 

// 'volatile' tells the Arduino this variable changes outside the main loop (in the interrupt)
volatile unsigned long pulseCount = 0; 

// Variables for timing
unsigned long previousMillis = 0;
const int updateInterval = 1000; // Update RPM every 1000 milliseconds (1 second)

// Variable to store the calculated RPM
unsigned long rpm = 0; 

void setup() {
  // Start the serial monitor at a fast baud rate
  Serial.begin(115200); 
  
  pinMode(irPin, INPUT);

  // Attach the interrupt to Pin 2. 
  // It triggers the 'countPulse' function every time the signal goes from LOW to HIGH.
  attachInterrupt(digitalPinToInterrupt(irPin), countPulse, FALLING);//RISING -> FALLING
  
  // Wait for serial port to connect (needed for R4 native USB)
  while (!Serial) {
    ; 
  }
  Serial.println("RPM Sensor Initialized. Waiting for motor...");
}

void loop() {
  unsigned long currentMillis = millis();

  // Every 1 second, calculate and print the RPM
  if (currentMillis - previousMillis >= updateInterval) {
    
    // 1. Pause interrupts temporarily so the data doesn't change while we read it
    noInterrupts();
    unsigned long currentPulses = pulseCount;
    pulseCount = 0; // Reset the counter for the next second
    interrupts(); // Turn interrupts back on

    // 2. Calculate RPM
    // Since we count pulses for 1 second, multiply by 60 to get pulses per minute.
    // 1 pulse = 1 revolution.
    rpm = currentPulses * 60;

    // 3. Print to Serial Monitor
    Serial.print("Motor RPM: ");
    Serial.println(rpm);

    // 4. Reset the timer
    previousMillis = currentMillis;
  }
}

// The Interrupt Service Routine (ISR)
// This function runs automatically every time the sensor sees the white tape.
// It must be kept as short and fast as possible!
void countPulse() {
  pulseCount++;
}
