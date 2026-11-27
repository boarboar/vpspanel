#include "presentation/WifiSetupDialog.h"

#if VPSPANEL_HAS_DISPLAY

#include <cstring>

#include "infra/ButtonInput.h"
#include "presentation/TftTheme.h"

namespace {

// Dialog layout. The header, two credential rows and five keyboard rows fill
// the 320x240 panel.
constexpr int kSetupHeaderH = 28;

constexpr int kFieldX = 6;
constexpr int kFieldW = 308;
constexpr int kFieldH = 24;
constexpr int kFieldSsidY = 30;
constexpr int kFieldPasswordY = 57;
constexpr int kFieldLabelX = 8;
constexpr int kFieldValueX = 54;

constexpr int kKeyX0 = 6;
constexpr int kKeyRowY0 = 86;
constexpr int kKeyW = 29;
constexpr int kKeyH = 24;
constexpr int kKeyColStep = 31;
constexpr int kKeyRowStep = 28;

constexpr int kActionY = 200;
constexpr int kActionW = 42;
constexpr int kActionH = 26;
constexpr int kActionStep = 44;

// Four rows of ten characters per page. Keeping the page size fixed means the
// selection index stays valid when the page changes.
constexpr char kLowerKeys[] = "1234567890qwertyuiopasdfghjkl-zxcvbnm._@";
constexpr char kUpperKeys[] = "1234567890QWERTYUIOPASDFGHJKL-ZXCVBNM._@";
constexpr char kSymbolKeys[] = "!@#$%^&*()-_=+[]{}|\\;:'\",.<>/?`~+,-./:;<";

static_assert(sizeof(kLowerKeys) - 1 == 40, "Keyboard page must have 40 keys");
static_assert(sizeof(kUpperKeys) - 1 == 40, "Keyboard page must have 40 keys");
static_assert(sizeof(kSymbolKeys) - 1 == 40, "Keyboard page must have 40 keys");

void copyInto(char* destination, uint8_t& length, const String& source, size_t maxLength) {
  length = static_cast<uint8_t>(source.length() > maxLength ? maxLength : source.length());
  memcpy(destination, source.c_str(), length);
  destination[length] = '\0';
}

}  // namespace

void WifiSetupDialog::open(const String& ssid, const String& password) {
  if (mutex_ == nullptr) mutex_ = xSemaphoreCreateMutex();

  copyInto(ssid_, ssidLength_, ssid, kMaxSsid);
  copyInto(password_, passwordLength_, password, kMaxPassword);

  field_ = Field::Ssid;
  page_ = Page::Lower;
  selection_ = 0;
  drawnSelection_ = 0;
  drawnField_ = Field::Ssid;
  drawnPage_ = Page::Lower;
  frameDrawn_ = false;
  fieldsDirty_ = false;
  notice_ = nullptr;
  open_.store(true);
}

void WifiSetupDialog::close() {
  open_.store(false);
  frameDrawn_ = false;
}

bool WifiSetupDialog::takeAcceptedCredentials(String& ssid, String& password) {
  if (mutex_ == nullptr) return false;

  char ssidCopy[kMaxSsid + 1];
  char passwordCopy[kMaxPassword + 1];

  xSemaphoreTake(mutex_, portMAX_DELAY);
  const bool pending = accepted_;
  if (pending) {
    memcpy(ssidCopy, acceptedSsid_, sizeof(ssidCopy));
    memcpy(passwordCopy, acceptedPassword_, sizeof(passwordCopy));
    accepted_ = false;
  }
  xSemaphoreGive(mutex_);

  if (!pending) return false;

  ssid = ssidCopy;
  password = passwordCopy;
  return true;
}

WifiSetupDialog::Result WifiSetupDialog::handleButtons(uint8_t pressed) {
  if (!open_.load()) return Result::None;

  if (notice_ != nullptr) {
    notice_ = nullptr;
    drawHeader();
  }

  if (pressed & ButtonInput::kButtonA) moveSelection(-1);
  if (pressed & ButtonInput::kButtonB) moveSelection(1);
  if (pressed & ButtonInput::kButtonC) return activateKey();
  return Result::None;
}

void WifiSetupDialog::update() {
  if (!open_.load()) return;

  if (!frameDrawn_) {
    drawFrame();
    frameDrawn_ = true;
    drawnSelection_ = selection_;
    drawnField_ = field_;
    drawnPage_ = page_;
    fieldsDirty_ = false;
    return;
  }

  if (page_ != drawnPage_) {
    drawKeyboard();
    drawnPage_ = page_;
    drawnSelection_ = selection_;
  }

  if (field_ != drawnField_) {
    drawFields();
    drawnField_ = field_;
    fieldsDirty_ = false;
  } else if (fieldsDirty_) {
    drawFields();
    fieldsDirty_ = false;
  }

  if (selection_ != drawnSelection_) {
    drawKey(drawnSelection_, false);
    drawKey(selection_, true);
    drawnSelection_ = selection_;
  }
}

