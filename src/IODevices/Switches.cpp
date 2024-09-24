#include "Switches.h"

void Switches::registerSwitch(byte p, byte n, bool stateful) {
  if (last < (MAX_SWITCHES - 1)) {
    if (stateful) {
      resetStatefulPort(p);
    }

    pinMode(p, INPUT);
    delayMicroseconds(10);
    port[++last] = p;
    number[last] = n;
    toggled[last] = false;
    // Set the inverted value as initial state to let update() send the initial state on game start.
    // Note, we have active LOW!
    state[last] = digitalRead(p);
  }
}

void Switches::resetStatefulPort(byte p) {
  // Set mid power output as input.
  pinMode(p, OUTPUT);
  digitalWrite(p, HIGH);
  delayMicroseconds(10);
  digitalWrite(p, LOW);
  delayMicroseconds(10);
  pinMode(p, INPUT);
}

void Switches::reset() {
  for (uint8_t i = 0; i < MAX_SWITCHES; i++) {
    port[i] = 0;
    number[i] = 0;
    state[i] = 0;
    toggled[i] = false;
    if (stateful[i]) resetStatefulPort(i);
    stateful[i] = false;
  }

  last = -1;
}

void Switches::update() {
  // Wait for SWITCH_DEBOUNCE milliseconds to debounce the switches. That covers
  // the edge case that a switch was hit right before the last polling of
  // events. After SWITCH_DEBOUNCE milliseconds every switch is allowed to
  // toggle once until the events get polled again.
  if (millis() - _ms >= SWITCH_DEBOUNCE) {
    for (int i = 0; i <= last; i++) {
      if (!toggled[i]) {
        // Note, we have active LOW!
        bool new_state = !digitalRead(port[i]);
        if (new_state != state[i]) {
          state[i] = new_state;
          toggled[i] = true;
          // Dispatch all switch events as "local fast".
          // If a PWM output registered to it, we have "fast flip". Useful for
          // flippers, kick backs, jets and sling shots.
          _eventDispatcher->dispatch(new Event(
              EVENT_SOURCE_SWITCH, word(0, number[i]), state[i], true));
          if (stateful[i]) resetStatefulPort(i);
        }
      }
    }
  }
}

void Switches::handleEvent(Event *event) {
  switch (event->sourceId) {
    case EVENT_POLL_EVENTS:
      if (boardId == (byte)event->value) {
        // This I/O board has been polled for events, so all current switch
        // states are transmitted. Reset switch debounce timer and toggles.
        _ms = millis();
        for (int i = 0; i <= last; i++) {
          toggled[i] = false;
        }
      }
      break;

    case EVENT_READ_SWITCHES:
      // The CPU requested all current states.
      for (int i = 0; i <= last; i++) {
        // Send all states of switches that haven't been toggled since last poll
        // (and dispatched their event already).
        if (!toggled[i]) {
          _eventDispatcher->dispatch(
              new Event(EVENT_SOURCE_SWITCH, word(0, number[i]), state[i]));
        } else {
          toggled[i] = false;
        }
      }
      _ms = millis();
      break;
  }
}
