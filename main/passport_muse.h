#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Optional official Muse BLE runtime. Start/poll on the network worker only;
 * UI calls are bounded snapshots or atomic requests, never BLE operations. */
bool passport_muse_supported(void);
void passport_muse_enter(void);
void passport_muse_leave(void);
void passport_muse_poll(void);
bool passport_muse_confirm(void);
void passport_muse_screen(char *text,size_t capacity);
unsigned passport_muse_runtime_state(void);
unsigned passport_muse_passkey(void);
