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
  if (initialized_) return;

  const uint32_t now = millis();

  for (uint8_t i = 0; i < kButtonCount; ++i) {
    State& state = buttons_[i];
    const int pin = pinFor(i);
    if (pin < 0) {
      state.candidate = state.stable = false;
      state.candidateSinceMs = now;
      continue;
    }

#if VPSPANEL_BUTTON_ACTIVE_LOW
    pinMode(pin, INPUT_PULLUP);
#else
    pinMode(pin, INPUT_PULLDOWN);
#endif

    // A button that is already held during begin() is the initial state, not a
    // press. It has to be released and pressed again to generate an event.
    state.candidate = state.stable = readPressed(pin);
    state.candidateSinceMs = now;
  }

  initialized_ = true;
  taskStarted_ =
      xTaskCreate(sampleTask, "buttons", 2048, this, 2, nullptr) == pdPASS;
  if (!taskStarted_) {
    Serial.println(F("buttons: could not start sampling task"));
  }
}

void ButtonInput::sampleTask(void* arg) {
  static_cast<ButtonInput*>(arg)->sampleLoop();
}

void ButtonInput::sampleLoop() {
  for (;;) {
    sample();
    vTaskDelay(pdMS_TO_TICKS(kSamplePeriodMs));
  }
}

uint8_t ButtonInput::roleFor(uint8_t index) {
#if VPSPANEL_SWAP_B_C
  // The C button (GPIO 2) is unreliable, so the reliable GPIO 1 button takes
  // over the C role and GPIO 2 takes over the B role. Pin order is A, B, C.
  switch (index) {
    case 1:
      return kButtonC;
    case 2:
      return kButtonB;
    default:
      return kButtonA;
  }
#else
  return static_cast<uint8_t>(1u << index);
#endif
}

void ButtonInput::sample() {
  const uint32_t now = millis();
  uint8_t pressed = 0;
  uint8_t stableRoles = 0;

  for (uint8_t i = 0; i < kButtonCount; ++i) {
    const int pin = pinFor(i);
    if (pin < 0) continue;

    State& state = buttons_[i];
    const bool level = readPressed(pin);

    if (level != state.candidate) {
      // The pin moved; restart the stability timer. Bounce keeps resetting it,
      // so only a level that survives kDebounceMs is accepted.
      state.candidate = level;
      state.candidateSinceMs = now;
      continue;
    }

    if (state.candidate != state.stable &&
        (now - state.candidateSinceMs) >= kDebounceMs) {
      state.stable = state.candidate;
      if (state.stable) {
        pressed |= roleFor(i);
      }
    }

    if (state.stable) stableRoles |= roleFor(i);
  }

  // The physical A and B buttons held together are a single chord event, not
  // two normal presses. The roles they map to depend on the B/C swap, so derive
  // the mask from the physical pin order instead of assuming logical A+B.
  const uint8_t chordRoles = roleFor(0) | roleFor(1);
  if ((stableRoles & chordRoles) == chordRoles) {
    if (!chordActive_) {
      chordActive_ = true;
      pressed |= kChordAB;
    }
    pressed &= ~chordRoles;
  } else {
    chordActive_ = false;
  }

  if (pressed != 0) {
    latched_.fetch_or(pressed, std::memory_order_relaxed);
  }
}

uint8_t ButtonInput::poll() {
  if (!initialized_) begin();

  // Fallback for the (unlikely) case where the sampling task could not be
  // created: keep the buttons usable at the display task's polling rate.
  if (!taskStarted_) sample();

  return latched_.exchange(0, std::memory_order_relaxed);
}
