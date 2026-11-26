# Hardware notes

Facts here are copied from the tree (`components/chronolink_hal/include/pdl_pins.h`,
`sdkconfig.defaults`, `docs/STATUS.md`) or are datasheet facts about the parts.
If a line disagrees with the schematic, the schematic wins — fix this file.

---

## 1. Module variants (bench vs production)

| Board | Module | Flash | PSRAM |
|---|---|---|---|
| Bench | ESP32-S3-WROOM-1-**N16R8** | 16 MB | 8 MB present, **disabled** |
| Production BOM | ESP32-S3-WROOM-1-**N16R2** | 16 MB | 2 MB present, **disabled** |

PSRAM stays disabled in `sdkconfig.defaults`, so **one image runs on both**
modules and no code may assume PSRAM exists. `docs/boot.md` and `docs/STATUS.md`
record the variant question; this table is the answer for the audio/prayer
work: the bench part is an R8, production is an R2, and the firmware does not
care because nothing allocates from PSRAM.

Consequence for the adhan path: `svc_audio`'s buffers and the libhelix decoder
state are internal RAM only. Do not "optimise" them into PSRAM later without
re-testing on the N16R2 production part.

---

## 2. Audio chain (MAX98357A)

I2S pins (authoritative source: `pdl_pins.h`):

| Signal | Pin | Notes |
|---|---|---|
| `PDL_PIN_I2S_BCLK` | 47 | bit clock |
| `PDL_PIN_I2S_WS` | 48 | LRCLK / word select |
| `PDL_PIN_I2S_DOUT` | 21 | data into the amplifier |
| `PDL_PIN_AUDIO_SD_MODE` | **1** | GPIO1, firmware-controlled: high = running, low = shutdown (0.6 uA) |

The channel is configured 16-bit, **mono**, 44.1 kHz, Philips standard
(`I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(..., I2S_SLOT_MODE_MONO)`), which sets
`slot_mask = I2S_STD_SLOT_LEFT`: mono PCM occupies the **LEFT slot only**.
Requirement was "left or both", so this is compliant.

**SD_MODE strap.** **R21 is DNP and R15 (10 kR) pulls SD_MODE low**, so the
amplifier is in hardware shutdown (0.6 uA, high-Z outputs) whenever firmware is
not driving the pin. Firmware drives it: `pdl_board_init()` sets GPIO1 as an
output and drives it **low** (silent boot), and `HAL_Audio_AmpSetEnabled()`
drives it high per playback. SD_MODE high selects the amplifier's (L+R)/2 mode;
with a left-slot-only stream that costs about 6 dB, which is why `svc_audio`
applies a 0.70 (-3.1 dB) software ceiling instead of digital full scale
(`docs/audio-adhan-design.md` §3.1).

### 2.1 Firmware SD_MODE control (implemented)

1. `components/chronolink_hal/include/pdl_pins.h` defines
   `PDL_PIN_AUDIO_SD_MODE 1`. **This is the only pin source**;
   `docs/hal.md` §4, `pdl_board_esp32.c`'s pin dump and this section follow it.
2. `pdl_board_init()` (`pdl_board_esp32.c`) configures GPIO1 as an output and
   drives it low before anything else touches audio. It also validates the pin
   as output-capable and includes it in the `PINDBG` dump. A `gpio_config()`
   failure here is logged and **not** fatal: the clock still has to keep time,
   and the failure reappears as class 3a' at the first adhan.
3. `HAL_Audio_AmpSetEnabled(bool)` (`hal_audio_max98357a.c`) is the only writer
   of the pin after board init, exposed to services as the HIL
   `audio_amp_set_enabled()`. It keeps `s_amp_enabled` so nothing can disable an
   amplifier it did not enable.
4. Wait **>= 10 ms** after releasing shutdown before starting BCLK/LRCLK: the
   MAX98357A tON is 7-7.5 ms and the first milliseconds are lost otherwise. This
   is `HAL_AUDIO_AMP_TON_GUARD_MS` inside the enable call, i.e. before any
   channel exists. The call is idempotent, so `HAL_Audio_Init()`'s internal
   enable adds no second wait.
5. Shutdown order at the end of a playback: silence flush -> drive SD_MODE low ->
   delete the I2S channel (`svc_audio`, design §4.5).

### 2.2 Clock-stop hazard

"Never remove LRCLK while BCLK is still running." `HAL_Audio_Deinit()` disables
and deletes the I2S channel, which stops both clocks together, and `svc_audio`
writes >= 2048 digital-zero samples through `audio_play_pcm()` and then drives
SD_MODE low before the channel is touched, so SDIN is already zero when the
clocks stop. Stopping BCLK puts the amplifier into automatic standby with
high-Z outputs (340-400 uA); driving SD_MODE low then takes it to shutdown
(0.6 uA), which is the state the device idles in. Idle hiss is impossible by
construction either way — it is the amplifier that is off, not merely the data
that is zero.

> **Unverified:** the architect's amendment cites 100 K pull-downs **R24/R25**
> on I2S_BCLK / I2S_LRCK. Those designators appear nowhere in this repository.
> Confirm against the schematic before relying on them.

---

## 3. Memory map and partitions

See `partitions.md` / `partitions.csv` (root). Audio on the device is a single
`/storage/adhan.mp3` on the FATFS `storage` partition; there is no SPIFFS and no
LittleFS anywhere in this project.
