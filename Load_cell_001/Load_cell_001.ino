#include "HX711.h"

// HX711 circuit wiring
const int LOADCELL_DOUT_PIN = 4;
const int LOADCELL_SCK_PIN = 5;

HX711 scale;

// Start with a generic calibration factor
float calibration_factor = 42500; //10000 -> 2355.71260306 -> 12335.71260306 -> 4711.42520612

void setup() {
  Serial.begin(115200);
  Serial.println("HX711 Calibration Sketch");
  Serial.println("REMOVE ALL WEIGHT FROM THE SENSOR...");
  delay(3000);

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  
  // This resets the scale to 0. 
  scale.tare(); 
  Serial.println("Scale is zeroed.");
  Serial.println("Place a known weight on the scale.");
}

void loop() {
  scale.set_scale(calibration_factor); // Adjust to this calibration factor

  Serial.print("Reading: ");
  // Print the average of 5 readings
  Serial.print(scale.get_units(5), 2); 
  Serial.print(" kg"); 
  Serial.print("  |  Calibration Factor: ");
  Serial.print(calibration_factor);
  Serial.println();

  if(Serial.available()) {
    char temp = Serial.read();
    if(temp == '+' || temp == 'a')
      calibration_factor += 100;
    else if(temp == '-' || temp == 'z')
      calibration_factor -= 100;
    else if(temp == '*' || temp == 's')
      calibration_factor += 1000;
    else if(temp == '/' || temp == 'x')
      calibration_factor -= 1000;
  }
}