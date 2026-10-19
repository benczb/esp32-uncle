#pragma once
#include <stddef.h>
#include <time.h>
#include "cJSON.h"
void passport_usage_format(const cJSON *payload,time_t now,unsigned english,char *out,size_t capacity);
