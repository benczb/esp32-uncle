#pragma once
#include "passport_train.h"
#include "passport_direct.h"
bool passport_train_fetch(bool trips,const char *key,uint64_t now,passport_train_feed_t *out,passport_direct_error_t *error);
