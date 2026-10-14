# PR 6 pre-flight: live clock face (HH:MM:SS)

Status: analysis complete, implementation in progress (same PR).
Reviewed at main = dbb2a96 (2026-10-14).

---

## 0. Primary-source facts established

| Fact | Evidence |
|---|---|
| `core1_task` (core 1, prio 5, 4096 st) pumps `display_manager_process_frame()` every 16 ms | `core_os.c:162-172` |
| `display_manager_init()` is called by **`core_os_init()`** (kernel→service, documented violation) | `core_os.c:71`, Constitution §7, `Architecture.md:131` |
| `ui_producer_start()` = `display_manager_init()` + a log line, nothing else | `ui_producer.c:9-13` |
| **No caller of `display_manager_submit_frame()` / `clear_queue()` / `register_icon()` exists anywhere** (incl. `deprecated/`) | repo-wide search — the frame path has **never been executed** |
| Queue depth 3, submit mutex, 5 ms + 5 ms timeouts | `display_manager.h:29`, `display_manager.c:189-201, 225-231` |
| `DM_MAX_TEXT_LEN` = 8, `char text[8]`, copy loop bounded by 8 | `display_manager.h:27,63`, `display_manager.h:180-182` |
| `font_engine_draw_text()` walks `for (p = text; *p; ++p)` → needs a NUL | `font_engine.c:73` |
| ST7796S init returns **HAL_OK** when RDDID is all-zero (non-fatal by design) | `hal_display_st7796s.c:550-576` |
| Panel geometry 320×480 from Kconfig, `get_width/height` return it | `hal_display_st7796s.c:816-824`, `hal_display.c:469-479` |
| `HAL_Display_Show()` (ST7796S) is a no-op | `hal_display_st7796s.c:639-643` |
| Built-in font covers 0x20–0x5F (digits + `:`) glyph 8×8, id 0 | `font_builtin_8x8.c:153-157`, `font_builtin_8x8.h:6` |
| svc_time tick runs in the **worker task** (core 0, idle+3), 1000 ms queue timeout, **does not publish** | `svc_time.c:99-101, 578-580, 643-675` |
| `TIME_EVENT_SOURCE_RTC` exists but only `SOURCE_NTP` is ever published | `time_events.h:16-17`, `svc_time.c:518` |
| `ui_producer` already lists `chronolink_hal` + `font_engine` in REQUIRES but uses neither | `ui_producer/CMakeLists.txt:4`, `ui_producer.c:1-5` |
| `display_manager/README.md` says "**lock-free** queue, **8 slots × 512 bytes**" | code says mutex + depth 3 × ~324 B → stale doc |

---

## 1. Architecture decision — recommend **(b) poll**, at 250 ms, render-on-seconds-change

**Against the Constitution.** §10: "Services … communicate through the event bus." §11: "The application contains UI and display logic, uses services, and never calls the HAL directly." `ui_producer` is the *application/UI* band (it owns no hardware, runs no logic engine, and does not talk over the bus). For an application consuming a service, §11's "uses services" is the sanctioned path, and `svc_time.h:26-27` explicitly licenses it: "the getters and setters may be called from any task." The event-bus rule binds *services*, so (b) does not violate it — while (a) does not satisfy it either, because the event carries **UTC only**:

```c
/* UTC only: local (DST-adjusted) time is derived at read time from the
   POSIX TZ rule, so it is never carried as state. */
```
`time_events.h:31-33`. So design (a) still has to call `svc_time_get_local()` (or do its own `localtime_r`) to show BST. **(a) = event trigger + a getter for the value; (b) = the getter alone.** That is strictly more machinery for the same result.

**Cost comparison.** Both are 1 wakeup/s. (a) adds: an `event_t` copy (36 B) pushed twice (kernel queue 32 slots + subscriber queue), a kernel-dispatcher wakeup, a new subscriber registration, plus a semantic change to `EVENT_TIME_UPDATE` — which today means, by explicit design decision, "a new UTC value was accepted":

