#include "passport_usage.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
static void quota(const cJSON *raw,time_t now,unsigned en,unsigned index,char *out,size_t cap){
 const cJSON *used=cJSON_GetObjectItemCaseSensitive(raw,"usedPercent"),*reset=cJSON_GetObjectItemCaseSensitive(raw,"resetsAt");
 char remaining[16]="--",when[32]="--",label[20];
 const cJSON *duration=cJSON_GetObjectItemCaseSensitive(raw,"windowDurationMins");
 snprintf(label,sizeof(label),en?"Window %u":"时段 %u",index);
 if(cJSON_IsNumber(duration)&&isfinite(duration->valuedouble)&&duration->valuedouble>=1&&duration->valuedouble<=525600){
  double minutes=duration->valuedouble;
  if(fmod(minutes,1440)==0)snprintf(label,sizeof(label),"%.0fd",minutes/1440);
  else if(fmod(minutes,60)==0)snprintf(label,sizeof(label),"%.0fh",minutes/60);
  else snprintf(label,sizeof(label),"%.0fm",minutes);
 }
 if(cJSON_IsNumber(used)&&isfinite(used->valuedouble)&&used->valuedouble>=0&&used->valuedouble<=100)snprintf(remaining,sizeof(remaining),"%.0f%%",100-used->valuedouble);
 if(cJSON_IsNumber(reset)&&isfinite(reset->valuedouble)&&reset->valuedouble>=1700000000&&reset->valuedouble<=4102444800&&now>=1700000000){
  if(reset->valuedouble<=now)snprintf(when,sizeof(when),"%s",en?"refresh":"请刷新");
  else {long mins=(long)ceil((reset->valuedouble-now)/60);snprintf(when,sizeof(when),"%ldh %ldm",mins/60,mins%60);}
 }
 snprintf(out,cap,en?"%s left %s\nReset %s":"%s 剩余 %s\n重置 %s",label,remaining,when);
}
void passport_usage_format(const cJSON *payload,time_t now,unsigned en,char *out,size_t cap){
 if(!cap)return;
 out[0]=0;unsigned count=0;
 const cJSON *item;cJSON_ArrayForEach(item,cJSON_GetObjectItemCaseSensitive(payload,"usage")){
  const cJSON *provider=cJSON_GetObjectItemCaseSensitive(item,"provider"),*status=cJSON_GetObjectItemCaseSensitive(item,"status");
  if(!cJSON_IsString(provider)||(strcmp(provider->valuestring,"codex")&&strcmp(provider->valuestring,"claude")))continue;
  char row[224],one[72],two[72];const char *name=!strcmp(provider->valuestring,"codex")?"Codex":"Claude";
  if(!cJSON_IsString(status)||strcmp(status->valuestring,"ok"))snprintf(row,sizeof(row),"%s\n%s\f",name,en?"Not connected / unavailable\nLink account in tracker":"未连接或不可用\n请在用量服务连接账号");
  else {quota(cJSON_GetObjectItemCaseSensitive(item,"primary"),now,en,1,one,sizeof(one));quota(cJSON_GetObjectItemCaseSensitive(item,"secondary"),now,en,2,two,sizeof(two));
   snprintf(row,sizeof(row),"%s #%u\n%s\n\n%s\f",name,count+1,one,two);
  }
  size_t used=strlen(out),n=strlen(row);if(used+n>=cap)break;memcpy(out+used,row,n+1);count++;
 }
 if(!count)snprintf(out,cap,"%s",en?"No usage available":"暂无账号用量");
}
