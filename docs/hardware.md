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
| `PDL_PIN_AUDIO_SD_MODE` | **-1** | not under firmware control on the shipped PCB |

The channel is configured 16-bit, **mono**, 44.1 kHz, Philips standard
(`I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(..., I2S_SLOT_MODE_MONO)`), which sets
`slot_mask = I2S_STD_SLOT_LEFT`: mono PCM occupies the **LEFT slot only**.
Requirement was "left or both", so this is compliant.

**SD_MODE strap.** R21 (0R) ties SD_MODE to +3V3, so the amplifier is always
enabled and runs in its (L+R)/2 mode. With a left-slot-only stream that costs
about 6 dB, which is why `svc_audio` applies a 0.70 (-3.1 dB) software ceiling
instead of digital full scale (`docs/audio-adhan-design.md` §3.1).

**GPIO1 / JP2 / R15.** GPIO1 is routed to JP2, a 3-pad solder jumper bridged
1-2 by default. Pad 1 = R21 to +3V3, pad 2 = amplifier SD_MODE, pad 3 = GPIO1's
trace. R15 (10 kR) sits on the SD_MODE net (pad 2), **not** on GPIO1's trace.
With 1-2 bridged, GPIO1 is isolated from the amplifier and flaps free.

### 2.1 Firmware SD_MODE control (reserved for the jamat-alert PR)

The path below is the one `pdl_pins.h` documents. (An earlier note said "bridge
JP1 pad 2-3 and remove R21"; the tree says **JP2**, and R21 does not have to be
removed because cutting the bridge isolates it on pad 1. The tree wins.)

1. Cut the JP2 **1-2** bridge. R21 stays on pad 1, now isolated.
2. Solder the JP2 **2-3** bridge, connecting SD_MODE to GPIO1's trace.
3. Keep R15 in place: it pulls SD_MODE low when GPIO1 is not driven, which is
   the wanted shutdown state.
4. Firmware: `PDL_PIN_AUDIO_SD_MODE` -1 -> 1, re-enable the GPIO output config
   in `HAL_Audio_Init()`, and add a HAL/HIL enable/shutdown setter (none exists
   today).
5. Wait **>= 10 ms** after releasing shutdown before starting BCLK/LRCLK: the
   MAX98357A tON is 7-7.5 ms, and the first milliseconds are lost otherwise.

Not in v1. v1 relies on the lazy I2S init/deinit plus a silence flush instead.

### 2.2 Clock-stop hazard

"Never remove LRCLK while BCLK is still running." `HAL_Audio_Deinit()` disables
and deletes the I2S channel, which stops both clocks together, and `svc_audio`
writes >= 2048 digital-zero samples through `audio_play_pcm()` first so SDIN is
already zero when the clocks stop. Stopping BCLK also puts the amplifier into
automatic standby with high-Z outputs (340-400 uA vs 0.6 uA in shutdown) — fine
on a mains-powered clock, and the reason idle hiss is impossible by
construction.

> **Unverified:** the architect's amendment cites 100 K pull-downs **R24/R25**
> on I2S_BCLK / I2S_LRCK. Those designators appear nowhere in this repository.
> Confirm against the schematic before relying on them.

---

## 3. Memory map and partitions

See `partitions.md` / `partitions.csv` (root). Audio on the device is a single
`/storage/adhan.mp3` on the FATFS `storage` partition; there is no SPIFFS and no
LittleFS anywhere in this project.
