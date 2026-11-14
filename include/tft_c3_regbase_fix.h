#pragma once

// ---------------------------------------------------------------------------
// Workaround: TFT_eSPI 2.5.43 + ESP-IDF 5.x on the ESP32-C3
// ---------------------------------------------------------------------------
//
// TFT_eSPI's C3 driver selects its SPI port with:
//
//     #define SPI_PORT SPI2_HOST     // SPI2_HOST == 1
//
// and then builds register addresses with the IDF macro
//
//     #define SPI_USER_REG(i) (REG_SPI_BASE(i) + 0x10)
//
// Newer IDF releases define REG_SPI_BASE() by *hardware instance number*:
//
//     #define REG_SPI_BASE(i) (((i)==2) ? (DR_REG_SPI2_BASE) : (0))
//
// SPI2_HOST is 1, so REG_SPI_BASE(1) evaluates to 0 and every register
// pointer ends up as a bare offset (0x10, 0x08, ...). The first SPI write then
// panics with "Store access fault" at address 0x10.
//
// The C3 has a single general-purpose SPI controller (instance 2), so the base
// is unambiguous. This header is force-included into every translation unit
// (see platformio.ini); it pulls in soc.h first, then replaces the macro.
// TFT_eSPI only defines its own fallback when REG_SPI_BASE is missing, so this
// definition is the one it uses.
//
// Remove this workaround once TFT_eSPI fixes the C3 port for IDF 5.x.

#include "soc/soc.h"

#undef REG_SPI_BASE
#define REG_SPI_BASE(i) (DR_REG_SPI2_BASE)
