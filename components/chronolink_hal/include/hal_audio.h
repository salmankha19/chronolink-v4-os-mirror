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

/* Play a mono or stereo 16-bit PCM buffer (blocking) */
hal_status_t HAL_Audio_Play(const int16_t *pcm, size_t samples);

/* Set master volume 0..100 */
hal_status_t HAL_Audio_SetVolume(uint8_t vol);

#ifdef __cplusplus
}
#endif