# HAL and PDL Reference

Covers `components/chronolink_hal` (the HAL, which also contains the
PDL headers) and `components/chronolink_driver_sdk`.

Everything here describes the code as it is. Planned work is in
[STATUS.md](STATUS.md), not here. If a statement disagrees with the
code, the code wins - fix the document.

---

## 1. Responsibilities

**PDL (Platform/Board Definition)** - pin numbers, board identity,
capability flags, pin validation, initial pin direction and level.

Files: `pdl_pins.h`, `pdl_capabilities.h`, `pdl_partitions.h`,
`pdl_board.h`, `pdl_compat.h`, `board_pins.h`, `pdl_board_esp32.c`.

**HAL (Hardware Abstraction Layer)** - drivers that touch hardware.
Never calls services, kernel, or application code.

The HAL does **not** currently do memory / PSRAM setup, hardware-profile
loading, a general device registry (other than sensors), or health
checks. Earlier docs listed `HAL_MemoryInit`, `HAL_BusInit`,
`HAL_LoadConfig`, `HAL_RegisterDevice`, `HAL_HealthCheck`, `HAL_Ready`.
**None of these exist.**

---

## 2. Status codes

```c
typedef enum {
    HAL_OK        =  0,
    HAL_ERR_INIT  = -1,   /* init failed, not initialised, or resource/slot problem */
    HAL_ERR_BUS   = -2,   /* bus busy / lock timeout / transfer timeout */
    HAL_ERR_DEV   = -3    /* device failure, invalid argument, or "not supported" */
} hal_status_t;

---

## 3. Init order

`HAL_Init()` (in `hal_init.c`), in order:

1. `pdl_board_init` - **fatal on failure**
2. `HAL_GPIO_Init()`
3. `HAL_I2C_Init()` - skipped if pins invalid
4. `HAL_SPI_Init()` - **fatal on failure**
5. `HAL_RTC_Init()` - soft-fail if no DS3231M
6. `HAL_Display_Init()` - **fatal on configured backend failure**
7. Sensor registration (per `pdl_capabilities.h`) + `HAL_Sensors_Init()`
8. `HAL_Audio_Init()` - failure logged and ignored

Full per-step rationale is in [boot.md](boot.md).

---

## 4. Pin map (from `pdl_pins.h`)

| Function | Signal | GPIO |
|---|---|---|
| SPI2 (shared: TFT + SD) | SCLK | 4 |
| | MOSI | 5 |
| | MISO | 6 |
| TFT ST7796S | CS | 7 |
| | DC | 10 |
| | RST | 16 |
| | Backlight | 8 |
| SD card | CS | 9 (no driver yet) |
| I2C0 (RTC + sensors) | SCL | 17 |
| | SDA | 18 |
| Audio MAX98357A (I2S) | BCLK | 47 |
| | WS / LRCLK | 48 |
| | DOUT | 21 |
| | SD_MODE | routed to JP2 (1-2 bridged by default); amp held on via R21 (0R) to 3V3 |
| BOOT / safe-mode button | | 0 (active low, dev only) |
| Status LED | | none (-1) |
| RTC INT | | not wired (-1) |

### Audio SD_MODE (MAX98357A pin 4)

SD_MODE is held HIGH on the shipped PCB by R21 (0R) to +3V3, so the
amplifier is always enabled (right-channel output). Firmware does not
control it.

ESP32-S3 GPIO1 is routed to JP2 (a 3-pad solder jumper, "Bridged12" by
default). JP2 pad 1 = R21 to +3V3, pad 2 = amplifier *SD_MODE, pad 3 =
GPIO1's trace. R15 (10kR) sits on the SD_MODE net (pad 2), not on
GPIO1's trace. With pads 1-2 bridged (default), GPIO1 is isolated from
the amplifier and from R15 -- GPIO1 floats.

To enable firmware control of SD_MODE:

1. Cut the JP2 1-2 bridge (open the solder). This disconnects R21 (0R)
   from SD_MODE. R21 stays on pad 1, which is now isolated -- no
   desoldering of R21 is required.
2. Solder the JP2 2-3 bridge, connecting amplifier *SD_MODE (pad 2) to
   GPIO1's trace (pad 3).
3. Keep R15 (10kR) in place: it is on the SD_MODE net and pulls SD_MODE
   low when GPIO1 is not driven, which is the desired shutdown state.
4. In firmware: change PDL_PIN_AUDIO_SD_MODE from -1 to 1, and re-enable
   the output config in HAL_Audio_Init(). A HAL/HIL setter for
   enable/shutdown would also be needed -- this codebase does not have
   one today.

The default (-1) reflects the shipped hardware and keeps the amplifier
permanently enabled.

I2C frequency: Kconfig default 100 kHz (`CONFIG_PDL_I2C_FREQ_HZ`).
The I2C driver currently does not honour it - see STATUS.md.

**Pin sources are split today.** Pins are defined in `pdl_pins.h`
(hard-coded), overlaid by `CONFIG_PDL_PIN_I2C_*` (HAL Kconfig) and
`CONFIG_BOARD_*` (`main/Kconfig.projbuild`, used by `board_pins.h` for
LCD, I2C, audio). `pdl_pins.h` wins for the audio pins.
`config/pins.example.json` is unused and lists different pins.

Treat `pdl_pins.h` as the reference until this is consolidated.

**I2C device addresses:** DS3231M `0x68`, SHT4x `0x44`, VEML7700 `0x10`,
BME280 `0x76`.

---

## 5. Capability flags (`pdl_capabilities.h`)

Compile-time only. There is no runtime presence query.

| Flag | Value | Meaning |
|---|---|---|
| `PDL_HAS_RTC_DS3231M` | 1 | DS3231M RTC present |
| `PDL_HAS_DISPLAY_ST7796` | 1 | ST7796S display panel present |
| `PDL_HAS_DISPLAY_MAX7219` | 0 | MAX7219 chain not present |
| `PDL_HAS_WIFI` | 1 | Wi-Fi hardware available |
| `PDL_HAS_BLE` | 0 | BLE not used |
| `PDL_HAS_SENSOR_SHT4X` | 1 | SHT4x sensor present |
| `PDL_HAS_SENSOR_BME280` | 0 | BME280 not populated |
| `PDL_HAS_SENSOR_VEML7700` | 1 | VEML7700 sensor present |
| `PDL_HAS_AUDIO_MAX98357A` | 1 | MAX98357A amp present |

`hal_init.c` registers sensor drivers and initialises audio only when
their flag is 1. Driver **source files compile unconditionally**;
the flag controls whether the driver is *registered* at boot.

---

## 6. Modules

| Module | Header(s) | Notes |
|---|---|---|
| GPIO | `hal_gpio.h`, `hal_gpio_safe.h` | `HAL_GPIO_Write/Read`; `board_gpio_*` range-checking wrappers |
| I2C | `hal_i2c.h` | One bus (I2C0), mutex-guarded, 100 ms transfer timeout, 200 ms lock timeout. Register read/write plus raw transmit / receive / transmit-receive |
| SPI | `hal_spi.h` | One shared bus (SPI2), mutex-guarded, 4 device slots, 8192-byte max transfer. `HAL_SPI_Transfer` is chip-select-addressed |
| RTC | `hal_rtc.h` | DS3231M: get/set time, oscillator-stop (battery loss) detection and clear. Soft-fails at init if absent |
| Display | `hal_display.h` | Router + backends - see below |
| Graphics | `hal_gfx.h` | Clipped primitives (lines, rects, circles, round rects, blit) over the display router, RGB565 API. Lives here for now; planned to move out |
| Sensors | `hal_sensors.h` | Registry of up to 4 drivers; legacy aggregate read plus read-by-name and read-all |
| Audio | `hal_audio.h` | MAX98357A over I2S, 44.1 kHz mono 16-bit, blocking play. Volume is a no-op (the amp has analog gain only) |
| Driver SDK | `chronolink_driver_sdk.h` | Register one custom display driver at runtime |

### Display router

`HAL_Display_*` calls are dispatched through a `display_backend_ops_t`
table selected at build time. Any function pointer may be NULL (the
router then returns `HAL_ERR_DEV`). Calls are serialised by a recursive
mutex (`CONFIG_CHRONOLINK_HAL_DISPLAY_MUTEX`, default on).

Selection priority: **mock > ST7796S > MAX7219 > remote > custom (SDK)**.
Because of this priority, the custom SDK driver is only used when no
built-in backend is selected, and enabling the mock **replaces** the
real backend (the Kconfig help text says "in addition"; the code says
"instead").

| Backend | Status |
|---|---|
| ST7796S 320x480 (SPI, RGB565) | Implemented: fill, fill-rect, pixel, row blit, MADCTL, hardware scroll. Text rendering is not implemented in the backend (use the font layer) |
| MAX7219 | **Stub: returns `HAL_ERR_DEV`, draws nothing** |
| Remote | **Stub: returns `HAL_ERR_DEV`, sends nothing** |
| Mock | For host-less builds (`CONFIG_CHRONOLINK_USE_MOCK_DISPLAY`) |
| Custom (SDK) | Works; one driver at a time, register before `HAL_Display_Init()` |
| None (headless) | `HAL_Display_Init()` returns `HAL_OK`; later draw calls return `HAL_ERR_INIT` |

`HAL_Display_HasCapability` returns `HAL_OK` if supported, `HAL_ERR_DEV`
if not. Backlight brightness (`HAL_CAP_BRIGHTNESS`) is declared but not
implemented by any backend.

Adding a backend: see [Architecture.md](Architecture.md) section 6.

### Sensors

| Driver | Provides | Notes |
|---|---|---|
| SHT4x (0x44) | temperature, humidity | CRC bytes are read but not verified |
| BME280 (0x76) | temperature, humidity, pressure | Disabled on this board |
| VEML7700 (0x10) | ambient light | Default config; lux scale factor needs verifying against the datasheet (STATUS.md) |

`HAL_Sensors_Read()` merges all initialised sensors into one struct,
with a `valid` bitmask. A failed probe at boot is **not** retried.

Sensor selection is done via `pdl_capabilities.h`. (Some older
comments mention sensor Kconfig options; those options do not exist in
the HAL Kconfig.)

---

## 7. Thread safety

| Module | Protected by |
|---|---|
| I2C | bus mutex (but first-call lazy init is not locked) |
| SPI (`HAL_SPI_*`) | bus mutex |
| Display | router recursive mutex |
| ST7796S backend | **not** on the `HAL_SPI` mutex: it adds its own device to the bus and toggles DC itself |
| `hal_gfx` | none; clip state and row buffer are global |
| Sensors, RTC | via the I2C mutex only |

---

## 8. Build configuration

- Kconfig: `components/chronolink_hal/Kconfig` (display backend choice,
  mutex, ST7796S parameters, I2C pins/frequency) and
  `main/Kconfig.projbuild` (`CONFIG_BOARD_*` pins).
- ST7796S Kconfig values are passed to the driver as compile
  definitions by the HAL `CMakeLists.txt`.
- Default (`sdkconfig.defaults`): ST7796S backend, mock disabled,
  16 MB flash.