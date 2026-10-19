#include "passport_direct.h"
#include "passport_public_data.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static atomic_int last_error;
static bool weather_refresh_pending;
static cJSON *weather_cache,*haze_cache;
static int64_t weather_next,haze_next,bus_next;
void passport_direct_weather_refresh(void){weather_refresh_pending=true;}
passport_direct_error_t passport_direct_error(void){return atomic_load(&last_error);}
/* Single network worker owns HTTP handles. Diagnostics never log query strings or secrets. */
uint8_t *passport_direct_download(const char *url,size_t limit,const char *account_key,size_t *length){
 *length=0;
 if(time(NULL)<1700000000){atomic_store(&last_error,DIRECT_CLOCK);ESP_LOGW("public_data","Clock not synchronised; HTTPS deferred");return NULL;}
 /* Small blocks avoid reallocating a contiguous multi-KB body while TLS owns RAM.
  * Assemble only after the TLS handle has released its handshake/record buffers. */
 typedef struct chunk {struct chunk *next;size_t used;char data[512];} chunk_t;
 chunk_t *head=NULL,*tail=NULL;char scratch[512];size_t used=0;bool ok=false,memory_failed=false;
 size_t request_size=strlen(url)+512;
 if(request_size>4608){atomic_store(&last_error,DIRECT_RESPONSE);return NULL;}
 esp_http_client_config_t cfg={.buffer_size_tx=request_size>1024?(int)request_size:1024,.url=url,.user_agent="PocketPassport/1.0",.timeout_ms=8000,.buffer_size=1024,.crt_bundle_attach=esp_crt_bundle_attach,.disable_auto_redirect=true};
 esp_http_client_handle_t h=esp_http_client_init(&cfg);
 int status=0;esp_err_t error=h?ESP_OK:ESP_ERR_NO_MEM;
 if(h && account_key){error=esp_http_client_set_header(h,"AccountKey",account_key);if(error==ESP_OK)error=esp_http_client_set_header(h,"Accept","application/json");}
 if(error==ESP_OK)error=esp_http_client_open(h,0);
 if(error==ESP_OK && esp_http_client_fetch_headers(h)>=0){
  status=esp_http_client_get_status_code(h);
  if(status==200){
   while(used<limit){
    size_t amount=tail&&tail->used<sizeof(scratch)?sizeof(scratch)-tail->used:sizeof(scratch);
    if(amount>limit-used)amount=limit-used;
    int n=esp_http_client_read(h,scratch,amount);if(n<=0)break;
    if(!tail || tail->used==sizeof(scratch)){
     chunk_t *part=malloc(sizeof(*part));
     if(!part){memory_failed=true;ESP_LOGW("public_data","Block allocation failed at %u bytes; free=%u largest=%u",(unsigned)used,(unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));break;}
     part->next=NULL;part->used=0;if(tail)tail->next=part;else head=part;tail=part;
    }
    memcpy(tail->data+tail->used,scratch,n);tail->used+=n;used+=n;
   }
   ok=!memory_failed && esp_http_client_is_complete_data_received(h);
   if(used==limit){char extra;ok=esp_http_client_read(h,&extra,1)==0 && ok;}
  }
 }
 if(h){esp_http_client_close(h);esp_http_client_cleanup(h);}
 char *body=ok?malloc(used+1):NULL;
 if(ok&&!body)memory_failed=true;
 size_t copied=0;
 while(head){chunk_t *next=head->next;if(body){memcpy(body+copied,head->data,head->used);copied+=head->used;}free(head);head=next;}
 if(body){body[copied]=0;*length=copied;}
 atomic_store(&last_error,body?DIRECT_OK:memory_failed||error==ESP_ERR_NO_MEM?DIRECT_MEMORY:error!=ESP_OK?DIRECT_CONNECT:(status==401||status==403)?DIRECT_AUTH:status!=200?DIRECT_HTTP:DIRECT_RESPONSE);
 if(memory_failed)ESP_LOGW("public_data","Response allocation failed at %u bytes; free=%u largest=%u",(unsigned)used,(unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
 if(!body)ESP_LOGW("public_data","Request failed: transport=%s HTTP=%d complete=%d bytes=%u free=%u largest=%u",esp_err_to_name(error),status,ok,(unsigned)used,(unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
 return (uint8_t *)body;
}
static cJSON *get_json_key(const char *url,size_t limit,const char *account_key){
 size_t n=0;uint8_t *body=passport_direct_download(url,limit,account_key,&n);
 if(!body)return NULL;
 cJSON *j=cJSON_Parse((const char *)body);free(body);
 if(!j)atomic_store(&last_error,DIRECT_RESPONSE);
 return j;
}
static cJSON *get_json(const char *url,size_t limit){return get_json_key(url,limit,NULL);}
static void stale(cJSON *j){if(j){cJSON_DeleteItemFromObject(j,"stale");cJSON_AddBoolToObject(j,"stale",true);}}
cJSON *passport_direct_weather(const char *city){
 static char saved[24];static passport_direct_error_t error;
int64_t now=esp_timer_get_time();
 if(weather_refresh_pending){cJSON_Delete(weather_cache);weather_cache=NULL;weather_next=0;weather_refresh_pending=false;}
 if(strcmp(saved,city)){cJSON_Delete(weather_cache);weather_cache=NULL;weather_next=0;snprintf(saved,sizeof(saved),"%s",city);}
 if(now>=weather_next){
  const char *coords=!strcmp(city,"singapore")?"1.3521&longitude=103.8198":!strcmp(city,"johor-bahru")?"1.4927&longitude=103.7414":!strcmp(city,"kuala-lumpur")?"3.139&longitude=101.6869":!strcmp(city,"penang")?"5.4141&longitude=100.3288":NULL;
  if(!coords)return NULL;
  char url[320];snprintf(url,sizeof(url),"https://api.open-meteo.com/v1/forecast?latitude=%s&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m&timezone=Asia%%2FSingapore",coords);
  cJSON *j=get_json(url,8192);
  error=passport_direct_error();
  if(j&&cJSON_IsObject(cJSON_GetObjectItem(j,"current"))){cJSON_Delete(weather_cache);weather_cache=j;weather_next=now+600000000;}
  else {if(error==DIRECT_OK)error=DIRECT_RESPONSE;cJSON_Delete(j);stale(weather_cache);weather_next=now+30000000;}
 }
 if(!weather_cache)atomic_store(&last_error,error);
 return cJSON_Duplicate(weather_cache,true);

}
cJSON *passport_direct_haze(const char *city,const char *region,double lat,double lon,bool location){
 if(strcmp(city,"singapore") || !strcmp(region,"unset") || (!strcmp(region,"auto")&&!location))
  return passport_public_haze(NULL,city,region,lat,lon,location,time(NULL));
 static bool failed;
int64_t now=esp_timer_get_time();
 if(now>=haze_next){cJSON *j=get_json("https://api-open.data.gov.sg/v2/real-time/api/psi",16384);
  cJSON *probe=passport_public_haze(j,"singapore","central",0,0,false,time(NULL));
  if(probe&&cJSON_IsTrue(cJSON_GetObjectItem(probe,"available"))&&passport_public_haze_compact(j)){cJSON_Delete(haze_cache);haze_cache=j;failed=false;haze_next=now+300000000;}
  else {cJSON_Delete(j);failed=true;haze_next=now+30000000;}cJSON_Delete(probe);
 }
 cJSON *j=passport_public_haze(haze_cache,city,region,lat,lon,location,time(NULL));if(failed)stale(j);return j;

}
cJSON *passport_direct_bus(const char *stop,unsigned page,const char *account_key){
 static char saved[6],credential[129];static bool failed;static unsigned failures;
 static passport_direct_error_t bus_error;int64_t now=esp_timer_get_time();
 if(!account_key || !account_key[0]){passport_public_bus_reset();atomic_store(&last_error,DIRECT_KEY);return NULL;}
 if(strlen(account_key)>=sizeof(credential)){atomic_store(&last_error,DIRECT_KEY);return NULL;}
 for(const unsigned char *p=(const unsigned char *)account_key;*p;p++)if(*p<=32||*p>=127){atomic_store(&last_error,DIRECT_KEY);return NULL;}
 if(strcmp(saved,stop)||strcmp(credential,account_key)){
  passport_public_bus_reset();snprintf(saved,sizeof(saved),"%s",stop);memset(credential,0,sizeof(credential));snprintf(credential,sizeof(credential),"%s",account_key);bus_next=0;failures=0;
 }
 if(now>=bus_next){
  char url[160];snprintf(url,sizeof(url),"https://datamall2.mytransport.sg/ltaodataservice/v3/BusArrival?BusStopCode=%s",stop);
  cJSON *j=get_json_key(url,32768,account_key);failed=!passport_public_bus_parse_lta(j,stop,time(NULL));cJSON_Delete(j);
  bus_error=passport_direct_error();if(failed&&bus_error==DIRECT_OK)bus_error=DIRECT_RESPONSE;
  if(failed){if(failures<3)failures++;bus_next=now+(int64_t)(failures==1?120:failures==2?240:300)*1000000;}
  else {failures=0;bus_next=now+60000000;}
  if(time(NULL)<1700000000)bus_next=now+5000000;
 }
 atomic_store(&last_error,failed?bus_error:DIRECT_OK);
 return passport_public_bus_page(page,time(NULL),failed);
}

void passport_direct_release_caches(void){
 cJSON_Delete(weather_cache);weather_cache=NULL;weather_next=0;
 cJSON_Delete(haze_cache);haze_cache=NULL;haze_next=0;
 passport_public_bus_reset();bus_next=0;
}
