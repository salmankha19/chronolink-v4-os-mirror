
---

### `docs/hil.md`

```markdown
# HIL — Hardware Independent Layer

Component: `components/hil`. Provides stable, simple C functions for
services, so services do not depend on HAL types or ESP-IDF headers.

---

## What it is (and is not)

HIL here is a set of **thin adapters** over `chronolink_hal`. Each
function converts `hal_status_t` into `bool` and, where needed,
converts types. Which physical driver runs is decided by HAL Kconfig
and `pdl_capabilities.h`, not by HIL.

It is **not**:

- a runtime driver manager or registry (no `hal_register_driver`,
  `hal_get_driver_for_role`, role names like `"temperature"`),
- a config loader (no YAML / JSON device files, no SPIFFS),
- a probe / auto-detect, capability monitor, or health monitor.

An earlier design document proposed those. They were never built. If
you want them, they are future work — see [STATUS.md](STATUS.md), not
current behaviour.

---

## API summary

| Header | Functions | Notes |
|---|---|---|
| `display_api.h` | `display_init`, `display_deinit`, `display_clear`, `display_draw_pixel`, `display_fill`, `display_draw_text`, `display_get_width`, `display_get_height`, `display_set_brightness` | Colors are 24-bit `0xRRGGBB`. Text uses `font_engine` with the built-in 8×8 font, fixed white. **`display_set_brightness` always returns `false`** (no backlight PWM in HAL) |
| `rtc_api.h` | `hil_rtc_init`, `hil_rtc_get_time`, `hil_rtc_set_time`, `hil_rtc_has_lost_power`, `hil_rtc_clear_lost_power_flag` | Check `hil_rtc_has_lost_power` once at boot; if set, treat the time as untrusted until an NTP sync runs |
| `sensor_api.h` | `sensor_get_temperature`, `sensor_get_humidity`, `sensor_get_pressure`, `sensor_get_light` | Returns `false` when no sensor provides the quantity or the read fails; `*out` is set to 0 on failure. Each call triggers a full sensor read |
| `audio_api.h` | `audio_init`, `audio_deinit`, `audio_play_pcm`, `audio_set_volume`, `audio_is_playing`, `audio_play_mp3` | **`audio_play_mp3` always returns `false`** (no decoder exists). Volume is a no-op in the HAL |
| `gpio_api.h` | `gpio_init`, `gpio_write`, `gpio_read` | Raw digital pins |
| `i2c_api.h` | `i2c_init`, `i2c_write`, `i2c_read` | **Single bus only**; `bus_id` and `freq_hz` are ignored (non-zero `bus_id` logs a warning). Raw transfers without register addressing |
| `spi_api.h` | `spi_init`, `spi_transfer` | Known limitation: `bus_id` is used as the chip-select pin and speed is the HAL default. Fine for one device; not a real multi-device API |
| `pin_config.h` | `PIN_DISPLAY_*`, `PIN_I2C_*`, `PIN_AUDIO_*` | Aliases for the board pins. **Its fallback values for the audio pins (26/25/27) are wrong for this PCB** (should be 47/48/21). Those fallbacks are only reached if `CONFIG_BOARD_AUDIO_*` is undefined — if you ever see them in use, that is a Kconfig bug, not a working configuration |

---

## Usage rules

1. Services depend on `hil`, not on `chronolink_hal`.
   **Exception:** `display_manager` currently calls HAL display and
   graphics functions directly. See [Architecture.md](Architecture.md)
   §5.
2. Always check the `bool` result. "No reading" is not the same as
   zero.
3. Do not add hardware logic to HIL. If a feature needs register
   access, add it to the HAL first and wrap it here.