# `svc_audio` — Adhan Playback Service (v1 design, rev 2)

**Status:** approved with amendments; round-2 amendments (B1–B2, H1–H4, M1–M3, +4 verification items) folded in-place. Ready for implementation.
**Date:** 2026-11-22. **Author:** lead architect (review dispositions incorporated).
**Authority rule:** the tree wins over this document. If code and doc disagree, fix the doc in the same change (`.clinerules`).
**Companion docs:** `docs/boot.md` (insertion point / rollback), `docs/Architecture.md` (task & event model), `docs/prayer-times-model.md` (adhan timing source).

## 0. Accepted dispositions

### 0.1 Round 1

| # | Item | Disposition |
|---|---|---|
| D1 | Event band | **Retracted** the 112–127 claim. `event_bus.h` reserves **96–111** for prayer/audio; **112+** is `svc_http`/`svc_ota`. Audio events at **98/99/100**. |
| D2 | Sample rate | **Retracted** 22.05 kHz. HAL fixed at 44.1 kHz; compression solves storage. **Bitrate is the constraint, not sample rate.** No resampling. |
| D3 | SHORT mode | `SVC_AUDIO_SHORT_DURATION_S = 10`; truncation of the same `/storage/adhan.mp3`. |
| D4 | `audio_stop()` in HIL | **Not added in v1.** Defer to jamat-alert PR. HAL `s_stop_requested` stays for that wrapper; no HAL change now. |
| D5 | Upload size cap | **Deferred** to upload-endpoint PR; scope note §1.3. |
| D6 | Insertion point | After `svc_http_start()` success, before `svc_scheduler_init()`; rationale §4.3. |
| D7 | Invariants | Two headline invariants, own section §5. |
| D8 | `sizeof(mp3dec_t)` | ~25 KB is recollection; **measure at build** (§7). |

### 0.2 Round 2 (folded)

| # | Item | Where landed |
|---|---|---|
| B1 | `event_bus.h` edit in scope; three new enumerators; §9 exception; §2 collision wording | §1.1, §2, §9 r2 |
| B2 | Three failure classes; init-failure ≠ deinit; mid-playback abort+deinit | §3.2, §4.5 |
| H1 | Payload shapes + `_Static_assert`; **size corrected 12→16 B**; `partial` flag added | §2.1 |
| H2 | `svc_audio_start()` return semantics; non-fatal | §4.3 |
| H3 | Volume sampled once at playback start | §3.1 |
| H4 | `svc_config.h` comment-only rewrite in scope | §1.1, §9 r3 |
| M1 | SHORT "at most 10 s, or whole if shorter" | §3.1 |
| M2 | Subscribe via `event_bus_subscribe`, own queue, filter `PRAYER_KIND_ADHAN` | §1.1, §3.1, §4.1, §9 r9 |
| M3 | Corrupt-at-first-frame → stage=DECODE | §3.2 |
| V+ | Four verification items | §8 |

## 1. Scope and storage contract

### 1.1 In scope (v1)
- Play `/storage/adhan.mp3` on `EVENT_PRAYER_TIME_REACHED` (97) **filtered to `prayer_time_event_t.kind == PRAYER_KIND_ADHAN`**, honouring `adhan_mode` ∈ {OFF, FULL, SHORT} and `adhan_volume`, both read **live** from `svc_config` at playback time.
- Single `svc_audio` task owns I2S for the whole playback (§4, §5-I2); subscribes via `event_bus_subscribe()` to its **own** queue (§4.1).
- Consume/emit events only within 96–111 (§2); duplicate adhan events **log + drop** before any stop decision (§4.2).
- **Header edit 1 — `components/event_bus/include/event_bus.h`:** add `EVENT_AUDIO_STARTED = 98`, `EVENT_AUDIO_COMPLETED = 99`, `EVENT_AUDIO_ERROR = 100` (the header currently stops at `EVENT_PRAYER_TIME_REACHED = 97`), plus the three payload structs and their `_Static_assert`s (§2.1).
- **Header edit 2 — `components/svc_config/include/svc_config.h`:** comment-only rewrite of the `adhan_volume` header comment (§9 r3); no behaviour change.

### 1.2 Out of scope (v1) — follow-ups
- Timetable-upload endpoint for audio assets (and its size cap, §1.3).
- Per-prayer files (Fajr differs) and per-prayer short assets.
- `audio_stop()` HIL wrapper and adhan↔jamat pre-emption (jamat-alert PR).

### 1.3 Scope note (verbatim)
> When the upload endpoint lands (follow-up), it must enforce **≤1.9 MB per file** (3 min at 64 kbps + margin), **reject stereo at parse time**, and **reject sample rates ≠ 44.1 kHz**. Until then, the asset is deployed via the harness or by manual file copy.

