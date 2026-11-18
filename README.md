# VPS Monitor (ESP32-C3)

An ESP32-C3 dashboard that polls a configurable list of Linux servers over SSH
and shows CPU usage, load average, memory usage and root filesystem usage — on
a web page, in the serial console, and on an optional ILI9341 TFT panel.

> Based on the original single-server sketch. Polling now runs on its own
> FreeRTOS task, configuration lives in flash (NVS), and the code is split into
> domain / infrastructure / presentation layers.

## Features

- Polls **any number of hosts** (up to 16), one SSH command per host per cycle.
- Web dashboard at `http://<device-ip>/` with auto-refresh (5 s) and a
  "Poll now" button.
- Server list and polling settings are managed at `http://<device-ip>/servers`.
- All configuration is stored in the ESP32 **NVS flash partition**; it survives
  reboots and firmware updates.
- Wi-Fi credentials default to `src/config/wifi_credentials.h` (which is
  **git-ignored**) and can be entered on the TFT setup screen, where they are
  saved to NVS.
- Serial console keeps printing poll results, so headless use still works.
- Optional **TJCTM24024-SPI (ILI9341) TFT dashboard**: three server cards per
  page, automatic paging, Wi-Fi/IP status and poll age. The display can be
  compiled out.
- Three buttons (**A/B/C**) on the TFT build: select a card, open its detail
  screen, return to the dashboard, and enter Wi-Fi credentials with an on-screen
  keyboard from the boot splash.

## Setup

1. Create the credentials file:

   ```bash
   cp src/config/wifi_credentials.example.h src/config/wifi_credentials.h
   ```

2. Edit `src/config/wifi_credentials.h`:

   ```cpp
   #define WIFI_SSID "your-wifi-ssid"
   #define WIFI_PASSWORD "your-wifi-password"
   #define WIFI_HOSTNAME "vpspanel"
   ```

   These are the fallback credentials. On a TFT build you can also press **A**
   on the boot splash and enter the credentials on screen; they are then saved
   in NVS and used instead of the header values.

3. Build and upload:

   ```bash
   pio run -t upload
   pio device monitor
   ```

4. Open the IP printed on the serial console (or `http://vpspanel.local/`),
   then go to **Servers** and add your hosts (host, port, SSH user, password).

The very first boot has an empty server list — add at least one server through
the web page. The old sketch's host (`192.168.1.100`) is not hard-coded any
more.

