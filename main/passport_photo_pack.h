#pragma once
#include "passport_model.h"
#define PASSPORT_AVATAR_PALETTE_BYTES 1024
#define PASSPORT_AVATAR_INDEXED_BYTES (PASSPORT_AVATAR_PALETTE_BYTES + PASSPORT_AVATAR_W * PASSPORT_AVATAR_H)
/* Lossless in-place RGB565 LE -> LVGL I8/BGRA palette. Buffer is unchanged
 * on rejection, including images containing more than 256 distinct colours. */
bool passport_photo_pack(uint8_t *data,size_t bytes);
