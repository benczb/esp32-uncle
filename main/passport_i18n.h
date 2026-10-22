#pragma once
unsigned passport_language_get(void);
void passport_language_set(unsigned language);
const char *passport_tr(const char *chinese,const char *english);
#define T(chinese,english) passport_tr(chinese,english)

/* Translate application-owned status text; external content stays unchanged. */
const char *passport_message(const char *english);
