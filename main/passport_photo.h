#pragma once
#include "passport_model.h"
#include "passport_photo_pack.h"
#include <stdbool.h>
#include <stdint.h>
extern uint8_t *passport_avatar;
extern bool passport_avatar_indexed;
extern size_t passport_avatar_bytes,passport_avatar_allocation;
/* Under passport_mutex. Invalidation does not free a widget's live source. */
void passport_photo_invalidate(void);
void passport_photo_apply_theme(uint8_t *pixels,size_t bytes,unsigned theme);
bool passport_photo_refresh_pending(void);
/* Reserve unpublished image memory under passport_mutex before restarting a
 * worker. This does no storage I/O and never replaces a live widget source. */
bool passport_photo_reserve(void);
bool passport_photo_reserved(void);
typedef bool (*passport_photo_reader_t)(uint8_t *data,size_t bytes,void *context);
/* Worker only, under passport_mutex. Reads into unpublished memory; the saved
 * flash blob is never modified. A failed load leaves the cache empty. */
bool passport_photo_load(passport_page_t page,bool stored,passport_photo_reader_t reader,void *context);
/* Under passport_mutex and LVGL lock, only AFTER deleting old image widgets
 * and dropping their image-cache entry. Never call from page selection. */
void passport_photo_release(void);
