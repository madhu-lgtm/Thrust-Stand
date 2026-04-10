#include "HX711.h"

const int LOADCELL_DOUT_PIN = 4;
const int LOADCELL_SCK_PIN = 5;

HX711 scale;

// Your new, gram-specific calibration factor!
float calibration_factor = 42.5; 

void setup() {
  Serial.begin(115200);
  Serial.println("Starting Thrust Scale (Grams)...");
  delay(1000);

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  
  // Set the new scale factor
  scale.set_scale(calibration_factor);
  
  // Zero out the scale
  scale.tare(); 
  Serial.println("Scale zeroed. Ready for thrust!");
}

void loop() {
  Serial.print("Thrust: ");
  
  // get_units(5) averages 5 readings for stability. 
  // The ', 0' tells it to print 0 decimal places (whole grams only).
  Serial.print(scale.get_units(5), 0); 
  
  Serial.println(" g");

  // A short delay to keep the Serial Monitor readable
  delay(250); 
}