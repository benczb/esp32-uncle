#include "passport_features.h"
#include "passport_model.h"
#include <string.h>

void passport_feature_copy(char *out,size_t capacity,const char *value){
 if(!out || !capacity)return;
 if(!value)value="";
 size_t length=passport_utf8_prefix(value,capacity-1);
 memcpy(out,value,length);out[length]=0;
}
bool passport_komoot_decode(const uint8_t *data,size_t length,passport_turn_t *turn){
 if(!data || !turn || length<9 || length>256 || data[4]>30)return false;
 passport_turn_t next={.direction=data[4]};
 for(unsigned i=0;i<4;i++){
  next.instruction|=(uint32_t)data[i]<<(8*i);
  next.distance|=(uint32_t)data[5+i]<<(8*i);
 }
 /* Reject malformed UTF-8 rather than rendering protocol bytes as text. */
 size_t end=9;while(end<length && data[end])end++;
 size_t used=0;bool truncated=false;
 for(size_t i=9;i<end;){
  uint8_t first=data[i];size_t bytes=first<128?1:first>=0xc2&&first<=0xdf?2:first<=0xef&&first>=0xe0?3:first>=0xf0&&first<=0xf4?4:0;
  if(!bytes || i+bytes>end || (bytes==1 && first<32))return false;
  for(size_t j=1;j<bytes;j++)if((data[i+j]&0xc0)!=0x80)return false;
  if(bytes>=3 && ((first==0xe0 && data[i+1]<0xa0)||(first==0xed && data[i+1]>=0xa0)||
     (first==0xf0 && data[i+1]<0x90)||(first==0xf4 && data[i+1]>=0x90)))return false;
  if(!truncated && used+bytes<sizeof(next.street)){memcpy(next.street+used,data+i,bytes);used+=bytes;}
  else truncated=true;
  i+=bytes;
 }
 next.street[used]=0;*turn=next;return true;
}
const char *passport_turn_name(unsigned direction){
 static const char *names[]={"Direction unavailable","Straight","Start","Arrived","Keep left","Turn left","Sharp left","Sharp right","Turn right","Keep right","Fork right","Fork left","U-turn","Direction unavailable","Direction unavailable","Exit roundabout","Exit roundabout"};
 if(direction<sizeof(names)/sizeof(names[0]))return names[direction];
 return direction==30?"Off route":direction<=29?"Roundabout":"Direction unavailable";
}
