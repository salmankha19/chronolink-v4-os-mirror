# `svc_audio` — Adhan Playback Service (v1 design, rev 4)

**Status:** implemented (rev 4). Rev 3 folded the tree back into the document
after the `svc_audio` v1 PR: payload count corrected to two structs / three
events, sample-rate rejection, stereo downmix, the sample-counted SHORT cap,
the open → init → first-decode ordering, and the HIL (not HAL) call path.
Anything still marked *pending build* is a measurement, not a decision.

**Rev 4 (architect amendment v3) — decoder swap, no API change.** The decoder
is `chmorgan/esp-libhelix-mp3` ^1.0.3 (fixed-point Helix), fetched by the ESP
component manager, NOT a vendored header. Reason: minimp3's core is
floating-point (its own README says so; `MINIMP3_FLOAT_OUTPUT` only changes the
output type), Xtensa has no SSE/NEON so it runs scalar float, Espressif's
ESP32-S3 speed guide states float is always slower than integer even with the
S3's single-precision FPU, and this board runs at 160 MHz. minimp3 also puts
~16.3 KB of scratch (`mp3dec_scratch_t`) on the CALLER's stack, which forced a
24 KB task stack; libhelix keeps all of its state in the heap, so the stack is
now 6 KB. libhelix's own known failure mode — reporting zero output samples in
a loop on damaged input — is bounded in §3.3. Rev 4 also adds the volume
ceiling (AMEND-1, §3.1), the pre-deinit silence flush (AMEND-2, §4.5), the
hardware notes in §7 and the `GET /debug/audio` test hook (§8).
**Date:** 2026-11-22 (rev 2), 2026-11-25 (rev 3), 2026-11-25 (rev 4). **Author:** lead architect
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
| D8 | `sizeof(mp3dec_t)` | ~25 KB was recollection and wrong. The minimp3 numbers (~6.7 KB `mp3dec_t`, ~16.3 KB `mp3dec_scratch_t` on the **caller's stack**) were read from that vendored header — which rev 4 replaced (AM5). The decoder is now libhelix, whose state is allocated inside `MP3InitDecoder()`, so §7 logs a heap delta rather than a `sizeof`; still *pending build*. |

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

### 0.3 Round 3 — architect amendment v3 (rev 4)

| # | Item | Where landed |
|---|---|---|
| AM1 | Software volume ceiling: gain = 0.70 x (adhan_volume / 100) as one Q15 multiplier per playback, integer math only. Volume 100 is about -3.1 dBFS, not full scale | §3.1, `svc_audio.c` AUDIO_VOLUME_MAX_Q15 |
| AM2 | Pre-deinit silence flush: >= 2 full HAL DMA chunks of digital zeros through `audio_play_pcm()` before `audio_deinit()` (MAX98357A click mitigation) | §4.5, `silence_flush()` |
| AM3 | Read-and-report only: which I2S slot mono PCM occupies. **Reported: LEFT slot only** (`I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(16 bit, I2S_SLOT_MODE_MONO)` sets `.slot_mask = I2S_STD_SLOT_LEFT`), which satisfies "left or both". No code change | §4.1, §7 |
| AM4 | Hardware notes: no I2S clock removal while BCLK runs, MAX98357A tON 7-7.5 ms, BCLK-stop standby, bench vs production module | §7 |
| AM5 | Decoder swap minimp3 -> libhelix (fixed-point). Failure table extended (decoder alloc, rate, bounded recovery) | §1.1, §3.2, §3.3, §6, §7 |
| AM6 | Bounded recovery: <= 8 consecutive non-productive decodes and <= 64 KB scanned without a frame, then class 2c | §3.3 |
| AM7 | Test hook `GET /debug/audio?prayer=<id>` under `CHRONOLINK_TEST_HOOKS` | §8, `svc_http.c` |

## 1. Scope and storage contract

### 1.1 In scope (v1)
- Play `/storage/adhan.mp3` on `EVENT_PRAYER_TIME_REACHED` (97) **filtered to `prayer_time_event_t.kind == PRAYER_KIND_ADHAN`**, honouring `adhan_mode` ∈ {OFF, FULL, SHORT} and `adhan_volume`, both read **live** from `svc_config` at playback time.
- Single `svc_audio` task owns I2S for the whole playback (§4, §5-I2); subscribes via `event_bus_subscribe()` to its **own** queue (§4.1).
- Consume/emit events only within 96–111 (§2); duplicate adhan events **log + drop** before any stop decision (§4.2).
- **Header edit 1 — `components/event_bus/include/event_bus.h`:** add `EVENT_AUDIO_STARTED = 98`, `EVENT_AUDIO_COMPLETED = 99`, `EVENT_AUDIO_ERROR = 100` (the header stopped at `EVENT_PRAYER_TIME_REACHED = 97`), plus **two** payload structs covering the **three** events — `audio_play_payload_t` (16 B, shared by 98 and 99) and `audio_error_payload_t` (8 B, 100) — each with its `_Static_assert`s (§2.1). *Rev 3:* the earlier "three payload structs" was wrong; there are three events and two shapes.
- **Header edit 2 — `components/svc_config/include/svc_config.h`:** comment-only rewrite of the `adhan_volume` header comment (§9 r3); no behaviour change.
- **Decoder dependency — `chmorgan/esp-libhelix-mp3` ^1.0.3:** declared in
  `components/svc_audio/idf_component.yml`, fetched into `managed_components/`
  by the ESP component manager (git-ignored; `dependencies.lock` is committed).
  Fixed-point Helix, heap-resident state (§3.3). No vendored decoder source in
  this repository.
- **Boot change — `chronolink_hal/src/hal_init.c`:** `HAL_Init()` no longer calls `audio_init()`. I2S comes up at the first playback and goes down again afterwards (§4.1, §5-I2); `docs/boot.md` is updated in the same PR.
- **Task-stack note (rev 4):** nothing large is on the task stack any more. libhelix keeps its decoder state in the heap and the PCM / mono / compressed buffers are in the resident decode context, so the stack is **6 KB**, not 24 KB. §7 asks for a measured high-water; trim it only against that number.

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
    uint8_t partial; /* 1 iff >= 1 PCM buffer was written before the failure */
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
- **Volume (H3):** sampled **once at playback start**; changes made during playback take effect at the **next** playback. Do not re-read volume inside the decode loop (would add a race for no benefit). Scaling is done in software on the int16 samples because the MAX98357A has no digital gain.
  - **Rev 4 — ceiling (AMEND-1):** the software gain is **0.70 x (adhan_volume / 100)**, so volume 100 is about **-3.1 dBFS**, not digital full scale, and volume 0 mutes. Rationale: 0 dBFS plus the board's 12 dB analog gain demands ~14.1 dBV, while ~3.2 W into 4 ohm is ~11 dBV — digital full scale would clip. Implemented as ONE Q15 multiplier computed per playback (`AUDIO_VOLUME_MAX_Q15` = round(0.70 x 32768) = 22938, `gain = volume x 22938 / 100`), applied per sample as `(sample * gain) >> 15`. Integer math only; because the multiplier is always < 32768 no sample can leave `int16_t` range, so there is no clamp in the loop.
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

Ordering matters for this table (rev 4): **fopen -> `MP3InitDecoder()` ->
`audio_init()` -> first decode** (§4.1). Two resources are acquired, each
released only if it was acquired. Class 1 happens before anything is acquired
(so there is nothing to release and no event); a decoder-allocation failure
happens before `audio_init()` (so there is no I2S to deinit); classes 2x happen
after init succeeded (so deinit is mandatory); class 3a happens *because* init
failed (so deinit is forbidden, while the decoder that WAS acquired is freed).

| # | Condition | stage | partial | Event | Cleanup |
|---|---|---|---|---|---|
| 1 | File missing / unreadable **at open** | — | — | **none** (log + skip; a mosque with no adhan configured is not a fault) | none: no I2S, no decoder, no buffers acquired |
| 1b | `MP3InitDecoder()` returns **NULL** (libhelix allocates its own state) | DECODE | 0 | `EVENT_AUDIO_ERROR` + abort | **no deinit** (I2S is not initialised yet); nothing else held |
| 2a | Opens, but there is **no sync frame** in the first 64 KB (zero-byte / non-MP3) (M3) | DECODE | 0 | `EVENT_AUDIO_ERROR` + abort | deinit + free decoder (init succeeded) |
| 2a' | Decode-**buffer** allocation fails (first playback only; the buffers are then resident) | DECODE | 0 | `EVENT_AUDIO_ERROR` + abort | **no deinit** (buffers are allocated before `audio_init()`, so I2S is still down) |
| 2a'' | **First decoded frame reports `samprate != 44100`** (rev 3) | DECODE | 0 | `EVENT_AUDIO_ERROR` + abort | deinit + free decoder. **No resampling** (D2) |
| 2b | `fread()` fails / returns 0 **before the end of the asset** (`ferror()` set, FATFS / wear-leveling) | READ | 1 | `EVENT_AUDIO_ERROR` + abort | deinit |
| 2c | **Bounded recovery exhausted** (8 consecutive non-productive decodes, or 64 KB scanned without a frame), a hard `MP3Decode()` error that does not resync, or a `samprate != 44100` frame mid-stream | DECODE | 1 | `EVENT_AUDIO_ERROR` + abort | deinit + free decoder |
| 3a | `audio_init()` returns false at playback request (HAL `HAL_Audio_Init()` failed) | HAL_INIT | 0 | `EVENT_AUDIO_ERROR`, skip playback, remain idle | **NO deinit** (init failed; nothing to deinit) — but the decoder WAS acquired, so `MP3FreeDecoder()` runs |
| 3b | `audio_play_pcm()` returns false (I2S write timeout / DMA stall) | HAL_WRITE | 1 if a buffer had already been written, else 0 | `EVENT_AUDIO_ERROR` + abort | deinit + free decoder |

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


### 3.3 Decoder (rev 4 — libhelix, fixed point)

**Dependency:** `chmorgan/esp-libhelix-mp3` ^1.0.3, declared in
`components/svc_audio/idf_component.yml` and fulfilled by the ESP component
manager into `managed_components/` (git-ignored; `dependencies.lock` is
committed so the resolution is reproducible). The packaging is Apache-2.0; the
decoder sources it wraps (upstream `libhelix-mp3`, under `pub/` and `real/`)
carry RPSL 1.0 / RCSL 1.0 headers, portions Copyright (c) 1995-2002
RealNetworks. Nothing is relicensed and nothing is vendored here — both licence
facts are recorded for review before ship.

**Real API** (read from the component's `pub/mp3dec.h`, not from memory):

```c
#define MAINBUF_SIZE 1940   /* 511 B reservoir + 1428 slots, rounded to 1940 */
#define MAX_NGRAN    2      /* granules per frame                            */
#define MAX_NCHAN    2      /* channels                                      */
#define MAX_NSAMP    576    /* samples per channel, per granule              */

typedef void *HMP3Decoder;  /* opaque; state is inside, on the heap */

HMP3Decoder MP3InitDecoder(void);
void        MP3FreeDecoder(HMP3Decoder hMP3Decoder);
int         MP3Decode(HMP3Decoder hMP3Decoder, const unsigned char **inbuf,
                      size_t *bytesLeft, short *outbuf, int useSize);
void        MP3GetLastFrameInfo(HMP3Decoder hMP3Decoder, MP3FrameInfo *info);
int         MP3FindSyncWord(const unsigned char *buf, int nBytes);

enum {                          /* raw return values of MP3Decode() */
    ERR_MP3_NONE                 =     0,
    ERR_MP3_INDATA_UNDERFLOW     =    -1,
    ERR_MP3_MAINDATA_UNDERFLOW   =    -2,
    ERR_MP3_FREE_BITRATE_SYNC    =    -3,
    ERR_MP3_OUT_OF_MEMORY        =    -4,
    ERR_MP3_NULL_POINTER         =    -5,
    ERR_MP3_INVALID_FRAMEHEADER  =    -6,
    ERR_MP3_INVALID_SIDEINFO     =    -7,
    ERR_MP3_INVALID_SCALEFACT    =    -8,
    ERR_MP3_INVALID_HUFFCODES    =    -9,
    ERR_MP3_INVALID_DEQUANTIZE   =   -10,
    ERR_MP3_INVALID_IMDCT        =   -11,
    ERR_MP3_INVALID_SUBBAND      =   -12,
    ERR_UNKNOWN                  = -9999,
};
```

`MP3Decode()` consumes input by advancing `*inbuf` and decrementing
`*bytesLeft` — the frame length is `avail - *bytesLeft` afterwards. `useSize = 0`
tells it the output buffer is the full `MAX_NSAMP * MAX_NGRAN * MAX_NCHAN`
shorts (2304 shorts / 4608 B). `MP3FrameInfo` reports `bitrate`, `nChans`,
`samprate`, `bitsPerSample`, `outputSamps` (interleaved total), `layer`,
`version`.

**Decode seam:** every libhelix symbol in `svc_audio.c` lives in three static
helpers — `dec_open()` (`MP3InitDecoder`), `dec_frame()` (`MP3FindSyncWord` +
`MP3Decode` + `MP3GetLastFrameInfo`, one frame) and `dec_close()`
(`MP3FreeDecoder`). No public API, HAL or HIL change.

**Buffers (all heap):**

| Item | Value | Where it lives |
|---|---|---|
| Compressed read buffer | 8192 B | resident decode context |
| `AUDIO_MIN_DECODE_BYTES` | `MAINBUF_SIZE` = 1940 B | must be queued before a decode, unless the file has ended |
| PCM output | 2304 shorts = 4608 B | resident decode context |
| Mono downmix | 1152 shorts = 2304 B | resident decode context |
| Decoder state | allocated inside `MP3InitDecoder()`, logged as a heap delta | per playback, freed with `MP3FreeDecoder()` |

Nothing large is on the task stack, which is why it is **6 KB** (§7 asks for a
measured high-water). The decoder object is created per playback, so no bit
reservoir, free-format bitrate or Huffman state survives into the next adhan;
the buffers stay resident so a 15 KB malloc/free cycle never runs at adhan time.

**Chunked reading:** `memmove` the unconsumed residual (which libhelix reports
by advancing the pointer) to the front of the buffer and top it up with
`fread()`, keeping at least `AUDIO_MIN_DECODE_BYTES` available before each
`MP3Decode()` call. Near EOF the rule is relaxed so a complete final frame still
plays; a tail that is not a complete frame is logged and ignored, not an error.

**Sync / ID3 recovery:** `MP3FindSyncWord()` runs before every decode, per the
component README. ID3 data routinely contains `0xFF 0xFB`; a false sync there
makes `MP3Decode()` return an error rather than the frame, so the loop skips to
the next real sync word and retries instead of treating the tag as end of stream
or as a fatal error. With no sync in the window, all but the last 3 bytes are
discarded (a sync word can straddle the read boundary).

**Underflow is not failure:** `ERR_MP3_INDATA_UNDERFLOW`,
`ERR_MP3_MAINDATA_UNDERFLOW` (normal while the bit reservoir fills over the
first frames) and `ERR_MP3_FREE_BITRATE_SYNC` all mean "supply more data".

**Bounded recovery (mandatory, AM6).** libhelix is known to report zero decoded
samples indefinitely on damaged input, which would hang the audio task. Two
counters, either of which aborts as class 2c: **8 consecutive non-productive
decode attempts** (nothing consumed) and **64 KB scanned without producing a
frame**. A successful frame resets both. A byte-skip that makes progress resets
the stall counter but keeps counting against the byte budget, so an ID3 tag is
skipped while a stream that never yields a frame still terminates -- but only
up to that budget: a tag **larger than 64 KB** (an embedded cover image is the
usual cause) is class 2c, not skipped. The fix is on the asset (re-save it
without large embedded art) until a follow-up parses the `ID3` header and
`fseek()`s past it (§10).

**Instrumentation:** free heap before/after `MP3InitDecoder()` (the delta is
the decoder's real cost), per-frame decode time via `esp_timer_get_time()` with
mean and max logged at the end of playback, and the stack high-water after each
playback. Budget: 44100 / 1152 = ~38.3 frames/s, so ~26 ms is available per
frame.

## 4. Task & lifecycle model

### 4.1 Single owner, subscription, lazy I2S

One `svc_audio` task (`"svc_audio"`, 6 KB stack, `tskIDLE_PRIORITY + 3`, core
0). It subscribes with `event_bus_subscribe(q, EVENT_PRAYER_TIME_REACHED)` to a
queue **it owns** (length 4); it never drains the kernel queue (M2).

**Exact ordering per request (rev 4 — §3.2's failure table depends on it):**

1. `svc_config_get()` -> `adhan_mode` (live) + `adhan_volume` (sampled once).
   OFF, or an unusable snapshot, returns here: **no I2S, no event**.
2. `fopen("/storage/adhan.mp3")` — class 1 on failure: nothing acquired, no
   event.
3. Resident decode buffers (first adhan only), then `MP3InitDecoder()` —
   classes 2a' / 1b. Nothing I2S-related has happened, so a failure here needs
   no deinit.
4. `audio_init()` — the **first** call that touches I2S. Class 3a on failure:
   **no deinit**, and the decoder is freed.
5. First decode and the `samprate == 44100` check: classes 2a / 2a''. From this
   point both the deinit and the decoder free are mandatory on every exit.
6. `EVENT_AUDIO_STARTED`, then the decode/write loop.
7. Silence flush, then `audio_deinit()` on this task, then `MP3FreeDecoder()`,
   then `fclose()`.
8. `EVENT_AUDIO_COMPLETED` on a normal end (an error path published
   `EVENT_AUDIO_ERROR` at the failing step, before this shutdown sequence).

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

**I2S slot report (AM3 — read-and-report, no code change).**
`HAL_Audio_Init()` configures the channel with
`I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
I2S_SLOT_MODE_MONO)`, and in the IDF header that macro sets
`.slot_mask = (mono_or_stereo == I2S_SLOT_MODE_MONO) ? I2S_STD_SLOT_LEFT :
I2S_STD_SLOT_BOTH`. Mono PCM therefore occupies the **LEFT slot only**, which
meets the requirement ("left slot or both") — not a right-slot-only blocking
finding. Two consequences are recorded in §7: the amplifier is strapped SD_MODE
high (its (L+R)/2 mode), so a left-only stream plays about 6 dB down, and the
§3.1 volume ceiling already assumes that headroom.

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
path after step 4 of §4.1 deinits exactly once, on that task, before the next
`xQueueReceive()`.

**Two resources, two pairs (rev 4).** The decoder is a second resource acquired
per playback, and it follows the same discipline one layer down: it is freed if
and only if `MP3InitDecoder()` returned non-NULL, on the `svc_audio` task, before
idle. That is why 3a (init failed) still frees the decoder while skipping the
deinit, and why a decoder-allocation failure frees nothing.

**Silence flush before deinit (AMEND-2).** Before `audio_deinit()`,
`silence_flush()` writes at least two full HAL DMA chunks of digital zeros
(`AUDIO_SILENCE_CHUNK` = 1024 samples, `AUDIO_SILENCE_CHUNKS` = 2, i.e. >= 2048
samples) through `audio_play_pcm()`. The MAX98357A datasheet recommends ramping
SDIN to zero before power-down; this is the v1 click/pop mitigation, and it is
also why the deinit-order hazard in §7 is tolerable. A failed flush write is
logged and does not stop the shutdown — the deinit is what actually silences the
amplifier.

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
| `minimp3` (rev 3, replaced in rev 4) | Float core, no Xtensa SIMD, and ~16.3 KB of caller-stack scratch that forced a 24 KB task stack. Replaced by the fixed-point `chmorgan/esp-libhelix-mp3` (§3.3). |
| `minimp3_ex.h` / `mp3dec_ex_read()` (rev 3) | The `ex` layer brings file handling and a second buffer model into a component that only needs frame-at-a-time decoding; 8 KB of manual chunk bookkeeping is smaller and keeps all allocation visible in `svc_audio`. |

## 7. Memory & task budget (verification, not estimate)

**Status: pending build — to be filled after flash.** Nothing in this section
is a measurement yet; the numbers below are what the code *asks for*, not what
the device reported.

| Item | Asked for by the code | Measured on hardware |
|---|---|---|
| Decoder state (allocated inside `MP3InitDecoder()`) | heap delta, logged as `decoder opened: free heap A -> B (delta N bytes)` — replaces the old `sizeof(mp3dec_t)` row, because libhelix's state is opaque | *pending build — to be filled after flash* |
| Decode buffers (`audio_decode_ctx_t`) | 8192 B read + 4608 B PCM + 2304 B mono = **15104 B**, logged as `decode buffers: ... free heap A -> B` on the first adhan | *pending build — to be filled after flash* |
| Task stack | **6144 B** requested; no decoder scratch on this stack (design AMEND-5) | *pending build — to be filled after flash* — read `stack high-water: N bytes free of 6144` after the first playback and trim only if it shows real headroom |
| Per-frame decode cost | mean and max logged at the end of every playback (`mean N us, max N us`); the real-time budget is 44100 / 1152 = ~38.3 frames/s, i.e. **~26000 us per frame** | *pending build — to be filled after flash* |
| Free heap at playback start | logged as `free_heap=` on every `%s adhan:` line | *pending build — to be filled after flash*; the ~130 KB target is confirmed or refuted here |

Fill this table from the serial log of the first successful adhan, not from a
recollection, then delete this paragraph.

### 7.1 Hardware notes (AM4)

**(a) Deinit order — do not remove LRCLK while BCLK is present.** The HAL's
`HAL_Audio_Deinit()` disables and deletes the I2S channel, which stops both
clocks together; v1 mitigates any ambiguity by writing the silence flush first
(§4.5) so the amp's SDIN is already zero when the clocks stop. The architect
amendment also cites 100 K pull-downs **R24/R25 on I2S_BCLK / I2S_LRCK** —
those designators do not appear anywhere in this repository, so treat them as
**unverified** until checked against the schematic. What the tree does document
(`components/chronolink_hal/include/pdl_pins.h`) is **R15 (10 kR)** pulling
SD_MODE low when GPIO1 is not driven. The structural fix is SD_MODE gating,
deferred to the jamat-alert PR.

**(b) MAX98357A tON is 7–7.5 ms.** Any future SD_MODE gating must wait **at
least 10 ms** after releasing shutdown before starting BCLK/LRCLK, or the first
milliseconds of audio are lost.

**(c) BCLK stop puts the amp into automatic standby** with high-Z outputs, so
there is no idle hiss by construction — a property of the lazy init/deinit
design, not an accident. Standby current 340–400 uA versus 0.6 uA in shutdown
is acceptable on a mains-powered clock, which is why v1 does not need SD_MODE
gating for power reasons.

**(d) Module variants.** The bench board is an **ESP32-S3-WROOM-1-N16R8**; the
production BOM is **N16R2**. PSRAM stays disabled in `sdkconfig.defaults`, so
one image runs on both — see `docs/hardware.md`.

**(e) SD_MODE strap and level.** R21 (0R) holds SD_MODE high on the shipped
PCB, i.e. the amplifier runs in its (L+R)/2 mode. Combined with the left-slot-
only mono stream (AM3) that is roughly 6 dB of headroom already lost before the
§3.1 ceiling — which is exactly why the ceiling exists. Firmware SD_MODE control
needs the JP2 rework described in `pdl_pins.h` and `docs/hardware.md`.

## 8. Verification checklist

Build:
- [ ] Decoder heap delta and decode-buffer size measured and written into §7.
- [ ] Per-frame decode mean/max written into §7 (budget ~26000 us/frame).
- [ ] libhelix resolves through `idf_component.yml` and `dependencies.lock` records the 1.0.3 line.
- [ ] The vendored decoder directory is gone: `git ls-files components` lists no
      `minimp3` directory (git history keeps it).
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
- [ ] Mono stream occupies the LEFT I2S slot only (AM3, §4.1) and is audible.
- [ ] Silence flush: the last 2048+ samples before deinit are zeros; no click/pop at power-down.
- [ ] `GET /debug/audio?prayer=0` (test env only) starts an adhan without waiting for a prayer time; `prayer=5` and a non-numeric value return 400.
- [ ] Production ELF contains no `debug/audio` string (`strings firmware.elf | findstr debug/audio`).
- [ ] Factory reset during playback: clean reboot, I2S reset, no hang.
- [ ] No idle I2S between playbacks (lazy init/deinit).
- [ ] `svc_http_start()` failure reboots before `svc_audio_start()`.
- [ ] Non-ADHAN `prayer_time_event_t.kind` does not trigger playback (M2).
- [ ] Stereo asset: downmixed to mono and plays (rev 3, §3.1).
- [ ] 48 kHz asset (or any rate ≠ 44.1 kHz): `EVENT_AUDIO_ERROR` with stage=DECODE, partial=0, no resampling, no I2S left initialised (rev 3, §3.2 2a'').
- [ ] `adhan_mode` OFF: no `MAX98357A I2S ready` line at all — the device is never touched.

Forced-failure paths (test hooks / stubs). Trigger them on demand with
`GET /debug/audio?prayer=<0..4>` in an `[env:single_mcu_test]` build — it
publishes a synthetic `EVENT_PRAYER_TIME_REACHED` with
`kind = PRAYER_KIND_ADHAN` (unauthenticated on purpose, closed LAN, absent from
the production image). Nothing else can fire that event on demand: svc_scheduler
gates it to once per prayer per day.
- [ ] **Init-failure:** stub `HAL_Audio_Init()` → `HAL_ERR_DEV`; confirm `EVENT_AUDIO_ERROR` (stage=HAL_INIT, partial=0), **no deinit**, service idle.
- [ ] **File-corrupt:** replace `adhan.mp3` with a text file; confirm `EVENT_AUDIO_ERROR` (stage=DECODE, partial=0), no crash, no I2S left initialised.
- [ ] **File-absent (class 1):** remove `adhan.mp3`; confirm a WARN in the log and **no event at all** (`EVENT_AUDIO_ERROR` count unchanged), no I2S init.
- [ ] **Non-fatal start:** force `xTaskCreate` → `pdFAIL` via test hook; confirm boot continues, no rollback gate, clock still runs.

## 9. Line-by-line implementation review rules
1. Read `event_bus.h`, the HIL audio header (`components/hil/include/audio_api.h` — `audio_init`, `audio_play_pcm`, `audio_deinit`; `s_stop_requested` lives in the HAL and is untouched), and the `svc_config` getters **before** writing code; the tree is authority. *Rev 3:* `audio_play_pcm()` is a HIL function, not a HAL one — the original rule pointed at the wrong header, which is how the HAL/HIL confusion in §4.1 started.
2. **No HAL change; no `audio_stop()`; no resampling interface.** Permitted edits: `event_bus.h` gains three enumerators at 98/99/100 plus **two** payload structs and four `_Static_assert`s (B1), `svc_config.h` comment-only (H4), `hal_init.c` loses the eager `HAL_Audio_Init()` call (§4.1 rev 3), the decoder is the managed component `chmorgan/esp-libhelix-mp3` (§3.3, nothing
vendored), and the gated `GET /debug/audio` hook is added in `svc_http.c`.
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
- **ID3v2 tag larger than 64 KB** (embedded cover art): aborts as class 2c
  because it exceeds the AM6 scan budget (§3.3). Documented, not fixed: the
  operator re-saves the asset without the image. A follow-up may parse the
  `ID3` header at open and `fseek()` past it before decoding starts.
- Per-prayer files and per-prayer short assets (v2).
- `audio_stop()` HIL wrapper + adhan↔jamat pre-emption (jamat-alert PR).
- Fill §7 from the first flash: decoder heap delta, decode-buffer size, stack
  high-water, per-frame decode mean/max, free heap at playback start. Trim the
  task stack only against a measured number.
- **Licence review before ship:** `chmorgan/esp-libhelix-mp3` packaging is
  Apache-2.0, the decoder sources it wraps are RPSL 1.0 / RCSL 1.0
  (RealNetworks). Recorded in §3.3 and in `idf_component.yml`; needs a human
  decision.
- SD_MODE gating for the jamat-alert PR: JP2 bridge 2-3, `PDL_PIN_AUDIO_SD_MODE`
  1, >= 10 ms after release before clocks (AM4 b). See `docs/hardware.md`.
- Unverified hardware claim: R24/R25 100 K pull-downs on I2S_BCLK/I2S_LRCK are
  cited by the amendment but appear nowhere in the tree — check the schematic.
- Consider a `svc_audio` health surface for `svc_http` (`GET /api/audio`) once `EVENT_AUDIO_ERROR` has a consumer.
