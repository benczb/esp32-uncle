#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
bool passport_muse_voice_ready(void);
bool passport_muse_voice_active(void);
/* Main input task, outside passport_mutex; release the old reply before capture. */
bool passport_muse_voice_prepare(void);
/* Main input task: invalidate pending work even on a quick leave/return. */
void passport_muse_voice_leave(void);
/* Audio worker only from this point. */
bool passport_muse_voice_begin(void);
bool passport_muse_voice_audio(const int16_t *pcm,size_t frames);
bool passport_muse_voice_finish(void);
void passport_muse_voice_cancel(void);
/* 0 idle, 1 pending, 2 done, 3 error. Cancels when Muse is no longer selected. */
unsigned passport_muse_voice_poll(void);

/* Fixed recording/status text; getter requires passport_mutex (as UI does). */
const char *passport_muse_voice_status(void);
void passport_muse_voice_message(const char *text);

/* Speaker owner, separate from microphone. Main polls only while the network
 * worker has retired and relinquished its cache/HTTP ownership. */
bool passport_muse_speech_can_start(void);
bool passport_muse_speech_busy(void);
void passport_muse_speech_stop(void);
void passport_muse_speech_poll(void);

/* Private USB hardware probe; queues a local tone, never records or sends chat. */
bool passport_muse_speech_probe_request(unsigned stack);
