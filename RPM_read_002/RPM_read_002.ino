//Test Program to verify is arduino actually capable of reading square wave 
const int irPin = 2;
int lastState = -1;

void setup() {
  Serial.begin(115200);
  pinMode(irPin, INPUT);
  while (!Serial) { ; }
  Serial.println("Polling Test Started...");
}

void loop() {
  int currentState = digitalRead(irPin);
  
  // Only print if the state just changed
  if (currentState != lastState) {
    Serial.print("Pin State: ");
    Serial.println(currentState);
    lastState = currentState;
  }
}