```c
/* Does not publish: EVENT_TIME_UPDATE means "a new UTC value was
   accepted", which a tick is not. */
```
`svc_time.c:578-579`. That decision is mirrored in `svc_time.h:15-17`, `STATUS.md:46-48` and `Architecture.md:107-110` — four documents to renegotiate. (b) adds: one `svc_time_get_snapshot()` call (mutex take + ~20 B copy) and one comparison.

**Two more (b) wins.** (i) `event_bus_subscribe()` is documented *"NOT thread-safe against `event_bus_dispatch()`"* (`event_bus.h:55-58`), and by the time `app_main` runs, `event_dispatch_task` is already dispatching (created in `core_os_start()`, which is inside `boot_manager_init()`; the 1 Hz heartbeat publishes from `core_os_init` the whole time). `svc_time_start()` already takes that risk; (a) would add a second instance for no benefit. (ii) (b) can dedupe on the rendered string, which (a) cannot do without the same comparison anyway.

**Refinement that removes (b)'s only weakness.** Tick at **250 ms** and render *only when the formatted `local` HH:MM:SS differs from the last rendered string*. That bounds display lag to <250 ms and eliminates the 1 Hz beat/aliasing between two independent 1 Hz loops (a second shown twice or skipped). Cost: 4 cheap getter calls/s. 4 Hz of `xQueueReceive`/`vTaskDelay` on a low-priority task is free.

**Switch to (a) when** a second consumer of the tick appears (prayer engine, health/logging) or when display latency <16 ms matters — and then as a *new event type* in the free `8..31` band (e.g. `EVENT_TIME_TICK`), **not** by redefining `EVENT_TIME_UPDATE`. That keeps the existing four documents true.

---

## 2. If (a) is chosen anyway: payload and flood

**Payload: no new field needed — but "second changed" is *not* implied.**

- `time_event_payload_t` is second-resolution and has no tick counter (`time_events.h:19-29`). A consumer wanting a tick id would need one; nothing would consume it. Don't add it.
- Two ways a published tick carries no change, both real:
  1. `tick_snapshot()` returns `NULL` when the RTC read fails and the state is `RTC_ONLY` → the snapshot is *unchanged* (`svc_time.c:595-608`). A naive publish would ship a duplicate second.
  2. Ticks can be **late**, never early: when an event arrives the loop `continue`s *without* ticking (`svc_time.c:644-665`), and the NTP handler can block the worker for up to `SVC_TIME_SYNC_WAIT_MS` (10 s, `svc_time.c:94`). So consecutive ticks can be >1 s apart; a consumer must not assume `+1`.
- Therefore: if (a), publish **only when `tick_snapshot()` returned non-NULL**, with the existing payload shape. `source` already distinguishes `RTC` vs `system`; `state` carries trust. No consumer-side dedupe needed beyond the string compare you'd do anyway.

**Flood: no.** 1 event/s into a 32-slot kernel queue (`core_os.c:56`) drained unconditionally by `event_dispatch_task` (`core_os.c:113-118`, `portMAX_DELAY`) needs a ~32 s outage to overflow; fan-out is a 36-byte non-blocking copy per subscriber with drop counting (`event_bus.c:124-145`). The only "flood" vector is logging: the dispatcher does `ESP_LOGD(TAG, "event type=%d", …)` per event (`core_os.c:115`). At the default level that is compiled out — but if the human's persisted `sdkconfig.single_mcu` sets a verbose level, 1 Hz becomes 1 log line/s, which is exactly the "must not spam" the PR goal forbids. Worth a one-line check in the PR notes.

---

## 3. Frame construction — and the blocker

**Blocker first: `"HH:MM:SS"` is 8 characters and `DM_MAX_TEXT_LEN` is 8 with no room for the terminator.**

