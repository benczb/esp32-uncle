#include "passport_photo.h"
#include <stdlib.h>
uint8_t *passport_avatar;
bool passport_avatar_indexed;
size_t passport_avatar_bytes,passport_avatar_allocation;
static bool refresh;
static uint8_t *reserved;
bool passport_photo_reserved(void){return reserved!=NULL;}
bool passport_photo_reserve(void){
 if(passport_avatar || reserved)return true;
 if(refresh)return false;
 reserved=malloc(PASSPORT_AVATAR_BYTES);return reserved!=NULL;
}
void passport_photo_apply_theme(uint8_t *data,size_t bytes,unsigned theme){
 if(!data || theme!=1)return;
 for(size_t i=0;i+1<bytes;i+=2){
  unsigned v=data[i]|((unsigned)data[i+1]<<8);
  unsigned r=((v>>11)&31)*255/31,g=((v>>5)&63)*255/63,b=(v&31)*255/31;
  unsigned y=(r*77+g*150+b*29)>>8;
  r=y;g=y;b=y;
  v=((r>>3)<<11)|((g>>2)<<5)|(b>>3);data[i]=v;data[i+1]=v>>8;
 }
}
void passport_photo_invalidate(void){refresh=true;}
bool passport_photo_refresh_pending(void){return refresh;}
bool passport_photo_load(passport_page_t page,bool stored,passport_photo_reader_t reader,void *context){
 if(!stored || (page!=PAGE_HOME && page!=PAGE_CARD) || passport_avatar || refresh || !reader)return false;
 uint8_t *data=reserved?reserved:malloc(PASSPORT_AVATAR_BYTES);reserved=NULL;
 if(!data)return false;
 if(!reader(data,PASSPORT_AVATAR_BYTES,context)){free(data);return false;}
 passport_avatar_allocation=PASSPORT_AVATAR_BYTES;
 passport_avatar_indexed=passport_photo_pack(data,PASSPORT_AVATAR_BYTES);
 passport_avatar_bytes=passport_avatar_indexed?PASSPORT_AVATAR_INDEXED_BYTES:PASSPORT_AVATAR_BYTES;
 if(passport_avatar_indexed){
  uint8_t *compact=realloc(data,passport_avatar_bytes);
  if(compact){data=compact;passport_avatar_allocation=passport_avatar_bytes;}
 }
 passport_avatar=data;
 return true;
}
void passport_photo_release(void){free(reserved);reserved=NULL;free(passport_avatar);passport_avatar=NULL;passport_avatar_indexed=false;passport_avatar_bytes=passport_avatar_allocation=0;refresh=false;}