### 1.4 Format & budget arithmetic
- Asset: MP3, **44.1 kHz, mono, ~64 kbps**. Path `/storage/adhan.mp3`.
- `storage` = `0x7e0000` = 8,257,536 B ≈ **7.875 MiB**.
- Five 3-minute files at 64 kbps mono = 5 × 180 s × 8,000 B/s = **7,200,000 B (7.2 MB)** → **~1.0 MiB headroom** for the timetable. Fits; no resampling (D2).
- v1 ships **one** asset; the 5-file figure is the future per-prayer budget.

## 2. Event-band allocation (corrected)

| Value | Enumerator | Owner | Status |
|---|---|---|---|
| 96–111 | reserved prayer/audio | `event_bus.h` | band |
| 96 | `EVENT_PRAYER_TIMES_UPDATED` | prayer | existing |
| 97 | `EVENT_PRAYER_TIME_REACHED` | prayer | existing (consumed by `svc_audio`) |
| 98 | `EVENT_AUDIO_STARTED` | `svc_audio` | **new in this PR** |
| 99 | `EVENT_AUDIO_COMPLETED` | `svc_audio` | **new in this PR** |
| 100 | `EVENT_AUDIO_ERROR` | `svc_audio` | **new in this PR** |
| 112+ | `svc_http` / `svc_ota` | — | **must not be used by `svc_audio`** |

**Collision rule (B1):** 98/99/100 must not collide with any existing enumerator; check the header at edit time. (96 is already taken by `EVENT_PRAYER_TIMES_UPDATED`; the earlier "do not reuse 96" phrasing is withdrawn as confusing.)

### 2.1 Payloads (H1)
`event_t.data` is 32 B. Each payload gets a `_Static_assert(sizeof(payload) <= sizeof(((event_t*)0)->data), ...)` in `event_bus.h`, matching the existing assert on `network_event_payload_u` (confirm the exact field name and assert style against the header at edit time).

```c
/* 98 STARTED / 99 COMPLETED — identical shape, 16 B (NOT 12 B: int64_t forces 8-byte alignment). */
typedef struct {
    uint8_t prayer_id;
    uint8_t mode;          /* 1=FULL, 2=SHORT (0=OFF never starts) */
    uint8_t _reserved[6];  /* explicit pad: 2 + 6 = 8, then i64 at offset 8 */
    int64_t scheduled_epoch; /* original scheduler epoch, for correlation with prayer_time_event_t */
} audio_play_payload_t;    /* sizeof == 16 */

/* 100 ERROR — 8 B. `partial` carries B2's partial-playback indicator. */
typedef struct {
    uint8_t prayer_id;
    uint8_t stage;   /* 0=OPEN,1=READ,2=DECODE,3=HAL_INIT,4=HAL_WRITE (parallels svc_network_stage_t) */
    uint8_t partial; /* 1 if playback had started before the failure, else 0 */
    uint8_t _reserved;
    int32_t err;     /* esp_err_t / HAL status */
} audio_error_payload_t; /* sizeof == 8 */
```
`stage=OPEN` is defined for parity but **never emitted in v1**: open-time failures are class 1 (log+skip, no event, §3.2).

## 3. Playback contract, modes, and failure classes

### 3.1 Modes and sampling
- Trigger: `EVENT_PRAYER_TIME_REACHED` with `kind == PRAYER_KIND_ADHAN` only; other kinds ignored (M2).
- OFF → no playback, no I2S init. FULL → whole file.
- **SHORT (M1):** plays **at most `SVC_AUDIO_SHORT_DURATION_S` (10 s)** of the asset, or the whole asset if shorter. For a 10-second asset, SHORT and FULL are indistinguishable. Rationale: the enum's "~5–10 s sample" is ambiguous; truncation gives one asset, one path, one future cap. 5 s is not recognisable; 15 s exceeds the band. A mosque wanting a dedicated short clip replaces `adhan.mp3`; the mode distinction then becomes cosmetic — accepted degradation. Per-prayer short content is v2.
- **Volume (H3):** sampled **once at playback start**; changes made during playback take effect at the **next** playback. Do not re-read volume inside the decode loop (would add a race for no benefit).
- `adhan_mode` read live per playback (§5-I1).

### 3.2 Failure classes (B2, M3) — three classes, each handled distinctly

