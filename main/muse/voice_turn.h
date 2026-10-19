#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef enum {MUSE_TURN_IDLE,MUSE_TURN_RECORDING,MUSE_TURN_MARKING,MUSE_TURN_ACK,
 MUSE_TURN_REPLY,MUSE_TURN_DONE,MUSE_TURN_ERROR} muse_turn_state_t;
typedef enum {MUSE_TURN_OK,MUSE_TURN_NO_MEMORY,MUSE_TURN_OFFLINE,MUSE_TURN_SEND_FAILED,
 MUSE_TURN_TIMEOUT,MUSE_TURN_BAD_RESPONSE,MUSE_TURN_TOO_LONG} muse_turn_error_t;
typedef void (*muse_turn_changed_t)(muse_turn_state_t,muse_turn_error_t,const char *text);
/* All operations except state/error access run serially on the audio worker.
 * The BLE response callback only copies bounded bytes under its private lock.
 * init allocates one mutex; per-turn buffers are freed on done/error/cancel. */
bool muse_turn_init(muse_turn_changed_t changed);
bool muse_turn_begin(void);
bool muse_turn_audio(const int16_t *pcm,size_t frames);
bool muse_turn_finish(void);
void muse_turn_poll(void);
void muse_turn_cancel(void);
muse_turn_state_t muse_turn_state(void);
muse_turn_error_t muse_turn_error(void);

const char *muse_turn_note_id(void);
const char *muse_turn_message_id(void);
