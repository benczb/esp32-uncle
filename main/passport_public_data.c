#define _POSIX_C_SOURCE 200809L
#include "passport_public_data.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
static const char *string(cJSON *o,const char *key){cJSON *v=cJSON_GetObjectItem(o,key);return cJSON_IsString(v)?v->valuestring:"";}
int64_t passport_iso_epoch(const char *s){
 if(!s||strlen(s)<20)return -1;
 for(unsigned i=0;i<19;i++){
  char expected=i==4||i==7?'-':i==10?'T':i==13||i==16?':':0;
  if(expected?s[i]!=expected:(s[i]<'0'||s[i]>'9'))return -1;
 }
 int y,m,d,h,n,sec;if(sscanf(s,"%d-%d-%dT%d:%d:%d",&y,&m,&d,&h,&n,&sec)!=6)return -1;
 if(y<1970||y>2100||m<1||m>12||d<1||h<0||h>23||n<0||n>59||sec<0||sec>59)return -1;
 int days[]={31,28,31,30,31,30,31,31,30,31,30,31};if(y%4==0&&(y%100!=0||y%400==0))days[1]=29;
 if(d>days[m-1])return -1;
 const char *z=s+19;if(*z=='.'){z++;if(*z<'0'||*z>'9')return -1;while(*z>='0'&&*z<='9')z++;}
 int offset=0;
 if(*z=='Z'&&!z[1])offset=0;
 else if((*z=='+'||*z=='-')&&strlen(z)==6&&z[3]==':'){
  for(int i=1;i<=5;i++)if(i!=3&&(z[i]<'0'||z[i]>'9'))return -1;
  int oh=(z[1]-'0')*10+z[2]-'0',om=(z[4]-'0')*10+z[5]-'0';if(oh>23||om>59)return -1;
  offset=(oh*60+om)*60;if(*z=='-')offset=-offset;
 }else return -1;
 /* Gregorian date -> Unix days; no dependence on host TZ or DST. */
 int adjusted=y-(m<=2),era=adjusted/400,yoe=adjusted-era*400;
 int mp=m+(m>2?-3:9),doy=(153*mp+2)/5+d-1;
 int64_t day=(int64_t)era*146097+yoe*365+yoe/4-yoe/100+doy-719468;
 return day*86400+h*3600+n*60+sec-offset;
}
static void utf8_copy(char *dest,size_t size,const char *text){
 size_t n=strlen(text);if(n>=size){n=size-1;while(n&&((unsigned char)text[n]&0xc0)==0x80)n--;}
 memcpy(dest,text,n);dest[n]=0;
}
static cJSON *unavailable(const char *reason){cJSON *j=cJSON_CreateObject();if(j){cJSON_AddBoolToObject(j,"available",false);cJSON_AddStringToObject(j,"reason",reason);}return j;}
cJSON *passport_public_haze(cJSON *raw,const char *city,const char *region,double lat,double lon,bool has_location,time_t now){
 if(strcmp(city,"singapore"))return unavailable("outside_singapore");
 if(!strcmp(region,"unset")||(!strcmp(region,"auto")&&!has_location))return unavailable("location_required");
 if(!strcmp(region,"auto")&&(!isfinite(lat)||!isfinite(lon)||lat<1.15||lat>1.48||lon<103.60||lon>104.10))return unavailable("outside_singapore");
 cJSON *code=cJSON_GetObjectItem(raw,"code"),*data=cJSON_GetObjectItem(raw,"data");if(!cJSON_IsNumber(code)||code->valueint!=0)return NULL;
 char selected[12];snprintf(selected,sizeof(selected),"%s",region);
 if(!strcmp(region,"auto")){
  double nearest=1e9;cJSON *r;cJSON_ArrayForEach(r,cJSON_GetObjectItem(data,"regionMetadata")){
   cJSON *loc=cJSON_GetObjectItem(r,"labelLocation"),*a=cJSON_GetObjectItem(loc,"latitude"),*b=cJSON_GetObjectItem(loc,"longitude");
   if(!cJSON_IsNumber(a)||!cJSON_IsNumber(b))continue;
   double dx=a->valuedouble-lat,dy=(b->valuedouble-lon)*cos(lat*0.0174532925199433),distance=dx*dx+dy*dy;
   if(distance<nearest){nearest=distance;snprintf(selected,sizeof(selected),"%.11s",string(r,"name"));}
  }
 }
 cJSON *latest=NULL,*item;int64_t stamp=-1;
 cJSON_ArrayForEach(item,cJSON_GetObjectItem(data,"items")){int64_t t=passport_iso_epoch(string(item,"timestamp"));if(t>stamp){stamp=t;latest=item;}}
 cJSON *readings=cJSON_GetObjectItem(cJSON_GetObjectItem(latest,"readings"),"psi_twenty_four_hourly"),*v=cJSON_GetObjectItem(readings,selected);
 if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<0||v->valuedouble>9999||stamp<0)return NULL;
 const char *category=v->valueint<=50?"good":v->valueint<=100?"moderate":v->valueint<=200?"unhealthy":v->valueint<=300?"very_unhealthy":"hazardous";
 cJSON *j=cJSON_CreateObject();if(!j)return NULL;
 cJSON_AddBoolToObject(j,"available",true);cJSON_AddNumberToObject(j,"value",v->valueint);cJSON_AddStringToObject(j,"category",category);
 cJSON_AddStringToObject(j,"region",selected);cJSON_AddStringToObject(j,"timestamp",string(latest,"timestamp"));cJSON_AddBoolToObject(j,"stale",now-stamp>7200);return j;
}
bool passport_public_haze_compact(cJSON *raw){
 cJSON *data=cJSON_GetObjectItem(raw,"data"),*items=cJSON_GetObjectItem(data,"items");
 cJSON *code=cJSON_GetObjectItem(raw,"code");
 if(!cJSON_IsNumber(code)||code->valueint || !cJSON_IsArray(items))return false;
 cJSON *latest=NULL,*item;int64_t stamp=-1;
 cJSON_ArrayForEach(item,items){int64_t t=passport_iso_epoch(string(item,"timestamp"));if(t>stamp){stamp=t;latest=item;}}
 cJSON *readings=cJSON_GetObjectItem(latest,"readings");
 cJSON *psi=cJSON_GetObjectItem(readings,"psi_twenty_four_hourly");
 if(!latest || !cJSON_IsObject(psi))return false;
 /* Unlink by node identity, preserving the same tie-breaking/latest-item
  * selection and case-insensitive lookup as passport_public_haze(). */
 for(item=items->child;item;){
  cJSON *next=item->next;
  if(item!=latest)cJSON_Delete(cJSON_DetachItemViaPointer(items,item));
  item=next;
 }
 for(item=readings->child;item;){
  cJSON *next=item->next;
  if(item!=psi)cJSON_Delete(cJSON_DetachItemViaPointer(readings,item));
  item=next;
 }
 return true;
}
#define MAX_ROWS 48
typedef struct {char service[13],label[64],visit[9];int entry,count;int64_t at[3];bool terminating[3];} bus_row_t;
static bus_row_t *rows;static unsigned row_count;static int64_t bus_stamp;static char bus_timestamp[40];static bool bus_valid;
void passport_public_bus_reset(void){free(rows);rows=NULL;row_count=0;bus_valid=false;}
static bool parse_bus_services(cJSON *services,int64_t timestamp,const char *updated,bool lta){
 if(timestamp<0||!cJSON_IsArray(services))return false;
 /* Three arrivals can have distinct destinations/visits. Keep the previous
  * cache intact if its replacement cannot be allocated. No startup row pool. */
 unsigned count=(unsigned)cJSON_GetArraySize(services);
 unsigned capacity=count>MAX_ROWS/3?MAX_ROWS:count*3;
 bus_row_t *replacement=capacity?realloc(rows,capacity*sizeof(*rows)):NULL;
 if(capacity && !replacement)return false;
 if(!capacity)free(rows);
 rows=replacement;row_count=0;bus_valid=false;
 bus_stamp=timestamp;snprintf(bus_timestamp,sizeof(bus_timestamp),"%.39s",updated);
 int entry=0;cJSON *service;
 cJSON_ArrayForEach(service,services){
  const char *number=string(service,"ServiceNo");if(!number[0]||strlen(number)>12){entry++;continue;}
  const char *slots[]={"NextBus","NextBus2","NextBus3"};
  for(unsigned k=0;k<3;k++){
   cJSON *arrival=cJSON_GetObjectItem(service,slots[k]);int64_t at=passport_iso_epoch(string(arrival,"EstimatedArrival"));if(at<0)continue;
   char label[64],visit[9];
   if(lta){const char *destination=string(arrival,"DestinationCode");snprintf(label,sizeof(label),destination[0]?"To stop %.5s":"%s",destination);}
   else utf8_copy(label,sizeof(label),string(arrival,"Label"));
   snprintf(visit,sizeof(visit),"%.8s",string(arrival,"VisitNumber"));
   bus_row_t *row=NULL;
   for(unsigned i=0;i<row_count;i++)if(!strcmp(rows[i].service,number)&&!strcmp(rows[i].label,label)&&!strcmp(rows[i].visit,visit)&&(label[0]||rows[i].entry==entry)){row=&rows[i];break;}
   if(!row){if(row_count>=capacity)continue;row=&rows[row_count++];memset(row,0,sizeof(*row));strcpy(row->service,number);strcpy(row->label,label);strcpy(row->visit,visit);row->entry=entry;}
   if(row->count<3){int i=row->count++;row->at[i]=at;row->terminating[i]=cJSON_IsTrue(cJSON_GetObjectItem(arrival,"Terminating"));}
  }entry++;
 }
 for(unsigned r=0;r<row_count;r++)for(int i=1;i<rows[r].count;i++)for(int k=i;k>0&&rows[r].at[k]<rows[r].at[k-1];k--){
  int64_t tmp=rows[r].at[k];rows[r].at[k]=rows[r].at[k-1];rows[r].at[k-1]=tmp;
  bool b=rows[r].terminating[k];rows[r].terminating[k]=rows[r].terminating[k-1];rows[r].terminating[k-1]=b;
 }
 if(!row_count){free(rows);rows=NULL;}
 else if(row_count<capacity){
  bus_row_t *compact=realloc(rows,row_count*sizeof(*rows));
  if(compact)rows=compact;
 }
 bus_valid=true;return true;
}
bool passport_public_bus_parse(cJSON *raw,const char *stop){
 cJSON *source=NULL,*s;cJSON_ArrayForEach(s,cJSON_GetObjectItem(raw,"busStops")){if(!strcmp(string(s,"BusStopCode"),stop)){source=s;break;}}
 return parse_bus_services(cJSON_GetObjectItem(source,"Services"),passport_iso_epoch(string(source,"UpdatedAt")),string(source,"UpdatedAt"),false);
}
bool passport_public_bus_parse_lta(cJSON *raw,const char *stop,time_t fetched){
 if(!stop || strcmp(string(raw,"BusStopCode"),stop) || fetched<1700000000)return false;
 /* LTA has no source UpdatedAt field. This is our fetch time in Singapore. */
 time_t sg=fetched+8*3600;struct tm tm;char stamp[40];
 if(!gmtime_r(&sg,&tm) || !strftime(stamp,sizeof(stamp),"%Y-%m-%dT%H:%M:%S+08:00",&tm))return false;
 return parse_bus_services(cJSON_GetObjectItem(raw,"Services"),fetched,stamp,true);
}
cJSON *passport_public_bus_page(unsigned page,time_t now,bool failed){
 if(!bus_valid)return unavailable("upstream_unavailable");
 unsigned pages=(row_count+3)/4;if(!pages)pages=1;if(page>=pages)page=pages-1;
 bool stale=failed||now-bus_stamp>180;
 cJSON *j=cJSON_CreateObject();if(!j)return NULL;cJSON_AddBoolToObject(j,"available",true);cJSON_AddNumberToObject(j,"page",page);cJSON_AddNumberToObject(j,"pages",pages);
 cJSON_AddBoolToObject(j,"stale",stale);cJSON_AddStringToObject(j,"updated_at",bus_timestamp);cJSON *list=cJSON_AddArrayToObject(j,"rows");
 for(unsigned n=page*4;n<row_count&&n<page*4+4;n++){
  bus_row_t *r=&rows[n];cJSON *row=cJSON_CreateObject();if(!row)break;cJSON_AddItemToArray(list,row);
  cJSON_AddStringToObject(row,"service",r->service);cJSON_AddStringToObject(row,"destination",r->label);cJSON_AddStringToObject(row,"visit",r->visit);
  cJSON *eta=cJSON_AddArrayToObject(row,"eta"),*term=cJSON_AddArrayToObject(row,"terminating");
  for(int i=0;i<r->count;i++){
   int64_t delta=r->at[i]-now;if(delta < -120)continue;
   char cell[12];if(stale)strcpy(cell,"--");else if(delta<=60)strcpy(cell,"Arr");else if(delta/60>60)strcpy(cell,"60+");else snprintf(cell,sizeof(cell),"%d",(int)(delta/60));
   cJSON_AddItemToArray(eta,cJSON_CreateString(cell));cJSON_AddItemToArray(term,cJSON_CreateBool(r->terminating[i]));
  }
 }return j;
}