WifiSetupDialog::Result WifiSetupDialog::activateKey() {
  if (selection_ < kCharKeys) {
    appendChar(activeChar(selection_));
    return Result::None;
  }

  switch (selection_ - kCharKeys) {
    case kActionField:
      switchField();
      return Result::None;
    case kActionPage:
      nextPage();
      return Result::None;
    case kActionSpace:
      appendSpace();
      return Result::None;
    case kActionDelete:
      backspace();
      return Result::None;
    case kActionClear:
      clearField();
      return Result::None;
    case kActionOk:
      return accept() ? Result::Ok : Result::None;
    case kActionCancel:
      close();
      return Result::Cancel;
    default:
      return Result::None;
  }
}

bool WifiSetupDialog::accept() {
  if (ssidLength_ == 0) {
    notice_ = "SSID required";
    drawHeader();
    return false;
  }
  if (passwordLength_ > 0 && passwordLength_ < 8) {
    notice_ = "Password: min 8";
    drawHeader();
    return false;
  }

  if (mutex_ != nullptr) xSemaphoreTake(mutex_, portMAX_DELAY);
  memcpy(acceptedSsid_, ssid_, ssidLength_ + 1);
  memcpy(acceptedPassword_, password_, passwordLength_ + 1);
  accepted_ = true;
  if (mutex_ != nullptr) xSemaphoreGive(mutex_);

  close();
  return true;
}

void WifiSetupDialog::switchField() {
  field_ = field_ == Field::Ssid ? Field::Password : Field::Ssid;
}

void WifiSetupDialog::nextPage() {
  switch (page_) {
    case Page::Lower:
      page_ = Page::Upper;
      break;
    case Page::Upper:
      page_ = Page::Symbols;
      break;
    case Page::Symbols:
      page_ = Page::Lower;
      break;
  }
}

void WifiSetupDialog::appendChar(char c) {
  if (field_ == Field::Ssid) {
    if (ssidLength_ >= kMaxSsid) return;
    ssid_[ssidLength_++] = c;
    ssid_[ssidLength_] = '\0';
  } else {
    if (passwordLength_ >= kMaxPassword) return;
    password_[passwordLength_++] = c;
    password_[passwordLength_] = '\0';
  }
  fieldsDirty_ = true;
}

void WifiSetupDialog::appendSpace() {
  appendChar(' ');
}

void WifiSetupDialog::backspace() {
  if (field_ == Field::Ssid) {
    if (ssidLength_ == 0) return;
    ssid_[--ssidLength_] = '\0';
  } else {
    if (passwordLength_ == 0) return;
    password_[--passwordLength_] = '\0';
  }
  fieldsDirty_ = true;
}

void WifiSetupDialog::clearField() {
  if (field_ == Field::Ssid) {
    if (ssidLength_ == 0) return;
    ssidLength_ = 0;
    ssid_[0] = '\0';
  } else {
    if (passwordLength_ == 0) return;
    passwordLength_ = 0;
    password_[0] = '\0';
  }
  fieldsDirty_ = true;
}

void WifiSetupDialog::moveSelection(int delta) {
  int next = (static_cast<int>(selection_) + delta) % static_cast<int>(kKeyCount);
  if (next < 0) next += static_cast<int>(kKeyCount);
  selection_ = static_cast<uint16_t>(next);
}

char WifiSetupDialog::activeChar(uint16_t index) const {
  const char* keys = page_ == Page::Lower
                         ? kLowerKeys
                         : (page_ == Page::Upper ? kUpperKeys : kSymbolKeys);
  return index < kCharKeys ? keys[index] : '\0';
}

const char* WifiSetupDialog::actionLabel(uint8_t action) const {
  switch (action) {
    case kActionField:
      return "Fld";
    case kActionPage:
      return page_ == Page::Lower ? "abc" : (page_ == Page::Upper ? "ABC" : "sym");
    case kActionSpace:
      return "Spc";
    case kActionDelete:
      return "Del";
    case kActionClear:
      return "Clr";
    case kActionOk:
      return "OK";
    case kActionCancel:
      return "Esc";
    default:
      return "";
  }
}

int WifiSetupDialog::keyX(uint16_t index) const {
  if (index < kCharKeys) {
    return kKeyX0 + static_cast<int>(index % kColumns) * kKeyColStep;
  }
  return kKeyX0 + static_cast<int>(index - kCharKeys) * kActionStep;
}

int WifiSetupDialog::keyY(uint16_t index) const {
  if (index < kCharKeys) {
    return kKeyRowY0 + static_cast<int>(index / kColumns) * kKeyRowStep;
  }
  return kActionY;
}

