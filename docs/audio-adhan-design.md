# `svc_audio` — Adhan Playback Service (v1 design, rev 3)

**Status:** implemented. Rev 3 folds the tree back into the document after the
`svc_audio` v1 PR: payload count corrected to two structs / three events,
sample-rate rejection, stereo downmix, the sample-counted SHORT cap, the
open → init → first-decode ordering, and the HIL (not HAL) call path.
Anything still marked *pending build* is a measurement, not a decision.
**Date:** 2026-11-22 (rev 2), 2026-11-25 (rev 3). **Author:** lead architect
(review dispositions incorporated).
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
| D8 | `sizeof(mp3dec_t)` | ~25 KB was recollection and wrong. From the vendored header (§3.3): `mp3dec_t` ≈ 6.7 KB, while `mp3dec_scratch_t` ≈ 16.3 KB lands on the **caller's stack**. The on-device value is logged at start and still pending build (§7). |

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
- **Header edit 1 — `components/event_bus/include/event_bus.h`:** add `EVENT_AUDIO_STARTED = 98`, `EVENT_AUDIO_COMPLETED = 99`, `EVENT_AUDIO_ERROR = 100` (the header stopped at `EVENT_PRAYER_TIME_REACHED = 97`), plus **two** payload structs covering the **three** events — `audio_play_payload_t` (16 B, shared by 98 and 99) and `audio_error_payload_t` (8 B, 100) — each with its `_Static_assert`s (§2.1). *Rev 3:* the earlier "three payload structs" was wrong; there are three events and two shapes.
- **Header edit 2 — `components/svc_config/include/svc_config.h`:** comment-only rewrite of the `adhan_volume` header comment (§9 r3); no behaviour change.
- **New component `components/minimp3`:** vendored, unmodified upstream `minimp3.h` plus the single translation unit that defines `MINIMP3_IMPLEMENTATION` (§3.3).
- **Boot change — `chronolink_hal/src/hal_init.c`:** `HAL_Init()` no longer calls `audio_init()`. I2S comes up at the first playback and goes down again afterwards (§4.1, §5-I2); `docs/boot.md` is updated in the same PR.
- **Task-stack note:** minimp3 puts `mp3dec_scratch_t` (~16.3 KB) on the *caller's* stack, which is why the task stack is 24 KB and why §7 asks for a measured high-water.

### 1.2 Out of scope (v1) — follow-ups
- Timetable-upload endpoint for audio assets (and its size cap, §1.3).
- Per-prayer files (Fajr differs) and per-prayer short assets.
- `audio_stop()` HIL wrapper and adhan↔jamat pre-emption (jamat-alert PR).

### 1.3 Scope note (verbatim)
> When the upload endpoint lands (follow-up), it must enforce **≤1.9 MB per file** (3 min at 64 kbps + margin), **reject stereo at parse time**, and **reject sample rates ≠ 44.1 kHz**. Until then, the asset is deployed via the harness or by manual file copy.

**How the asset gets there today (rev 3):** there is no `data/` staging
directory in the repo and no audio upload endpoint, so the working options are

1. `pio run -e single_mcu -t uploadfs` from a `data/` directory you create
   containing `adhan.mp3` (and, if you want to keep it, the current
   `/storage/timetable.json` — `uploadfs` writes the whole FATFS image);
2. a temporary bench harness that copies the file over the existing HTTP
   plumbing.

Until the file exists, the service is silent by design: class 1 logs
`/storage/adhan.mp3 is absent or unreadable -- adhan skipped` and emits no
event.


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

The payload lives in `event_t.data`, a `uint8_t[sizeof(boot_event_payload_u)]`
(32 B). The header's existing assert style is:

```c
_Static_assert(sizeof(((event_t*)0)->data) >= sizeof(boot_event_payload_u),
               "event_t.data too small for boot_event_payload_u");
```

The audio payloads follow that pattern exactly (same field access expression,
same message shape) and additionally pin the exact sizes:

