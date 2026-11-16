#include "infra/ButtonInput.h"

#include "config/input_config.h"

namespace {

int pinFor(uint8_t index) {
  switch (index) {
    case 0:
      return VPSPANEL_BUTTON_A_PIN;
    case 1:
      return VPSPANEL_BUTTON_B_PIN;
    default:
      return VPSPANEL_BUTTON_C_PIN;
  }
}

bool readPressed(int pin) {
  if (pin < 0) return false;
#if VPSPANEL_BUTTON_ACTIVE_LOW
  return digitalRead(pin) == LOW;
#else
  return digitalRead(pin) == HIGH;
#endif
}

}  // namespace

void ButtonInput::begin() {
  const uint32_t now = millis();

  for (uint8_t i = 0; i < kButtonCount; ++i) {
    const int pin = pinFor(i);
    if (pin >= 0) {
#if VPSPANEL_BUTTON_ACTIVE_LOW
      pinMode(pin, INPUT_PULLUP);
#else
      pinMode(pin, INPUT_PULLDOWN);
#endif
    }

    State& state = buttons_[i];
    state.raw = readPressed(pin);
    // A button that is already held at boot must be released before it can
    // count as a press.
    state.armed = !state.raw;
    state.lastEventMs = now;
  }

  initialized_ = true;
}

uint8_t ButtonInput::poll() {
  if (!initialized_) begin();

  const uint32_t now = millis();
  uint8_t pressed = 0;

  for (uint8_t i = 0; i < kButtonCount; ++i) {
    const int pin = pinFor(i);
    if (pin < 0) continue;

    State& state = buttons_[i];
    const bool raw = readPressed(pin);
    if (raw == state.raw) continue;

    state.raw = raw;
    if (raw) {
      // Report the press on the first sampled low level, then lock out contact
      // bounce. Waiting for a stable level used to lose very short taps.
      if (state.armed) {
        state.armed = false;
        if ((now - state.lastEventMs) >= kPressLockoutMs) {
          state.lastEventMs = now;
          pressed |= static_cast<uint8_t>(1u << i);
        }
      }
    } else {
      // Re-arm only after a sampled release; bounce during the release is
      // harmless because a new event also needs the lockout to expire.
      state.armed = true;
    }
  }

  return pressed;
}