int WifiSetupDialog::keyWidth(uint16_t index) const {
  return index < kCharKeys ? kKeyW : kActionW;
}

int WifiSetupDialog::keyHeight(uint16_t index) const {
  return index < kCharKeys ? kKeyH : kActionH;
}

void WifiSetupDialog::drawFrame() {
  tft_.fillScreen(kPanel);

  drawHeader();
  drawFields();
  drawKeyboard();
}

void WifiSetupDialog::drawHeader() {
  tft_.fillRect(0, 0, kScreenW, kSetupHeaderH, kBg);

  tft_.setTextDatum(TL_DATUM);
  tft_.setTextColor(kAccent, kBg);
  tft_.drawString("WIFI SETUP", 8, 5, 2);

  // Clear the hint area so a shorter notice never leaves fragments behind.
  tft_.fillRect(118, 0, kScreenW - 126, kSetupHeaderH, kBg);
  tft_.setTextDatum(TR_DATUM);
  if (notice_ != nullptr) {
    tft_.setTextColor(kErr, kBg);
    tft_.drawString(notice_, kScreenW - 8, 9, 1);
  } else {
    tft_.setTextColor(kMuted, kBg);
    tft_.drawString("A/C: move  B: press", kScreenW - 8, 9, 1);
  }
}

void WifiSetupDialog::drawFields() {
  drawField(Field::Ssid, "SSID", ssid_, field_ == Field::Ssid);
  drawField(Field::Password, "PASS", password_, field_ == Field::Password);
}

void WifiSetupDialog::drawField(Field field, const char* label, const char* value,
                                bool active) {
  const int y = field == Field::Ssid ? kFieldSsidY : kFieldPasswordY;
  const uint16_t background = active ? kPanel2 : kPanel;
  const uint16_t border = active ? kSelected : kLine;

  tft_.fillRoundRect(kFieldX, y, kFieldW, kFieldH, 6, background);
  tft_.drawRoundRect(kFieldX, y, kFieldW, kFieldH, 6, border);

  tft_.setTextDatum(ML_DATUM);
  tft_.setTextColor(active ? kSelected : kMuted, background);
  tft_.drawString(label, kFieldX + kFieldLabelX, y + kFieldH / 2, 2);

  const int valueX = kFieldX + kFieldValueX;
  const int maxWidth = kFieldX + kFieldW - 8 - valueX;

  if (value[0] == '\0') {
    tft_.setTextColor(kMuted, background);
    tft_.drawString("not set", valueX, y + kFieldH / 2, 1);
  } else {
    drawTailFitted(String(value), valueX, y + kFieldH / 2, maxWidth, 2, kText,
                   background);
  }
}

void WifiSetupDialog::drawKeyboard() {
  tft_.startWrite();
  for (uint16_t i = 0; i < kKeyCount; ++i) {
    drawKey(i, i == selection_);
  }
  tft_.endWrite();
}

void WifiSetupDialog::drawKey(uint16_t index, bool selected) {
  const bool charKey = index < kCharKeys;
  const int x = keyX(index);
  const int y = keyY(index);
  const int w = keyWidth(index);
  const int h = keyHeight(index);

  tft_.fillRoundRect(x, y, w, h, 4, kPanel2);
  tft_.drawRoundRect(x, y, w, h, 4, selected ? kSelected : kLine);

  const char* label =
      charKey ? nullptr : actionLabel(static_cast<uint8_t>(index - kCharKeys));
  const uint16_t color = selected ? kSelected : (charKey ? kText : kAccent);

  tft_.setTextDatum(MC_DATUM);
  tft_.setTextColor(color, kPanel2);
  if (charKey) {
    const char text[2] = {activeChar(index), '\0'};
    tft_.drawString(text, x + w / 2, y + h / 2, 2);
  } else {
    tft_.drawString(label, x + w / 2, y + h / 2, 2);
  }
}

void WifiSetupDialog::drawTailFitted(const String& text, int x, int y, int maxWidth,
                                     uint8_t font, uint16_t color,
                                     uint16_t background) {
  String shown = text;
  if (tft_.textWidth(shown, font) > maxWidth) {
    const String prefix = "...";
    while (shown.length() > 0 && tft_.textWidth(prefix + shown, font) > maxWidth) {
      shown.remove(0, 1);
    }
    shown = prefix + shown;
  }

  tft_.setTextDatum(ML_DATUM);
  tft_.setTextColor(color, background);
  tft_.drawString(shown, x, y, font);
}

#else  // !VPSPANEL_HAS_DISPLAY

void WifiSetupDialog::open(const String&, const String&) {}

void WifiSetupDialog::close() {}

bool WifiSetupDialog::takeAcceptedCredentials(String&, String&) {
  return false;
}

WifiSetupDialog::Result WifiSetupDialog::handleButtons(uint8_t) {
  return Result::None;
}

void WifiSetupDialog::update() {}

#endif
