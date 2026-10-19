#include "passport_train.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdarg.h>
#include <time.h>
typedef struct {const uint8_t *p,*end;bool bad;} reader_t;
typedef struct {unsigned id,wire;uint64_t number;const uint8_t *data;size_t size;} field_t;
typedef struct {passport_train_feed_t feed;size_t cap;bool bad;} writer_t;
static bool varint(reader_t *r,uint64_t *n){
 *n=0;
 for(unsigned shift=0;shift<70;shift+=7){
  if(r->p==r->end){r->bad=true;return false;}
  uint8_t v=*r->p++;if(shift==63 && v>1){r->bad=true;return false;}
  *n|=(uint64_t)(v&127)<<shift;if(!(v&128))return true;
 }
 r->bad=true;return false;
}
static bool next(reader_t *r,field_t *f){
 if(r->bad||r->p==r->end)return false;
 uint64_t tag,n;if(!varint(r,&tag)||!(tag>>3)||tag>>3>0x1fffffff){r->bad=true;return false;}
 *f=(field_t){.id=(unsigned)(tag>>3),.wire=tag&7};
 if(f->wire==0)return varint(r,&f->number);
 if(f->wire==2){if(!varint(r,&n)||n>(uint64_t)(r->end-r->p)){r->bad=true;return false;}f->size=(size_t)n;}
 else if(f->wire==1)f->size=8;else if(f->wire==5)f->size=4;else {r->bad=true;return false;}
 if(f->size>(size_t)(r->end-r->p)){r->bad=true;return false;}
 f->data=r->p;r->p+=f->size;return true;
}
static reader_t sub(field_t f){return (reader_t){f.data,f.data+f.size,false};}
static void append(writer_t *w,const char *s,size_t n){
 if(w->bad)return;
 if(n>PASSPORT_TRAIN_TEXT_LIMIT-w->feed.length){w->bad=true;return;}
 size_t need=w->feed.length+n+1;
 if(need>w->cap){size_t cap=(need+511)&~(size_t)511;if(cap>PASSPORT_TRAIN_TEXT_LIMIT+1)cap=PASSPORT_TRAIN_TEXT_LIMIT+1;char *p=realloc(w->feed.text,cap);if(!p){w->bad=true;return;}w->feed.text=p;w->cap=cap;}
 memcpy(w->feed.text+w->feed.length,s,n);w->feed.length+=n;w->feed.text[w->feed.length]=0;
}
static void line(writer_t *w,const char *format,...){char s[128];va_list a;va_start(a,format);int n=vsnprintf(s,sizeof(s),format,a);va_end(a);if(n<0||(size_t)n>=sizeof(s)){w->bad=true;return;}append(w,s,(size_t)n);}
static void string(writer_t *w,const char *name,field_t f){
 if(f.wire!=2||memchr(f.data,0,f.size)){w->bad=true;return;}
 line(w,"%s: ",name);append(w,(const char *)f.data,f.size);append(w,"\n",1);
}
static void stamp(writer_t *w,const char *name,uint64_t n){
 if(n>4102444800ULL){line(w,"%s: %" PRIu64 "\n",name,n);return;}
 time_t t=(time_t)n+28800;struct tm *tm=gmtime(&t);char s[32];
 if(!tm||!strftime(s,sizeof(s),"%d %b %Y %H:%M SGT",tm)){w->bad=true;return;}line(w,"%s: %s\n",name,s);
}
enum {ALERT,RANGE,SELECTOR,TRIP,UPDATE,STOP,EVENT,TRANSLATED,TRANSLATION,VEHICLE};
typedef struct {unsigned id;int child;const char *name;int format;} spec_t;
/* Field numbers follow the published GTFS-Realtime proto2 contract. Format:
 * 0 text, 1 unsigned, 2 signed int32, 3 timestamp; child>=0 nested message. */
