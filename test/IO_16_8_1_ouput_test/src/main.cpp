// Markus Kalkbrenner 2023

#include <Arduino.h>

bool state = 0;

void setup() {
  pinMode(19, OUTPUT);
  pinMode(20, OUTPUT);
  pinMode(21, OUTPUT);
  pinMode(22, OUTPUT);
  pinMode(23, OUTPUT);
  pinMode(24, OUTPUT);
  pinMode(26, OUTPUT);
  pinMode(27, OUTPUT);
}

void loop() {
  digitalWrite(19, state);
  digitalWrite(20, state);
  digitalWrite(21, state);
  digitalWrite(22, state);
  digitalWrite(23, state);
  digitalWrite(24, state);
  digitalWrite(26, state);
  digitalWrite(27, state);

  state = !state;

  delay(1000);
}
