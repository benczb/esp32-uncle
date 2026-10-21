#pragma once
#include <stdint.h>

/* Embedded crossing recording: unsigned 8-bit PCM, 16 kHz mono,
 * assets/music/traffic-crossing.pcm. Playback upconverts to the
 * 16-bit BSP audio format. */
#define PASSPORT_TRAFFIC_PCM_BYTES 574741u
#define PASSPORT_TRAFFIC_DURATION_US 35921312LL

#define PASSPORT_TRAFFIC_LENS_SIZE 120
extern const uint16_t *const passport_traffic_lens_red;
extern const uint16_t *const passport_traffic_lens_green;
