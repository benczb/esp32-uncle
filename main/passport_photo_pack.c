#include "passport_photo_pack.h"
#include <string.h>
static uint16_t pixel(const uint8_t *data,unsigned i){return data[2*i]|(uint16_t)data[2*i+1]<<8;}
static unsigned find(const uint16_t *palette,unsigned count,uint16_t color){
 unsigned low=0,high=count;
 while(low<high){unsigned mid=low+(high-low)/2;if(palette[mid]<color)low=mid+1;else high=mid;}
 return low;
}
bool passport_photo_pack(uint8_t *data,size_t bytes){
 if(!data || bytes!=PASSPORT_AVATAR_BYTES)return false;
 uint16_t palette[256];unsigned count=0;
 for(unsigned i=0;i<PASSPORT_AVATAR_W*PASSPORT_AVATAR_H;i++){
  uint16_t color=pixel(data,i);unsigned at=find(palette,count,color);
  if(at==count || palette[at]!=color){
   if(count==256)return false; /* Still no writes to the source buffer. */
   memmove(palette+at+1,palette+at,(count-at)*sizeof(*palette));palette[at]=color;count++;
  }
 }
 /* Indices overwrite only pixels already read. Move the finished index plane
  * forward afterward, avoiding an additional full-image allocation. */
 for(unsigned i=0;i<PASSPORT_AVATAR_W*PASSPORT_AVATAR_H;i++)data[i]=(uint8_t)find(palette,count,pixel(data,i));
 memmove(data+PASSPORT_AVATAR_PALETTE_BYTES,data,PASSPORT_AVATAR_W*PASSPORT_AVATAR_H);
 memset(data,0,PASSPORT_AVATAR_PALETTE_BYTES);
 for(unsigned i=0;i<count;i++){
  unsigned r=palette[i]>>11,g=(palette[i]>>5)&63,b=palette[i]&31;
  data[4*i]=(uint8_t)((b<<3)|(b>>2));data[4*i+1]=(uint8_t)((g<<2)|(g>>4));
  data[4*i+2]=(uint8_t)((r<<3)|(r>>2));data[4*i+3]=255;
 }
 return true;
}
