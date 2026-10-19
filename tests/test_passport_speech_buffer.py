"""Adapted upstream Muse speech-window/slow-link test (Apache-2.0)."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ReplySpeechTest(unittest.TestCase):
    def run_c(self, source, extra=()):
        with tempfile.TemporaryDirectory() as directory:
            c = Path(directory) / 'test.c'
            exe = Path(directory) / 'test'
            c.write_text(source)
            subprocess.run(['cc', '-std=gnu11', '-fsanitize=address,undefined', '-g',
                            '-I', str(ROOT / 'main/muse'), str(c), *map(str, extra), '-o', str(exe)],
                           check=True, capture_output=True, timeout=30)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_bounded_frames_cancel_fallback_and_slow_link(self):
        self.run_c(r'''
#include "muse_speech_buffer.h"
#include <assert.h>
#include <string.h>
static unsigned char p[129], out[120];
static void number(unsigned char *p, unsigned n){for(int i=0;i<4;i++)p[i]=n>>(8*i);}
static bool feed(muse_speech_buffer_t *b,unsigned id,unsigned frame,unsigned kind){
 number(p,id);number(p+4,frame);p[8]=kind;
 return muse_speech_buffer_feed(b,p,kind?9:129);
}
int main(void){
 muse_speech_buffer_t b;
 muse_speech_buffer_begin(&b,7);
 assert(!feed(&b,6,0,0) && !b.abort && b.count==0);
 for(unsigned i=0;i<8;i++)assert(feed(&b,7,i,0));
 assert(b.count==8 && !feed(&b,7,8,0) && b.terminal);
 muse_speech_buffer_begin(&b,8);
 assert(feed(&b,8,0,0));assert(!feed(&b,8,0,0) && b.abort);
 muse_speech_buffer_begin(&b,9);
 assert(feed(&b,9,0,0));assert(feed(&b,9,0,2));
 assert(!muse_speech_buffer_take(&b,out));
 b.count=0;b.rejected=true;b.abort=false;
 assert(!feed(&b,9,1,0));assert(feed(&b,9,0,3));
 assert(b.received==0 && b.consumed==0 && !b.rejected);
 assert(feed(&b,9,0,0));b.started=true;b.rejected=true;
 assert(!feed(&b,9,0,3));
 muse_speech_buffer_begin(&b,10);
 assert(feed(&b,10,0,0));assert(feed(&b,10,1,1));
 assert(muse_speech_buffer_take(&b,out)==120 && b.count==0 && b.ended);
 assert(!feed(&b,10,1,0));
 muse_speech_buffer_begin(&b,11);
 assert(feed(&b,11,0,4) && b.terminal && b.abort);
 // 120 s: one aggregate transport packet per 30 ms event. Credit status
 // and its ACK each consume an event; foreground synthesis is pre-generated.
 // This proves queue/packet budget, not a Bluetooth or I2S device result.
 muse_speech_buffer_begin(&b,12);
 unsigned sent=0,limit=8,played=0,next_play=0,controls=0;bool playing=false;
 for(unsigned ms=0;played<2000 && ms<125000;ms+=30){
   if(controls)controls--;
   else if(sent<limit && sent<2000){assert(feed(&b,12,sent,0));sent++;}
   if(!playing && b.count>=4){playing=true;next_play=ms+60;}
   if(playing && ms>=next_play){
     assert(muse_speech_buffer_take(&b,out)==120);played++;next_play+=60;
     if(played==1 || played%4==0){limit=played+8;controls+=2;}
   }
   assert(b.count<=8);
 }
 assert(played==2000 && sent==2000 && !b.abort);
 // Malformed/stale fuzz input never escapes the bounded receiver.
 for(unsigned i=0;i<10000;i++){
   muse_speech_buffer_begin(&b,i+20);
   for(unsigned j=0;j<129;j++)p[j]=(i*31+j*17)&255;
   muse_speech_buffer_feed(&b,p,i%130);
   assert(b.count<=8);
 }
}
''', [ROOT / 'main/muse/muse_speech_buffer.c'])


if __name__ == "__main__":
    unittest.main()