```c
struct { … uint8_t font_id; char text[DM_MAX_TEXT_LEN]; } text;   /* [8]  */
…
for (int i = 0; i < DM_MAX_TEXT_LEN; i++)                          /* 8 */
    c->arg.text.text[i] = (text && text[i]) ? text[i] : '\0';
```
`display_manager.h:63,180-182`. A source string of length exactly 8 fills all 8 bytes with **no NUL**; `font_engine_draw_text()` then walks off the end of `arg.text.text` into the union padding and the *next* command in the frame until it happens to hit a zero byte (`font_engine.c:73-96`). No crash (still mapped frame memory), but undefined glyph output and an out-of-bounds read on the very first frame. This must be fixed or designed around.

Two acceptable fixes:
- **(preferred)** `DM_MAX_TEXT_LEN 16` **and** guarantee termination (`text[DM_MAX_TEXT_LEN-1] = '\0'`, or cap the loop at `DM_MAX_TEXT_LEN-1`). Struct growth: `dm_cmd_t` 20 → 28 B, `dm_frame_t` 324 → 452 B, queue 3 → 1.36 kB. Trivial.
- **(minimal)** keep 8 and split into two commands: `"HH:MM"` (5+NUL ✓) and `"SS"` (2+NUL ✓). Ugly for layout; I'd take the header change.

**Sizes, computed from the declarations** (I cannot compile or run anything here): `dm_cmd_t` = 4 B enum + 16 B union (`text` member: 2+2+2+1+8 = 15 → padded to 16) = **20 B**; `dm_frame_t` = 2 B header + 2 B pad + 16×20 = **324 B**. Queue = `xQueueCreate(3, 324)` ≈ 1.1 kB heap, allocated at `display_manager_init()` — i.e. *during `core_os_init`*, before any frame has ever existed.

**First frame — 3 commands, `flags = DM_FRAME_FLAG_NONE`, `cmd_count = 3`:**

1. `dm_cmd_clear(c, black)` — **first frame only** (a `s_first_frame` flag in the producer). Reason: `DM_CMD_CLEAR` → `HAL_GFX_FillRect(0,0,320,480,…)` → `HAL_Display_FillRect` → the chunked DMA fill: 320×480×2 = **307 kB at 20 MHz ≈ 12 ms of pure transfer plus 37 chunk setup overheads ≈ 15-20 ms**, *and* it lazily allocates the 8 kB DMA fill buffer (`ensure_fill_dma_buf`, `hal_display_st7796s.c:749`). Doing that every second is ~2 % of core 1 and pointless.
2. `dm_cmd_fill_rect(...)` over the **text bounding box** (black) — 64×8 = 512 px = 1 kB ≈ **<1 ms**. Required because `font_engine` only plots lit pixels (no transparent-background draw, `font_engine.c:86-91`); without the erase, old digits bleed through.
3. `dm_cmd_draw_text(c, x, y, white, /*font_id*/0, buf)`.

No `DM_CMD_SWAP` needed: ST7796S is a direct-write panel, `Show()` is a no-op (`hal_display_st7796s.c:639-643`), and the frame flags default to 0 (no force-swap).

**Where the text comes from.** `svc_time_snapshot_t.local` (`svc_time.h:59-64`) → `snprintf(buf, sizeof buf, "%02u:%02u:%02u", snap.local.hours, snap.local.minutes, snap.local.seconds)`. `buf` must be `char[9]` minimum (or 16 after the header fix). Note the payload's `local` is produced by `localtime_r()` under `svc_time`'s lock, so DST is already applied — the producer never touches `TZ`.

**Coordinate constants** belong in one place in `ui_producer.c` (e.g. `UI_CLOCK_X`, `UI_CLOCK_Y`) and the bbox derives from them (8 glyphs × 8 px = 64 px wide, 8 px tall). Portrait 320×480 → x=(320−64)/2=128 is the obvious centre. Don't scatter the numbers.