```c
/* 98 STARTED / 99 COMPLETED -- identical shape, 16 B (NOT 12 B: int64_t
   forces 8-byte alignment). */
typedef struct {
    uint8_t prayer_id;
    uint8_t mode;            /* 1=FULL, 2=SHORT (0=OFF never starts) */
    uint8_t _reserved[6];    /* explicit pad: 2 + 6 = 8, then i64 at offset 8 */
    int64_t scheduled_epoch; /* original scheduler epoch, for correlation
                                with prayer_time_event_t */
} audio_play_payload_t;      /* sizeof == 16 */

/* 100 ERROR -- 8 B. `partial` carries B2's partial-playback indicator. */
typedef struct {
    uint8_t prayer_id;
    uint8_t stage;   /* 0=OPEN,1=READ,2=DECODE,3=HAL_INIT,4=HAL_WRITE
                        (SVC_AUDIO_STAGE_* in svc_audio.h) */
    uint8_t partial; /* 1 if playback had started before the failure, else 0 */
    uint8_t _reserved;
    int32_t err;     /* SVC_AUDIO_ERR_* / HAL status, see svc_audio.h */
} audio_error_payload_t;     /* sizeof == 8 */
```

**Two structs, three events** — rev 3 correction: 98 and 99 are identical
shapes and share `audio_play_payload_t`; only 100 is distinct. Four
`_Static_assert`s: `<= sizeof(event_t.data)` for each struct (the header's
own pattern) and `== 16` / `== 8` for the exact sizes.

`stage=OPEN` is defined for parity but **never emitted in v1**: open-time failures are class 1 (log+skip, no event, §3.2).

`err` carries `SVC_AUDIO_ERR_*` (negative, `svc_audio.h`) because the HIL
audio adapter returns `bool`: the granular HAL status is visible only in the
HAL/HIL log line.

## 3. Playback contract, modes, and failure classes

### 3.1 Modes, sampling, and channel handling

- Trigger: `EVENT_PRAYER_TIME_REACHED` with `kind == PRAYER_KIND_ADHAN` only; other kinds are logged and dropped (M2).
- OFF → no playback, no I2S init. FULL → whole file.
- **SHORT (M1):** plays **at most `SVC_AUDIO_SHORT_DURATION_S` (10 s)** of the asset, or the whole asset if shorter. For a 10-second asset, SHORT and FULL are indistinguishable. Rationale: the enum's "~5–10 s sample" is ambiguous; truncation gives one asset, one path, one future cap. 5 s is not recognisable; 15 s exceeds the band. A mosque wanting a dedicated short clip replaces `adhan.mp3`; the mode distinction then becomes cosmetic — accepted degradation. Per-prayer short content is v2.
  - **Rev 3 — how the cap is counted:** in **decoded samples**, i.e.
    `played >= SVC_AUDIO_SAMPLE_RATE_HZ * SVC_AUDIO_SHORT_DURATION_S`
    (441 000 mono samples), never in wall-clock time, so a slow decode still
    yields exactly 10 s of audio. The last buffer is truncated to land on the
    cap. **Reaching the cap is a normal completion** (`EVENT_AUDIO_COMPLETED`),
    never an error.
- **Volume (H3):** sampled **once at playback start**; changes made during playback take effect at the **next** playback. Do not re-read volume inside the decode loop (would add a race for no benefit). Scaling is done in software on the int16 samples (0 = muted, 100 = unity) because the MAX98357A has no digital gain.
- **Stereo (rev 3):** a stereo stream is **downmixed to mono by averaging L/R**
  before scaling, because the I2S channel is mono. Upload-time stereo
  rejection (§1.3) belongs to the deferred endpoint PR; until then a stereo
  asset must play, not fail.
- `adhan_mode` read live per playback (§5-I1). If `svc_config_get()` itself
  fails, the request is logged and skipped with **no event**: playing at an
  unknown volume is worse than silence.
