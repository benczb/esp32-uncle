"""Fault-inject the real bus cache allocator without replacing JSON parsing."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <assert.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
static void *owned;
static size_t live_bytes,peak_bytes;
static unsigned calls,fail_at;
static void *cache_realloc(void *p,size_t n){
 assert(p==owned && n);if(++calls==fail_at)return NULL;
 void *next=realloc(p,n);assert(next);owned=next;live_bytes=n;
 if(n>peak_bytes)peak_bytes=n;
 return next;
}
static void cache_free(void *p){assert(!p || p==owned);free(p);owned=NULL;live_bytes=0;}
#define realloc cache_realloc
#define free cache_free
#include "passport_public_data.c"
#undef realloc
#undef free
static cJSON *response(unsigned services,unsigned directions){
 cJSON *root=cJSON_CreateObject(),*list=cJSON_AddArrayToObject(root,"Services");
 cJSON_AddStringToObject(root,"BusStopCode","82009");
 for(unsigned i=0;i<services;i++){
  cJSON *s=cJSON_CreateObject();cJSON_AddItemToArray(list,s);
  char number[13];snprintf(number,sizeof(number),"%u",i+1);cJSON_AddStringToObject(s,"ServiceNo",number);
  const char *names[]={"NextBus","NextBus2","NextBus3"};
  for(unsigned d=0;d<directions;d++){
   cJSON *a=cJSON_AddObjectToObject(s,names[d]);char dest[6];snprintf(dest,sizeof(dest),"%05u",d+1);
   cJSON_AddStringToObject(a,"DestinationCode",dest);
   cJSON_AddStringToObject(a,"EstimatedArrival","2026-10-17T12:02:00+08:00");
  }
 }
 return root;
}
int main(void){
 time_t now=passport_iso_epoch("2026-10-17T12:00:00+08:00");
 assert(!rows&&!live_bytes&&!calls);
 cJSON *empty=response(0,0),*one=response(1,1),*large=response(25,3);
 assert(passport_public_bus_parse_lta(empty,"82009",now)&&!live_bytes);
 assert(passport_public_bus_parse_lta(one,"82009",now)&&live_bytes==sizeof(bus_row_t));
 fail_at=calls+1;assert(!passport_public_bus_parse_lta(large,"82009",now));
 assert(row_count==1 && bus_valid && live_bytes==sizeof(bus_row_t));
 cJSON *page=passport_public_bus_page(0,now,true);
 assert(cJSON_IsTrue(cJSON_GetObjectItem(page,"available"))&&cJSON_IsTrue(cJSON_GetObjectItem(page,"stale")));cJSON_Delete(page);
 fail_at=0;assert(passport_public_bus_parse_lta(large,"82009",now));
 assert(row_count==MAX_ROWS&&live_bytes==MAX_ROWS*sizeof(bus_row_t));
 fail_at=calls+2;assert(passport_public_bus_parse_lta(one,"82009",now));
 assert(row_count==1&&bus_valid&&live_bytes==3*sizeof(bus_row_t));
 fail_at=0;
 for(unsigned i=0;i<1000;i++){
  assert(passport_public_bus_parse_lta(one,"82009",now)&&live_bytes==sizeof(bus_row_t));
  passport_public_bus_reset();assert(!rows&&!owned&&!live_bytes&&!bus_valid);
 }
 assert(peak_bytes<=MAX_ROWS*sizeof(bus_row_t));
 cJSON_Delete(empty);cJSON_Delete(one);cJSON_Delete(large);
 passport_public_bus_reset();assert(!live_bytes);
 return 0;
}
'''


class BusCacheTest(unittest.TestCase):
    def test_bounded_cache_failure_preservation_and_reset(self):
        idf = Path(os.environ.get('IDF_PATH', ROOT/'.toolchains/esp-idf-v5.5.3'))
        cjson = idf / 'components/json/cJSON'
        if (cjson/'cJSON.c').is_file():
            json_flags = ['-I'+str(cjson), str(cjson/'cJSON.c')]
        else:
            json_flags = shlex.split(subprocess.check_output(
                ['pkg-config', '--cflags', '--libs', 'libcjson'], text=True))
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'cache.c'
            binary = Path(directory) / 'cache'
            source.write_text(HARNESS)
            subprocess.run(['cc', '-std=c11', '-D_POSIX_C_SOURCE=200809L',
                            '-Wall', '-Wextra', '-Werror', '-I'+str(ROOT/'main'),
                            str(source), *json_flags,
                            '-lm', '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