**Is `font_engine → HAL_GFX_DrawPixel` a boundary violation?** It is a *pre-existing, documented* one, not a new one PR 6 creates. `display_manager.c:163` already calls `font_engine_draw_text()`, and the HIL text adapter can't express this model (`display_api.c:63` renders fixed-white 8×8 with no color/font_id; `hal_display.h:165-167` states the ST7796S backend rejects `HAL_CAP_TEXT` and "a graphics/font layer should be used"). It is tracked as `Architecture.md:131-132` ("A service calls the HAL directly, bypassing HIL: `display_manager` … call `HAL_GFX_*` / `HAL_Display_*`") and `STATUS.md:266-267` (open decision 3: "allow services to use the display HAL directly, or add a HIL graphics adapter?"). PR 6 stays exactly on the existing side of that line (it adds no new HAL call), and should say so. The clean fix — a HIL `gfx_api` wrapping `HAL_GFX_*` primitives + clip — is its own PR; note it, don't smuggle it in.

---

## 4. Where `ui_producer` lives, and `REQUIRES`

**Layer: App (6), not Services.** Constitution §11 assigns UI and display logic to the application; §10 defines services as logic engines that use the HIL and talk over the event bus. `ui_producer` does none of that. **Discrepancy to fix in PR 6:** `STATUS.md:44-45` files `display_manager`, `ui_producer`, `font_engine`, `font` under "**Services:**". That sentence contradicts the Constitution's layer order (`.clinerules` layer list, App(6) = UI/display logic) and should be reclassified.

**`REQUIRES` changes (design (b)):**
- `ui_producer`: **add `svc_time`**. That brings `svc_time/include` (for `svc_time_get_snapshot`, `svc_time_snapshot_t`) and, transitively via svc_time's public `REQUIRES`, `hil` (→ `rtc_api.h` for `rtc_time_t`), `event_bus`, `freertos`, `log`. `esp_err.h` resolves through the IDF common requirements. Keep `display_manager` (brings `hal_gfx.h` for `hal_gfx_color_t`, and `chronolink_hal`). **Drop `chronolink_hal` and `font_engine`** — currently listed and unused (`ui_producer.c:1-5`); leaving them is the kind of thing Constitution §4 warns about ("do not rely on include paths to hide a missing REQUIRES" — the inverse also holds). No `esp_timer` needed: use `vTaskDelay` in the producer's own task.
- `display_manager`: **no change** (already `freertos chronolink_hal font_engine`).
- `svc_time`: **no change**.
- If design (a): additionally `event_bus`, and still `svc_time` (for the DST-correct local value — see §1).

**Task shape:** one `ui_producer` task, 3072-4096 B stack (a `dm_frame_t` is 324 B by value on the stack; `svc_time` uses 4096 as precedent), priority `tskIDLE_PRIORITY + 2` or `+3`, **pinned to core 0**, keeping core 1 for the display pump per Constitution §7 and the existing `SVC_TIME_TASK_CORE 0` / `core1_task` split.

---

## 5. Devkit behaviour with no panel — a silent no-op that *looks* successful

Chain, verified line by line:

1. `boot_manager_init()` → `HAL_Init()` (`boot_manager.c:260`) → `HAL_Display_Init()` (`hal_init.c:155`) → `hal_display_select_ops()` returns `&HAL_DISPLAY_ST7796S_OPS` (`hal_display.c:236-249`, `sdkconfig.defaults:11`).
2. `HAL_Display_ST7796S_Init()`: GPIO, hard reset, SPI bus/device, init sequence, RDDID → all-zero → `ESP_LOGW "Panel ID all zero -- SPI miswired, or no panel"` (`hal_display_st7796s.c:563`) — **non-fatal by design**, then `st7796s_initialized = true; return HAL_OK` (`:574-576`).
3. So the router ends with `s_initialized = true` and a **live ops table** (`hal_display.c:322-325`); `s_backend = HAL_DISPLAY_BACKEND_ST7796S`. It does *not* clear `s_ops` — that only happens if `Init()` returned non-OK (`hal_display.c:326-330`), in which case `HAL_Display_Init` would have propagated the failure up through `hal_init.c:156-158` and aborted the boot into the safe loop (known issue 12, `STATUS.md:197-199`). The observation that the devkit *continues* after the warning proves the ops table is live.
4. `HAL_Display_GetWidth/Height` therefore return **320/480** (`hal_display.c:469-479` → `hal_display_st7796s.c:816-824`), so `HAL_GFX_Init()` sets the clip to 320×480 (`hal_gfx.c:59-87`).
5. First frame: `core1_task` dequeues → `HAL_GFX_ResetClip()` + `HAL_Display_SetScrollStart(0)` (`display_manager.c:253-254`) → per command `dm_execute_cmd()` → `HAL_GFX_*` → `HAL_Display_*` → ST7796S ops → SPI transfers. Nothing in the data path reads MISO, so `spi_device_*_transmit` returns `ESP_OK` and the frame "succeeds" into the void.

