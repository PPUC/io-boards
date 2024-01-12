/*
  InputController.h
  Created by Markus Kalkbrenner.
*/

#ifndef INPUTCONTROLLER_h
#define INPUTCONTROLLER_h

#include "EventDispatcher/Event.h"
#include "EventDispatcher/EventDispatcher.h"
#include "InputDevices/InputControllerTestButtons.h"
#include "InputDevices/LightMatrix.h"
#include "InputDevices/PIN2DMD.h"
#include "InputDevices/Solenoids.h"
#include "InputDevices/SwitchMatrix.h"
#include "PPUC.h"
#include "VisualPinball/PUPComLink.h"

class InputController {
 public:
  InputController(int controllerType, byte platform);

  InputController(int controllerType, byte platform,
                  EventDispatcher* eventDispatcher);

  Solenoids* solenoids();

  SwitchMatrix* switchMatrix();

  LightMatrix* lightMatrix();

  PIN2DMD* pin2Dmd();

  PUPComLink* pupComLink();

  InputControllerTestButtons* testButtons();

  EventDispatcher* eventDispatcher();

  byte platform;

 private:
  Solenoids* _solenoids;
  SwitchMatrix* _switchMatrix;
  LightMatrix* _lightMatrix;
  PIN2DMD* _pin2Dmd;
  PUPComLink* _pupComLink;
  InputControllerTestButtons* _testButtons;
  EventDispatcher* _eventDispatcher;
};

#endif
