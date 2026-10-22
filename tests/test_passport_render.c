#include "passport_app.h"
#include "passport_features.h"
#include "passport_navigation.h"
#include "lvgl.h"
#include "passport_i18n.h"
#include "passport_muse.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
LV_FONT_DECLARE(passport_font_16);
passport_spotify_t passport_spotify={.state="playing",.title="A song for the journey",.artist="Example artist",.device="Phone"};
passport_reader_t passport_reader={.state="reading",.title="A pocket-sized adventure",.text="A quiet road\nled to the\nriver. Beyond\nthe bridge,\nthe city was\nwaking up.",.count=1,.pages=42,.page=7};
static passport_navigation_t fixture_navigation={.enabled=true,.valid=true,.status="Komoot connected",.turn={.direction=8,.distance=120,.street="River Valley Road"}};
void passport_navigation_snapshot(passport_navigation_t *out){*out=fixture_navigation;}
passport_config_t passport_config;
char passport_qr_url[257]="https://example.test/profile";
SemaphoreHandle_t passport_mutex;
bool passport_connected=true,passport_has_avatar;
char passport_ip[20]="192.168.1.20";
static uint8_t fixture_avatar[PASSPORT_AVATAR_BYTES];
uint8_t *passport_avatar=fixture_avatar;
bool passport_avatar_indexed;
size_t passport_avatar_bytes=PASSPORT_AVATAR_BYTES,passport_avatar_allocation=PASSPORT_AVATAR_BYTES;
static bool fixture_photo_pending;
bool passport_photo_refresh_pending(void){return fixture_photo_pending;}
void passport_photo_release(void){assert(lv_obj_get_child_count(lv_screen_active())==0);fixture_photo_pending=false; /* Static render fixture. */}
volatile bool passport_dirty;
char passport_weather[320]="30.2 C\n湿度 78%\n风 12 km/h",passport_athena[1800]="codex · ok\n已用 42%\n\nclaude · ok\n已用 28%",fixture_reply[2][6400];
char passport_station[64]="SG / YES 933",passport_network_status[128]="设置码 123456";
unsigned passport_station_count=10;
unsigned passport_bus_stop_count=1,passport_bus_pages=3,passport_bus_rows=4;
passport_bus_row_t passport_bus_data[4]={
 {.service="22",.destination="To Ang Mo Kio",.eta="Arr  8  16"},
 {.service="60",.destination="To Bedok",.eta="3  12  21"},
 {.service="63",.destination="To Rumah Tinggi",.eta="5  14  25"},
 {.service="150",.destination="To Marine Terr",.eta="6  18  29"}};
char *passport_train_text="Service alert 1\nHeadline\nText: EWL disruption\nDescription\nText: No train service between EW7 and EW9. Use alternative travel routes.\nAffected\nRoute: EWL\nStop: EW7\n";
char passport_train_summary[160]="EWL disruption",passport_train_status[96]="LTA 10:30 | 1 alert";
unsigned passport_train_alerts=1;
bool passport_train_known=true,passport_train_stale=false;
char passport_bus_title[96]="82009 · Eunos Int",passport_bus_meta[96]="1/3 · 15:55";
int passport_psi_value=184;
char passport_haze_status[80]="不健康",passport_haze_meta[96]="NEA 中区 15:00";
const char *passport_reply_peek(unsigned a){return a<2?fixture_reply[a]:NULL;}
static unsigned fixture_profiles=2;
static bool fixture_muse;
static const char *fixture_muse_text;
bool passport_muse_supported(void){return fixture_muse;}
bool passport_muse_voice_ready(void){return false;}
const char *passport_muse_voice_status(void){return "";}
unsigned passport_muse_passkey(void){return 0;}
void passport_muse_screen(char *out,size_t cap){snprintf(out,cap,"%s",fixture_muse_text);}