**Answer: no-op returning HAL_OK — not `HAL_ERR_DEV`, not a crash.** `HAL_ERR_DEV` is only reachable from a NULL op (`hal_display.c:363`) or an out-of-range coordinate (`hal_display_st7796s.c:582,739`); both are checked against the *configured* 320×480, which the layout respects.

**No spam, no leak:** there is not a single log statement in the pixel/fill/text hot path. `write_text` would log "not implemented" *per call* (`hal_display_st7796s.c:648`) — the frame model has no text-via-`WriteText` command, so this is not reachable. Allocations: the one-time lazy DMA fill buffer (`ensure_fill_dma_buf`) and the router's recursive mutex; nothing per frame.

**Cost (estimate — I cannot measure without a panel and must not run `pio`):** ~15-20 ms for a full clear, <1 ms for the 512-px erase rect, and ~160 lit pixels (8 glyphs × ~20 dots) × ~3 SPI transactions each ≈ 500 transactions/s ≈ order of 10 ms/s if the driver overhead is ~20 µs/transaction. All of it blocking, under the router's recursive mutex, on core 1. ~1 % of core 1 at 1 Hz — acceptable; a full clear per second would be ~2 % and is still not the reason to avoid it (the reason is that it's pointless).

---

## 6. Verification without a panel — recommend (i), a bounded frame counter

- **(i) Instrument `display_manager`** — increment a `static uint32_t s_frames_dispatched` in `process_frame()`, keep it permanently as a stat with a tiny accessor (it is the seed of the future health/metrics event), and add **one bounded log line** marked `TEMPORARY`. This proves the *whole* chain: producer → `submit_frame` → queue → `core1_task` → `dm_execute_cmd` → HAL. Cheapest, no Kconfig, no `sdkconfig.single_mcu` deletion. Use `ESP_LOGD` for the per-frame detail (silent at the default level) plus one `ESP_LOGI` at a slow cadence (e.g. every 5th frame) so the human sees it on the monitor.
- **(ii) Mock backend** — `CONFIG_CHRONOLINK_USE_MOCK_DISPLAY` is currently `not set` (`sdkconfig.defaults:10`); turning it on makes `hal_display_select_ops()` return MOCK with priority *over* ST7796S (`hal_display.c:236-239`) — known issue 11, "mock replaces rather than adds" (`STATUS.md:194-196`) — and `Mock_GetWidth/Height` return 0 (`hal_display_mock.c:105-113`), so `HAL_GFX_Init` silently falls back to the compile-time 480×320. Worse: **the mock does not count calls** — every function ignores its args and returns `HAL_OK` (`hal_display_mock.c:19-55`) — so option (ii) as written proves *nothing* extra without editing the mock, which is a HAL change. And per `.clinerules`, touching `sdkconfig.defaults` invalidates the persisted `sdkconfig.single_mcu`, so the human must delete it. Keep (ii) in reserve for a future fixture/CI harness, not for PR 6's acceptance.
- **(iii) RDDID read-back** — already implemented and already logging (`hal_display_st7796s.c:555-568`). It is a *presence* smoke test: a non-zero ID proves SPI + panel, and all-zero proves their absence. It says nothing about whether frames were produced. Don't extend it into a periodic task; keep it as the bring-up signal it already is.

