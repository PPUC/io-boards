#ifndef MULTI_CORE_CROSS_LINK_h
#define MULTI_CORE_CROSS_LINK_h

#if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
#include <pico/util/queue.h>
#endif

#include <Arduino.h>

#include "Event.h"

#ifndef EVENT_STACK_SIZE
#define EVENT_STACK_SIZE 128
#endif

typedef struct QueuedEvent {
  byte sourceId;
  word eventId;
  byte value;
  bool localFast;
} EventItem;

typedef struct QueuedConfigEvent {
  byte boardId;
  byte topic;
  byte index;
  byte key;
  int value;
} ConfigEventItem;

class MultiCoreCrossLink {
#if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
 public:
  MultiCoreCrossLink() {
    if (get_core_num() == 0) {
      queue_init(&_eventQueue[0], sizeof(EventItem), EVENT_STACK_SIZE);
      queue_init(&_eventQueue[1], sizeof(EventItem), EVENT_STACK_SIZE);
      queue_init(&_configEventQueue, sizeof(ConfigEventItem), EVENT_STACK_SIZE);
    }
  }

  ~MultiCoreCrossLink(){};

  void pushEvent(Event *event) {
    QueuedEvent queuedEvent;
    queuedEvent.sourceId = event->sourceId;
    queuedEvent.eventId = event->eventId;
    queuedEvent.value = event->value;
    queuedEvent.localFast = event->localFast;

    queue_add_blocking(&_eventQueue[get_core_num() ^ 1], &queuedEvent);
  }

  Event *popEvent() {
    QueuedEvent queuedEvent;
    queue_remove_blocking(&_eventQueue[get_core_num()], &queuedEvent);

    return new Event(queuedEvent.sourceId, queuedEvent.eventId,
                     queuedEvent.value, queuedEvent.localFast);
  }

  int eventAvailable() { return queue_get_level(&_eventQueue[get_core_num()]); }

  void pushConfigEvent(ConfigEvent *event) {
    if (get_core_num() == 0) {
      QueuedConfigEvent queuedConfigEvent;
      queuedConfigEvent.boardId = event->boardId;
      queuedConfigEvent.topic = event->topic;
      queuedConfigEvent.index = event->index;
      queuedConfigEvent.key = event->key;
      queuedConfigEvent.value = event->value;

      queue_add_blocking(&_configEventQueue, &queuedConfigEvent);
    }
  }

  ConfigEvent *popConfigEvent() {
    QueuedConfigEvent queuedConfigEvent;
    queue_remove_blocking(&_configEventQueue, &queuedConfigEvent);

    return new ConfigEvent(queuedConfigEvent.boardId, queuedConfigEvent.topic,
                           queuedConfigEvent.index, queuedConfigEvent.key,
                           queuedConfigEvent.value);
  }

  int configEventAvailable() {
    return get_core_num() == 1 && queue_get_level(&_configEventQueue);
  }

 private:
  queue_t _eventQueue[2];
  queue_t _configEventQueue;
#endif
};

#endif