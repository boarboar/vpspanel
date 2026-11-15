#include "infra/StatusLed.h"

#include "config/led_config.h"

namespace {

constexpr int kLedPin = VPSPANEL_LED_PIN;

// 2 Hz: the LED changes state every 250 ms.
constexpr uint32_t kBlinkHalfPeriodMs = 250;

}  // namespace

void StatusLed::begin() {
  if (kLedPin >= 0) pinMode(kLedPin, OUTPUT);
  apply(false);
}

void StatusLed::setWifiConnecting(bool connecting) {
  wifiConnecting_ = connecting;
}

void StatusLed::setPolling(bool polling) {
  polling_ = polling;
}

void StatusLed::update() {
  const bool active = wifiConnecting_ || polling_;

  if (!active) {
    blinking_ = false;
    if (on_) apply(false);
    return;
  }

  const uint32_t now = millis();
  if (!blinking_) {
    // Light the LED as soon as a connect attempt or a poll starts instead of
    // waiting for the first half period to elapse.
    blinking_ = true;
    lastBlinkMs_ = now;
    apply(true);
    return;
  }

  if ((now - lastBlinkMs_) >= kBlinkHalfPeriodMs) {
    lastBlinkMs_ = now;
    apply(!on_);
  }
}

void StatusLed::apply(bool on) {
  on_ = on;
  if (kLedPin < 0) return;

#if VPSPANEL_LED_ADDRESSABLE
  // The C3 Arduino variant maps the WS2812 to the virtual LED_BUILTIN pin;
  // digitalWrite() turns the whole LED white or off.
  digitalWrite(kLedPin, on ? HIGH : LOW);
#else
  const bool level = VPSPANEL_LED_ACTIVE_HIGH ? on : !on;
  digitalWrite(kLedPin, level ? HIGH : LOW);
#endif
}
