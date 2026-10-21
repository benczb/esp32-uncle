#include "passport_model.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

int passport_test_command(const char *line){
 static const char *commands[]={"@passport status","@passport up","@passport down","@passport ok",
                              "@passport hold-up","@passport hold-down","@passport hold-ok",
                              "@passport wifi-awake","@passport wifi-legacy","@passport wifi-normal","@passport wifi-baseline","@passport setup","@passport release-down","@passport refresh-weather","@passport opus-test","@passport opus-test-small"};
 if(!line)return -1;
 for(unsigned i=0;i<sizeof(commands)/sizeof(commands[0]);i++)if(!strcmp(line,commands[i]))return (int)i;
 return -1;
}
bool passport_relay_pcm_frame_valid(const uint8_t *frame,size_t bytes,uint32_t generation){
 if(!frame || bytes<6 || bytes>1028 || (bytes&1))return false;
 uint32_t received=(uint32_t)frame[0]|(uint32_t)frame[1]<<8|(uint32_t)frame[2]<<16|(uint32_t)frame[3]<<24;
 return received==generation;
}
static const passport_page_t apps[] = { PAGE_BUS, PAGE_TRAIN, PAGE_ATHENA, PAGE_HERMES,
 PAGE_MUSE, PAGE_CARD, PAGE_RADIO, PAGE_WEATHER, PAGE_PREFERENCES, PAGE_TRAFFIC };
void passport_navigate(passport_model_t *m, unsigned key, bool hold) {
 if (key > 2) return;
 if (hold) { if(key == 2) {
  if(m->page==PAGE_SETTINGS || m->page==PAGE_APPEARANCE || m->page==PAGE_BRIGHTNESS || m->page==PAGE_LANGUAGE){m->page=PAGE_PREFERENCES;m->reply_page=0;}
  else m->page=m->page==PAGE_APPS?PAGE_HOME:PAGE_APPS;
 }
 else if(key == 1 && m->page == PAGE_HOME) m->page = PAGE_SETTINGS;
 return; }
 if(m->page == PAGE_HOME) { m->page = key == 0 ? PAGE_CARD : key == 1 ? PAGE_BUS : PAGE_APPS; m->reply_page=0; }
 else if(m->page == PAGE_APPS) {
  const unsigned count=sizeof(apps)/sizeof(apps[0]);
  if(key == 0) m->selected=(m->selected+count-1)%count;
  else if(key == 1) m->selected=(m->selected+1)%count;
  else if(key == 2) { m->page=apps[m->selected]; m->reply_page=0; }
 } else if(m->page==PAGE_PREFERENCES){
  if(key==0)m->reply_page=(m->reply_page+3)%4;
  else if(key==1)m->reply_page=(m->reply_page+1)%4;
  else {static const passport_page_t pages[]={PAGE_SETTINGS,PAGE_APPEARANCE,PAGE_BRIGHTNESS,PAGE_LANGUAGE};m->page=pages[m->reply_page%4];m->reply_page=0;}
 } else if(m->page==PAGE_LANGUAGE){
  if(key<2)m->reply_page=(m->reply_page+1)%2;
 } else if(m->page==PAGE_BRIGHTNESS){
  if(key==0 && m->reply_page<100)m->reply_page+=10;
  else if(key==1 && m->reply_page>10)m->reply_page-=10;
 } else if(m->page==PAGE_APPEARANCE){
  if(key==0)m->reply_page=(m->reply_page+2)%3;
  else if(key==1)m->reply_page=(m->reply_page+1)%3;
 } else if(m->page == PAGE_RADIO) {
  if(key == 0 && m->station) m->station--;
  else if(key == 1) m->station++;
  else if(key == 2) m->playing = !m->playing;
 } else if(m->page == PAGE_TRAFFIC) {
  /* Up/Down choose which contact QR the QR view shows; OK (handled by the
   * main loop) toggles between the light and the QR view. */
  if(key == 0) m->traffic_qr=(m->traffic_qr+PASSPORT_TRAFFIC_QR_COUNT-1)%PASSPORT_TRAFFIC_QR_COUNT;
  else if(key == 1) m->traffic_qr=(m->traffic_qr+1)%PASSPORT_TRAFFIC_QR_COUNT;
 } else if(m->page == PAGE_TRAIN || m->page == PAGE_HERMES || m->page == PAGE_MUSE){if(key==0 && m->reply_page)m->reply_page--;else if(key==1 && m->reply_page<32767)m->reply_page++;}
 else if(m->page == PAGE_CARD) {m->reply_page=(m->reply_page+1)%PASSPORT_CARD_VIEWS;} else { if(key == 0 && m->reply_page) m->reply_page--;
 else if(key == 1 && m->reply_page < 63) m->reply_page++; }
}
size_t passport_utf8_prefix(const char *text, size_t limit) {
 size_t len=strlen(text); if(len <= limit) return len;
 while(limit && (((unsigned char)text[limit] & 0xc0) == 0x80)) limit--;
 return limit;
}

void passport_bus_navigate(passport_model_t *m,unsigned key,unsigned stops,unsigned pages){
 if(!stops){m->bus_stop=0;m->reply_page=0;return;}
 if(!pages)pages=1;
 m->bus_stop%=stops;
 if(key==0){if(m->reply_page)m->reply_page--;else {m->bus_stop=m->bus_stop?m->bus_stop-1:stops-1;m->reply_page=0;}}
 if(key==1){if(m->reply_page+1<pages)m->reply_page++;else {m->bus_stop=(m->bus_stop+1)%stops;m->reply_page=0;}}
}

