/*
 * minimp3_impl.c
 *
 * The single translation unit that instantiates minimp3 for ChronoLink v4.
 * MINIMP3_IMPLEMENTATION must be defined exactly once (here); every other
 * user includes <minimp3.h> for the declarations only.
 *
 * MINIMP3_ONLY_MP3   - drops the MPEG Layer I/II decoder tables and code.
 *                      The adhan asset is Layer III; shipping Layer I/II
 *                      would be dead flash.
 * MINIMP3_NO_SIMD    - force the portable scalar path. The ESP32-S3 is
 *                      Xtensa and has none of minimp3's SIMD backends
 *                      (SSE/NEON/ARM), but pinning it also keeps a host
 *                      build (prayer_host_tests / CI) byte-identical in
 *                      behaviour to the device build.
 *
 * MINIMP3_FLOAT_OUTPUT is deliberately NOT defined: svc_audio feeds
 * audio_play_pcm(), which takes int16_t PCM. Defining it would change
 * mp3d_sample_t to float and silently break that call.
 */
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD

#include "minimp3.h"
