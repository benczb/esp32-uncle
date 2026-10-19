"""Exercise real page-owned reply memory, including allocation failure and retry."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include "passport_reply.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
void *__real_calloc(size_t,size_t);
void __real_free(void *);
static size_t allocated;
static unsigned live,fail;
void *__wrap_calloc(size_t n,size_t size){
 assert(n*size==PASSPORT_REPLY_CAPACITY);
 if(fail){fail--;return NULL;}
 void *p=__real_calloc(n,size);assert(p);live++;allocated+=n*size;return p;
}
void __wrap_free(void *p){if(p){assert(live);live--;allocated-=PASSPORT_REPLY_CAPACITY;}__real_free(p);}
int main(void){
 assert(!passport_reply_peek(0) && !passport_reply_buffer(0));
 assert(!passport_reply_buffer(2) && !passport_reply_peek(2));
 for(unsigned cycle=0;cycle<2000;cycle++){
  for(unsigned page=0;page<=PAGE_SETTINGS;page++){
   passport_reply_select((passport_page_t)page);
   if(page==PAGE_HERMES || page==PAGE_MUSE){
    unsigned a=page==PAGE_MUSE;char *reply=passport_reply_buffer(a);
    assert(reply && !reply[0] && live==1 && allocated==PASSPORT_REPLY_CAPACITY);
    assert(!passport_reply_buffer(!a) && !passport_reply_peek(!a));
    memset(reply,'x',PASSPORT_REPLY_CAPACITY-1);reply[PASSPORT_REPLY_CAPACITY-1]=0;
    passport_reply_select((passport_page_t)page);
    assert(passport_reply_buffer(a)==reply && strlen(reply)==PASSPORT_REPLY_CAPACITY-1);
   }else{
    assert(!live && !allocated && !passport_reply_peek(0) && !passport_reply_peek(1));
    /* A late result cannot recreate memory after navigation. */
    assert(!passport_reply_buffer(0) && !passport_reply_buffer(1));
   }
  }
 }
 passport_reply_select(PAGE_HOME);fail=1;
 passport_reply_select(PAGE_HERMES);assert(!live && !passport_reply_peek(0));
 char *retry=passport_reply_buffer(0);assert(retry && live==1 && !retry[0]);
 passport_reply_select(PAGE_MUSE);assert(live==1 && !passport_reply_peek(0));
 passport_reply_select(PAGE_HOME);assert(!live && !allocated);
 return 0;
}
'''


class ReplyMemoryTest(unittest.TestCase):
    def test_page_ownership_stale_results_and_retry(self):
        with tempfile.TemporaryDirectory() as directory:
            cfile = Path(directory) / 'reply.c'
            binary = Path(directory) / 'reply'
            cfile.write_text(HARNESS)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I' + str(ROOT / 'main'), str(cfile),
                            str(ROOT / 'main/passport_reply.c'),
                            '-Wl,--wrap=calloc', '-Wl,--wrap=free', '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