bool passport_wifi_navigate(passport_model_t *m,unsigned key,unsigned profiles){
 if(!profiles)return false;
 m->wifi_profile%=profiles;
 if(key==0)m->wifi_profile=(m->wifi_profile+profiles-1)%profiles;
 else if(key==1)m->wifi_profile=(m->wifi_profile+1)%profiles;
 return key==2;
}

bool passport_wifi_should_retry(unsigned failures,uint32_t now,uint32_t deadline){return failures<3 && deadline && (int32_t)(deadline-now)>0;}

bool passport_valid_qr_url(const char *url){
 if(!url)return false;
 if(strlen(url)>256)return false;
 if(url[0] && strncmp(url,"https://",8) && strncmp(url,"http://",7) && strncmp(url,"weixin://",9))return false;
 for(const unsigned char *p=(const unsigned char *)url;*p;p++)if(*p<=32 || *p==127)return false;
 return true;
}
bool passport_valid_clock(double value){return isfinite(value)&&value>=1700000000&&value<=4102444800;}

bool passport_resolve_profile_password(char *out,size_t cap,const char *stored,const char *entered){
 const char *chosen=entered&&entered[0]?entered:stored;
 if(!chosen || strlen(chosen)<8 || strlen(chosen)>63 || strlen(chosen)>=cap)return false;
 memcpy(out,chosen,strlen(chosen)+1);return true;
}

bool passport_service_allowed(passport_page_t active,passport_page_t requested){
 if((unsigned)active>PAGE_TRAFFIC || (unsigned)requested>PAGE_TRAFFIC)return false;
 if(requested==PAGE_WEATHER)return true;
 if(requested==PAGE_APPS || requested==PAGE_CARD)return false;
 return active==requested;
}

bool passport_valid_lta_key(const char *key){
 if(!key || strlen(key)>128)return false;
 for(const unsigned char *p=(const unsigned char *)key;*p;p++)if(*p<=32 || *p>=127)return false;
 return true;
}
bool passport_browser_relay_page(passport_page_t page){
 return page==PAGE_SETTINGS || page==PAGE_APPS || page==PAGE_HERMES ||
  page==PAGE_ATHENA || page==PAGE_RADIO || page==PAGE_PREFERENCES;
}
bool passport_web_required(passport_page_t page,bool setup_ap,bool relay_active){
 if(page==PAGE_HOME)return setup_ap;
 if(page==PAGE_APPS || page==PAGE_RADIO || page==PAGE_PREFERENCES)return relay_active;
 return passport_browser_relay_page(page);
}

bool passport_assistant_volume_key(passport_model_t *m,unsigned key,bool hold,int *delta){
 *delta=0;if((m->page!=PAGE_HERMES && m->page!=PAGE_MUSE) || key>2)return false;
 if(hold && key==2){m->volume_mode=false;return false;}
 if(key==2 && !hold){m->volume_mode=!m->volume_mode;return true;}
 if(m->volume_mode){*delta=key==0?10:-10;return true;}return false;
}

void passport_screensaver_init(passport_screensaver_t *state,int64_t now_us){
 state->last_activity_us=now_us;state->asleep=false;
}
bool passport_screensaver_activity(passport_screensaver_t *state,int64_t now_us){
 bool woke=state->asleep;state->asleep=false;state->last_activity_us=now_us;return woke;
}
bool passport_screensaver_poll(passport_screensaver_t *state,int64_t now_us,bool busy){
 if(state->asleep || busy)return false;
 /* A clock step backwards (or a stale timestamp) restarts the countdown
  * instead of sleeping immediately. */
 if(now_us<state->last_activity_us){state->last_activity_us=now_us;return false;}
 if(now_us-state->last_activity_us<PASSPORT_SCREENSAVER_IDLE_US)return false;
 state->asleep=true;return true;
}

passport_traffic_phase_t passport_traffic_phase(int64_t elapsed_us,int64_t total_us){
 if(elapsed_us<0)elapsed_us=0;
 if(total_us<=0 || elapsed_us>=total_us)return PASSPORT_TRAFFIC_PHASE_DONE;
 return elapsed_us<PASSPORT_TRAFFIC_RED_US?PASSPORT_TRAFFIC_PHASE_RED:PASSPORT_TRAFFIC_PHASE_GREEN;
}
unsigned passport_traffic_countdown(int64_t elapsed_us,int64_t total_us){
 if(passport_traffic_phase(elapsed_us,total_us)!=PASSPORT_TRAFFIC_PHASE_GREEN)return 0;
 int64_t remaining=total_us-elapsed_us;
 return (unsigned)((remaining+999999)/1000000);
}
bool passport_traffic_green_lit(int64_t elapsed_us,int64_t total_us){
 if(passport_traffic_phase(elapsed_us,total_us)!=PASSPORT_TRAFFIC_PHASE_GREEN)return false;
 if(total_us-elapsed_us>PASSPORT_TRAFFIC_FLASH_US)return true;
 /* Flashing green man: 600 ms period, lit for the first 360 ms. */
 return elapsed_us%600000<360000;
}

void passport_clock_format(char *out,size_t capacity,unsigned weekday,unsigned hour,unsigned minute,bool synced){
 static const char *days[]={"SUN","MON","TUE","WED","THU","FRI","SAT"};
 if(!out || !capacity)return;
 if(!synced || weekday>6 || hour>23 || minute>59){snprintf(out,capacity,"--- --:--");return;}
 snprintf(out,capacity,"%s %02u:%02u",days[weekday],hour,minute);
}
