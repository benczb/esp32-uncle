#include "passport_app.h"
#include "passport_muse.h"
#include "passport_muse_voice.h"
#include "passport_i18n.h"
#include "lvgl.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "src/misc/lv_text_private.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
LV_FONT_DECLARE(passport_font_16);
static lv_obj_t *screen;
static uint32_t ink, paper, edge, accent;
static lv_image_dsc_t portrait;
/* One English face/size for every screen; fallback keeps mixed CJK text visible. */
static const lv_font_t *ui_font(void){
 static lv_font_t english;static bool initialized;
 if(!initialized){english=lv_font_montserrat_16;english.fallback=&passport_font_16;initialized=true;}
 return passport_language_get()?&english:&passport_font_16;
}
static const char *titles_zh[]={"随身护照","应用目录","个人名片","AI 用量","Hermes 助手","Muse 助手","城市收音机","本地天气","巴士到站","Wi-Fi 设置","列车警报","外观","设置","亮度","语言"};
static const char *titles_en[]={"Pocket Passport","Menu","Profile card","AI Usage","Hermes","Muse","SG/MY radio","Weather","Bus arrivals","Wi-Fi","Train alerts","Appearance","Settings","Brightness","Language"};
static lv_obj_t *box(int x,int y,int w,int h) {
 lv_obj_t *o=lv_obj_create(screen); lv_obj_remove_style_all(o);
 lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
 lv_obj_set_style_border_width(o,1,0);lv_obj_set_style_border_color(o,lv_color_hex(edge),0);
 lv_obj_set_style_bg_color(o,lv_color_hex(paper),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
 lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;
}
/* Network/profile input may exceed the font contract. Show '?' deliberately for
 * unsupported code points, never pass missing CJK glyphs silently to LVGL. */
static void supported_text(char *out,size_t cap,const char *in) {
 size_t n=0;
 for(size_t i=0;in[i] && n+5<cap;) {
  uint32_t cp=(unsigned char)in[i];size_t bytes=1;
  if(cp>=0xc2 && cp<=0xf4) {
   bytes=cp<0xe0?2:cp<0xf0?3:4; cp&=(1u<<(7-bytes))-1;
   size_t j;for(j=1;j<bytes;j++) {if(!in[i+j] || ((unsigned char)in[i+j]&0xc0)!=0x80)break;cp=(cp<<6)|((unsigned char)in[i+j]&63);}
   if(j!=bytes){out[n++]='?';i++;continue;}
  } else if(cp>=128) {out[n++]='?';i++;continue;}
  lv_font_glyph_dsc_t g={0};
  if(cp=='\n' || (lv_font_get_glyph_dsc(&passport_font_16,&g,cp,0)&&!g.is_placeholder)) {
   memcpy(out+n,in+i,bytes);n+=bytes;
  } else out[n++]='?';i+=bytes;
 }
 out[n]=0;
}
static lv_obj_t *text(int x,int y,int w,const char *value) {
 lv_obj_t *l=lv_label_create(screen);lv_obj_set_pos(l,x,y);lv_obj_set_width(l,w);
 lv_obj_set_style_text_font(l,ui_font(),0);
 lv_obj_set_style_text_color(l,lv_color_hex(ink),0);
 lv_obj_set_style_text_line_space(l,4,0);
 char safe[1600];supported_text(safe,sizeof(safe),value);lv_label_set_text(l,safe);return l;
}
/* Shared typography applies equally to body text and button instructions. */
static lv_obj_t *hint(int x,int y,int w,const char *value) {
 return text(x,y,w,value);
}
static void avatar(int x,int y) {
 box(x-1,y-1,82,114);
 if(passport_has_avatar && passport_avatar) {
  portrait=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=passport_avatar_indexed?LV_COLOR_FORMAT_I8:LV_COLOR_FORMAT_RGB565,
    .w=PASSPORT_AVATAR_W,.h=PASSPORT_AVATAR_H,.stride=PASSPORT_AVATAR_W*(passport_avatar_indexed?1:2)},
    .data_size=passport_avatar_bytes,.data=passport_avatar};
  lv_obj_t *im=lv_image_create(screen);lv_image_set_src(im,&portrait);lv_obj_set_pos(im,x,y);
 } else {
  if(passport_language_get()) {
   const char *lines[]={"Your","photo","Phone","upload"};
   for(int row=0;row<4;row++) text(x+7,y+10+row*24,70,lines[row]);
  } else {
   text(x+7,y+28,70,"你的头像");text(x+7,y+53,70,"手机上传");
  }
 }
}
/* Page boundaries use the actual current font and LVGL wrapping, never byte
 * slices. Seven rows leave room for source freshness and navigation. */
