#pragma once
#include "passport_model.h"
#define PASSPORT_REPLY_CAPACITY 6400
/* Caller holds passport_mutex. Selection releases inactive RAM only, not the
 * bridge's latest reply. Buffer access retries allocation only for the selected
 * assistant; a late result from an old function cannot recreate its buffer. */
void passport_reply_select(passport_page_t page);
char *passport_reply_buffer(unsigned agent);
const char *passport_reply_peek(unsigned agent);

void passport_reply_compact(unsigned agent);
