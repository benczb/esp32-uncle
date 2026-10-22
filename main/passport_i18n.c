#include "passport_i18n.h"
#include <stdatomic.h>
static atomic_uint current_language;
unsigned passport_language_get(void){return atomic_load(&current_language);}
void passport_language_set(unsigned language){atomic_store(&current_language,language==1?1:0);}
const char *passport_tr(const char *chinese,const char *english){return passport_language_get()==1?english:chinese;}
#include <string.h>
const char *passport_message(const char *english){
 if(!english)return "";
 if(passport_language_get())return english;
 static const struct {const char *en,*zh;} messages[]={
  {"OK: Connect to Komoot","确定：连接 Komoot"},
  {"Bluetooth reconnecting","蓝牙正在重新连接"},
  {"Waiting for Komoot route","等待 Komoot 路线"},
  {"Arrived","已到达"},
  {"Komoot connected","Komoot 已连接"},
  {"Komoot read failed; reconnecting","路线读取失败，正在重连"},
  {"Komoot subscription failed","路线订阅失败"},
  {"Komoot notifications unavailable","路线通知不可用"},
  {"Komoot direction service unavailable","导航服务不可用"},
  {"Komoot service not found","未找到 Komoot 服务"},
  {"Connecting to Komoot","正在连接 Komoot"},
  {"Reading Komoot service","正在读取导航服务"},
  {"Komoot unavailable; retrying","Komoot 不可用，正在重试"},
  {"Komoot disconnected; retrying","Komoot 已断开，正在重试"},
  {"Navigation stopped","导航已停止"},
  {"Bluetooth needs free memory; retrying","蓝牙内存不足，正在重试"},
  {"Start navigation in Komoot","请在 Komoot 开始导航"},
  {"Komoot read failed","路线读取失败"},
  {"Enable Navigation BLE in build","此固件未启用蓝牙导航"},
  {"Direction unavailable","方向不可用"},
  {"Straight","直行"},
  {"Start","出发"},
  {"Keep left","靠左行驶"},
  {"Turn left","左转"},
  {"Sharp left","向左急转"},
  {"Sharp right","向右急转"},
  {"Turn right","右转"},
  {"Keep right","靠右行驶"},
  {"Fork right","岔路向右"},
  {"Fork left","岔路向左"},
  {"U-turn","掉头"},
  {"Exit roundabout","驶出环岛"},
  {"Off route","已偏离路线"},
  {"Roundabout","环岛"},
  {"loading","正在加载"},
  {"playing","正在播放"},
  {"paused","已暂停"},
  {"idle","等待播放"},
  {"offline","未连接"},
  {"error","连接错误"},
  {"Connect Spotify at companion /tools","请在手机设置连接 Spotify"},
  {"Upload books at companion /tools","请在手机设置上传图书"},
  {"Upload TXT or EPUB at /tools","请在手机设置上传图书"},
  {"Check companion, Spotify login and active player","检查电脑连接及 Spotify 登录"},
  {"Start playback in Spotify","请在 Spotify 播放歌曲"},
  {"Spotify rate limited; retry later","Spotify 请求过多，请稍后重试"},
  {"Sign in to Spotify at /tools","请在手机设置登录 Spotify"},
  {"Spotify login expired; sign in again","Spotify 登录已过期，请重新登录"},
  {"Spotify permission or Premium required","需要 Spotify 授权或高级会员"},
  {"Open Spotify and start playback on your phone","请在手机 Spotify 播放歌曲"},
  {"Spotify unavailable","Spotify 暂不可用"},
  {"Spotify unavailable; check companion connection","Spotify 不可用，请检查电脑连接"},
 };
 for(unsigned i=0;i<sizeof(messages)/sizeof(messages[0]);i++)if(!strcmp(english,messages[i].en))return messages[i].zh;
 return english;
}
