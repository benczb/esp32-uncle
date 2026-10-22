#include "passport_model.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
int main(void) {
 assert(passport_valid_lta_key("") && passport_valid_lta_key("test-lta-key"));
 assert(!passport_valid_lta_key(NULL) && !passport_valid_lta_key("bad\r\nHeader"));
 assert(!passport_valid_lta_key(" leading-space") && !passport_valid_lta_key("空"));

 assert(passport_test_command("@passport status")==0);
 assert(passport_test_command("@passport refresh-weather")==13);
 assert(passport_test_command("@passport up")==1);
 assert(passport_test_command("@passport down")==2);
 assert(passport_test_command("@passport ok")==3);
 assert(passport_test_command("@passport hold-up")==4);
 assert(passport_test_command("@passport hold-down")==5);
 assert(passport_test_command("@passport hold-ok")==6);
 assert(passport_test_command("@passport wifi-awake")==7);
 assert(passport_test_command("@passport wifi-legacy")==8);
 assert(passport_test_command("@passport wifi-normal")==9);
 assert(passport_test_command("@passport wifi-baseline")==10);
 assert(passport_test_command("@passport setup")==11);
 assert(passport_test_command("@passport release-down")==12);
 assert(passport_test_command(NULL)==-1);
 assert(passport_test_command("@passport ok extra")==-1);
 assert(passport_test_command("@passport status\n")==-1);
 assert(passport_test_command("@passport token")==-1);
 uint8_t frame[1030]={0x78,0x56,0x34,0x12};
 assert(passport_relay_pcm_frame_valid(frame,6,0x12345678));
 assert(passport_relay_pcm_frame_valid(frame,1028,0x12345678));
 assert(!passport_relay_pcm_frame_valid(NULL,6,0x12345678));
 assert(!passport_relay_pcm_frame_valid(frame,5,0x12345678));
 assert(!passport_relay_pcm_frame_valid(frame,7,0x12345678));
 assert(!passport_relay_pcm_frame_valid(frame,1030,0x12345678));
 assert(!passport_relay_pcm_frame_valid(frame,6,0x12345679));
 passport_model_t shortcut={.page=PAGE_HOME,.reply_page=3};
 passport_navigate(&shortcut,1,false);assert(shortcut.page==PAGE_BUS && shortcut.reply_page==0);
 passport_model_t m={0}; passport_navigate(&m,2,false); assert(m.page==PAGE_APPS);
 /* Both boundaries wrap, and each item remains selectable after wrapping. */
 passport_navigate(&m,0,false);assert(m.selected==PASSPORT_APP_COUNT-1);
 passport_navigate(&m,1,false);assert(m.selected==0);
 const passport_page_t expected[]={PAGE_BUS,PAGE_TRAIN,PAGE_ATHENA,PAGE_HERMES,PAGE_MUSE,PAGE_CARD,PAGE_RADIO,PAGE_WEATHER,PAGE_PREFERENCES,PAGE_NAVIGATION,PAGE_SPOTIFY,PAGE_READER};
 for(unsigned i=0;i<3*PASSPORT_APP_COUNT;i++){
  assert(m.selected==i%PASSPORT_APP_COUNT);
  passport_navigate(&m,2,false);assert(m.page==expected[i%PASSPORT_APP_COUNT]);
  passport_navigate(&m,2,true);assert(m.page==PAGE_APPS);
  passport_navigate(&m,1,false);
 }
 passport_model_t settings={.page=PAGE_PREFERENCES};
 passport_navigate(&settings,1,false);assert(settings.reply_page==1);
 passport_navigate(&settings,2,false);assert(settings.page==PAGE_APPEARANCE && settings.reply_page==0);
 passport_navigate(&settings,1,false);passport_navigate(&settings,1,false);assert(settings.reply_page==2);
 passport_navigate(&settings,1,false);assert(settings.reply_page==0);
 passport_navigate(&settings,2,false);assert(settings.page==PAGE_APPEARANCE);
 passport_navigate(&settings,2,true);assert(settings.page==PAGE_PREFERENCES);
 passport_navigate(&settings,2,false);assert(settings.page==PAGE_SETTINGS);
 passport_navigate(&settings,2,true);assert(settings.page==PAGE_PREFERENCES);
 passport_navigate(&settings,1,false);passport_navigate(&settings,1,false);assert(settings.reply_page==2);
 passport_navigate(&settings,2,false);assert(settings.page==PAGE_BRIGHTNESS);
 settings.reply_page=70;
 for(unsigned i=0;i<20;i++)passport_navigate(&settings,0,false);
 assert(settings.reply_page==100);
 for(unsigned i=0;i<20;i++)passport_navigate(&settings,1,false);
 assert(settings.reply_page==10);
 passport_navigate(&settings,2,false);assert(settings.page==PAGE_BRIGHTNESS);
 passport_navigate(&settings,2,true);assert(settings.page==PAGE_PREFERENCES);
 passport_navigate(&settings,0,false);assert(settings.reply_page==3);
 passport_navigate(&settings,2,false);assert(settings.page==PAGE_LANGUAGE);
 passport_navigate(&settings,1,false);assert(settings.reply_page==1);
 passport_navigate(&settings,0,false);assert(settings.reply_page==0);
 passport_navigate(&settings,2,true);assert(settings.page==PAGE_PREFERENCES);
 assert(m.selected==0);
 for(unsigned i=0;i<3*PASSPORT_APP_COUNT;i++){passport_navigate(&m,0,false);assert(m.selected==(PASSPORT_APP_COUNT-1-i%PASSPORT_APP_COUNT));}
 m.selected=9;passport_navigate(&m,0,false);assert(m.selected==8);
 passport_navigate(&m,2,false);assert(m.page==PAGE_PREFERENCES);
 passport_navigate(&m,2,true); assert(m.page==PAGE_APPS);
 passport_navigate(&m,2,true); assert(m.page==PAGE_HOME);
 passport_navigate(&m,0,false); assert(m.page==PAGE_CARD);
 passport_navigate(&m,2,false);assert(m.reply_page==1);
 passport_navigate(&m,2,false);assert(m.reply_page==2);
 passport_navigate(&m,2,false);assert(m.reply_page==0);
 passport_navigate(&m,1,false);assert(m.reply_page==1);
 passport_navigate(&m,2,true);assert(m.page==PAGE_APPS);
 /* Screensaver: sleeps only after the full idle timeout, never while busy,
  * and any activity wakes it and restarts the countdown. */
 /* Audio and a background phone relay must not pin the backlight on. */
 assert(!passport_screensaver_keep_awake(PAGE_RADIO,false,false));
 assert(!passport_screensaver_keep_awake(PAGE_RADIO,false,true));
 assert(!passport_screensaver_keep_awake(PAGE_ATHENA,false,true));
 for(unsigned page=PAGE_HOME;page<=PAGE_LANGUAGE;page++)
  assert(passport_web_required((passport_page_t)page,false,true));
 assert(passport_screensaver_keep_awake(PAGE_SETTINGS,false,true));
 assert(passport_screensaver_keep_awake(PAGE_RADIO,true,true));
 passport_screensaver_t saver;passport_screensaver_init(&saver,1000000);
 assert(!saver.asleep);
 assert(!passport_screensaver_poll(&saver,1000000+PASSPORT_SCREENSAVER_IDLE_US-1,false));
 assert(!saver.asleep);
 assert(!passport_screensaver_poll(&saver,1000000+PASSPORT_SCREENSAVER_IDLE_US,true));
 assert(!saver.asleep);
 assert(passport_screensaver_poll(&saver,1000000+PASSPORT_SCREENSAVER_IDLE_US,false));
 assert(saver.asleep);
 assert(!passport_screensaver_poll(&saver,1000000+2*PASSPORT_SCREENSAVER_IDLE_US,false));
 assert(passport_screensaver_activity(&saver,2000000));
 assert(!saver.asleep);
 assert(!passport_screensaver_poll(&saver,2000000+PASSPORT_SCREENSAVER_IDLE_US-1,false));
 assert(!passport_screensaver_activity(&saver,2000000));
 /* A backwards clock step restarts the countdown instead of sleeping. */
 passport_screensaver_init(&saver,5000000);
 assert(!passport_screensaver_poll(&saver,4000000,false));
 assert(!saver.asleep);
 assert(passport_screensaver_poll(&saver,4000000+PASSPORT_SCREENSAVER_IDLE_US,false));
 assert(passport_wifi_should_retry(1,1000,30000));
 assert(passport_wifi_should_retry(2,29999,30000));
 assert(!passport_wifi_should_retry(3,1000,30000));
 assert(!passport_wifi_should_retry(0,30000,30000));
 assert(!passport_wifi_should_retry(0,30001,30000));
 assert(passport_wifi_should_retry(1,UINT32_MAX-100,200));
 assert(!passport_wifi_should_retry(1,201,200));
 assert(passport_utf8_prefix("新加坡",4)==3);
 assert(passport_utf8_prefix("abc",2)==2);
 assert(passport_utf8_prefix("新加坡",30)==9);
 m.page=PAGE_RADIO; passport_navigate(&m,0,false); assert(m.station==0);
 passport_navigate(&m,2,false); assert(m.playing);
 m.page=PAGE_BUS;m.bus_stop=0;m.reply_page=0;
 passport_bus_navigate(&m,1,2,2);assert(m.reply_page==1 && m.bus_stop==0);
 passport_bus_navigate(&m,1,2,2);assert(m.reply_page==0 && m.bus_stop==1);
 passport_bus_navigate(&m,0,2,1);assert(m.bus_stop==0);
 passport_bus_navigate(&m,0,2,1);assert(m.bus_stop==1);
 passport_bus_navigate(&m,1,0,0);assert(m.bus_stop==0 && m.reply_page==0);
 m.wifi_profile=0;assert(!passport_wifi_navigate(&m,0,2));assert(m.wifi_profile==1);
 assert(passport_wifi_navigate(&m,2,2));assert(m.wifi_profile==1);
 assert(!passport_wifi_navigate(&m,1,2));assert(m.wifi_profile==0);
 assert(!passport_wifi_navigate(&m,2,0));
 assert(passport_valid_qr_url("https://example.test/profile"));
 assert(passport_valid_qr_url("https://wa.me/123456789"));
 assert(passport_valid_qr_url("weixin://test"));assert(passport_valid_qr_url(""));
 assert(!passport_valid_qr_url("javascript:alert(1)"));
 assert(!passport_valid_qr_url("https://example.test/\nmalformed"));
 char clock[16];passport_clock_format(clock,sizeof(clock),1,12,55,true);assert(!strcmp(clock,"MON 12:55"));
 passport_clock_format(clock,sizeof(clock),0,0,5,true);assert(!strcmp(clock,"SUN 00:05"));
 passport_clock_format(clock,sizeof(clock),5,23,59,true);assert(!strcmp(clock,"FRI 23:59"));
 passport_clock_format(clock,sizeof(clock),1,12,55,false);assert(!strcmp(clock,"--- --:--"));
 passport_clock_format(clock,sizeof(clock),7,24,60,true);assert(!strcmp(clock,"--- --:--"));
 passport_clock_format(NULL,0,0,0,0,true);
 assert(passport_valid_clock(1800000000));assert(!passport_valid_clock(0));
 assert(!passport_valid_clock(NAN));assert(!passport_valid_clock(INFINITY));assert(!passport_valid_clock(4102444801.0));
 char password[65];
 assert(passport_resolve_profile_password(password,sizeof(password),"old-home-test",NULL));assert(!strcmp(password,"old-home-test"));
 assert(passport_resolve_profile_password(password,sizeof(password),"old-home-test","new-home-test"));assert(!strcmp(password,"new-home-test"));
 assert(passport_resolve_profile_password(password,sizeof(password),"new-home-test",""));assert(!strcmp(password,"new-home-test"));
 assert(!passport_resolve_profile_password(password,sizeof(password),"old-home-test","bad"));
 for(int active=PAGE_HOME;active<=PAGE_TRAIN;active++)for(int requested=PAGE_HOME;requested<=PAGE_TRAIN;requested++){
  bool expected=requested==PAGE_WEATHER || (active==requested && requested!=PAGE_APPS && requested!=PAGE_CARD);
  assert(passport_service_allowed(active,requested)==expected);
 }
 assert(!passport_service_allowed(PAGE_TRAIN+1,PAGE_BUS));
 for(int page=PAGE_HOME;page<=PAGE_LANGUAGE;page++){
  bool relay_page=page==PAGE_SETTINGS||page==PAGE_APPS||page==PAGE_HERMES||page==PAGE_ATHENA||page==PAGE_RADIO||page==PAGE_PREFERENCES;
  assert(passport_browser_relay_page(page)==relay_page);
  for(unsigned ap=0;ap<2;ap++)for(unsigned relay=0;relay<2;relay++){
   bool expected=relay||page==PAGE_SETTINGS||page==PAGE_HERMES||page==PAGE_ATHENA||
    (page==PAGE_HOME&&ap)||((page==PAGE_APPS||page==PAGE_RADIO||page==PAGE_PREFERENCES)&&relay);
   assert(passport_web_required(page,ap,relay)==expected);
  }
 }
 puts("Passport navigation/UTF-8 and exclusive service policy: PASS");
}
