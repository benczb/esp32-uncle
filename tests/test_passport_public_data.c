#include "passport_public_data.h"
#include "passport_usage.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static void haze_cache_checks(void){
 cJSON *raw=cJSON_Parse("{\"code\":0,\"data\":{\"regionMetadata\":[{\"name\":\"east\",\"labelLocation\":{\"latitude\":1.35,\"longitude\":103.94}},{\"name\":\"central\",\"labelLocation\":{\"latitude\":1.35,\"longitude\":103.82}}],\"items\":[{\"timestamp\":\"2026-10-17T11:00:00+08:00\",\"readings\":{\"psi_twenty_four_hourly\":{\"east\":90,\"central\":80}}},{\"timestamp\":\"2026-10-17T12:00:00+08:00\",\"readings\":{\"psi_twenty_four_hourly\":{\"east\":101,\"central\":99},\"pm25_sub_index\":{\"east\":101},\"unrelated\":{\"ignored\":[1,2,3]}}}]}}");
 time_t now=passport_iso_epoch("2026-10-17T12:00:00+08:00");
 const char *regions[]={"east","central","auto","unset"};char *before[4];
 for(unsigned i=0;i<4;i++){
  cJSON *out=passport_public_haze(raw,"singapore",regions[i],1.35,103.94,true,now+7201);
  assert(out);before[i]=cJSON_PrintUnformatted(out);assert(before[i]);cJSON_Delete(out);
 }
 assert(passport_public_haze_compact(raw));
 cJSON *items=cJSON_GetObjectItem(cJSON_GetObjectItem(raw,"data"),"items");
 assert(cJSON_GetArraySize(items)==1);
 cJSON *readings=cJSON_GetObjectItem(cJSON_GetArrayItem(items,0),"readings");
 assert(readings->child && !readings->child->next);
 assert(passport_public_haze_compact(raw)); /* Idempotent, including coordinates. */
 for(unsigned i=0;i<4;i++){
  cJSON *out=passport_public_haze(raw,"singapore",regions[i],1.35,103.94,true,now+7201);
  char *after=cJSON_PrintUnformatted(out);assert(after && !strcmp(before[i],after));
  free(before[i]);free(after);cJSON_Delete(out);
 }
 cJSON_Delete(raw);
 raw=cJSON_Parse("{\"code\":0,\"data\":{\"items\":[{\"timestamp\":\"2026-10-17T12:00:00+08:00\",\"readings\":{\"other\":123}}]}}");
 char *snapshot=cJSON_PrintUnformatted(raw);assert(!passport_public_haze_compact(raw));
 char *after=cJSON_PrintUnformatted(raw);assert(!strcmp(snapshot,after));free(snapshot);free(after);cJSON_Delete(raw);
 assert(!passport_public_haze_compact(NULL));
}
static void usage_checks(void){
 char out[1800];cJSON *j=cJSON_Parse("{\"usage\":[{\"provider\":\"codex\",\"status\":\"ok\",\"primary\":{\"usedPercent\":25,\"resetsAt\":1800003600},\"secondary\":{\"usedPercent\":100,\"resetsAt\":1799999999}},{\"provider\":\"claude\",\"status\":\"not_connected\"}]}");
 passport_usage_format(j,1800000000,1,out,sizeof(out));assert(strstr(out,"75%"));assert(strstr(out,"0%"));assert(strstr(out,"1h 0m"));assert(strstr(out,"refresh"));assert(strstr(out,"Not connected"));
 passport_usage_format(j,0,1,out,sizeof(out));assert(!strstr(out,"1h 0m"));
 char tiny[12];passport_usage_format(j,1800000000,0,tiny,sizeof(tiny));assert(tiny[11]==0 || strlen(tiny)<11);cJSON_Delete(j);
}
int main(void){
 usage_checks();
 haze_cache_checks();
 int64_t now=passport_iso_epoch("2026-10-17T12:00:00+08:00");assert(now==passport_iso_epoch("2026-10-17T04:00:00Z"));
 assert(passport_iso_epoch("2026-02-29T04:00:00Z")==-1);assert(passport_iso_epoch("2024-02-29T04:00:00.123Z")>0);
 cJSON *raw=cJSON_Parse("{\"code\":0,\"data\":{\"items\":[{\"timestamp\":\"2026-10-17T12:00:00+08:00\",\"readings\":{\"psi_twenty_four_hourly\":{\"east\":101}}}],\"regionMetadata\":[{\"name\":\"east\",\"labelLocation\":{\"latitude\":1.35,\"longitude\":103.94}}]}}");
 cJSON *j=passport_public_haze(raw,"singapore","auto",1.35,103.94,true,now);assert(j);assert(cJSON_GetObjectItem(j,"value")->valueint==101);assert(!strcmp(cJSON_GetObjectItem(j,"category")->valuestring,"unhealthy"));assert(cJSON_IsFalse(cJSON_GetObjectItem(j,"stale")));cJSON_Delete(j);
 j=passport_public_haze(raw,"singapore","east",0,0,false,now+7201);assert(cJSON_IsTrue(cJSON_GetObjectItem(j,"stale")));cJSON_Delete(j);
 j=passport_public_haze(raw,"singapore","unset",0,0,false,now);assert(!strcmp(cJSON_GetObjectItem(j,"reason")->valuestring,"location_required"));cJSON_Delete(j);cJSON_Delete(raw);
 raw=cJSON_Parse("{\"busStops\":[{\"BusStopCode\":\"82009\",\"UpdatedAt\":\"2026-10-17T12:00:00+08:00\",\"Services\":[{\"ServiceNo\":\"15\",\"NextBus\":{\"EstimatedArrival\":\"2026-10-17T12:00:30+08:00\",\"Label\":\"To Eunos\",\"VisitNumber\":\"1\",\"Terminating\":true},\"NextBus2\":{\"EstimatedArrival\":\"2026-10-17T12:05:00+08:00\",\"Label\":\"To Eunos\",\"VisitNumber\":\"1\"},\"NextBus3\":{\"EstimatedArrival\":\"2026-10-17T12:04:00+08:00\",\"Label\":\"Other direction\",\"VisitNumber\":\"2\"}}]}]}");
 assert(passport_public_bus_parse(raw,"82009"));j=passport_public_bus_page(0,now,false);cJSON *rows=cJSON_GetObjectItem(j,"rows");assert(cJSON_GetArraySize(rows)==2);cJSON *eta=cJSON_GetObjectItem(cJSON_GetArrayItem(rows,0),"eta");assert(!strcmp(cJSON_GetArrayItem(eta,0)->valuestring,"Arr"));assert(!strcmp(cJSON_GetArrayItem(eta,1)->valuestring,"5"));cJSON_Delete(j);
 j=passport_public_bus_page(0,now,true);assert(cJSON_IsTrue(cJSON_GetObjectItem(j,"stale")));assert(!strcmp(cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(j,"rows"),0),"eta"),0)->valuestring,"--"));cJSON_Delete(j);cJSON_Delete(raw);
 raw=cJSON_Parse("{\"BusStopCode\":\"82009\",\"Services\":[{\"ServiceNo\":\"22\",\"NextBus\":{\"DestinationCode\":\"54009\",\"VisitNumber\":\"1\",\"EstimatedArrival\":\"2026-10-17T12:02:00+08:00\"}}]}");
 assert(!passport_public_bus_parse_lta(raw,"00481",now));
 assert(passport_public_bus_parse_lta(raw,"82009",now));
 j=passport_public_bus_page(0,now,false);cJSON *row=cJSON_GetArrayItem(cJSON_GetObjectItem(j,"rows"),0);
 assert(!strcmp(cJSON_GetObjectItem(row,"destination")->valuestring,"To stop 54009"));
 assert(!strcmp(cJSON_GetArrayItem(cJSON_GetObjectItem(row,"eta"),0)->valuestring,"2"));
 assert(strstr(cJSON_GetObjectItem(j,"updated_at")->valuestring,"12:00:00+08:00"));cJSON_Delete(j);cJSON_Delete(raw);
 puts("Direct PSI / bus adapters: PASS");return 0;
}