static size_t train_span(const char *text_value){
 lv_text_attributes_t attributes;lv_text_attributes_init(&attributes);attributes.max_width=196;attributes.line_space=4;
 size_t n=0;for(unsigned row=0;row<7 && text_value[n];row++){
  uint32_t used=lv_text_get_next_line(text_value+n,LV_TEXT_LEN_MAX,ui_font(),NULL,&attributes);
  if(!used)break;
  size_t room=passport_utf8_prefix(text_value+n,1499-n);
  if(used>room){n+=room;break;}
  n+=used;
  if(n==1499)break;
 }
 return n;
}
void passport_ui_render(const passport_model_t *m,int battery) {
 unsigned theme=m->page==PAGE_APPEARANCE?m->reply_page%3:passport_config.theme;
 /* White means white text on black; full colour gives state and navigation
  * separate colour roles without allocating another framebuffer. */
 paper=theme==1?0x000000:theme==2?0x101821:0x071710;
 ink=theme==1?0xffffff:theme==2?0xf4f7fa:0x9bf7bc;
 edge=theme==1?0xffffff:theme==2?0x65c9f5:0x528565;
 accent=theme==2?0xffd479:ink;
 if(screen)lv_obj_clean(screen);else screen=lv_obj_create(NULL);
 /* An LVGL image retains its source pointer: delete it and drop the cache
  * before returning display RAM to the heap. Storage loading is a worker job. */
 lv_image_cache_drop(&portrait);
 if(passport_photo_refresh_pending() || (m->page!=PAGE_HOME && m->page!=PAGE_CARD)){passport_photo_release();portrait.data=NULL;}
 /* Rebuilds must cover old glyphs independently of the inherited theme. */
 lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
 lv_obj_set_style_bg_color(screen,lv_color_hex(paper),0);
 lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
 char clock[16];time_t now=time(NULL);struct tm local={0};
 bool synced=passport_valid_clock((double)now) && localtime_r(&now,&local)!=NULL;
 passport_clock_format(clock,sizeof(clock),local.tm_wday,local.tm_hour,local.tm_min,synced);
 text(15,13,170,m->page==PAGE_HOME?clock:(passport_language_get()?titles_en[m->page]:titles_zh[m->page]));
 char b[48];if(battery<0)snprintf(b,sizeof(b),"--%%");else snprintf(b,sizeof(b),"%d%%",battery);
 text(186,13,50,b);
 box(14,37,212,1);
 if(m->page==PAGE_HOME) {
  avatar(19,48);lv_obj_t *name=text(17,171,88,passport_config.name);
  lv_obj_set_height(name,20);lv_label_set_long_mode(name,LV_LABEL_LONG_DOT);
  lv_obj_t *tagline=text(17,195,88,passport_config.tagline);lv_obj_set_height(tagline,44);lv_label_set_long_mode(tagline,LV_LABEL_LONG_DOT);
  box(110,47,113,90);text(117,50,106,"PSI · 24h");
  char psi[16];if(passport_psi_value<0)strcpy(psi,"--");else snprintf(psi,sizeof(psi),"%d",passport_psi_value);
  lv_obj_t *reading=text(117,71,106,psi);if(!passport_language_get())lv_obj_set_style_text_font(reading,&lv_font_montserrat_32,0);
  if(passport_psi_value>100)lv_obj_set_style_text_color(reading,lv_color_hex(theme==1?0xffffff:0xffa789),0);
  text(117,111,106,passport_haze_status);
  box(110,142,113,96);
  const char *weather=passport_weather[0]?passport_weather:T("天气待更新","Weather pending");
  /* Four bounded rows fit the fixed card with the same font used elsewhere. */
  for(unsigned row=0;row<4 && weather && *weather;row++){
   const char *end=strchr(weather,'\n');size_t n=end?(size_t)(end-weather):strlen(weather);
   if(n>127)n=127;
   n=passport_utf8_prefix(weather,n);
   char line[128];memcpy(line,weather,n);line[n]=0;
   if(passport_language_get() && !strncmp(line,"Wind ",5)){
    char *unit=strstr(line," km/h");if(unit){*unit=0;strcat(line,"kph");}
   }
   lv_obj_t *entry=hint(117,147+(int)row*22,103,line);
   lv_obj_set_height(entry,18);lv_label_set_long_mode(entry,LV_LABEL_LONG_DOT);
   weather=end?end+1:NULL;
  }
  box(17,242,206,24);
  char notice[192];const char *meta=passport_haze_meta;
  if(passport_train_alerts){snprintf(notice,sizeof(notice),T("%s列车: %s","%sRail: %s"),passport_train_stale?T("缓存 ","Cached "):"",passport_train_summary);meta=notice;}
  else if(passport_train_stale){meta=T("列车警报暂不可用","Train alerts unavailable");}
  lv_obj_t *banner=text(24,245,192,meta);lv_obj_set_height(banner,18);lv_label_set_long_mode(banner,LV_LABEL_LONG_DOT);
  if(passport_train_alerts||passport_train_stale)lv_obj_set_style_text_color(banner,lv_color_hex(theme==1?0xffffff:0xffa789),0);
  hint(18,269,210,T("上:名片 下:巴士","Up: Card Down: Bus"));
  hint(18,290,210,T("确定:应用菜单","OK: Menu"));
 } else if(m->page==PAGE_APPS) {
  static const char *apps_zh[]={"01  巴士到站","02  列车警报","03  AI 用量","04  Hermes 助手","05  Muse 助手","06  个人名片","07  城市收音机","08  本地天气","09  设置"};
  static const char *apps_en[]={"01  Bus arrivals","02  Train alerts","03  AI Usage","04  Hermes","05  Muse","06  Profile card","07  SG/MY radio","08  Weather","09  Settings"};
  unsigned first=m->selected>=8?m->selected-7:0;
  for(unsigned i=first;i<PASSPORT_APP_COUNT && i<first+8;i++) {
   int y=48+(i-first)*28;
   if(i==m->selected){lv_obj_t *s=box(15,y-2,210,26);lv_obj_set_style_bg_color(s,lv_color_hex(accent),0);}
   lv_obj_t *l=text(24,y+3,199,(passport_language_get()?apps_en[i]:apps_zh[i]));
   if(i==m->selected)lv_obj_set_style_text_color(l,lv_color_hex(paper),0);
  }
  hint(20,270,208,T("上下选择 确定:打开","Up/Down | OK: Open"));
  hint(20,292,208,T("长按确定:返回首页","Hold OK: Home"));
 } else if(m->page==PAGE_CARD) {
  if(m->reply_page%PASSPORT_CARD_VIEWS==1){
   box(20,52,200,200); /* A white quiet zone and black modules remain scannable in both themes. */
   if(passport_qr_url[0]){
    lv_obj_t *qr=lv_qrcode_create(screen);lv_qrcode_set_size(qr,196);lv_obj_set_pos(qr,22,54);
    lv_qrcode_set_dark_color(qr,lv_color_black());lv_qrcode_set_light_color(qr,lv_color_white());lv_qrcode_set_quiet_zone(qr,true);
    if(lv_qrcode_update(qr,passport_qr_url,strlen(passport_qr_url))!=LV_RESULT_OK){lv_obj_delete(qr);text(25,120,190,T("二维码生成失败","QR generation failed"));}
   }else text(25,120,190,T("请在手机设置二维码链接","Set QR link on phone"));
  }else if(m->reply_page%PASSPORT_CARD_VIEWS==2){
   /* The built-in NTAG213 is a passive tag with no MCU connection: the
    * firmware cannot write it or detect a tap. This view only tells the
    * other person what to do; setup happens once from a phone. */
   text(25,56,194,"NFC");
   text(25,88,194,T("请将手机贴近背面\nNFC 标记处","Hold a phone against\nthe NFC mark on the back"));
   text(25,140,194,T("手机会打开与二维码\n相同的链接","The phone opens the\nsame link as the QR code"));
   text(25,192,194,passport_qr_url[0]?T("先用手机的 NFC 工具\n把链接写入一次","Write it once with an NFC\nwriter app on your phone"):T("请先在手机设置里\n填写二维码链接","Set the QR link\non your phone first"));
  }else {
   avatar(80,48);text(25,174,194,passport_config.name);text(25,200,194,passport_config.tagline);
   text(25,226,194,passport_config.contact[0]?passport_config.contact:T("在手机设置中填写联系方式","Edit contact on phone"));
  }
  hint(20,262,210,T("确定:名片/二维码/NFC","OK: profile / QR / NFC"));
  hint(20,286,210,T("长按确定:返回菜单","Hold OK: Back to Menu"));
 } else if(m->page==PAGE_BRIGHTNESS){
  char level[48];snprintf(level,sizeof(level),T("亮度 %u%%","Brightness %u%%"),m->reply_page);
  text(22,68,198,level);
  text(22,111,198,T("上键:增亮 10%\n下键:调暗 10%","Up: +10%\nDown: -10%"));
  text(22,178,198,passport_brightness_save_failed()?T("保存失败 请重试","Save failed. Retry."):
   m->reply_page==passport_brightness_get()?T("已保存","Saved"):T("预览 · 确定保存","Preview | OK: Save"));
  hint(22,263,198,T("确定:保存亮度","OK: Save brightness"));
  hint(22,287,198,T("长按确定:返回设置","Hold OK: Settings"));
 } else if(m->page==PAGE_PREFERENCES || m->page==PAGE_APPEARANCE || m->page==PAGE_LANGUAGE){
  bool appearance=m->page==PAGE_APPEARANCE,language=m->page==PAGE_LANGUAGE;
  unsigned count=appearance?3:language?2:4;
  const char *choices[]={appearance?T("荧光绿","Neon Green"):language?"中文":T("Wi-Fi 网络","Wi-Fi network"),
   appearance?T("白色","White"):language?"English":T("外观","Appearance"),
   appearance?T("全彩","Full Colour"):T("亮度","Brightness"),T("语言","Language")};
  for(unsigned i=0;i<count;i++){
   int y=70+i*42;
   if(i==m->reply_page){lv_obj_t *selected=box(18,y-4,204,32);lv_obj_set_style_bg_color(selected,lv_color_hex(accent),0);}
   lv_obj_t *label=text(27,y,184,choices[i]);
   if(i==m->reply_page)lv_obj_set_style_text_color(label,lv_color_hex(paper),0);
  }
  if(appearance || language)text(22,211,198,m->reply_page==(appearance?passport_config.theme:passport_language_get())?T("已保存","Saved"):
   language?T("确定:保存语言","OK: Save language"):T("预览 · 确定保存","Preview | OK: Save"));
  hint(22,263,198,(appearance || language)?T("上下选择 确定保存","Up/Down | OK: Save"):T("上下选择 确定打开","Up/Down | OK: Open"));
  hint(22,287,198,(appearance || language)?T("长按确定:返回设置","Hold OK: Settings"):T("长按确定:返回菜单","Hold OK: Back to Menu"));
 } else if(m->page==PAGE_SETTINGS) {
  bool profiles=passport_wifi_profile_count()>0;
  if(profiles){
   char choice[80];snprintf(choice,sizeof(choice),T("选择网络: %s","Wi-Fi: %s"),passport_wifi_profile_label(m->wifi_profile));
   text(22,48,198,choice);
  }else text(22,48,198,T("设置 Wi-Fi","Wi-Fi setup"));
  lv_obj_t *status=text(16,76,210,passport_network_status);
  lv_obj_set_height(status,64);lv_label_set_long_mode(status,LV_LABEL_LONG_CLIP);
  char url[80];snprintf(url,sizeof(url),"http://%s",passport_ip);
  lv_obj_t *address=text(22,148,198,url);
  if(!passport_language_get())lv_obj_set_style_text_font(address,&lv_font_montserrat_14,0);
  if(profiles){
   hint(22,176,198,T("上下键:选择\nWi-Fi 网络","Up/Down: Choose\nWi-Fi network"));
   hint(22,220,198,T("确定键:连接","OK: Connect"));
  }else{
   hint(22,176,198,T("用手机连接\nPocket-Passport","Connect your phone to\nPocket-Passport"));
   hint(22,220,198,T("再打开上方网址","Then open this address"));
  }
  hint(22,242,198,T("长按下键:配置 Wi-Fi","Hold Down: Set up Wi-Fi"));
  hint(22,266,198,T("手机网页:编辑资料","Edit profile on phone"));
  hint(22,290,198,T("长按确定:返回设置","Hold OK: Settings"));
 } else if(m->page==PAGE_ATHENA) {
  const char *entry=passport_athena[0]?passport_athena:T("等待电脑桥接服务\n请在手机设置服务地址","Waiting for bridge\nSet its URL on phone");
  unsigned total=0;for(const char *p=entry;*p;p++)if(*p=='\f')total++;
  unsigned chosen=total?m->reply_page%total:0;
  for(unsigned i=0;i<chosen;i++){const char *next=strchr(entry,'\f');if(next)entry=next+1;}
  const char *end=strchr(entry,'\f');size_t n=end?(size_t)(end-entry):strlen(entry);if(n>220)n=passport_utf8_prefix(entry,220);
  char summary[224];memcpy(summary,entry,n);summary[n]=0;text(22,53,198,summary);
  hint(22,262,198,T("上下切换账号 · 确定刷新","Up/Down:page OK:sync"));
  hint(22,286,198,T("长按确定:返回菜单","Hold OK: Back to Menu"));
 } else if((m->page==PAGE_HERMES || m->page==PAGE_MUSE) && m->volume_mode && !(m->page==PAGE_MUSE && passport_muse_passkey())){
  char volume[64];snprintf(volume,sizeof(volume),T("音量 %u%%","Volume %u%%"),passport_audio_volume_percent());
  text(22,66,198,volume);text(22,110,198,T("上键:增大 10%\n下键:减小 10%","Up: +10%\nDown: -10%"));
  hint(22,262,198,T("确定:返回回复","OK: Back to reply"));
  hint(22,286,198,T("长按确定:返回菜单","Hold OK: Back to Menu"));
 } else if(m->page==PAGE_MUSE && passport_muse_supported() &&
   (!passport_muse_voice_ready() || ((!passport_reply_peek(1) || !passport_reply_peek(1)[0]) && !passport_muse_voice_status()[0]) || passport_muse_passkey())) {
  char status[384];passport_muse_screen(status,sizeof(status));
  lv_obj_t *body=text(22,52,198,status);lv_obj_set_height(body,194);lv_label_set_long_mode(body,LV_LABEL_LONG_DOT);
  if(passport_muse_voice_ready() && !passport_muse_passkey()){
   char v[64];snprintf(v,sizeof(v),T("确定:音量 %u%%","OK: Volume %u%%"),passport_audio_volume_percent());hint(22,262,198,v);
  }else hint(22,262,198,T("确定:确认配对/重试","OK: approve / retry"));
  hint(22,286,198,T("长按确定:返回菜单","Hold OK: Back to Menu"));
 } else if(m->page==PAGE_HERMES || m->page==PAGE_MUSE) {
  int a=m->page==PAGE_MUSE;const char *reply=passport_reply_peek(a);
  if(a && passport_muse_voice_status()[0])reply=passport_muse_voice_status();
  if(!reply)reply=T("内存不足\n请稍后重试","Not enough memory\nPlease retry");
  if(m->page==PAGE_MUSE && !reply[0])reply=T("使用 Muse Passport 手机伴侣\n蓝牙集成尚未完成","Use Muse Passport app\nBluetooth integration pending");
  if(!reply[0])reply=T("在手机设置页发送消息\n回复将在此显示","Send text on the phone\nReplies appear here");
  /* Match the actual font wrapping; seven rows leave the volume footer clear. */
  size_t len=strlen(reply),offset=0;
  unsigned p=0;while(p<m->reply_page && offset<len){offset+=train_span(reply+offset);p++;}
  if(offset>=len){offset=0;}
  char chunk[1500];size_t count=train_span(reply+offset);memcpy(chunk,reply+offset,count);chunk[count]=0;
  lv_obj_t *body=text(22,52,198,chunk);lv_obj_set_height(body,178);lv_label_set_long_mode(body,LV_LABEL_LONG_DOT);
  char v[64];snprintf(v,sizeof(v),T("确定:音量 %u%%","OK: Volume %u%%"),passport_audio_volume_percent());hint(22,238,198,v);
  hint(22,262,198,(m->page==PAGE_HERMES || passport_muse_voice_ready())?T("按住下键说话 松开发送","Hold Down: talk"):T("Muse 蓝牙连接待完成","Muse Bluetooth pending"));
  hint(22,286,198,T("长按确定:返回菜单","Hold OK: Back to Menu"));
 } else if(m->page==PAGE_RADIO) {
  text(24,58,192,"SINGAPORE / MALAYSIA");box(22,96,196,98);
  text(33,112,175,passport_station[0]?passport_station:T("正在读取电台目录","Loading stations"));
  text(33,151,175,m->playing?T("正在播放","Playing"):T("已暂停","Paused"));
  text(24,208,190,T("上下换台","Up/Down: station"));
  text(24,232,190,T("确定:播放/暂停","OK: play/pause"));
  char volume[64];snprintf(volume,sizeof(volume),T("音量 %u%% 长按上下","Vol %u%% Hold U/D"),passport_audio_volume_percent());
  text(24,256,190,volume);
  text(24,286,190,T("长按确定:停止并返回","Hold OK: Back to Menu"));
 } else if(m->page==PAGE_BUS) {
  lv_obj_t *title=text(20,49,200,passport_bus_title);lv_obj_set_height(title,20);lv_label_set_long_mode(title,LV_LABEL_LONG_DOT);
  if(!passport_bus_rows)text(22,108,196,passport_bus_meta);
  for(unsigned i=0;i<passport_bus_rows && i<4;i++){
   int y=79+i*30;
   text(22,y,55,passport_bus_data[i].service);
   text(80,y,144,passport_bus_data[i].eta);
  }
  if(passport_bus_rows)text(22,246,196,passport_bus_meta);
  hint(22,268,198,T("上下键:翻页","Up/Down: Page"));
  hint(22,290,198,T("长按确定:返回菜单","Hold OK: Back to Menu"));
 } else if(m->page==PAGE_TRAIN) {
  lv_obj_t *status=text(20,48,200,passport_train_status[0]?passport_train_status:T("正在检查 LTA 列车警报","Checking LTA train feeds"));
  lv_obj_set_height(status,20);lv_label_set_long_mode(status,LV_LABEL_LONG_DOT);
  const char *details=passport_train_text&&passport_train_text[0]?passport_train_text:
   passport_train_stale?T("数据暂不可用。不能确认列车服务正常。请确定刷新。","Train data unavailable. Service status is unknown. Press OK to retry."):
   passport_train_known?T("LTA 当前未报告列车服务警报或列车中断行程更新。","LTA currently reports no train service alerts or disruption trip updates."):
   T("等待 LTA 数据。请先连接 Wi-Fi，并在手机设置保存 LTA 密钥。","Waiting for LTA data. Connect Wi-Fi and save your LTA key in phone Settings.");
  unsigned pages=0;size_t at=0,start=0,length=0;
  do {size_t n=train_span(details+at);if(!n)break;pages++;at+=n;}while(details[at] && pages<32768);
  if(!pages)pages=1;
  unsigned selected=m->reply_page%pages;
  for(unsigned i=0;i<selected;i++)start+=train_span(details+start);
  length=train_span(details+start);if(length>1499)length=passport_utf8_prefix(details+start,1499);
  char body[1500];memcpy(body,details+start,length);body[length]=0;
  lv_obj_t *message=text(22,76,196,body);lv_obj_set_height(message,154);lv_label_set_long_mode(message,LV_LABEL_LONG_WRAP);
  char page[48];snprintf(page,sizeof(page),T("%u/%u%s","%u/%u%s"),selected+1,pages,passport_train_stale?T(" · 不完整/缓存"," | partial/cached"):"");text(22,239,196,page);
  hint(22,263,196,T("上下翻页 确定刷新","Up/Down | OK: Refresh"));
  hint(22,287,198,T("长按确定:返回菜单","Hold OK: Back to Menu"));
 } else if(m->page==PAGE_WEATHER) {
  text(24,56,192,passport_config.city);text(24,100,192,passport_weather[0]?passport_weather:T("等待天气数据","Waiting for weather"));
  hint(22,218,198,T("来源:Open-Meteo\n室外天气 · 非机内传感器","Open-Meteo\nOutdoor weather"));
  hint(22,266,198,T("确定:刷新","OK: Refresh"));
  hint(22,290,198,T("长按确定:返回菜单","Hold OK: Back to Menu"));
 }
 /* Language changes replace differently sized labels; repaint the full root. */
 lv_obj_invalidate(screen);
 lv_screen_load(screen);
}
