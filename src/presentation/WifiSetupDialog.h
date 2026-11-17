#pragma once

#include <Arduino.h>

#include <atomic>

#include "config/display_config.h"

#if VPSPANEL_HAS_DISPLAY
#include <TFT_eSPI.h>
#endif

// Wi-Fi setup dialog, shown from the boot splash. It lets the user enter an
// SSID and password with a virtual keyboard driven by the three buttons:
//
//   A / B  move the highlight through the keyboard
//   C      press the highlighted key
//
// The keyboard has lowercase, uppercase and symbol pages, plus actions to
// switch fields, insert a space, delete, clear, accept and cancel.
//
// The dialog owns the open flag because the loop and polling tasks read it to
// suspend Wi-Fi, mDNS, the web listener and SSH polling while it is up.
class WifiSetupDialog {
 public:
  enum class Result : uint8_t {
    None,
    Ok,
    Cancel,
  };

  static constexpr size_t kMaxSsid = 32;      // 802.11 SSID limit
  static constexpr size_t kMaxPassword = 63;  // WPA2 passphrase limit

#if VPSPANEL_HAS_DISPLAY
  explicit WifiSetupDialog(TFT_eSPI& tft) : tft_(tft) {}
#else
  WifiSetupDialog() = default;
#endif

  // Opens the dialog with the current credentials prefilled for editing.
  void open(const String& ssid, const String& password);
  void close();

  bool isOpen() const { return open_.load(); }

  // Serviced once per button event. Returns Ok/Cancel when C activates the
  // corresponding action and the caller should leave the dialog; everything
  // else returns None.
  Result handleButtons(uint8_t pressed);

  // Redraws the parts that changed since the previous call. No-op while the
  // dialog is closed.
  void update();

  // The values currently shown in the fields. Valid after open(), including
  // after the dialog closed with OK or Cancel.
#if VPSPANEL_HAS_DISPLAY
  const char* ssid() const { return ssid_; }
  const char* password() const { return password_; }
#endif

  // Copies the credentials confirmed with OK and clears the pending flag.
  // Called by the firmware after the dialog closes to reconnect and persist.
  bool takeAcceptedCredentials(String& ssid, String& password);

 private:
#if VPSPANEL_HAS_DISPLAY
  enum class Field : uint8_t { Ssid, Password };
  enum class Page : uint8_t { Lower, Upper, Symbols };

  static constexpr uint8_t kColumns = 10;
  static constexpr uint8_t kCharRows = 4;
  static constexpr uint8_t kCharKeys = kColumns * kCharRows;
  static constexpr uint8_t kActionKeys = 7;
  static constexpr uint16_t kKeyCount = kCharKeys + kActionKeys;

  enum : uint8_t {
    kActionField = 0,
    kActionPage,
    kActionSpace,
    kActionDelete,
    kActionClear,
    kActionOk,
    kActionCancel,
  };

  void moveSelection(int delta);
  Result activateKey();
  bool accept();
  void switchField();
  void nextPage();
  void appendChar(char c);
  void appendSpace();
  void backspace();
  void clearField();

  char activeChar(uint16_t index) const;
  const char* actionLabel(uint8_t action) const;
  int keyX(uint16_t index) const;
  int keyY(uint16_t index) const;
  int keyWidth(uint16_t index) const;
  int keyHeight(uint16_t index) const;

  void drawFrame();
  void drawHeader();
  void drawFields();
  void drawKeyboard();
  void drawKey(uint16_t index, bool selected);
  void drawField(Field field, const char* label, const char* value, bool active);
  void drawTailFitted(const String& text, int x, int y, int maxWidth, uint8_t font,
                      uint16_t color, uint16_t background);

  TFT_eSPI& tft_;
  char ssid_[kMaxSsid + 1] = {};
  char password_[kMaxPassword + 1] = {};
  char acceptedSsid_[kMaxSsid + 1] = {};
  char acceptedPassword_[kMaxPassword + 1] = {};
  uint8_t ssidLength_ = 0;
  uint8_t passwordLength_ = 0;
  Field field_ = Field::Ssid;
  Page page_ = Page::Lower;
  uint16_t selection_ = 0;
  uint16_t drawnSelection_ = 0;
  Field drawnField_ = Field::Ssid;
  Page drawnPage_ = Page::Lower;
  bool frameDrawn_ = false;
  bool fieldsDirty_ = false;
  const char* notice_ = nullptr;

  SemaphoreHandle_t mutex_ = nullptr;  // guards accepted* between tasks
  bool accepted_ = false;
#endif

  std::atomic<bool> open_{false};
};
