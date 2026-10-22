#include "passport_features.h"
#include "passport_model.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void){
 uint8_t packet[256]={0x78,0x56,0x34,0x12,8,0x2c,1,0,0};
 memcpy(packet+9,"River Road",10);passport_turn_t turn={0};
 assert(passport_komoot_decode(packet,19,&turn));
 assert(turn.instruction==0x12345678 && turn.distance==300 && turn.direction==8 && !strcmp(turn.street,"River Road"));
 assert(!passport_komoot_decode(packet,8,&turn));assert(!passport_komoot_decode(NULL,9,&turn));
 packet[4]=31;assert(!passport_komoot_decode(packet,19,&turn));packet[4]=3;
 assert(passport_komoot_decode(packet,9,&turn));assert(!strcmp(passport_turn_name(turn.direction),"Arrived"));
 packet[9]=0xf0;packet[10]=0x80;packet[11]=0x80;packet[12]=0x80;assert(!passport_komoot_decode(packet,13,&turn));
 packet[9]=0xed;packet[10]=0xa0;packet[11]=0x80;assert(!passport_komoot_decode(packet,12,&turn));
 memset(packet+9,'a',247);assert(passport_komoot_decode(packet,256,&turn));assert(strlen(turn.street)==127);
 memset(packet+9,0,247);memcpy(packet+9,"桥 Street",10);assert(passport_komoot_decode(packet,19,&turn));assert(!strcmp(turn.street,"桥 Street"));
 memset(packet+9,'a',126);memcpy(packet+135,"桥Z",4);
 assert(passport_komoot_decode(packet,139,&turn));assert(strlen(turn.street)==126);
 char short_text[3];passport_feature_copy(short_text,sizeof(short_text),"桥 Street");assert(!short_text[0]);
 passport_screensaver_t saver;passport_screensaver_init(&saver,0);
 assert(!passport_screensaver_poll_timeout(&saver,10000000,false,90000000));
 assert(passport_screensaver_poll_timeout(&saver,90000000,false,90000000));
 assert(passport_service_allowed(PAGE_SPOTIFY,PAGE_SPOTIFY));assert(!passport_service_allowed(PAGE_READER,PAGE_SPOTIFY));
 puts("Komoot packet bounds/UTF-8, reader timeout and service isolation: PASS");
}