- **Where `EVENT_AUDIO_STARTED` is published (rev 3):** after
  `audio_init()` succeeds and the first chunk has been decoded, formatted and
  validated — immediately **before** the first `audio_play_pcm()` call. It is
  *not* published at open time, so a corrupt file never announces a playback
  that does not happen.


### 3.2 Failure classes (B2, M3) — three classes, each handled distinctly

Ordering matters for this table: **open → init → first decode** (§4.1). Class 1
happens before `audio_init()` (so nothing to deinit); classes 2x happen after
init succeeded (so deinit is mandatory); class 3a happens *because* init
failed (so deinit is forbidden).

| # | Condition | stage | partial | Event | Cleanup |
|---|---|---|---|---|---|
| 1 | File missing / unreadable **at open** | — | — | **none** (log + skip; a mosque with no adhan configured is not a fault) | none (I2S not yet initialised) |
| 2a | Opens, but minimp3 finds **no first sync frame** (zero-byte / corrupt) (M3) | DECODE | 0 | `EVENT_AUDIO_ERROR` + abort | deinit (init succeeded) |
| 2a' | Decoder-context allocation fails (first playback only; the context is then resident) | DECODE | 0 | `EVENT_AUDIO_ERROR` + abort | deinit |
| 2a'' | **First decoded frame reports `hz != 44100`** (rev 3) | DECODE | 0 | `EVENT_AUDIO_ERROR` + abort | deinit. **No resampling** (D2) |
| 2b | `fread()` fails / returns 0 **before the end of the asset** (`ferror()` set, FATFS / wear-leveling) | READ | 1 | `EVENT_AUDIO_ERROR` + abort | deinit |
| 2c | Mid-playback decode fails, or a `hz != 44100` frame appears mid-stream | DECODE | 1 | `EVENT_AUDIO_ERROR` + abort | deinit |
| 3a | `audio_init()` returns false at playback request (HAL `HAL_Audio_Init()` failed) | HAL_INIT | 0 | `EVENT_AUDIO_ERROR`, skip playback, remain idle | **NO deinit** (init failed; nothing to deinit) |
| 3b | `audio_play_pcm()` returns false (I2S write timeout / DMA stall) | HAL_WRITE | 1 if a buffer had already been written, else 0 | `EVENT_AUDIO_ERROR` + abort | deinit |

Rules that survive refactoring:
- **Deinit is called if and only if init succeeded**, and always on the `svc_audio` task before returning to idle (§4.5, §5-I2). The tempting bug — deinit on a *failed* init (3a) — is explicitly forbidden.
- Class 2 and 3b: **no retry, no second file, no user toast.** Log ERROR, publish, abort, deinit, idle.
- Class 1 emits no event; classes 2/3 always emit `EVENT_AUDIO_ERROR` with the stage/partial above.
- **`partial` (exact meaning, rev 3):** `1` if and only if **at least one PCM
  buffer was successfully written to the I2S channel** before the failure,
  otherwise `0`. A failure on the very first `audio_play_pcm()` therefore
  reports `partial = 0` even though `EVENT_AUDIO_STARTED` was already
  published. This is the only definition; the table's `1`s are the normal
  cases, not an exception to it.
- **`fread()` refinement (rev 3):** a regular file ends with `fread() == 0`
  and `feof()` set — that is a normal completion, not class 2b. Class 2b is
  `ferror()` (or a short read that the FATFS layer reports as an error). The
  original "fread() returns 0 mid-playback" wording could not distinguish the
  two, and treating every EOF as an error would fail every adhan.


### 3.3 Decoder (rev 3)

`components/minimp3` vendors upstream `minimp3.h` unmodified (CC0, 80 975 B,
fetched 2026-11-25) and adds one translation unit that defines
`MINIMP3_IMPLEMENTATION` plus `MINIMP3_ONLY_MP3` (Layer III only — the asset
format) and `MINIMP3_NO_SIMD` (Xtensa has none of minimp3's SIMD backends;
pinning the scalar path also keeps a host build identical in behaviour).

