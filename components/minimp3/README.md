# minimp3 (vendored, header-only)

`include/minimp3.h` is an unmodified copy of upstream minimp3:

- Source: <https://raw.githubusercontent.com/lieff/minimp3/master/minimp3.h>
- Vendored: 2026-11-25 (80,975 bytes)
- Licence: CC0 / public domain (stated in the file header)

`minimp3_impl.c` is the **single** translation unit that defines
`MINIMP3_IMPLEMENTATION`. Every other user includes `<minimp3.h>` for
declarations only. It also defines `MINIMP3_ONLY_MP3` (adhan assets are
MPEG Layer III; Layer I/II code would be dead flash) and
`MINIMP3_NO_SIMD` (Xtensa has none of minimp3's SIMD backends).

`MINIMP3_FLOAT_OUTPUT` is deliberately **not** defined: the consumer
(`svc_audio`) feeds `audio_play_pcm()`, which is int16 PCM.

Do not hand-edit the header. To update, replace it wholesale and record
the new byte count and date here.

Two sizing facts the consumer depends on (measured from this copy, not
recollection):

- `MINIMP3_MAX_SAMPLES_PER_FRAME` is `1152*2` = 2304 interleaved
  samples, so the PCM output buffer is 2304 `int16_t`.
- `mp3dec_decode_frame()` allocates `mp3dec_scratch_t` (~16.3 KB) on the
  **caller's stack**, so the calling task's stack must be sized for it.
- `mp3dec_t` itself is ~6.7 KB (float MDCT/QMF state plus a 511-byte
  bit reservoir) and can live on the heap.

Consumer: `components/svc_audio`.
