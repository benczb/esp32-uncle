#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
 char state[16],title[160],artist[160],device[96];
 unsigned progress_ms,duration_ms;
} passport_spotify_t;
typedef struct {
 char state[16],title[160],text[512];
 unsigned index,count,page,pages;
 bool large;
} passport_reader_t;
/* Protected by passport_mutex. Only the network worker publishes responses. */
extern passport_spotify_t passport_spotify;
extern passport_reader_t passport_reader;
/* Nonblocking queue request: 0 previous, 1 next, 2 open/toggle, 3 size, 4 shelf. */
bool passport_feature_key(unsigned page,unsigned action);

typedef struct {
 uint32_t instruction,distance;
 uint8_t direction;
 char street[128];
} passport_turn_t;
bool passport_komoot_decode(const uint8_t *data,size_t length,passport_turn_t *turn);
const char *passport_turn_name(unsigned direction);
/* UTF-8-aware bounded copy, preserving complete code points. */
void passport_feature_copy(char *out,size_t capacity,const char *value);
