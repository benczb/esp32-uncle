#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Bounded, transactional decoding. Failure never becomes an all-clear. */
#define PASSPORT_TRAIN_TEXT_LIMIT 32768
#define PASSPORT_TRAIN_FEED_LIMIT 65536
#define PASSPORT_TRAIN_PAGES 32768
typedef struct {
 char *text;
 size_t length;
 unsigned alerts;
 uint64_t timestamp;
 char headline[160];
} passport_train_feed_t;
bool passport_train_decode(const uint8_t *bytes,size_t size,bool trips,uint64_t now,passport_train_feed_t *out);
void passport_train_free(passport_train_feed_t *feed);
/* Only LTA's documented HTTPS S3 download host; AccountKey is never sent there. */
bool passport_train_download_url(const char *url);
