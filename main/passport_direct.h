#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "cJSON.h"
typedef enum {DIRECT_OK,DIRECT_CLOCK,DIRECT_MEMORY,DIRECT_CONNECT,DIRECT_HTTP,DIRECT_RESPONSE,DIRECT_KEY,DIRECT_AUTH} passport_direct_error_t;
passport_direct_error_t passport_direct_error(void);
cJSON *passport_direct_weather(const char *city);
/* Network-worker ownership; discard the old cache on the next fetch. */
void passport_direct_weather_refresh(void);
cJSON *passport_direct_haze(const char *city,const char *region,double latitude,double longitude,bool has_location);
cJSON *passport_direct_bus(const char *stop,unsigned page,const char *account_key);

uint8_t *passport_direct_download(const char *url,size_t limit,const char *account_key,size_t *length);

/* Network owner only; displayed weather remains available during speech. */
void passport_direct_release_caches(void);
