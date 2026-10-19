#include "passport_i18n.h"
#include <stdatomic.h>
static atomic_uint current_language;
unsigned passport_language_get(void){return atomic_load(&current_language);}
void passport_language_set(unsigned language){atomic_store(&current_language,language==1?1:0);}
const char *passport_tr(const char *chinese,const char *english){return passport_language_get()==1?english:chinese;}
