#pragma once

// Minimal stand-in for the Pico SDK's inter-core queue, so that
// MultiCoreCrossLink.h compiles natively. This is an angle-bracket include, so
// it resolves purely through the -I path and never shadows a real header.
//
// The queue is not exercised by the PWM tests: PwmDevices talks to
// EventDispatcher, not to the cross-link. These are stubs, not a simulation.

#include <cstdint>
#include <cstring>

struct queue_t {
  void* data = nullptr;
  uint32_t element_size = 0;
  uint32_t element_count = 0;
  uint32_t level = 0;
};

inline void queue_init(queue_t* q, uint32_t element_size,
                       uint32_t element_count) {
  q->element_size = element_size;
  q->element_count = element_count;
  q->level = 0;
}

inline uint32_t queue_get_level(queue_t* q) { return q->level; }
inline bool queue_try_add(queue_t*, const void*) { return false; }
inline bool queue_try_remove(queue_t*, void*) { return false; }
inline void queue_add_blocking(queue_t*, const void*) {}
inline void queue_remove_blocking(queue_t*, void*) {}
