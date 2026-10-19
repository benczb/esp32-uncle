#include "passport_train_fetch.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static unsigned calls,mode;
passport_direct_error_t passport_direct_error(void){return DIRECT_CONNECT;}
uint8_t *passport_direct_download(const char *url,size_t limit,const char *key,size_t *length){
 calls++;const void *data;size_t n;
 static const uint8_t empty[]={10,5,10,3,'2','.','0'};
 const char *response=mode==1?"{\"value\":[{\"Link\":\"https://evil.example/steal\"}]}":"{\"value\":[{\"Link\":\"https://dmprod-datasets.s3.ap-southeast-1.amazonaws.com/train-gtfs-real-time/gtfs_realtime.pb?X-Amz-Signature=fixture\"}]}";
 if(calls==1){assert(key&&!strcmp(key,"fixture-key"));assert(strstr(url,"https://datamall2.mytransport.sg/ltaodataservice/GTFS"));assert(!strstr(url,key));assert(limit==8192);data=response;n=strlen(response);}
 else {assert(calls==2&&!key);assert(limit==PASSPORT_TRAIN_FEED_LIMIT);assert(strstr(url,"https://dmprod-datasets.s3.ap-southeast-1.amazonaws.com/"));if(mode==2)return NULL;data=empty;n=sizeof(empty);}
 uint8_t *p=malloc(n+1);assert(p);memcpy(p,data,n);p[n]=0;*length=n;return p;
}
int main(void){
 passport_train_feed_t f={0};passport_direct_error_t error;
 assert(!passport_train_fetch(false,"",1800000000,&f,&error)&&error==DIRECT_KEY&&!calls);
 for(mode=0;mode<3;mode++){
  calls=0;bool ok=passport_train_fetch(mode==2,"fixture-key",1800000000,&f,&error);
  if(!mode){assert(ok&&calls==2&&error==DIRECT_OK&&!f.alerts);passport_train_free(&f);}
  else if(mode==1)assert(!ok&&calls==1&&error==DIRECT_RESPONSE);
  else assert(!ok&&calls==2&&error==DIRECT_CONNECT);
 }
 puts("LTA GTFS fetch: key confined to API, untrusted download rejected, failures preserved: PASS");
}
