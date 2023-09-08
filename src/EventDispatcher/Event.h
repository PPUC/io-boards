/*
  Event.h
  Created by Markus Kalkbrenner, 2021-2023.

  Play more pinball!
*/

#ifndef EVENT_h
#define EVENT_h

#define EVENT_SOURCE_ANY 42      // "*"
#define EVENT_SOURCE_DEBUG 66    // "B" Debug
#define EVENT_CONFIGURATION 67   // "C" Configure I/O
#define EVENT_SOURCE_DMD 68      // "D" VPX/DOF/PUP
#define EVENT_SOURCE_EVENT 69    // "E" VPX/DOF/PUP common event from different system, like
#define EVENT_SOURCE_EFFECT 70   // "F" custom event from running Effect
#define EVENT_SOURCE_GI 71       // "G" WPC GI
#define EVENT_SOURCE_LIGHT 76    // "L" VPX/DOF/PUP lights, mainly playfield inserts
#define EVENT_NULL 78            // "N" NULL event
#define EVENT_SOURCE_SOUND 79    // "O" sound command
#define EVENT_POLL_EVENTS 80     // "P" Poll events command, mainly read switches
#define EVENT_READ_SWITCHES 82   // "R" Read current state of all switches on i/o boards
#define EVENT_SOURCE_SOLENOID 83 // "S" VPX/DOF/PUP includes flashers
#define EVENT_SOURCE_SWITCH 87   // "W" VPX/DOF/PUP
#define EVENT_PING 88            // "X"
#define EVENT_PONG 89            // "Y"
#define EVENT_RESET 90           // "Z"
#define EVENT_NO_ERROR 98        // NO ERROR
#define EVENT_ERROR 99           // ERROR

#define CONFIG_TOPIC_PLATFORM 102      // "f"
#define CONFIG_TOPIC_LED_STRING 103    // "g"
#define CONFIG_TOPIC_LAMPS 108         // "l"
#define CONFIG_TOPIC_MECHS 109         // "m"
#define CONFIG_TOPIC_PWM 112           // "p"
#define CONFIG_TOPIC_SWITCHES 115      // "s"
#define CONFIG_TOPIC_SWITCH_MATRIX 120 // "x"

#define CONFIG_TOPIC_HOLD_POWER_ACTIVATION_TIME 65 // "A"
#define CONFIG_TOPIC_BRIGHTNESS 66                 // "B"
#define CONFIG_TOPIC_COLOR 67                      // "C"
#define CONFIG_TOPIC_FAST_SWITCH 70                // "F"
#define CONFIG_TOPIC_AFTER_GLOW 71                 // "G"
#define CONFIG_TOPIC_HOLD_POWER 72                 // "H"
#define CONFIG_TOPIC_LED_NUMBER 76                 // "L"
#define CONFIG_TOPIC_MAX_PULSE_TIME 77             // "M"
#define CONFIG_TOPIC_NUMBER 78                     // "N"
#define CONFIG_TOPIC_AMOUNT_LEDS 79                // "O"
#define CONFIG_TOPIC_PORT 80                       // "P"
#define CONFIG_TOPIC_MAX_PULSE_TIME 77             // "M"
#define CONFIG_TOPIC_MIN_PULSE_TIME 84             // "T"
#define CONFIG_TOPIC_LIGHT_UP 85                   // "U"
#define CONFIG_TOPIC_ACTIVE_LOW 86                 // "V"
#define CONFIG_TOPIC_POWER 87                      // "W"
#define CONFIG_TOPIC_TYPE 89                       // "Y"
#define CONFIG_TOPIC_NULL 99                       // NULL

#define PWM_TYPE_SOLENOID 1 // Coil
#define PWM_TYPE_FLASHER 2  // Flasher
#define PWM_TYPE_LAMP 3     // Lamp

#define LED_TYPE_GI 1      // GI
#define LED_TYPE_FLASHER 2 // Flasher
#define LED_TYPE_LAMP 3    // Lamp

#define MATRIX_TYPE_COLUMN 1 // Column
#define MATRIX_TYPE_ROW 2    // Row

struct Event {
    byte sourceId;
    word eventId;
    byte value;
    bool localFast;

    Event(byte sId)
    {
        sourceId = sId;
        eventId = 1;
        value = 1;
        localFast = false;
    }

    Event(byte sId, word eId) {
        sourceId = sId;
        eventId = eId;
        value = 1;
        localFast = false;
    }

    Event(byte sId, word eId, byte v) {
        sourceId = sId;
        eventId = eId;
        value = v;
        localFast = false;
    }

    Event(byte sId, word eId, byte v, bool lf) {
        sourceId = sId;
        eventId = eId;
        value = v;
        localFast = lf;
    }

    // Clone the event.
    Event(const Event* other) {
        sourceId = other->sourceId;
        eventId = other->eventId;
        value = other->value;
        localFast = other->localFast;
    }

    bool operator==(const Event &other) const {
        return this->sourceId == other.sourceId
            && this->eventId == other.eventId
            && this->value == other.value;
    }

    bool operator!=(const Event &other) const {
        return !(*this == other);
    }
};

struct ConfigEvent {
    byte sourceId = EVENT_CONFIGURATION;
    byte boardId;  //
    byte topic;    // lamps
    byte index;    // 0, index of assignment
    byte key;      // ledType, assignment/brightness
    int value;     // FFFF00FF

    ConfigEvent(char b, char t, char i, char k, int v) {
        boardId = b;
        topic = t;
        index = i;
        key = k;
        value = v;
    }

    // Clone the event.
    ConfigEvent(const ConfigEvent* other) {
        boardId = other->boardId;
        topic = other->topic;
        index = other->index;
        key = other->key;
        value = other->value;
    }
};

#endif
