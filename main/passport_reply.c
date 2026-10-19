#include "passport_reply.h"
#include <stdlib.h>
#include <string.h>
static size_t capacity[2];
static passport_page_t selected=PAGE_HOME;
static char *replies[2];
char *passport_reply_buffer(unsigned agent){
 if(agent>1 || selected!=(agent?PAGE_MUSE:PAGE_HERMES))return NULL;
 if(capacity[agent]<PASSPORT_REPLY_CAPACITY){
  char *p=replies[agent]?realloc(replies[agent],PASSPORT_REPLY_CAPACITY):calloc(PASSPORT_REPLY_CAPACITY,1);
  if(!p)return NULL;
  if(!replies[agent])p[0]=0;
  replies[agent]=p;capacity[agent]=PASSPORT_REPLY_CAPACITY;
 }
 return replies[agent];
}
const char *passport_reply_peek(unsigned agent){return agent<2?replies[agent]:NULL;}
void passport_reply_select(passport_page_t page){
 selected=page;
 for(unsigned a=0;a<2;a++)if(page!=(a?PAGE_MUSE:PAGE_HERMES)){free(replies[a]);replies[a]=NULL;capacity[a]=0;}
 if(page==PAGE_HERMES || page==PAGE_MUSE)(void)passport_reply_buffer(page==PAGE_MUSE);
}

void passport_reply_compact(unsigned agent){
 if(agent>1 || !replies[agent])return;
 size_t n=strlen(replies[agent])+1;
 char *p=realloc(replies[agent],n);if(p){replies[agent]=p;capacity[agent]=n;}
}