#define S(i,n) {i,-1,n,0}
#define U(i,n) {i,-1,n,1}
#define I(i,n) {i,-1,n,2}
#define T(i,n) {i,-1,n,3}
#define M(i,c,n) {i,c,n,0}
static const spec_t alert[]={M(1,RANGE,"Active period"),M(2,RANGE,"Communication"),M(3,RANGE,"Impact period"),M(5,SELECTOR,"Affected"),U(6,"Cause code"),U(7,"Effect code"),M(8,TRANSLATED,"URL"),M(10,TRANSLATED,"Headline"),M(11,TRANSLATED,"Description"),M(12,TRANSLATED,"Spoken headline"),M(13,TRANSLATED,"Spoken description"),U(14,"Severity code"),M(17,TRANSLATED,"Cause detail"),M(18,TRANSLATED,"Effect detail")};
static const spec_t range[]={T(1,"From"),T(2,"Until")};
static const spec_t selector[]={S(1,"Agency"),S(2,"Route"),U(3,"Route type"),M(4,TRIP,"Trip"),S(5,"Stop"),U(6,"Direction")};
static const spec_t trip[]={S(1,"Trip ID"),S(2,"Start time"),S(3,"Start date"),U(4,"Schedule code"),S(5,"Route"),U(6,"Direction")};
static const spec_t update[]={M(1,TRIP,"Trip"),M(2,STOP,"Stop update"),M(3,VEHICLE,"Vehicle"),T(4,"Updated"),I(5,"Delay seconds")};
static const spec_t stop[]={U(1,"Stop sequence"),M(2,EVENT,"Arrival"),M(3,EVENT,"Departure"),S(4,"Stop"),U(5,"Schedule code"),U(7,"Occupancy code")};
static const spec_t event[]={I(1,"Delay seconds"),T(2,"Time"),I(3,"Uncertainty seconds"),T(4,"Scheduled time")};
static const spec_t translated[]={M(1,TRANSLATION,"Translation")};
static const spec_t translation[]={S(1,"Text"),S(2,"Language")};
static const spec_t vehicle[]={S(1,"Vehicle ID"),S(2,"Label"),S(3,"License")};
static const struct {const spec_t *s;size_t n;} schemas[]={
#define SC(x) {x,sizeof(x)/sizeof(x[0])}
 SC(alert),SC(range),SC(selector),SC(trip),SC(update),SC(stop),SC(event),SC(translated),SC(translation),SC(vehicle)};