If you connect the optional TFT panel, wire it as described in
[TFT display](#tft-display-tjctm24024-spi--ili9341) before powering up.

## Web UI

| Page              | Purpose                                                       |
| ----------------- | ------------------------------------------------------------- |
| `/`               | Live metrics: status, CPU, load, memory (with bar), disk usage |
| `/servers`        | Add / edit / delete servers, pause polling, change intervals   |
| `/api/metrics`    | JSON snapshot used by the dashboard                           |
| `/api/poll`       | `POST` to request an immediate polling cycle                  |

Server entries can be **paused** instead of deleted; paused hosts keep their
last measurement and are skipped by the poller.

## TFT display (TJCTM24024-SPI / ILI9341)

The firmware can drive a 2.4" 240x320 ILI9341 SPI module (TJCTM24024-SPI) as a
local dashboard. It renders the same `MonitorService` snapshots as the web page:

- Header: title plus the device IP (or `WiFi offline`).
- Three server cards per page: name with `host:port` in brackets, state chip
  (`OK` / `ERROR` / `WAIT` / `PAUSED`), and one line with CPU, memory and disk
  usage percentages.
- Footer: poll interval, `polling...` or the age of the last cycle, and `page x/y`.
- With more than three servers the view rotates to the next page every 7 s;
  values refresh every 2 s (or immediately when a new poll cycle completes).
- The panel is initialised before the Wi-Fi association and shows a splash
  screen with the SSID it is about to join, so it is never blank during boot.
- A full-screen per-server detail view (state, user@host:port, CPU usage, load
  average, memory with bar, root filesystem usage, poll age/duration and the
  SSH error text when a poll failed) is opened from Selection mode with
  button C.

### Buttons

Three momentary buttons connect between the GPIO and GND (the internal
pull-ups are enabled):

| Button | ESP32-C3 | Function                                                     |
| ------ | -------- | ------------------------------------------------------------ |
| A      | GPIO 0   | Boot splash: open Wi-Fi setup; Selection mode: previous card; setup screen: previous key |
| B      | GPIO 1   | Selection mode: next card; setup screen: next key             |
| C      | GPIO 2   | Enter Selection mode / open details / back / press setup key |
| common | GND      |                                                              |

The boot splash shows `press A to setup WiFi`. Pressing **A** there opens the
Wi-Fi setup screen. **A/B** move the highlight across the virtual keyboard and
**C** presses the highlighted key. The keyboard has lowercase, uppercase and
symbol pages (the `abc` action cycles them) and these action keys:

| Key   | Action                                                        |
| ----- | ------------------------------------------------------------- |
| `Fld` | Switch between the SSID and password fields                   |
| `abc` | Cycle lowercase / uppercase / symbols                         |
| `Spc` | Insert a space                                                |
| `Del` | Delete the last character of the active field                 |
| `Clr` | Clear the active field                                        |
| `OK`  | Save the credentials to NVS and reconnect Wi-Fi               |
| `Esc` | Leave the screen without changing anything                    |

Both fields are prefilled with the credentials currently in use. A valid SSID
is required; a non-empty password must have at least 8 characters. The password
is shown in clear text while editing so it can be verified with three buttons.
After **OK** the credentials are stored in the ESP32 NVS partition, so they
survive reboots without reflashing. The values in
`src/config/wifi_credentials.h` are only the fallback used when NVS has no saved
credentials.

The splash stays up while the boot-time Wi-Fi association runs, and for two
seconds after leaving the setup screen.

While the setup screen is open, Wi-Fi is powered down, mDNS and the web server
listener are stopped, and no new SSH poll is started. Leaving the screen
reconnects Wi-Fi (restoring mDNS and the web server once an IP address is
available) and polling resumes on the next cycle.

Behaviour:

- A short press of **C** on the dashboard enters Selection mode. The selected
  card gets a highlighted border and automatic paging stops.
- In Selection mode **A** and **B** move the selection to the previous/next
  card. Selection wraps around and follows across page boundaries.
- **C** opens the detail screen of the selected card. **C** there returns to
  the dashboard with the card still selected.
- 30 seconds without a button press leaves Selection mode and resumes automatic
  paging. The timeout is `VPSPANEL_SELECTION_TIMEOUT_MS` in
  `src/config/input_config.h`; the pins and the active level live there too.

The module's XPT2046 touch controller is not used. The chip does answer on SPI,
but a resistive panel needs its four analog lines (`X+ X- Y+ Y-`) connected to
the controller, and on this module that connection is open: all four touch
channels read floating values and `T_IRQ` never asserts even while pressing.
Consequently `T_CS` and `T_IRQ` do not have to be wired.

### Wiring

| TJCTM24024-SPI | ESP32-C3 | Note                                   |
| -------------- | -------- | -------------------------------------- |
| VCC            | 3V3      |                                        |
| GND            | GND      |                                        |
| CS             | GPIO 7   |                                        |
| RESET          | GPIO 10  |                                        |
| DC             | GPIO 3   |                                        |
| MOSI           | GPIO 6   |                                        |
| SCK            | GPIO 4   |                                        |
| MISO           | GPIO 5   |                                        |
| LED            | 3V3      | see backlight note below               |

The touch pins of the module (`T_CLK`, `T_CS`, `T_DIN`, `T_DO`, `T_IRQ`) are
left unconnected — see the note above.

These are the defaults in `src/config/display_config.h`, mirrored by the `-DTFT_*`
flags in `platformio.ini`; change both to match your board. Avoid GPIO 2, 8 and 9
(strapping pins), GPIO 11-17 (SPI flash) and GPIO 18/19 (USB).

### Configuration

- `src/config/display_config.h` — feature flag, wiring documentation, rotation
  and backlight defaults.
  - `VPSPANEL_HAS_DISPLAY` — set to `0` to build the firmware without the
    display and buttons (no display task is started, so the TFT is not drawn
    and the buttons are not read).
  - `VPSPANEL_TFT_*` — the pin values TFT_eSPI must use,
    `VPSPANEL_TFT_ROTATION` (`1` or `3`; use `3` if the image is upside down),
    and `VPSPANEL_TFT_BL` / `VPSPANEL_TFT_BACKLIGHT_ON`.
- `src/config/input_config.h` — button pins (`VPSPANEL_BUTTON_A_PIN`,
  `VPSPANEL_BUTTON_B_PIN`, `VPSPANEL_BUTTON_C_PIN`,
  `VPSPANEL_BUTTON_ACTIVE_LOW`) and the Selection-mode timeout.
- `platformio.ini` — the matching `-DTFT_*` / `-DUSER_SETUP_LOADED` build flags
  that configure the **TFT_eSPI library
  itself** (driver, 240x320 size, pins, 40 MHz SPI, fonts 1/2/4).
  TFT_eSPI is compiled as a separate library and does
  not see this project's headers, so these flags are the authoritative setup for
  the driver. `static_assert`s in `TftDashboard.cpp` fail the build if they drift
  from `display_config.h`.
- `include/tft_c3_regbase_fix.h` — force-included workaround for TFT_eSPI 2.5.43
  on ESP-IDF 5.x: the library passes `SPI2_HOST` (value `1`) to
  `REG_SPI_BASE()`, while IDF 5.x expects the hardware instance (`2`). Without
  the fix every SPI register pointer becomes a bare offset and the firmware
  panics with `Store access fault` at address `0x10`. Delete it once TFT_eSPI
  fixes its C3 port.

> Pins are deliberately passed as build flags: an `include/User_Setup.h` in the
> project only affects the project's own sources, not the TFT_eSPI library
> build. Relying on it makes the library fall back to its default ESP8266 pin
> map (GPIO 12-15, the ESP32-C3 SPI-flash pins), which hangs the chip as soon as
> it touches the bus.

**Backlight:** most modules wire `LED` straight to 3V3, which is why
`VPSPANEL_TFT_BL` defaults to `-1`. Only set it to a GPIO if your module has a
backlight transistor; a bare LED pin can draw more current than a GPIO should
source. If colours are inverted (light background instead of dark), add
`-DTFT_INVERSION_ON=1` to `build_flags` in `platformio.ini`.

The panel is drawn from its own 10 KB FreeRTOS task, so a slow SPI refresh never
blocks HTTP responses or the SSH poller. That task also polls and debounces the
three buttons.

## Built-in status LED

The board's built-in LED gives feedback even when the panel is absent or before
Wi-Fi is up:

- **Wi-Fi connecting / reconnecting** — 2 Hz blink.
- **Polling cycle running** — 2 Hz blink.
- **Idle** — off.

On most ESP32-C3 boards (ESP32-C3 SuperMini and similar) the built-in LED is a
plain single-colour LED on GPIO 8 wired to 3V3, so it lights when the pin is
driven LOW. That is the default. The official ESP32-C3-DevKitM-1 instead has an
addressable WS2812 RGB LED on GPIO 8; the Arduino core exposes it as
`LED_BUILTIN`. Configuration lives in `src/config/led_config.h`:

- `VPSPANEL_LED_PIN` — defaults to GPIO 8; set to `-1` on a board with no usable
  built-in LED.
- `VPSPANEL_LED_ADDRESSABLE` — `0` (default) for a plain GPIO LED, `1` for the
  WS2812/RGB LED of the official DevKitM-1 (`LED_BUILTIN` API).
- `VPSPANEL_LED_ACTIVE_HIGH` — plain LEDs only; `0` (default) for an LED wired
  to 3V3, `1` for an LED wired to GND.

The LED is serviced from the loop task (`StatusLed::update()` is non-blocking)
and also from the blocking Wi-Fi connect loop in `setup()`.

## Architecture

```
src/
├── main.cpp                     composition root: wiring, Wi-Fi, tasks
├── config/
│   ├── display_config.h             TFT enable flag, pins, rotation
│   ├── input_config.h               button pins, active level, selection timeout
│   ├── led_config.h                 built-in status LED pin / polarity
│   ├── wifi_credentials.example.h   template (committed)
│   └── wifi_credentials.h           real credentials (git-ignored)
├── domain/                      business logic, no Arduino UI/HTTP/storage code
│   ├── AppSettings.h            poll interval, SSH timeout
│   ├── ServerConfig.h           one monitored host (plain data)
│   ├── ServerMetrics.h          one measurement (plain data)
│   ├── MetricsParser.{h,cpp}    parses "LOAD:.. RAM:.. DISK:.. CPU:.."
│   ├── ServerStore.h            persistence port (interface)
│   ├── ServerRegistry.{h,cpp}   thread-safe server list + settings CRUD
│   ├── IMetricsProvider.h       metrics transport port (interface)
│   └── MonitorService.{h,cpp}   scheduling, polling, latest results
├── infra/                       concrete adapters for the outside world
│   ├── NvsServerStore.{h,cpp}   JSON config in the NVS flash partition
│   ├── NvsWifiCredentialStore.{h,cpp}  Wi-Fi credentials in NVS
│   ├── SshMetricsProvider.{h,cpp}  LibSSH-ESP32 implementation
│   ├── ButtonInput.{h,cpp}      debounced A/B/C button presses
│   └── StatusLed.{h,cpp}        built-in LED: Wi-Fi + polling feedback
└── presentation/                output only, talks to the domain services
    ├── ConsoleView.{h,cpp}      serial output (MonitorService listener)
    ├── WebDashboard.{h,cpp}     HTTP routes, forms, JSON API
    ├── TftDashboard.{h,cpp}     ILI9341 dashboard (optional)
    ├── TftTheme.h               shared TFT palette and panel geometry
    ├── WifiSetupDialog.{h,cpp}  on-screen Wi-Fi credential keyboard
    └── WebAssets.h              HTML/CSS/JS in flash (PROGMEM)
include/
└── tft_c3_regbase_fix.h         TFT_eSPI + IDF 5.x C3 register fix
```

TFT driver configuration (TFT_eSPI) lives in `platformio.ini` build flags, not in
an `include/User_Setup.h`: library sources are compiled without the project
`include` path.

Dependency direction: `presentation`/`infra` → `domain`. The domain never
includes `WiFi.h`, `WebServer.h`, `Preferences.h` or libssh. That means the
polling rules and parsers can be unit-tested or reused without the web UI, and a
different presentation (MQTT, display, etc.) only needs to observe
`MonitorService`.

### Threading

- **Loop task** — runs the web server and Wi-Fi supervision. `SET_LOOP_TASK_STACK_SIZE`
  is set to 16 KB.
- **Polling task** — dedicated 40 KB stack for libssh/mbedTLS. It calls
  `libssh_begin()` once and then polls when `MonitorService::pollDue()` says so.
- **Display task** — 10 KB stack (only when `VPSPANEL_HAS_DISPLAY = 1`). It
  redraws the TFT at its own pace, polls the A/B/C buttons and never touches the
  network. The panel is initialised from `setup()` before Wi-Fi, then handed
  over to this task.
- **Status LED** — no task of its own: non-blocking `StatusLed::update()` calls
  from the loop task and the boot-time Wi-Fi connect loop drive the blink.
- Shared state (`ServerRegistry`, latest metrics) is protected by FreeRTOS
  mutexes. A slow or unreachable server delays the next poll, not the web UI.

## Where the data lives

| Data                          | Location                                             |
| ----------------------------- | ---------------------------------------------------- |
| Server list, poll settings    | NVS namespace `vpspanel`, key `config` (JSON blob)    |
| Wi-Fi SSID/password, hostname | `src/config/wifi_credentials.h` (git-ignored)         |
| Latest metrics                | RAM, per server (`MonitorService`)                    |

Notes:

- Live metrics are intentionally **not written to flash on every poll**. NVS
  would receive ~5 800 writes/day per configured cycle, which is unnecessary
  flash wear. They are refreshed automatically after a reboot.
- NVS is limited to 20 KB on the `huge_app.csv` partition; the JSON config for
  16 servers is only a few KB.
- `board_build.partitions = huge_app.csv` is required because the LibSSH
  firmware (~1.5 MB) does not fit the default 1.3 MB application slot. There is
  no OTA slot with this layout.

## Security notes

This is a LAN tool, not a hardened gateway:

- SSH host keys are **not** verified (`ssh_session_is_known_server` is not used).
- SSH passwords and the whole configuration are stored **unencrypted** in NVS,
  as are the Wi-Fi credentials entered on the setup screen.
- HTTP (not HTTPS) with no authentication.

Use a dedicated low-privilege SSH account with a command-restricted key/password,
keep the device on a trusted network, and do not expose port 80 to the internet.

## Serial output

```
========================================
  VPS Monitor - ESP32 SSH dashboard
========================================
Storage: configuration loaded from flash (NVS)
Settings: poll every 15 s, SSH timeout 10 s
Servers: 2 configured (max 16)
  1. vps-1  192.168.1.10:22  user=monitor  enabled
  2. vps-2  example.com:2222  user=root  paused
Wi-Fi: connected, IP 192.168.1.50
Web UI: http://192.168.1.50/
mDNS: http://vpspanel.local/
Free heap: 245000 bytes
[poll] checking 2 server(s)
[ ok ] vps-1 (192.168.1.10:22) cpu=12.5% load=0.15 mem=143/7951 MB disk=4123/40258 MB (10%) (812 ms)
[poll] done: 1 server(s) in 838 ms
```

## Tuning

- `kPollTaskStack` in `src/main.cpp` — increase if SSH crashes with stack
  overflow (LibSSH's own exec example uses 51 KB).
- Poll interval 5–3600 s, SSH timeout 2–60 s (editable on the web page).
- The remote command is a single line in
  `src/infra/SshMetricsProvider.cpp`; extend the parser in
  `src/domain/MetricsParser.cpp` if you add metrics.
- Display refresh (1 s), paging interval (7 s), button polling (10 ms), button
  press lockout (30 ms) and servers per page are constants in
  `src/presentation/TftDashboard.cpp` / `src/infra/ButtonInput.h`; the
  Selection-mode timeout is in `src/config/input_config.h`, and the setup
  keyboard layout/pages are in `src/presentation/WifiSetupDialog.cpp`. The
  display task stack is set in `src/main.cpp` (10 KB).