`MINIMP3_FLOAT_OUTPUT` is **not** defined: `audio_play_pcm()` takes `int16_t`,
and `svc_audio.c` asserts `sizeof(mp3d_sample_t) == sizeof(int16_t)` so a
future flag flip cannot silently break the call.

Sizing that the consumer depends on, read from the vendored header rather than
from recollection:

| Item | Value | Where it lives |
|---|---|---|
| `MINIMP3_MAX_SAMPLES_PER_FRAME` | `1152*2` = 2304 interleaved | `svc_audio` PCM buffer |
| `mp3dec_t` | ~6.7 KB (float MDCT/QMF state + 511 B reservoir) | heap (resident decode context) |
| `mp3dec_scratch_t` | ~16.3 KB | **caller's stack**, inside `mp3dec_decode_frame()` |

The last row is why the task stack is 24 KB rather than the 4 KB the other
services use, and why §7 asks for a measured high-water.

Chunked reading: `mp3dec_decode_frame()` only accepts a buffer that ends on a
frame boundary, so `svc_audio` decodes only while at least `AUDIO_FRAME_WINDOW`
(2304 + 4 bytes) are queued, keeps one window across a refill so a frame
straddling the read boundary is not lost, and skips leading garbage / ID3v2
bytes using the `info.frame_bytes` minimp3 reports. A read buffer that fills
without yielding a frame is a decode failure (class 2a/2c), not an infinite
loop.

## 4. Task & lifecycle model

### 4.1 Single owner, subscription, lazy I2S

One `svc_audio` task (`"svc_audio"`, 24 KB stack, `tskIDLE_PRIORITY + 3`, core
0). It subscribes with `event_bus_subscribe(q, EVENT_PRAYER_TIME_REACHED)` to a
queue **it owns** (length 4); it never drains the kernel queue (M2).

