"""Fault-inject the production web-server lifetime with real page policy."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include "passport_model.h"
#include <assert.h>
#include <stdatomic.h>
#include <string.h>
#define ESP_OK 0
#define ESP_FAIL (-1)
#define ESP_LOGW(...) ((void)0)
#define HTTP_GET 0
#define HTTP_POST 1
typedef void *httpd_handle_t;
typedef struct {unsigned stack_size,max_uri_handlers;} httpd_config_t;
typedef struct {const char *uri;int method;int (*handler)(void *);} httpd_uri_t;
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
#define HANDLER(name) static int name(void *r){(void)r;return 0;}
HANDLER(settings_get) HANDLER(config_get) HANDLER(config_post) HANDLER(clock_post)
HANDLER(avatar_post) HANDLER(chat_post) HANDLER(relay_post)
static httpd_handle_t settings_server;
static atomic_bool settings_web_ready,setup_active;
static bool relay,fail_start,fail_stop;
static unsigned page,live,starts,stops,registered,fail_route;
static int token;
static unsigned passport_function_active(void){return page;}
static bool phone_relay_active(void){return relay;}
static int httpd_start(httpd_handle_t *out,const httpd_config_t *cfg){
 assert(!live&&out&&!*out&&cfg->stack_size==8192&&cfg->max_uri_handlers==7);
 starts++;if(fail_start)return ESP_FAIL;
 live=1;registered=0;*out=&token;return ESP_OK;
}
static int httpd_stop(httpd_handle_t server){
 assert(server==&token&&live==1);stops++;
 if(fail_stop)return ESP_FAIL;
 live=0;return ESP_OK;
}
static int httpd_register_uri_handler(httpd_handle_t server,const httpd_uri_t *r){
 assert(server==&token&&live==1&&r&&r->uri&&r->handler);
 assert(r->method==HTTP_GET||r->method==HTTP_POST);
 return ++registered==fail_route?ESP_FAIL:ESP_OK;
}
'''
END = r'''
static void close_server(void){page=PAGE_HOME;setup_active=false;relay=false;fail_stop=false;settings_web_poll();assert(!live&&!settings_server&&!settings_web_ready);}
int main(void){
 page=PAGE_HOME;settings_web_poll();assert(!starts&&!live);
 page=PAGE_SETTINGS;fail_start=true;settings_web_poll();assert(starts==1&&!settings_server&&!settings_web_ready);
 fail_start=false;settings_web_poll();assert(live&&settings_web_ready&&registered==7);
 unsigned previous=starts;settings_web_poll();assert(starts==previous);
 page=PAGE_HOME;fail_stop=true;settings_web_poll();assert(live&&settings_server&&!settings_web_ready);
 page=PAGE_SETTINGS;settings_web_poll();assert(starts==previous&&live&&!settings_web_ready);
 fail_stop=false;settings_web_poll();assert(starts==previous+1&&live&&settings_web_ready);close_server();
 for(unsigned route=1;route<=7;route++){
  fail_route=route;page=PAGE_SETTINGS;settings_web_poll();assert(!live&&!settings_server&&!settings_web_ready);
  fail_route=0;settings_web_poll();assert(live&&settings_web_ready);close_server();
 }
 page=PAGE_SETTINGS;fail_route=4;fail_stop=true;previous=starts;
 settings_web_poll();assert(live&&settings_server&&!settings_web_ready&&starts==previous+1);
 settings_web_poll();assert(starts==previous+1&&live&&!settings_web_ready);
 fail_stop=false;fail_route=0;settings_web_poll();assert(starts==previous+2&&settings_web_ready);close_server();
 page=PAGE_HOME;setup_active=true;settings_web_poll();assert(settings_web_ready);close_server();
 page=PAGE_APPS;relay=true;settings_web_poll();assert(settings_web_ready);
 relay=false;settings_web_poll();assert(!live&&!settings_web_ready);
 page=PAGE_RADIO;settings_web_poll();assert(!live); /* Native radio needs no phone server. */
 page=PAGE_SETTINGS;relay=true;settings_web_poll();assert(settings_web_ready);
 previous=starts;
 page=PAGE_PREFERENCES;settings_web_poll();assert(settings_web_ready&&starts==previous);
 page=PAGE_APPS;settings_web_poll();assert(settings_web_ready&&starts==previous);
 page=PAGE_RADIO;settings_web_poll();assert(settings_web_ready&&starts==previous);
 for(unsigned destination=PAGE_HOME;destination<=PAGE_LANGUAGE;destination++){
  page=destination;settings_web_poll();assert(settings_web_ready&&starts==previous);
 }
 page=PAGE_RADIO;
 relay=false;settings_web_poll();assert(!live&&!settings_web_ready);
 page=PAGE_MUSE;settings_web_poll();assert(!live); /* Native companion owns Muse. */
 for(unsigned cycle=0;cycle<1000;cycle++){
  page=PAGE_SETTINGS;settings_web_poll();assert(live&&settings_web_ready);
  previous=starts;settings_web_poll();assert(starts==previous);
  close_server();
 }
 return 0;
}
'''


class WebLifetimeTest(unittest.TestCase):
    def test_start_routes_stop_retry_and_repeated_page_lifetime(self):
        source = (ROOT / 'main/passport_network.c').read_text()
        start = source.index('static void settings_web_poll(')
        end = source.index('static void network_worker(', start)
        with tempfile.TemporaryDirectory() as directory:
            cfile = Path(directory) / 'web.c'
            binary = Path(directory) / 'web'
            cfile.write_text(HARNESS + source[start:end] + END)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I'+str(ROOT/'main'), str(cfile),
                            str(ROOT/'main/passport_model.c'), '-lm', '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
