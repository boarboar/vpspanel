#pragma once

#include <Arduino.h>

#include <atomic>

// Debounces the three dashboard buttons and reports each press exactly once.
//
// Sampling runs in a tiny task of its own instead of in the display task. A
// full-screen SPI refresh blocks the display task for tens of milliseconds, and
// a tap that started and ended inside one of those refreshes used to be lost.
//
// A level change only counts once the pin has stayed at the new level for
// kDebounceMs. That filters contact bounce on both edges, including the bounce
// when a button is released (which used to be reported as a second, phantom
// press). Holding a button down does not repeat.
class ButtonInput {
 public:
  // Logical roles. The physical B and C buttons can trade roles with
  // VPSPANEL_SWAP_B_C (see input_config.h), so kButtonB/kButtonC are roles,
  // not necessarily the B/C pins.
  enum : uint8_t {
    kButtonA = 1 << 0,
    kButtonB = 1 << 1,
    kButtonC = 1 << 2,
  };

  // Configures the pins and starts the sampling task. A button that is already
  // held during begin() must be released before it counts as a press.
  void begin();

  // Cheap, non-blocking; call it often. Returns the presses seen since the
  // previous call, even if several display refreshes happened in between.
  uint8_t poll();

 private:
  struct State {
    bool candidate = false;         // latest sampled level
    bool stable = false;            // debounced level
    uint32_t candidateSinceMs = 0;  // when candidate last changed
  };

  static constexpr uint8_t kButtonCount = 3;
  static constexpr uint32_t kSamplePeriodMs = 2;
  static constexpr uint32_t kDebounceMs = 20;

  // FreeRTOS entry point and the sampler it runs; the sampler is also called
  // from poll() if the task could not be started.
  static void sampleTask(void* arg);
  void sampleLoop();
  void sample();
  static uint8_t roleFor(uint8_t index);

  State buttons_[kButtonCount];
  std::atomic<uint8_t> latched_{0};
  bool initialized_ = false;
  bool taskStarted_ = false;
};