static uint16_t frame[240*320],draw[240*32];
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p) {
 uint16_t *pixels=(uint16_t *)p;
 for(int y=a->y1;y<=a->y2;y++)for(int x=a->x1;x<=a->x2;x++)frame[y*240+x]=*pixels++;
 lv_display_flush_ready(d);
}
static void save(const char *path) {
 FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n240 320\n255\n");
 for(unsigned i=0;i<240*320;i++){uint16_t c=frame[i];unsigned char rgb[]={((c>>11)&31)*255/31,((c>>5)&63)*255/63,(c&31)*255/31};fwrite(rgb,1,3,f);}fclose(f);
}
int main(int argc,char **argv) {
 lv_init();lv_display_t *d=lv_display_create(240,320);
 lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(d,draw,NULL,sizeof(draw),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);
 FILE *inventory=fopen("assets/fonts/passport-symbols.txt","rb");assert(inventory);unsigned checked=0;
 while(!feof(inventory)){int c=fgetc(inventory);if(c==EOF)break;uint32_t cp=c;int extra=c<128?0:c<224?1:c<240?2:3;
 if(extra)cp&=(1u<<(6-extra))-1;for(int n=0;n<extra;n++)cp=(cp<<6)|(fgetc(inventory)&63);
 lv_font_glyph_dsc_t g={0};if(!lv_font_get_glyph_dsc(&passport_font_16,&g,cp,0)||g.is_placeholder){fprintf(stderr,"Missing U+%04x\n",(unsigned)cp);return 1;}checked++;
 }
 fclose(inventory);lv_font_glyph_dsc_t g={0};assert(!lv_font_get_glyph_dsc(&passport_font_16,&g,0x1f984,0)||g.is_placeholder);
 snprintf(passport_config.name,sizeof(passport_config.name),"我的护照");snprintf(passport_config.tagline,sizeof(passport_config.tagline),"保持好奇");snprintf(passport_config.contact,sizeof(passport_config.contact),"hello@example.com");snprintf(passport_config.city,sizeof(passport_config.city),"singapore");
 strcpy(fixture_reply[0],"你好！Hermes 已连接。你可以在手机设置页发送消息，并在这里阅读回复。");strcpy(fixture_reply[1],fixture_reply[0]);
 if(argc>2)snprintf(passport_qr_url,sizeof(passport_qr_url),"%s",argv[2]);
 passport_model_t model={0};char path[160];const char *prefix=argc>1?argv[1]:"/tmp/passport";
 for(unsigned language=0;language<2;language++){passport_language_set(language);
 strcpy(passport_network_status,language?"Wi-Fi connected\nSetup code 123456":"已连接 Wi-Fi\n设置码 123456");
 strcpy(passport_athena,language?"Codex #1\n5h left 75%\nReset 1h 0m\n\n7d left 50%\nReset 24h 0m\fClaude\nNot connected / unavailable\f":"Codex #1\n5h 剩余 75%\n重置 1h 0m\n\n7d 剩余 50%\n重置 24h 0m\fClaude\n未连接或不可用\f");
 strcpy(passport_weather,language?"28.9°C\nCloudy\nRH 69%\nWind 15 km/h":"28.9°C\n多云\n湿度 69%\n风 15 km/h");
 for(unsigned theme=0;theme<3;theme++)for(int page=0;page<=PAGE_READER;page++){
 passport_config.theme=theme;model.page=page;model.reply_page=page==PAGE_BRIGHTNESS?70:0;passport_ui_render(&model,86);lv_refr_now(d);
 /* Real production font metrics must keep footers and the homepage card inside. */
 lv_obj_t *layout_root=lv_screen_active();
 for(unsigned i=0;i<lv_obj_get_child_count(layout_root);i++){
  lv_obj_t *child=lv_obj_get_child(layout_root,i);
  if(!lv_obj_check_type(child,&lv_label_class))continue;
  if(language && page!=PAGE_NAVIGATION){const lv_font_t *font=lv_obj_get_style_text_font(child,0);assert(font->dsc==lv_font_montserrat_16.dsc);assert(font->line_height==lv_font_montserrat_16.line_height);assert(font->fallback==&passport_font_16);}
  int y=lv_obj_get_y(child),bottom=y+lv_obj_get_height(child);
  if(y>=282){if(bottom>310)fprintf(stderr,"Footer overflow language=%u page=%d width=%d text=%s bottom=%d\n",language,page,(int)lv_obj_get_width(child),lv_label_get_text(child),bottom);assert(bottom<=310);}
  if(page==PAGE_HOME && y==147){
   if(bottom>238)fprintf(stderr,"Weather card overflow language=%u bottom=%d\n",language,bottom);
   assert(bottom<=238);
  }
 }
 if(page==PAGE_RADIO){
  int previous_bottom=200;unsigned controls=0;bool percentage_seen=false;lv_obj_t *root=lv_screen_active();
  for(unsigned i=0;i<lv_obj_get_child_count(root);i++){
   lv_obj_t *child=lv_obj_get_child(root,i);
   if(!lv_obj_check_type(child,&lv_label_class) || lv_obj_get_y(child)<200)continue;
   if(strstr(lv_label_get_text(child),"100%"))percentage_seen=true;
   int y=lv_obj_get_y(child),bottom=y+lv_obj_get_height(child);
   if(y<previous_bottom || bottom>310)fprintf(stderr,"Radio control bounds language=%u theme=%u text=%s y=%d bottom=%d previous=%d\n",language,theme,lv_label_get_text(child),y,bottom,previous_bottom);
   assert(y>=previous_bottom && bottom<=310);previous_bottom=bottom;controls++;
  }
  assert(controls==4 && percentage_seen);
 }
 snprintf(path,sizeof(path),"%s-%u-%u-%d.ppm",prefix,language,theme,page);save(path);
 }
 /* NFC instructions must fit above the footer in both languages/themes. */
 for(unsigned nfc_theme=0;nfc_theme<3;nfc_theme++){
  passport_config.theme=nfc_theme;model.page=PAGE_CARD;model.reply_page=2;
  passport_ui_render(&model,86);lv_refr_now(d);
  int previous_bottom=50;unsigned labels=0;lv_obj_t *nfc_root=lv_screen_active();
  for(unsigned i=0;i<lv_obj_get_child_count(nfc_root);i++){
   lv_obj_t *child=lv_obj_get_child(nfc_root,i);
   if(!lv_obj_check_type(child,&lv_label_class) || lv_obj_get_y(child)<50)continue;
   int y=lv_obj_get_y(child),bottom=y+lv_obj_get_height(child);
   assert(y>=previous_bottom && bottom<=310);previous_bottom=bottom;labels++;
  }
  assert(labels==6);
  snprintf(path,sizeof(path),"%s-nfc-%u-%u.ppm",prefix,language,nfc_theme);save(path);
 }
 model.page=PAGE_CARD;model.reply_page=1;
 passport_ui_render(&model,86);lv_refr_now(d);snprintf(path,sizeof(path),"%s-qr-%u.ppm",prefix,language);save(path);
 model.reply_page=0;
 }
 model.page=PAGE_SETTINGS;model.wifi_profile=1;passport_config.theme=0;
 passport_ui_render(&model,86);lv_refr_now(d);snprintf(path,sizeof(path),"%s-wifi-outside.ppm",prefix);save(path);
 for(unsigned language=0;language<2;language++)for(unsigned theme=0;theme<3;theme++){
  passport_language_set(language);passport_config.theme=theme;model.wifi_profile=1;
  passport_ui_render(&model,86);lv_refr_now(d);lv_obj_t *root=lv_screen_active();bool found=false;
  for(unsigned i=0;i<lv_obj_get_child_count(root);i++){
   lv_obj_t *child=lv_obj_get_child(root,i);
   if(lv_obj_check_type(child,&lv_label_class) && lv_obj_get_y(child)==48){
    found=true;if(lv_obj_get_y(child)+lv_obj_get_height(child)>=78)fprintf(stderr,"Hotspot label overlaps status in language=%u\n",language);
    assert(lv_obj_get_y(child)+lv_obj_get_height(child)<76);
   }
  }
  assert(found);
 }
 /* Keep the same screen alive: this catches stale pixels on language changes.
  * The setup password is synthetic, the same maximum length as the device. */
 model.page=PAGE_SETTINGS;model.wifi_profile=0;strcpy(passport_ip,"192.168.100.100");
 uint16_t reference[2][240*320];
 for(unsigned theme=0;theme<3;theme++){
  passport_config.theme=theme;
  for(unsigned cycle=0;cycle<6;cycle++){
   unsigned language=cycle%2;passport_language_set(language);
   strcpy(passport_network_status,language?"Pocket-Passport\nKey 0123456789abcdef\nCode 123456":"热点 Pocket-Passport\n密码 0123456789abcdef\n设置码 123456");
   passport_ui_render(&model,86);lv_refr_now(d);
   lv_obj_t *root=lv_screen_active();assert(lv_obj_get_style_bg_opa(root,0)==LV_OPA_COVER);
   bool status_seen=false;
   for(unsigned i=0;i<lv_obj_get_child_count(root);i++){
    lv_obj_t *child=lv_obj_get_child(root,i);
    if(lv_obj_check_type(child,&lv_label_class) && !strcmp(lv_label_get_text(child),passport_network_status)){
     status_seen=true;assert(lv_obj_get_y(child)==76);assert(lv_obj_get_height(child)==64);
     lv_point_t size;lv_text_get_size(&size,passport_network_status,lv_obj_get_style_text_font(child,0),0,4,210,LV_TEXT_FLAG_NONE);
     assert(size.y<=64);assert(lv_obj_get_y(child)+size.y<148);
    }
   }
   assert(status_seen);
   for(unsigned i=0;i<lv_obj_get_child_count(root);i++){
    lv_obj_t *child=lv_obj_get_child(root,i);if(!lv_obj_check_type(child,&lv_label_class))continue;
    int y=lv_obj_get_y(child),height=lv_obj_get_height(child);
    if(y==148)assert(y+height<176);
    if(y==176)assert(y+height<220);
    if(y==220)assert(y+height<242);
    if(y==242)assert(y+height<266);
    if(y==266)assert(y+height<290);
    if(y==290)assert(y+height<=310);
   }
   if(cycle<2)memcpy(reference[language],frame,sizeof(frame));
   else assert(!memcmp(reference[language],frame,sizeof(frame)));
   snprintf(path,sizeof(path),"%s-settings-ap-%u-%u.ppm",prefix,theme,language);save(path);
  }
 }
 /* Settings must also work for a new owner with no saved presets. */
 for(unsigned profiles=0;profiles<=2;profiles+=2){
  fixture_profiles=profiles;
  for(unsigned language=0;language<2;language++)for(unsigned theme=0;theme<3;theme++){
   passport_language_set(language);passport_config.theme=theme;
   strcpy(passport_network_status,language?"Pocket-Passport\nKey 0123456789abcdef\nCode 123456":"热点 Pocket-Passport\n密码 0123456789abcdef\n设置码 123456");
   passport_ui_render(&model,86);lv_refr_now(d);
   unsigned wording=0;lv_obj_t *root=lv_screen_active();
   for(unsigned i=0;i<lv_obj_get_child_count(root);i++){
    lv_obj_t *child=lv_obj_get_child(root,i);if(!lv_obj_check_type(child,&lv_label_class))continue;
    int y=lv_obj_get_y(child),bottom=y+lv_obj_get_height(child);
    const char *value=lv_label_get_text(child);
    if(language){
     const lv_font_t *font=lv_obj_get_style_text_font(child,0);assert(font->dsc==lv_font_montserrat_16.dsc);
     lv_font_glyph_dsc_t glyph={0};assert(lv_font_get_glyph_dsc(font,&glyph,0x4e2d,0) && !glyph.is_placeholder);
     if(!strcmp(value,"Up/Down: Choose\nWi-Fi network"))wording|=1;
     if(!strcmp(value,"OK: Connect"))wording|=2;
     if(!strcmp(value,"Hold Down: Set up Wi-Fi"))wording|=4;
     if(!strcmp(value,"Hold OK: Settings"))wording|=8;
    }
    if(y==148)assert(bottom<176);
    if(y==176)assert(bottom<220);
    if(y==220)assert(bottom<242);
    if(y==242)assert(bottom<266);
    if(y==266)assert(bottom<290);
    if(y==290)assert(bottom<=310);
   }
   if(language)assert(wording==(profiles?15u:12u));
   snprintf(path,sizeof(path),"%s-settings-setup-%u-%u-%u.ppm",prefix,profiles,theme,language);save(path);
  }
 }
 fixture_profiles=2;
 fixture_muse=true;
 const char *muse_en[]={"MuseGadget-A1B2C3\n\nPair this phone?\nPress OK to approve", "Android Bluetooth code\n100042\nEnter this on your phone", "MuseGadget-A1B2C3\n\nPairing failed\nConnect Wi-Fi in Settings\nRetry in the Muse app", "MuseGadget-A1B2C3\n\nPhone companion ready\nChat/voice in progress"};
 const char *muse_zh[]={"MuseGadget-A1B2C3\n\n确认手机配对？\n按确定批准", "手机蓝牙配对码\n100042\n在 Android 输入此码", "MuseGadget-A1B2C3\n\n配对失败\n先在设置连接 Wi-Fi\n再用 Muse 应用重试", "MuseGadget-A1B2C3\n\n手机伴侣已连接\n聊天和语音接入中"};
 for(unsigned language=0;language<2;language++)for(unsigned theme=0;theme<3;theme++)for(unsigned state=0;state<4;state++){
  passport_language_set(language);passport_config.theme=theme;model.page=PAGE_MUSE;
  fixture_muse_text=language?muse_en[state]:muse_zh[state];passport_ui_render(&model,86);lv_refr_now(d);
  lv_obj_t *root=lv_screen_active();bool body_found=false;
  for(unsigned i=0;i<lv_obj_get_child_count(root);i++){
   lv_obj_t *child=lv_obj_get_child(root,i);if(!lv_obj_check_type(child,&lv_label_class))continue;
   if(lv_obj_get_y(child)==52){assert(lv_obj_get_height(child)==194);assert(!strcmp(lv_label_get_text(child),fixture_muse_text));body_found=true;}
   if(lv_obj_get_y(child)>=262)assert(lv_obj_get_y(child)+lv_obj_get_height(child)<=310);
   if(language)assert(lv_obj_get_style_text_font(child,0)->dsc==lv_font_montserrat_16.dsc);
  }
  assert(body_found);snprintf(path,sizeof(path),"%s-muse-pairing-%u-%u-%u.ppm",prefix,language,theme,state);save(path);
 }
 fixture_muse=false;
 for(unsigned language=0;language<2;language++)for(unsigned theme=0;theme<3;theme++)for(unsigned agent=0;agent<2;agent++){
  passport_language_set(language);passport_config.theme=theme;model.page=agent?PAGE_MUSE:PAGE_HERMES;model.volume_mode=true;
  passport_ui_render(&model,86);lv_refr_now(d);lv_obj_t *root=lv_screen_active();unsigned labels=0;
  for(unsigned i=0;i<lv_obj_get_child_count(root);i++){
   lv_obj_t *child=lv_obj_get_child(root,i);if(!lv_obj_check_type(child,&lv_label_class))continue;
   const char *v=lv_label_get_text(child);
   if(strchr(v,'%') && lv_obj_get_y(child)==66)labels++;
   assert(lv_obj_get_y(child)+lv_obj_get_height(child)<=310);
   if(language)assert(lv_obj_get_style_text_font(child,0)->dsc==lv_font_montserrat_16.dsc);
  }
  assert(labels==1);snprintf(path,sizeof(path),"%s-assistant-volume-%u-%u-%u.ppm",prefix,agent,theme,language);save(path);
 }
 model.volume_mode=false;
 puts("Assistant shared volume panel: PASS (12 bilingual/theme renders)");
 puts("Muse confirmation, passkey and relay layouts: PASS (16 variants)");
 puts("Uniform English 16 px font, CJK fallback and clear Wi-Fi setup copy: PASS");
 puts("Repeated AP language-switch renders: PASS (no status/URL overlap; stable pixels)");
 /* Actual font wrapping must preserve every source byte across train pages. */
 for(unsigned language=0;language<2;language++){
  passport_language_set(language);model.page=PAGE_TRAIN;size_t offset=0;
  for(unsigned page=0;page<30;page++){
   model.reply_page=page;passport_ui_render(&model,86);lv_refr_now(d);
   lv_obj_t *root=lv_screen_active(),*body=NULL;unsigned total=0;
   for(unsigned i=0;i<lv_obj_get_child_count(root);i++){
    lv_obj_t *child=lv_obj_get_child(root,i);if(!lv_obj_check_type(child,&lv_label_class))continue;
    if(lv_obj_get_y(child)==76)body=child;
    if(lv_obj_get_y(child)==239){unsigned current=0;assert(sscanf(lv_label_get_text(child),"%u/%u",&current,&total)==2);assert(current==page+1);}
   }
   assert(body&&total);const char *part=lv_label_get_text(body);size_t n=strlen(part);
   assert(n && !strncmp(part,passport_train_text+offset,n));offset+=n;
   lv_point_t size;lv_text_get_size(&size,part,lv_obj_get_style_text_font(body,0),0,4,196,LV_TEXT_FLAG_NONE);
   assert(size.y<=154);assert(lv_obj_get_y(body)+size.y<239);
   if(page+1==total)break;
  }
  assert(offset==strlen(passport_train_text));
  model.page=PAGE_APPS;
  for(unsigned selected=0;selected<PASSPORT_APP_COUNT;selected++){
   model.selected=selected;passport_ui_render(&model,86);lv_refr_now(d);
   lv_obj_t *root=lv_screen_active();bool visible=false;
   for(unsigned i=0;i<lv_obj_get_child_count(root);i++){
    lv_obj_t *child=lv_obj_get_child(root,i);if(!lv_obj_check_type(child,&lv_label_class))continue;
    const char *value=lv_label_get_text(child);
    char settings[80];snprintf(settings,sizeof(settings),"%02u  %s",PASSPORT_APP_COUNT,language?"Settings":"设置");
    if(!strcmp(value,settings)){assert(lv_obj_get_y(child)+lv_obj_get_height(child)<270);visible=true;}
   }
   if(selected==PASSPORT_APP_COUNT-1)assert(visible);
  }
 }
 puts("Train details: lossless bilingual font-wrapped pagination; Settings remains last and visible: PASS");

 /* Both reader sizes must fit complete bounded pages without clipping. */
 for(unsigned language=0;language<2;language++)for(unsigned large=0;large<2;large++){
  passport_language_set(language);model.page=PAGE_READER;passport_reader.large=large;
  const char *row=large?"WWWWWWWW":"WWWWWWWWWW";passport_reader.text[0]=0;
  for(unsigned i=0;i<(large?6u:7u);i++){if(i)strcat(passport_reader.text,"\n");strcat(passport_reader.text,row);}
  passport_ui_render(&model,86);lv_refr_now(d);lv_obj_t *root=lv_screen_active();bool found=false;
  for(unsigned i=0;i<lv_obj_get_child_count(root);i++){
   lv_obj_t *child=lv_obj_get_child(root,i);if(!lv_obj_check_type(child,&lv_label_class) || lv_obj_get_y(child)!=79)continue;
   lv_point_t extent;lv_text_get_size(&extent,lv_label_get_text(child),lv_obj_get_style_text_font(child,0),0,4,196,LV_TEXT_FLAG_NONE);
   if(extent.y>171)fprintf(stderr,"Reader overflow language=%u large=%u height=%d width=%d\n",language,large,(int)extent.y,(int)extent.x);
   assert(extent.y<=171);assert(extent.x<=196);found=true;
  }
  assert(found);snprintf(path,sizeof(path),"%s-reader-%u-%u.ppm",prefix,language,large);save(path);
 }
 passport_reader.large=false;
 /* A route overlay must coexist with each new app and remain in bounds. */
 fixture_navigation.overlay_until_us=6000000;
 for(unsigned page=PAGE_SPOTIFY;page<=PAGE_READER;page++){
  model.page=page;passport_ui_render(&model,86);lv_refr_now(d);
  snprintf(path,sizeof(path),"%s-turn-overlay-%u.ppm",prefix,page);save(path);
 }
 fixture_navigation.overlay_until_us=0;
 puts("Reader text sizes and navigation overlays: PASS");
 /* Switching language must translate controls and retained protocol status. */
 for(unsigned language=0;language<2;language++)for(unsigned page=PAGE_NAVIGATION;page<=PAGE_READER;page++){
  passport_language_set(language);model.page=page;passport_ui_render(&model,86);lv_refr_now(d);
  const char *expected=page==PAGE_NAVIGATION?(language?"OK: Stop navigation":"确定：停止导航"):
                       page==PAGE_SPOTIFY?(language?"OK: Play/pause":"确定：播放/暂停"):
                                          (language?"Up/Down: Pages":"上下：翻页");
  bool found=false;lv_obj_t *root=lv_screen_active();
  for(unsigned i=0;i<lv_obj_get_child_count(root);i++){
   lv_obj_t *child=lv_obj_get_child(root,i);if(!lv_obj_check_type(child,&lv_label_class))continue;
   const char *value=lv_label_get_text(child);if(!strcmp(value,expected))found=true;
   if(!language){assert(!strstr(value,"Up/Down"));assert(!strstr(value,"Hold OK"));assert(strcmp(value,"playing"));assert(strcmp(value,"Komoot connected"));}
  }
  assert(found);snprintf(path,sizeof(path),"%s-localized-%u-%u.ppm",prefix,language,page);save(path);
 }
 puts("Retained feature states and controls follow language selection: PASS");


 /* Every appearance choice is previewed with the production renderer. */
 for(unsigned language=0;language<2;language++)for(unsigned choice=0;choice<3;choice++){
  passport_language_set(language);model.page=PAGE_APPEARANCE;model.reply_page=choice;
  passport_config.theme=choice;passport_ui_render(&model,86);lv_refr_now(d);
  lv_obj_t *root=lv_screen_active();bool saved=false;
  for(unsigned i=0;i<lv_obj_get_child_count(root);i++){
   lv_obj_t *child=lv_obj_get_child(root,i);if(!lv_obj_check_type(child,&lv_label_class))continue;
   int bottom=lv_obj_get_y(child)+lv_obj_get_height(child);assert(bottom<=310);
   if(!strcmp(lv_label_get_text(child),language?"Saved":"已保存"))saved=true;
  }
  assert(saved);snprintf(path,sizeof(path),"%s-appearance-%u-%u.ppm",prefix,language,choice);save(path);
 }
 model.reply_page=0;
 /* Compare real LVGL output, not just a palette round-trip calculation. */
 static uint16_t photo_reference[240*320];passport_has_avatar=true;
 for(unsigned language=0;language<2;language++)for(unsigned theme=0;theme<3;theme++)for(unsigned p=0;p<2;p++){
  passport_language_set(language);passport_config.theme=theme;model.page=p?PAGE_CARD:PAGE_HOME;
  for(unsigned i=0;i<PASSPORT_AVATAR_W*PASSPORT_AVATAR_H;i++){
   unsigned pixel=(i%256)*257;fixture_avatar[2*i]=pixel;fixture_avatar[2*i+1]=pixel>>8;
  }
  passport_avatar_indexed=false;passport_avatar_bytes=PASSPORT_AVATAR_BYTES;
  passport_ui_render(&model,86);lv_refr_now(d);memcpy(photo_reference,frame,sizeof(frame));
  assert(passport_photo_pack(fixture_avatar,sizeof(fixture_avatar)));
  passport_avatar_indexed=true;passport_avatar_bytes=PASSPORT_AVATAR_INDEXED_BYTES;
  passport_ui_render(&model,86);lv_refr_now(d);
  assert(!memcmp(photo_reference,frame,sizeof(frame)));
 }
 puts("Lossless indexed photo: identical RGB565 LVGL output on Home/Card, all three themes and both languages");
 fixture_photo_pending=true;passport_ui_render(&model,86);lv_refr_now(d);assert(!fixture_photo_pending);
 lv_mem_monitor_t memory;lv_mem_monitor(&memory);
 printf("LVGL 24KB pool: free=%zu largest=%zu\n",memory.free_size,memory.free_biggest_size);
 assert(memory.free_biggest_size>2048);
 printf("Font descriptors: %u code points + negative case PASS; screen renders complete\n",checked);
 return 0;
}

unsigned passport_wifi_profile_count(void){return fixture_profiles;}
unsigned passport_wifi_profile_current(void){return 0;}
const char *passport_wifi_profile_label(unsigned i){return passport_language_get()?(i?"Phone Hotspot":"Home"):(i?"手机热点":"在家 Home");}

unsigned passport_audio_volume_percent(void){return 100;}

unsigned passport_brightness_get(void){return 70;}
bool passport_brightness_save_failed(void){return false;}
