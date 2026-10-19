#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_http_client.h"
#include "cJSON.h"
struct mock_http {const char *body;size_t at;};
static struct mock_http http;
static size_t budget,live,peak,peak_during_tls;
typedef union {max_align_t alignment;size_t size;} allocation_header_t;
static void *body_malloc(size_t n){
 if(n>budget-live)return NULL;
 allocation_header_t *h=malloc(sizeof(*h)+n);if(!h)return NULL;h->size=n;live+=n;if(live>peak)peak=live;return h+1;
}
static void body_free(void *p){if(p){allocation_header_t *h=(allocation_header_t *)p-1;live-=h->size;free(h);}}
#define malloc body_malloc
#define free body_free
#include "../main/passport_direct.c"
#undef malloc
#undef free
static unsigned key_headers, opens;static int response_status=200;
void esp_crt_bundle_attach(void){}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t h,const char *key,const char *value){(void)h;if(!strcmp(key,"AccountKey")){assert(!strcmp(value,"test-lta-key"));key_headers++;}return ESP_OK;}
int64_t esp_timer_get_time(void){return 1000000000;}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg){assert(cfg->crt_bundle_attach && cfg->disable_auto_redirect);assert(cfg->buffer_size_tx>=(int)strlen(cfg->url)+128);assert(!strstr(cfg->url,"test-lta-key"));http.at=0;return &http;}
esp_err_t esp_http_client_open(esp_http_client_handle_t h,int n){(void)h;(void)n;opens++;return ESP_OK;}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h){return (int64_t)strlen(h->body);}
int esp_http_client_get_status_code(esp_http_client_handle_t h){(void)h;return response_status;}
int esp_http_client_read(esp_http_client_handle_t h,char *out,int capacity){size_t n=strlen(h->body)-h->at;if(n>(size_t)capacity)n=capacity;if(n>127)n=127;memcpy(out,h->body+h->at,n);h->at+=n;return (int)n;}
/* A completed network response may still have application bytes buffered. */
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h){(void)h;return true;}
void esp_http_client_close(esp_http_client_handle_t h){(void)h;}
void esp_http_client_cleanup(esp_http_client_handle_t h){(void)h;peak_during_tls=peak;budget+=32768;}
static void reset(const char *body,size_t available){http.body=body;budget=available;peak=live=peak_during_tls=0;}
int main(void){
 char signed_url[3500];memset(signed_url,'a',sizeof(signed_url));memcpy(signed_url,"https://example.test/file?signature=",35);signed_url[sizeof(signed_url)-1]=0;
 reset("{\"ok\":true}",4096);cJSON *signed_result=get_json(signed_url,1024);assert(signed_result);cJSON_Delete(signed_result);assert(live==0);

 reset("{\"ok\":true}",4096);cJSON *j=get_json("https://example.test",32768);assert(j);assert(cJSON_IsTrue(cJSON_GetObjectItem(j,"ok")));cJSON_Delete(j);assert(peak_during_tls<4096 && live==0);
 char large[8000];memset(large,'a',sizeof(large));memcpy(large,"{\"text\":\"",9);large[7164]='"';large[7165]='}';large[7166]=0;
 reset(large,8192);j=get_json("https://example.test",32768);assert(j);assert(strlen(cJSON_GetObjectItem(j,"text")->valuestring)==7155);cJSON_Delete(j);assert(peak_during_tls<8192 && live==0);
 reset(large,4096);j=get_json("https://example.test",32768);assert(!j && passport_direct_error()==DIRECT_MEMORY && live==0);
 reset("{\"ok\":true}",4096);j=get_json("https://example.test",11);assert(j);cJSON_Delete(j);assert(live==0);
 reset("{\"ok\":true}                       ",4096);j=get_json("https://example.test",11);assert(!j && live==0);
 reset("{}",4096);assert(!passport_direct_weather("singapore"));assert(passport_direct_error()==DIRECT_RESPONSE);
 atomic_store(&last_error,DIRECT_OK); /* An unrelated successful service must not hide a cached weather error. */
 assert(!passport_direct_weather("singapore"));assert(passport_direct_error()==DIRECT_RESPONSE && live==0);
 reset("{\"current\":{\"temperature_2m\":30}}",4096);unsigned weather_before=opens;
 passport_direct_weather_refresh();j=passport_direct_weather("singapore");assert(j&&opens==weather_before+1);cJSON_Delete(j);
 weather_before=opens;j=passport_direct_weather("singapore");assert(j&&opens==weather_before);cJSON_Delete(j);
 /* A forced request cannot claim success from an old warm cache. */
 reset("{}",4096);passport_direct_weather_refresh();assert(!passport_direct_weather("singapore"));assert(opens==weather_before+1);
 reset("{}",4096);unsigned before=opens;assert(!passport_direct_bus("82009",0,""));assert(opens==before && passport_direct_error()==DIRECT_KEY);
 assert(!passport_direct_bus("82009",0,"bad\r\nHeader: value"));assert(opens==before);
 reset("{\"BusStopCode\":\"82009\",\"Services\":[]}",4096);
 j=passport_direct_bus("82009",0,"test-lta-key");assert(j&&cJSON_IsTrue(cJSON_GetObjectItem(j,"available")));cJSON_Delete(j);assert(key_headers==1);
 response_status=403;reset("{}",4096);j=passport_direct_bus("00481",0,"test-lta-key");assert(j&&!cJSON_IsTrue(cJSON_GetObjectItem(j,"available")));cJSON_Delete(j);assert(passport_direct_error()==DIRECT_AUTH);
 atomic_store(&last_error,DIRECT_OK);before=opens;j=passport_direct_bus("00481",0,"test-lta-key");cJSON_Delete(j);assert(opens==before&&passport_direct_error()==DIRECT_AUTH);

 response_status=200;reset("{\"BusStopCode\":\"00481\",\"Services\":[]}",4096);
 before=opens;passport_direct_release_caches();j=passport_direct_bus("00481",0,"test-lta-key");
 assert(j && opens==before+1 && cJSON_IsTrue(cJSON_GetObjectItem(j,"available")));cJSON_Delete(j);
 puts("Public response reader: buffered completion, small-memory budget, growth failure and exact limits PASS");
}