static const char *enum_name(int type,unsigned field,uint64_t value){
 static const char *effects[]={NULL,"No service","Reduced service","Significant delays","Detour","Additional service","Modified service","Other effect","Unknown effect","Stop moved","No effect","Accessibility issue"};
 static const char *causes[]={NULL,"Unknown cause","Other cause","Technical problem","Strike","Demonstration","Accident","Holiday","Weather","Maintenance","Construction","Police activity","Medical emergency","Special event"};
 if(type==ALERT&&field==7&&value<sizeof(effects)/sizeof(effects[0]))return effects[value];
 if(type==ALERT&&field==6&&value<sizeof(causes)/sizeof(causes[0]))return causes[value];
 if(type==TRIP&&field==4){if(value==3)return "Cancelled";if(value==7)return "Deleted";if(value==0)return "Scheduled";}
 if(type==STOP&&field==5){if(value==1)return "Skipped stop";if(value==2)return "No prediction";}
 return NULL;
}
static void walk(writer_t *w,reader_t r,int type,unsigned depth){
 if(depth>8){w->bad=true;return;}field_t f;
 while(next(&r,&f)){
  const spec_t *s=NULL;for(size_t i=0;i<schemas[type].n;i++)if(schemas[type].s[i].id==f.id){s=&schemas[type].s[i];break;}
  if(!s)continue;
  if(s->child>=0){if(f.wire!=2){w->bad=true;break;}line(w,"%s\n",s->name);walk(w,sub(f),s->child,depth+1);}
  else if(s->format==0)string(w,s->name,f);
  else if(f.wire!=0){w->bad=true;break;}
  else if(s->format==3)stamp(w,s->name,f.number);
  else if(s->format==2)line(w,"%s: %" PRId32 "\n",s->name,(int32_t)(uint32_t)f.number);
  else {const char *label=enum_name(type,f.id,f.number);if(label)line(w,"%s: %s (%" PRIu64 ")\n",s->name,label,f.number);else line(w,"%s: %" PRIu64 "\n",s->name,f.number);}
 }
 if(r.bad)w->bad=true;
}
static bool active(reader_t alert_reader,uint64_t now,bool *bad){
 reader_t r=alert_reader;field_t f;bool periods=false,live=false;
 /* Prefer communication_period (2), then active_period (1). Impact periods
  * alone do not hide advance communications. */
 unsigned chosen=1;while(next(&r,&f))if(f.id==2)chosen=2;
 if(r.bad){*bad=true;return false;}
 r=alert_reader;
 while(next(&r,&f))if(f.id==chosen){
  if(f.wire!=2){*bad=true;return false;}reader_t t=sub(f);field_t p;uint64_t start=0,end=UINT64_MAX;
  while(next(&t,&p)){if((p.id==1||p.id==2)&&p.wire!=0)t.bad=true;if(p.id==1&&p.wire==0)start=p.number;if(p.id==2&&p.wire==0)end=p.number;}
  if(t.bad||end<start){*bad=true;return false;}periods=true;if(now>=start&&now<end)live=true;
 }
 if(r.bad)*bad=true;
 return !periods||live;
}
void passport_train_free(passport_train_feed_t *f){if(f){free(f->text);memset(f,0,sizeof(*f));}}
bool passport_train_decode(const uint8_t *bytes,size_t size,bool trips,uint64_t now,passport_train_feed_t *out){
 if(!out||!bytes||!size||size>PASSPORT_TRAIN_FEED_LIMIT)return false;
 writer_t w={0};reader_t r={bytes,bytes+size,false};field_t f;bool header=false;
 while(next(&r,&f)){
  if(f.id==1){
   if(header||f.wire!=2){w.bad=true;break;}reader_t h=sub(f);field_t p;bool version=false;
   while(next(&h,&p)){
    if(p.id==1&&p.wire==2&&p.size==3&&(!memcmp(p.data,"2.0",3)||!memcmp(p.data,"1.0",3)))version=true;
    if(p.id==2&&(p.wire!=0||p.number!=0))w.bad=true; /* no differential all-clear */
    if(p.id==3&&p.wire==0)w.feed.timestamp=p.number;
   }
   header=version&&!h.bad;
  }else if(f.id==2){
   if(f.wire!=2){w.bad=true;break;}reader_t entity=sub(f);field_t p,payload={0},id={0};bool deleted=false;
   while(next(&entity,&p)){
    if(p.id==1)id=p;
    if(p.id==2&&p.wire==0)deleted=p.number!=0;
    if(p.id==(trips?3u:5u))payload=p;
   }
   if(entity.bad||id.wire!=2||!id.size){w.bad=true;break;}
   if(deleted||!payload.id)continue;
   if(payload.wire!=2){w.bad=true;break;}
   if(!trips&&!active(sub(payload),now,&w.bad))continue;
   w.feed.alerts++;line(&w,"\n%s %u\n",trips?"Trip disruption":"Service alert",w.feed.alerts);string(&w,"Alert ID",id);
   size_t start=w.feed.length;walk(&w,sub(payload),trips?UPDATE:ALERT,0);
   if(!w.feed.headline[0]){
    const char *p=w.feed.text?strstr(w.feed.text+start,"Headline\n"):NULL;
    p=p?strstr(p,"Text: "):NULL;
    if(p)p+=6;else p=trips?"Train trip disruption":"Train service alert";
    size_t n=strcspn(p,"\r\n");if(n>=sizeof(w.feed.headline))n=sizeof(w.feed.headline)-1;
    while(n&&((unsigned char)p[n]&0xc0)==0x80)n--;
    memcpy(w.feed.headline,p,n);w.feed.headline[n]=0;
   }
  }
 }
 if(r.bad||!header||w.bad){passport_train_free(&w.feed);return false;}
 if(w.feed.timestamp>now+300){passport_train_free(&w.feed);return false;}
 if(w.feed.alerts && w.feed.timestamp)stamp(&w,"Feed updated",w.feed.timestamp);
 if(w.bad){passport_train_free(&w.feed);return false;}
 *out=w.feed;return true;
}
bool passport_train_download_url(const char *url){
 static const char prefix[]="https://dmprod-datasets.s3.ap-southeast-1.amazonaws.com/";
 if(!url||strncmp(url,prefix,sizeof(prefix)-1)||strlen(url)>4096)return false;
 const char *p=url+sizeof(prefix)-1;
 if(strncmp(p,"train-gtfs-real-time/",21)&&strncmp(p,"train-gtfs-trip-update/",23))return false;
 for(;*url;url++)if((unsigned char)*url<=32||(unsigned char)*url>=127||*url=='\\')return false;
 return true;
}