| # | Condition | stage | partial | Event | Cleanup |
|---|---|---|---|---|---|
| 1 | File missing / unreadable **at open** | — | — | **none** (log + skip; a mosque with no adhan configured is not a fault) | none (I2S not yet initialised) |
| 2a | Opens, but minimp3 finds **no first sync frame** (zero-byte / corrupt) (M3) | DECODE | 0 | `EVENT_AUDIO_ERROR` + abort | deinit (init succeeded) |
| 2b | Mid-playback `fread()` returns 0 (FATFS / wear-leveling) | READ | 1 | `EVENT_AUDIO_ERROR` + abort | deinit |
| 2c | Mid-playback decode fails | DECODE | 1 | `EVENT_AUDIO_ERROR` + abort | deinit |
| 3a | `HAL_Audio_Init()` returns non-OK at playback request | HAL_INIT | 0 | `EVENT_AUDIO_ERROR`, skip playback, remain idle | **NO deinit** (init failed; nothing to deinit) |
| 3b | `audio_play_pcm()` returns false mid-playback (I2S write timeout / DMA stall) | HAL_WRITE | 1 | `EVENT_AUDIO_ERROR` + abort | deinit |

Rules that survive refactoring:
- **Deinit is called if and only if init succeeded**, and always on the `svc_audio` task before returning to idle (§4.5, §5-I2). The tempting bug — deinit on a *failed* init (3a) — is explicitly forbidden.
- Class 2 and 3b: **no retry, no second file, no user toast.** Log ERROR, publish, abort, deinit, idle.
- Class 1 emits no event; classes 2/3 always emit `EVENT_AUDIO_ERROR` with the stage/partial above.

## 4. Task & lifecycle model

### 4.1 Single owner, subscription, lazy I2S
One `svc_audio` task. It subscribes with `event_bus_subscribe(q, EVENT_PRAYER_TIME_REACHED)` to a queue **it owns**; it never drains the kernel queue (M2). Immediately before `audio_play_pcm()` it calls `HAL_Audio_Init()`; immediately after `audio_play_pcm()` returns it calls `HAL_Audio_Deinit()`. No other task calls either. No idle I2S instance; the Play-vs-Stop/Deinit race is unreachable by construction.

### 4.2 Duplicate events
A second adhan request while playback is active is **logged and dropped** before any stop decision. No pre-emption in v1.

