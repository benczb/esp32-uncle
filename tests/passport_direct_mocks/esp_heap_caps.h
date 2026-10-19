#pragma once
#include <stddef.h>
#define MALLOC_CAP_8BIT 1
static inline size_t heap_caps_get_free_size(unsigned caps){(void)caps;return 65536;}
static inline size_t heap_caps_get_largest_free_block(unsigned caps){(void)caps;return 32768;}
