# Flash Layout

The authoritative file is [`../partitions.csv`](../partitions.csv) at the
repo root. That is the file the ESP-IDF build system reads. **This
document explains it; the CSV defines it.** If the two ever disagree,
the CSV wins - and this file is a bug.

If you change `partitions.csv`, update this file in the same commit.

---

## Current layout

Target: ESP32-S3 module, 16 MB flash.

| Name | Type / SubType | Offset | Size | Purpose |
|---|---|---|---|---|
| `nvs` | data / nvs | 0x9000 | 32 KB (0x8000) | Non-volatile key-value store. Holds WiFi credentials, boot flags, user config. |
| `otadata` | data / ota | 0x11000 | 8 KB (0x2000) | OTA slot selector. The bootloader reads this to decide which app slot to boot. |
| `phy_init` | data / phy | 0x13000 | 4 KB (0x1000) | RF calibration data. Board-specific, written once at first boot. |
| `app0` | app / ota_0 | 0x20000 | 4 MB (0x400000) | Application slot 0. Booted on first-ever flash. |
| `app1` | app / ota_1 | 0x420000 | 4 MB (0x400000) | Application slot 1. Used by OTA as the "other" slot. |
| `storage` | data / fat | 0x820000 | 0x7E0000 (~7.9 MB) | FATFS user data, mounted at `/storage`. |

The layout ends exactly at `0x1000000` = 16 MB.

---

## Why the offsets are what they are

ESP-IDF reserves the first ~32 KB for the bootloader and partition
table:

- `0x0000`-`0x8000`: second-stage bootloader.
- `0x8000`-`0x9000`: partition table itself.

Everything else is laid out consecutively from `0x9000`, with **one
alignment pad**: `phy_init` ends at `0x14000`, but `app0` starts at
`0x20000`. The 48 KB between them is unused padding, required because
ESP-IDF app partitions must be aligned on a 64 KB boundary
(`0x10000`). `0x14000` is not 64 KB-aligned, so the first app slot is
pushed forward to the next aligned address.

Every other boundary is tight: `offset + size` of one row equals the
`offset` of the next.

---

## Why these partitions exist

**`nvs`** - the ESP-IDF key-value store. `svc_network` stores WiFi
credentials here (namespace `wifi`, keys `ssid` / `pass`; see
`components/svc_network/src/svc_network.c`). Boot flags are expected to
be written here by the future web UI. Anything that must survive a
reboot but isn't a user file goes in NVS.

**`otadata`** - read only by the bootloader, never by application
code. It records which app slot is "current" and whether that slot is
marked as verified. **Deliberately not erased by a data reset** - the
boot-slot preference is not user data.

**`phy_init`** - RF calibration. Written once during first boot and
read thereafter. **Deliberately not erased by a data reset** - it is
board-specific, and regenerating it costs a small amount of boot time
for no benefit.

**`app0` / `app1`** - two symmetric OTA slots. There is **no `factory`
partition**; on first-ever flash, `app0` boots. An OTA update writes
to `app1` (or back to `app0` on the next update). The bootloader uses
`otadata` to decide which to run.

The two slots are identical in size so that an OTA can always write a
complete new image into the inactive slot before switching.

**`storage`** - a FATFS volume mounted at `/storage` by
`boot_manager.c`. This is the only user-writable filesystem on the
device. It is wear-levelled (`esp_vfs_fat_spiflash_mount_rw_wl`).

---

## Constitutional constraint

[`CHRONOLINK_CONSTITUTION.md`](CHRONOLINK_CONSTITUTION.md) Section 6
requires the `storage` row to be exactly:

```
storage,data,fat,0x820000,0x7E0000,
```

The offset and size are not arbitrary. `0x820000` is where `app1`
ends; `0x7E0000` makes the total land exactly at 16 MB. Changing
either without changing the other produces a partition table the
bootloader will reject.

The constitution also forbids SPIFFS / LittleFS partition types.

---

## What is not here yet

The following are **planned**, not implemented. See
[STATUS.md](STATUS.md).

- **A `factory` partition.** Adding one would give the device a
  known-good slot to fall back to if both OTA slots are corrupt. Cost:
  the 16 MB budget means `app0` and `app1` would need to shrink to
  make room, unless the `factory` image is much smaller. Not yet
  designed.
- **A separate "user data" partition** distinct from NVS. Currently
  user config lives in NVS and user files live in `/storage`; if
  configuration ever needs a structured format (e.g. JSON), a
  dedicated partition might be cleaner. Not needed today.

---

## Reading this alongside `boot.md`

[boot.md](boot.md) describes the boot sequence. It mentions the
partition table in its "Flash layout" and "Reset mechanisms" sections
- both should agree with the table above. If they don't, the CSV is
still the source of truth and the docs need fixing.