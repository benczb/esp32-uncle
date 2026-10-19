#include "passport_train_fetch.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>
bool passport_train_fetch(bool trips,const char *key,uint64_t now,passport_train_feed_t *out,passport_direct_error_t *error){
 *error=DIRECT_KEY;if(!key||!*key)return false;
 const char *endpoint=trips?"https://datamall2.mytransport.sg/ltaodataservice/GTFSRealtimeTrainTripUpdates":"https://datamall2.mytransport.sg/ltaodataservice/GTFSRealTimeTrainServiceAlerts";
 size_t bytes=0;uint8_t *data=passport_direct_download(endpoint,8192,key,&bytes);
 if(!data){*error=passport_direct_error();return false;}
 cJSON *j=cJSON_Parse((const char *)data);free(data);
 cJSON *row=cJSON_GetArrayItem(cJSON_GetObjectItem(j,"value"),0);
 const char *url=cJSON_GetStringValue(cJSON_GetObjectItem(row,"Link"));
 if(!url)url=cJSON_GetStringValue(cJSON_GetObjectItem(j,"Link"));
 char *link=NULL;
 if(passport_train_download_url(url)){size_t n=strlen(url)+1;link=malloc(n);if(link)memcpy(link,url,n);}
 cJSON_Delete(j);
 if(!link){*error=DIRECT_RESPONSE;return false;}
 /* New handle, no AccountKey or bearer header, no redirect following. */
 data=passport_direct_download(link,PASSPORT_TRAIN_FEED_LIMIT,NULL,&bytes);free(link);
 if(!data){*error=passport_direct_error();return false;}
 bool ok=passport_train_decode(data,bytes,trips,now,out);free(data);
 *error=ok?DIRECT_OK:DIRECT_RESPONSE;return ok;
}
