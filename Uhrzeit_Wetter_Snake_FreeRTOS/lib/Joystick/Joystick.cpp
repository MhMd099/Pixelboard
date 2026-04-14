#include "Joystick.h"

Joystick::Joystick(int pinX, int pinY, int pinTaster)
  : Taster_v2(pinTaster), xAchse(pinX), yAchse(pinY) {

  this->tasterPin = pinTaster;

  pinMode(tasterPin, INPUT_PULLUP);

  xAchse.setInputRange(0, 4095);
  yAchse.setInputRange(0, 4095);

  xAchse.setScaleRange(-100, 100);
  yAchse.setScaleRange(-100, 100);

  kalibrieren();
  deadZone = 50;
}

void Joystick::kalibrieren() {
  xMitte = xAchse.readRaw();
  yMitte = yAchse.readRaw();
}

int Joystick::readXRaw() {
  return xAchse.readRaw();
}

int Joystick::readYRaw() {
  return yAchse.readRaw();
}

int Joystick::readXPercent() {
  int raw = readXRaw() - xMitte;
  if (abs(raw) < deadZone) raw = 0;
  return constrain(map(raw, -xMitte, 4095 - xMitte, -100, 100), -100, 100);
}

int Joystick::readYPercent() {
  int raw = readYRaw() - yMitte;
  if (abs(raw) < deadZone) raw = 0;
  return constrain(map(raw, -yMitte, 4095 - yMitte, -100, 100), -100, 100);
}

void Joystick::setXScaleRange(int minVal, int maxVal) {
  xAchse.setScaleRange(minVal, maxVal);
}

void Joystick::setYScaleRange(int minVal, int maxVal) {
  yAchse.setScaleRange(minVal, maxVal);
}

void Joystick::setXInputRange(int minVal, int maxVal) {
  xAchse.setInputRange(minVal, maxVal);
}

void Joystick::setYInputRange(int minVal, int maxVal) {
  yAchse.setInputRange(minVal, maxVal);
}

void Joystick::setInverted(bool invertX, bool invertY) {
  xAchse.setInverted(invertX);
  yAchse.setInverted(invertY);
}

bool Joystick::isPressed() {
  return (digitalRead(tasterPin) == LOW);
}

void Joystick::update() {
  klickenErkennen();
}