**Recommended acceptance evidence for the human's `pio run`:** (a) boot log shows `HAL_ST7796S: Panel ID all zero -- SPI miswired, or no panel` **and continues** (expected, not a regression); (b) `display_manager: frame dispatched …` lines appear and the counter advances ~1/s; (c) the frame's text field matches `local = UTC + 1` (BST) for the same second that svc_time's temporary status print shows; (d) no drop warnings (`Frame queue full`, `event_bus_dispatch: subscriber queue full`) and no `submit failed`.

**Do this in the same PR:** `STATUS.md:62-63` records that svc_time's 5 s status print is "marked for removal when the display path lands" — that is exactly PR 6. **Swap** it for the display-side print rather than running both: total temporary log volume stays ~1 line/s, and the one temporary line now proves the *new* path.

---

## 7. Risks, ordering, CMake, STATUS items

**Risks (highest first)**
1. **`DM_MAX_TEXT_LEN` = 8 → unterminated `"HH:MM:SS"` out-of-bounds read** on frame 1 (§3). Fix before the first frame is submitted.
2. **The frame path has never run.** No caller of `submit_frame` exists; every assumption (struct copy, queue depth, clip reset, flags, mutex timeouts) is unverified. Mitigate by keeping frame 1 trivial (clear + fill + one text) and by instrumenting (§6).
3. **Producer blocking:** `display_manager_submit_frame()` can block ~10 ms worst case (5 ms mutex + 5 ms queue, `display_manager.c:225-231`). The submit must happen in `ui_producer`'s own task — never in an `esp_timer` callback, never in the kernel dispatcher. (Both (a) and (b) satisfy this: with (a) the fan-out copy is done by `event_dispatch_task`, and the submit still happens in the subscriber's task.)
4. **`ui_producer_start()` calls `display_manager_init()` a second time** → `display_manager: Already initialized` WARN on every boot (`display_manager.c:181-184`). Drop the call from `ui_producer` (core_os already does it). The *proper* fix — removing both calls from `core_os` and giving `ui_producer` (or a small display task on core 1) the pump — clears the Constitution §7 violation; it is a bigger, differently-scoped diff, so **my recommendation is to split it out** and keep PR 6 as "producer only", with the violation either left documented or named as PR 6b. Note the subtlety: if the pump moves, `core1_task` loses its only job.
5. **svc_time is not started when the first poll runs** (`main.c:45` vs `:50`) → `ESP_ERR_INVALID_STATE` (`svc_time.h:100-103`). Tolerate it: log once, retry next tick. Do **not** log per poll. Do not reorder `main.c` — that would invalidate `main.c:5-14`, `Architecture.md:92-96` and `docs/boot.md`.
6. **Render lag/aliasing** with a plain 1 Hz poll (§1) — fixed by the 250 ms + render-on-change cadence.
7. **Log volume:** dispatcher `LOGD` + 1 Hz frame log + the svc_time print = up to 3 lines/s. Keep exactly one temporary line; verify the persisted verbose level isn't in play.
8. **First-frame full clear** costs ~15-20 ms and a lazy 8 kB DMA allocation; do it once, not per second.
9. **`event_bus_subscribe()` race** exists only if (a) is chosen (`event_bus.h:55-58`); the store order (`q`, `type`, `drop_count`, then `used`) makes it mostly benign, but it is worth a `portENTER_CRITICAL` hardening + a STATUS.md known-issue entry either way — `svc_time_start()` already relies on it. Cheap, but optional for PR 6.

