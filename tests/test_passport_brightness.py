"""Exercise actual brightness control/NVS code, including queue and save failures."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class BrightnessTest(unittest.TestCase):
 def test_default_saved_value_queue_and_storage_failures(self):
  source=(ROOT/'main/passport_network.c').read_text()
  state=source[source.index('static atomic_uint display_brightness'):source.index('static char lta_account_key')]
  request=source[source.index('void passport_brightness_request('):source.index('void passport_theme_request(')]
  block=source[source.index('  if(work.kind==7){'):source.index('  if(work.kind==6){')]
  load=source[source.index(' uint8_t brightness=70;'):source.index(' uint8_t language=0;')]
  harness=r'''
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define ESP_OK 0
#define pdTRUE 1
static unsigned store=1,writes,commits,stored=70;
static bool fail_write,fail_commit,missing,fail_queue,passport_dirty;
static void *control_queue=(void *)1;
typedef struct {unsigned kind,index;} control_t;
static control_t queued;
static int xQueueSend(void *q,const control_t *c,int timeout){assert(q==control_queue && timeout==0);if(fail_queue)return 0;queued=*c;return pdTRUE;}
static int nvs_set_u8(unsigned h,const char *key,unsigned level){assert(h==store && !strcmp(key,"brightness"));writes++;if(fail_write)return -1;stored=level;return ESP_OK;}
static int nvs_get_u8(unsigned h,const char *key,uint8_t *out){assert(h==store && !strcmp(key,"brightness"));if(missing)return -1;*out=stored;return ESP_OK;}
static int nvs_commit(unsigned h){assert(h==store);commits++;return fail_commit?-1:ESP_OK;}
static void lock(void){}
static void unlock(void){}
'''
  end=r'''
int main(void){
 assert(passport_brightness_get()==70);
 missing=true;load_saved();assert(passport_brightness_get()==70);
 missing=false;
 for(unsigned i=0;i<=110;i++){stored=i;load_saved();assert(passport_brightness_get()==(i>=10 && i<=100 && i%10==0?i:70));}
 stored=70;load_saved();
 passport_brightness_request(80);assert(queued.kind==7 && queued.index==80 && !passport_brightness_save_failed());
 queued=(control_t){0};passport_brightness_request(0);passport_brightness_request(15);passport_brightness_request(110);assert(!queued.kind);
 fail_queue=true;passport_brightness_request(80);assert(passport_brightness_save_failed() && passport_brightness_get()==70);fail_queue=false;
 save(80);assert(passport_brightness_get()==80 && !passport_brightness_save_failed());
 unsigned before=writes;save(0);save(15);save(101);assert(writes==before && passport_brightness_get()==80 && passport_brightness_save_failed());
 fail_write=true;save(90);assert(passport_brightness_get()==80 && passport_brightness_save_failed());fail_write=false;
 fail_commit=true;save(90);assert(passport_brightness_get()==80 && passport_brightness_save_failed());fail_commit=false;
 save(100);assert(passport_brightness_get()==100 && !passport_brightness_save_failed() && passport_dirty && commits>0);
}
'''
  code=harness+state+request+'\nstatic void load_saved(void){atomic_store(&display_brightness,70);'+load+'}\nstatic void save(unsigned level){struct {unsigned kind,wifi_profile;} work={7,level};for(unsigned once=0;once<1;once++){'+block+'}}\n'+end
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/'test.c').write_text(code)
   subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