**Exact ordering per request (rev 3 — §3.2's failure table depends on it):**

1. `svc_config_get()` → `adhan_mode` (live) + `adhan_volume` (sampled once).
   OFF, or an unusable snapshot, returns here: **no I2S, no event**.
2. `fopen("/storage/adhan.mp3")` — class 1 on failure: **no I2S, no event**.
3. `audio_init()` — the **first** call that touches I2S. Class 3a on failure:
   **no deinit**.
4. First decode and the `hz == 44100` check (plus the one-off decode-context
   allocation): classes 2a / 2a' / 2a''. From this point deinit is mandatory.
5. `EVENT_AUDIO_STARTED`, then the decode/write loop.
6. `audio_deinit()` on this task, after the last `audio_play_pcm()` returns.
7. `EVENT_AUDIO_COMPLETED` on a normal end (an error path already published
   `EVENT_AUDIO_ERROR` at the failing step).

**Boot change (rev 3):** `HAL_Init()` used to call `HAL_Audio_Init()`, which
left an enabled I2S channel idle for the whole uptime — exactly what this
invariant forbids. That call is removed from
`chronolink_hal/src/hal_init.c`, so `"MAX98357A I2S ready"` now appears at the
first adhan and `"deinitialized"` after it. Nothing else in the boot path
depended on audio being up; `docs/boot.md` is updated in the same PR.

**Call path (rev 3):** the service calls `audio_init()`, `audio_play_pcm()` and
`audio_deinit()` from the HIL adapter (`components/hil/audio_api.c`), not
`HAL_Audio_*` directly, per `docs/Architecture.md` §2 rule 2 and `.clinerules`
("services use HIL, not HAL"). The HIL is a `bool`-returning pass-through, so
no I2S knowledge leaks above layer 3 and the ownership property is unchanged:
`audio_api.c` is the only HAL-level init/deinit caller in a shipping build
(the `CONFIG_AUDIO_TEST_ENABLE`-gated dev harness calls the HAL directly, and
that option is off in `sdkconfig.single_mcu`). The earlier wording here named
`HAL_Audio_Init()`/`HAL_Audio_Deinit()` for the service side; the tree says
HIL, and the tree wins.

### 4.2 Duplicate events

A second adhan request while playback is active is **logged and dropped**
before any stop decision. No pre-emption in v1.

**Mechanism (rev 3):** the task is blocked inside the playback for the whole
adhan, so it cannot observe a duplicate while it is busy. The kernel dispatcher
keeps forwarding copies into the owned queue (length 4) instead. Deterministic
handling: **after playback ends and deinit completes**, the task drains its own
queue non-blocking and logs + drops every accumulated request, then resumes the
blocking wait. A queued stale adhan is therefore **never** played (playing one
would start a second adhan minutes late). Draining happens after every handled
event, which is the same thing at idle and is the only time it matters.

### 4.3 Insertion point, rollback rationale, and return semantics (H2)
Start `svc_audio` **after `svc_http_start()` on its success path, before `svc_scheduler_init()`** — so the first `EVENT_PRAYER_TIME_REACHED` of the day already has a subscriber.
> `svc_http_start()` failure already triggers rollback-reboot (CR #3 fix in `main.c`), so the failure path never reaches `svc_audio_start()`. `svc_audio` therefore needs no rollback-gate logic of its own.

**Return contract:** `svc_audio_start()` returns `ESP_OK` on successful task creation. Missing file, missing config, and decoder-allocation failure are handled at **playback** time (§3.2) and do **not** fail start. The service is **non-fatal**: if `svc_audio_start()` returns error, `main.c` logs and continues, exactly as for `svc_scheduler_start()`. The rollback gate exists only for services whose absence breaks recovery (CR #3's `http_err` check); `svc_audio` is not one of them.

### 4.4 Factory reset during playback
Factory reset reboots the device, resetting the I2S peripheral. No explicit stop required (D4).

### 4.5 I2S lifecycle discipline (B2)
Deinit runs **only** when a prior init succeeded, on the `svc_audio` task, before returning to idle — and only after the open → init → first-decode ordering of §4.1 has placed the failure in the right class. On init failure (3a) the service publishes `EVENT_AUDIO_ERROR` and stays idle **without** deinit. This is the concrete guard against the well-meaning "deinit on failed init" bug.

The ordering is what makes the rule checkable: 3a is the only return path that
has not passed `audio_init()`, and it is the only one that skips deinit. Every
path after step 3 of §4.1 deinits exactly once, on that task, before the next
`xQueueReceive()`.

## 5. Invariants that must survive refactoring

**I1 — `adhan_mode` is read on every playback, from live `svc_config`, never cached at service start.**
A mosque flipping OFF→FULL mid-day must hear the next adhan. Same "read fresh" family as `adhan_volume`, but the failure mode differs (silent vs too-loud), so it is stated separately and must not be merged into a cached snapshot.

**I2 — I2S is initialised lazily on `svc_audio`'s own task at the moment playback is requested, and deinitialised on that same task after `audio_play_pcm()` returns. No other task may drive the audio lifecycle.**
This makes the Play-vs-Stop/Deinit race unreachable. It must not be "simplified" by hoisting init into `svc_audio_start()` or into `HAL_Init()`.
*Wording reconciliation (confirmed correct in round 2):* the earlier "init inside `svc_audio_start()` only when playback is requested" phrasing is withdrawn as self-contradictory; I2 as written here is the authoritative intent.
*Rev 3:* the calls themselves go through the HIL audio adapter (§4.1 "Call
path"), so "may call `HAL_Audio_Init()`" is satisfied by exactly one non-dev
caller (`components/hil/audio_api.c`) reached from this task.


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
| Keep `HAL_Audio_Init()` inside `HAL_Init()` (rev 3) | It was there, and it left an enabled I2S channel idle for the whole uptime — the exact state I2 forbids, with the deinit race reachable from any task. Removed from `chronolink_hal/src/hal_init.c`. |
| `svc_audio` calling `HAL_Audio_*` directly (rev 3) | Violates `docs/Architecture.md` §2 rule 2 and `.clinerules` ("services use HIL, not HAL"); the HIL adapter already exists. The service calls `audio_init` / `audio_play_pcm` / `audio_deinit`. |
| Resample a non-44.1 kHz asset (rev 3) | D2 already rejects resampling; a wrong-rate asset is a decode failure (2a''/2c) with a clear log instead of silent pitch shift. |
| `minimp3_ex.h` / `mp3dec_ex_read()` (rev 3) | The `ex` layer brings file handling and a second buffer model into a component that only needs frame-at-a-time decoding; 8 KB of manual chunk bookkeeping is smaller and keeps all allocation visible in `svc_audio`. |

## 7. Memory & task budget (verification, not estimate)

**Status: pending build — to be filled after flash.** Nothing in this section
is a measurement yet; the numbers below are what the code *asks for*, not what
the device reported.

| Item | Asked for by the code | Measured on hardware |
|---|---|---|
| `sizeof(mp3dec_t)` | ~6.7 KB from the vendored header (rev 3 corrected the earlier ~25 KB recollection); logged once at `svc_audio_start()` | *pending build — to be filled after flash* |
| Decode context (`mp3dec_t` + 2304 `int16_t` PCM + 1152 `int16_t` mono + 8 KB read buffer) | `sizeof(audio_decode_ctx_t)`, logged at start and on first allocation | *pending build — to be filled after flash* |
| Task stack | 24 KB requested (minimp3's ~16.3 KB `mp3dec_scratch_t` goes on this stack) | *pending build — to be filled after flash* — read the `stack high-water` log line after the first playback and trim only if it shows real headroom |
| Free heap at playback start | logged as `free_heap=` on every `%s adhan:` line | *pending build — to be filled after flash*; the ~130 KB target is confirmed or refuted here |

Fill this table from the serial log of the first successful adhan, not from a
recollection, then delete this paragraph.

## 8. Verification checklist

Build:
- [ ] `sizeof(mp3dec_t)` measured and written into §7.
- [ ] `svc_audio` task stack high-water recorded.
- [ ] Enumerators 98/99/100 present, no collision; nothing in 112+ used by `svc_audio`.
- [ ] **Payload size check:** `_Static_assert(sizeof(payload) <= sizeof(event_t.data))` for **each of the two** audio payloads, plus the exact-size asserts (`== 16`, `== 8`); build passes. (Rev 3: two structs, three events — the earlier "three audio payloads" counted events.)
- [ ] **Two-payload check (rev 3):** `audio_play_payload_t` is shared by `EVENT_AUDIO_STARTED` and `EVENT_AUDIO_COMPLETED`; no third struct exists.
- [ ] `HAL_Init()` logs no `MAX98357A I2S ready` at boot; the line appears at the first adhan.

Hardware:
- [ ] OFF→FULL mid-day: next adhan heard (I1).
- [ ] Volume change mid-day applied at next playback; **no** mid-playback effect (H3).
- [ ] SHORT ≤10 s; shorter asset plays whole; 10 s asset SHORT==FULL (M1).
- [ ] Duplicate adhan: log + drop, no stop, no second playback.
- [ ] Factory reset during playback: clean reboot, I2S reset, no hang.
- [ ] No idle I2S between playbacks (lazy init/deinit).
- [ ] `svc_http_start()` failure reboots before `svc_audio_start()`.
- [ ] Non-ADHAN `prayer_time_event_t.kind` does not trigger playback (M2).
- [ ] Stereo asset: downmixed to mono and plays (rev 3, §3.1).
- [ ] 48 kHz asset (or any rate ≠ 44.1 kHz): `EVENT_AUDIO_ERROR` with stage=DECODE, partial=0, no resampling, no I2S left initialised (rev 3, §3.2 2a'').
- [ ] `adhan_mode` OFF: no `MAX98357A I2S ready` line at all — the device is never touched.

Forced-failure paths (test hooks / stubs):
- [ ] **Init-failure:** stub `HAL_Audio_Init()` → `HAL_ERR_DEV`; confirm `EVENT_AUDIO_ERROR` (stage=HAL_INIT, partial=0), **no deinit**, service idle.
- [ ] **File-corrupt:** replace `adhan.mp3` with a text file; confirm `EVENT_AUDIO_ERROR` (stage=DECODE, partial=0), no crash, no I2S left initialised.
- [ ] **File-absent (class 1):** remove `adhan.mp3`; confirm a WARN in the log and **no event at all** (`EVENT_AUDIO_ERROR` count unchanged), no I2S init.
- [ ] **Non-fatal start:** force `xTaskCreate` → `pdFAIL` via test hook; confirm boot continues, no rollback gate, clock still runs.

## 9. Line-by-line implementation review rules
1. Read `event_bus.h`, the HIL audio header (`components/hil/include/audio_api.h` — `audio_init`, `audio_play_pcm`, `audio_deinit`; `s_stop_requested` lives in the HAL and is untouched), and the `svc_config` getters **before** writing code; the tree is authority. *Rev 3:* `audio_play_pcm()` is a HIL function, not a HAL one — the original rule pointed at the wrong header, which is how the HAL/HIL confusion in §4.1 started.
2. **No HAL change; no `audio_stop()`; no resampling interface.** Permitted edits: `event_bus.h` gains three enumerators at 98/99/100 plus **two** payload structs and four `_Static_assert`s (B1), `svc_config.h` comment-only (H4), `hal_init.c` loses the eager `HAL_Audio_Init()` call (§4.1 rev 3), and the vendored `minimp3` component is added.
3. `svc_config.h`'s `adhan_volume` header comment rewritten from "future HW only" to "software PCM scaling in `svc_audio`; 0=muted, 100=unity"; getter/setter semantics (0..100, clamped) unchanged (H4).
4. `adhan_mode` and `adhan_volume` fetched via live getters inside the playback path, not at start (I1); volume sampled once at playback start (H3). Concretely: one `svc_config_get()` snapshot per playback, read after the event arrives.
5. The audio init/deinit lifecycle is driven **only** from `svc_audio`'s task, bracketing the `audio_play_pcm()` loop (I2). In the tree, the HAL-level functions live behind `components/hil/audio_api.c`; nothing else in a shipping build calls them (the `CONFIG_AUDIO_TEST_ENABLE` dev harness is the one gated exception). Deinit only if init succeeded (§4.5).
6. Failure handling matches §3.2 exactly: class 1 no event; class 2/3 `EVENT_AUDIO_ERROR` with stage/partial; no retry/second file/toast.
7. Duplicate-event handling is log+drop *before* any stop logic, and a stale queued adhan is never played (§4.2).
8. Insertion per §4.3; no rollback-gate code in `svc_audio`; non-fatal start contract honoured.
9. Subscribe via `event_bus_subscribe` to the owned queue; filter `kind == PRAYER_KIND_ADHAN`; never drain the kernel queue (M2).
10. SHORT is a duration cap on the single asset, counted in decoded samples; no second file path.
11. Payload structs and `_Static_assert`s match the header's existing pattern (`sizeof(((event_t*)0)->data)`), sizes are pinned at 16 B / 8 B, and the field names are re-checked against `event_bus.h` at edit time.
12. Any doc/code disagreement fixed in the same change.

## 10. Open items / follow-ups
- Upload endpoint + §1.3 cap (follow-up PR) — it must reject stereo and non-44.1 kHz at parse time, which is now also enforced at playback time (§3.2).
- Per-prayer files and per-prayer short assets (v2).
- `audio_stop()` HIL wrapper + adhan↔jamat pre-emption (jamat-alert PR).
- Fill §7 from the first flash: `sizeof(mp3dec_t)`, decode-context size, stack high-water, free heap at playback start. Trim the task stack only against a measured number.
- Consider a `svc_audio` health surface for `svc_http` (`GET /api/audio`) once `EVENT_AUDIO_ERROR` has a consumer.