**Ordering constraints (all currently satisfied — state them in the PR so they stay true)**
- `font_engine_register()` **before** the first `DM_CMD_DRAW_TEXT`, else a WARN per text command and nothing drawn (`font_engine.c:61-64`); satisfied by `main.c:43` before `:45`.
- `display_manager_init()` **before** the first submit — satisfied inside `boot_manager_init()`; note `submit_frame()` returns `false` **silently** when uninitialised (`display_manager.c:215-217`), so the bool **must** be checked (`.clinerules`: "Every `hal_status_t` / `bool` return must be checked").
- `HAL_Display_Init()` **before** `HAL_GFX_Init()` (hal.md contract) — satisfied: `HAL_Init` → `HAL_Display_Init`, then `core_os_init` → `display_manager_init` → `HAL_GFX_Init` (`hal_display.h:128-135`).
- `core_os_start()` is inside `boot_manager_init()`, so `core1_task` is already pumping before `app_main:45` — a first frame submitted from `ui_producer_start()` is safe. Prefer submitting from the producer task anyway.

**CMake**
- `ui_producer`: `REQUIRES freertos svc_time display_manager` (drop `chronolink_hal`, `font_engine` — unused). Add `event_bus` only if (a).
- `display_manager`, `svc_time`, `font_engine`: unchanged. No new Kconfig, no new pins, no `build_flags` (Constitution §5).

**STATUS.md / docs items PR 6 should carry**
1. `STATUS.md:44-45` — "Services: … `display_manager`, `ui_producer`, `font_engine`, `font`" contradicts Constitution §11; reclassify as the UI/App band.
2. `STATUS.md:62-63` + `svc_time.c:613,672-674` — the 5 s status print's removal trigger *is* PR 6: remove it (or restate the trigger).
3. `STATUS.md:20-48` "Implemented" — add the live clock face bullet; `STATUS.md:46-48` svc_time bullet stays true under (b).
4. `STATUS.md:147-161` "Stubbed or placeholder" — `ui_producer` is **not** listed as a placeholder today even though it only inits the display manager; PR 6 must remove/never-add it and be explicit that the placeholder is gone.
5. New known issue (or a "Resolved" entry if fixed here): `DM_MAX_TEXT_LEN` gives no room for the NUL (`display_manager.h:27,180-182`).
6. `STATUS.md:165-227` — leave #16 alone; if (a) is chosen, add the per-tick publish semantics to the svc_time section and update `Architecture.md:107-110`, `svc_time.h:15-17`, `time_events.h:31-33`.
7. `components/display_manager/README.md` — **doc/code disagreement to fix in the same change** (`.clinerules`): "lock-free queue" vs `s_submit_mutex`; "8 slots × 512 bytes (~4 KB)" vs `DM_FRAME_QUEUE_DEPTH 3` and `sizeof(dm_frame_t)` ≈ 324 B, heap-allocated by `xQueueCreate`, not static; also "consumes declarative JSON frames" (`display_manager.h:16`) is stale wording — the command list has no JSON.
8. `docs/Architecture.md:97-113` — update the runtime model with `ui_producer`'s cadence and the clock face; §5 table row 1 if the kernel→service call is fixed.
9. `docs/STATUS.md:3` — bump the reviewed hash from `e109e70` to PR 6's merge commit.
10. Open decision 3 (`STATUS.md:266-267`) stays open — PR 6 adds no new HAL-direct call, and the HIL `gfx_api` extraction is a follow-up PR.

**Not touched by PR 6, worth knowing:** the router's per-primitive `display_lock()` means the font path takes/releases the recursive mutex ~500×/s rather than once per frame — fine now, relevant if the face ever animates faster.

---

### Bottom line
Take **(b)**: `ui_producer` grows one task, pinned to core 0, waking every 250 ms, calling `svc_time_get_snapshot()`, and submitting a 2-3 command `dm_frame_t` only when the formatted local `HH:MM:SS` changes. It is the smallest diff, requires no change to any documented event semantics, no new event type, no subscriber-table race, and no document renegotiation — and it fixes a real, currently-latent defect (`DM_MAX_TEXT_LEN`) on the way in. The devkit path is a verified no-op returning `HAL_OK`, so the PR's job is to make that no-op *observable* with exactly one temporary log line, and to hand the human a boot log that proves frames flow.