### 4.3 Insertion point, rollback rationale, and return semantics (H2)
Start `svc_audio` **after `svc_http_start()` on its success path, before `svc_scheduler_init()`**.
> `svc_http_start()` failure already triggers rollback-reboot (CR #3 fix in `main.c`), so the failure path never reaches `svc_audio_start()`. `svc_audio` therefore needs no rollback-gate logic of its own.

**Return contract:** `svc_audio_start()` returns `ESP_OK` on successful task creation. Missing file, missing config, and decoder-allocation failure are handled at **playback** time (§3.2) and do **not** fail start. The service is **non-fatal**: if `svc_audio_start()` returns error, `main.c` logs and continues, exactly as for `svc_scheduler_start()`. The rollback gate exists only for services whose absence breaks recovery (CR #3's `http_err` check); `svc_audio` is not one of them.

### 4.4 Factory reset during playback
Factory reset reboots the device, resetting the I2S peripheral. No explicit stop required (D4).

### 4.5 I2S lifecycle discipline (B2)
`HAL_Audio_Deinit()` runs **only** when a prior `HAL_Audio_Init()` succeeded, on the `svc_audio` task, before returning to idle. On init failure (3a) the service publishes `EVENT_AUDIO_ERROR` and stays idle **without** deinit. This is the concrete guard against the well-meaning "deinit on failed init" bug.

## 5. Invariants that must survive refactoring

**I1 — `adhan_mode` is read on every playback, from live `svc_config`, never cached at service start.**
A mosque flipping OFF→FULL mid-day must hear the next adhan. Same "read fresh" family as `adhan_volume`, but the failure mode differs (silent vs too-loud), so it is stated separately and must not be merged into a cached snapshot.

**I2 — I2S is initialised lazily on `svc_audio`'s own task at the moment playback is requested, and deinitialised on that same task after `audio_play_pcm()` returns. No other task may call `HAL_Audio_Init()` / `HAL_Audio_Deinit()`.**
This makes the Play-vs-Stop/Deinit race unreachable. It must not be "simplified" by hoisting init into `svc_audio_start()`.
*Wording reconciliation (confirmed correct in round 2):* the earlier "init inside `svc_audio_start()` only when playback is requested" phrasing is withdrawn as self-contradictory; I2 as written here is the authoritative intent.

## 6. Rejected alternatives

| Alternative | Why rejected |
|---|---|
| 22.05 kHz asset + resample to 44.1 | Storage already solved by compression; adds interface + interpolation + artifacts for unneeded margin (D2). |
| Dedicated short asset | Two assets/paths/caps; truncation simpler; cosmetic degradation acceptable (§3.1). |
| `audio_stop()` in HIL now | Dead code; defer to jamat-alert PR (D4). |
| Upload size cap in v1 | Endpoint absent; note added (§1.3, D5). |
| Hoist I2S init to service start | Reopens the race I2 closes (§5). |
| Cache `adhan_mode`/`adhan_volume` at start | Stale config; violates I1 and the volume sibling rule. |
| Deinit on failed init | Nothing to deinit; explicitly forbidden (§3.2 3a, §4.5). |
| Real-time volume re-read in decode loop | Adds a race for no benefit; volume sampled once (H3). |

## 7. Memory & task budget (verification, not estimate)
- `sizeof(mp3dec_t)` (vendored minimp3): ~25 KB is recollection. **Measure at build**, write the build's number back here. ~40 KB still fits (~130 KB free), but the doc carries the measured value (D8).
- Task stack sizes from measured high-water, not guesswork.
- ~130 KB heap-free is a budget target to confirm at build with audio allocated.

## 8. Verification checklist

Build:
- [ ] `sizeof(mp3dec_t)` measured and written into §7.
- [ ] `svc_audio` task stack high-water recorded.
- [ ] Enumerators 98/99/100 present, no collision; nothing in 112+ used by `svc_audio`.
- [ ] **Payload size check:** `_Static_assert(sizeof(payload) <= sizeof(event_t.data))` for each of the three audio payloads; build passes.

Hardware:
- [ ] OFF→FULL mid-day: next adhan heard (I1).
- [ ] Volume change mid-day applied at next playback; **no** mid-playback effect (H3).
- [ ] SHORT ≤10 s; shorter asset plays whole; 10 s asset SHORT==FULL (M1).
- [ ] Duplicate adhan: log + drop, no stop, no second playback.
- [ ] Factory reset during playback: clean reboot, I2S reset, no hang.
- [ ] No idle I2S between playbacks (lazy init/deinit).
- [ ] `svc_http_start()` failure reboots before `svc_audio_start()`.
- [ ] Non-ADHAN `prayer_time_event_t.kind` does not trigger playback (M2).

Forced-failure paths (test hooks / stubs):
- [ ] **Init-failure:** stub `HAL_Audio_Init()` → `HAL_ERR_DEV`; confirm `EVENT_AUDIO_ERROR` (stage=HAL_INIT, partial=0), **no deinit**, service idle.
- [ ] **File-corrupt:** replace `adhan.mp3` with a text file; confirm `EVENT_AUDIO_ERROR` (stage=DECODE, partial=0), no crash, no I2S left initialised.
- [ ] **Non-fatal start:** force `xTaskCreate` → `pdFAIL` via test hook; confirm boot continues, no rollback gate, clock still runs.

## 9. Line-by-line implementation review rules
1. Read `event_bus.h`, the HAL audio header (`audio_play_pcm`, `s_stop_requested`), and `svc_config` getters **before** writing code; the tree is authority.
2. **No HAL change; no `audio_stop()`; no resampling interface. One exception: `event_bus.h` gains three enumerators at 98/99/100** plus payload structs and `_Static_assert`s (B1).
3. `svc_config.h`'s `adhan_volume` header comment rewritten from "future HW only" to "software PCM scaling in `svc_audio`; 0=muted, 100=unity"; getter/setter semantics (0..100, clamped) unchanged (H4).
4. `adhan_mode` and `adhan_volume` fetched via live getters inside the playback path, not at start (I1); volume sampled once at playback start (H3).
5. `HAL_Audio_Init`/`Deinit` appear **only** on the `svc_audio` task, bracketing `audio_play_pcm`; grep shows zero other call sites (I2). Deinit only if init succeeded (§4.5).
6. Failure handling matches §3.2 exactly: class 1 no event; class 2/3 `EVENT_AUDIO_ERROR` with stage/partial; no retry/second file/toast.
7. Duplicate-event handling is log+drop *before* any stop logic.
8. Insertion per §4.3; no rollback-gate code in `svc_audio`; non-fatal start contract honoured.
9. Subscribe via `event_bus_subscribe` to the owned queue; filter `kind == PRAYER_KIND_ADHAN`; never drain the kernel queue (M2).
10. SHORT is a duration cap on the single asset; no second file path.
11. Payload structs and `_Static_assert`s match the header's existing pattern; confirm field names at edit time.
12. Any doc/code disagreement fixed in the same change.

## 10. Open items / follow-ups
- Upload endpoint + §1.3 cap (follow-up PR).
- Per-prayer files and per-prayer short assets (v2).
- `audio_stop()` HIL wrapper + adhan↔jamat pre-emption (jamat-alert PR).
- Write measured `sizeof(mp3dec_t)` and stack high-water back into §7.
