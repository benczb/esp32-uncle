#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "cJSON.h"
int64_t passport_iso_epoch(const char *text);
cJSON *passport_public_haze(cJSON *raw,const char *city,const char *region,double lat,double lon,bool has_location,time_t now);
/* In-place cache reduction: keep latest PSI and all region coordinates.
 * No allocation, no change to supported haze output. Invalid input unchanged. */
bool passport_public_haze_compact(cJSON *raw);
void passport_public_bus_reset(void);
bool passport_public_bus_parse(cJSON *raw,const char *stop);
cJSON *passport_public_bus_page(unsigned page,time_t now,bool failed);

bool passport_public_bus_parse_lta(cJSON *raw,const char *stop,time_t fetched);
