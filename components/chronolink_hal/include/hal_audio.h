/*
 * hal_audio.h
 *
 * ChronoLink V4 OS — Audio HAL
 *
 * Copyright (c) 2026 ChronoLink Project
 */
#pragma once
#include "hal.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

hal_status_t HAL_Audio_Init(void);
hal_status_t HAL_Audio_Deinit(void);

/* Play a 16-bit mono PCM buffer (blocking). The I2S channel is
   configured for mono; a stereo buffer is not accepted. */
hal_status_t HAL_Audio_Play(const int16_t *pcm, size_t samples);

/* Request a stop of any in-flight HAL_Audio_Play() from another task.
   Returns HAL_OK if a stop was requested, HAL_ERR_INIT if not initialised. */
hal_status_t HAL_Audio_Stop(void);

/* Set master volume 0..100 */
hal_status_t HAL_Audio_SetVolume(uint8_t vol);

/* Amplifier power state — MAX98357A SD_MODE (PDL_PIN_AUDIO_SD_MODE).
 *
 * On the shipped V4 PCB R21 is DNP and R15 (10 kR) pulls SD_MODE low, so the
 * amplifier is in hardware shutdown (0.6 uA, high-Z outputs) until firmware
 * drives the pin high. That is the state pdl_board_init() leaves it in, so a
 * boot can never click.
 *
 *   en = true   drive SD_MODE high: amplifier running. This call BLOCKS for
 *               the >= 10 ms tON guard (MAX98357A tON is 7-7.5 ms) so the
 *               first BCLK edge cannot fall inside the amplifier's wake-up —
 *               call it on a task, before HAL_Audio_Init().
 *   en = false  drive SD_MODE low: shutdown, 0.6 uA instead of 340-400 uA
 *               standby. Do it after SDIN has been ramped to zero.
 *
 * Idempotent: requesting the state the pin is already in returns HAL_OK and
 * does NOT restart the tON guard, so HAL_Audio_Init()'s internal enable is a
 * no-op for a caller that already enabled the amplifier.
 *
 * Return contract:
 *   HAL_OK       the requested state is in effect (or already was).
 *   HAL_ERR_DEV  the pin could not be configured or driven, OR this board has
 *                no firmware-controlled SD_MODE (PDL_PIN_AUDIO_SD_MODE < 0).
 *                A caller that needs the amplifier on (svc_audio) must treat
 *                this as a hard failure: the adhan would be silent.
 *                HAL_OK is never returned from the no-pin build, because the
 *                request would not have taken effect (Constitution section 12).
 *
 * Task context only — the OFF->ON transition blocks for the 10 ms tON guard
 * rounded up to at least one tick.
 *
 * The flag it maintains is what makes HAL_Audio_Deinit() disable the
 * amplifier if and only if this driver enabled it. */
hal_status_t HAL_Audio_AmpSetEnabled(bool en);

#ifdef __cplusplus
}
#endif