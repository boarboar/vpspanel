#pragma once

#include <Arduino.h>

// Debounces the three dashboard buttons and reports each press exactly once.
//
// poll() returns a bitmask of the buttons that produced a press since the
// previous call. The first sampled edge is reported immediately; further
// edges are ignored for kPressLockoutMs so contact bounce cannot create
// duplicate presses. Holding a button down does not repeat.
class ButtonInput {
 public:
  enum : uint8_t {
    kButtonA = 1 << 0,
    kButtonB = 1 << 1,
    kButtonC = 1 << 2,
  };

  // Configures the pins. Presses already active during begin() are treated as
  // the initial state and do not generate an event.
  void begin();

  // Cheap, non-blocking; call it often (the display task does this every
  // 10 ms, so even a very short tap is sampled).
  uint8_t poll();

 private:
  struct State {
    bool raw = false;       // latest sampled level
    bool armed = true;      // a new press may be reported
    uint32_t lastEventMs = 0;
  };

  static constexpr uint8_t kButtonCount = 3;
  static constexpr uint32_t kPressLockoutMs = 30;

  State buttons_[kButtonCount];
  bool initialized_ = false;
};
