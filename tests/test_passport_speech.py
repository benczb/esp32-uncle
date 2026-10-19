"""Reply speech authentication, deduplication and bounded local synthesis."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch
import urllib.error
import urllib.request
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'companion'))
import server as bridge
import speech
ROOT = Path(__file__).resolve().parents[1]


class SpeechTest(unittest.TestCase):
    def test_local_voice_uses_stdin_and_caches_one_reply(self):
        with patch.object(speech, '_CACHE', ('', b'')), \
             patch.object(speech, 'executable', return_value='/private/espeak-ng'), \
             patch.object(speech.subprocess, 'run') as run:
            run.side_effect = [subprocess.CompletedProcess([], 0, b'wave'),
                               subprocess.CompletedProcess([], 0, b'\x01\x02' * 400)]
            ident = 'a' * 32
            self.assertEqual(speech.synthesize(ident, 'Hello'), b'\x01\x02' * 400)
            self.assertEqual(speech.synthesize(ident, 'Hello'), b'\x01\x02' * 400)
            self.assertEqual(run.call_count, 2)
            args, kwargs = run.call_args_list[0]
            self.assertNotIn('Hello', args[0])
            self.assertEqual(kwargs['input'], b'Hello')
            self.assertNotIn('shell', kwargs)
            for bad in ('', 'x'*32, '../token', 'a'*33):
                with self.assertRaises(ValueError):
                    speech.synthesize(bad, 'Hello')
            with self.assertRaises(ValueError):
                speech.synthesize('b'*32, 'x'*6001)

    def test_speech_route_auth_and_expired_ids(self):
        server = bridge.ThreadingHTTPServer(('127.0.0.1', 0), bridge.Handler)
        server.token = 'test-speech-token'
        thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        base = 'http://127.0.0.1:' + str(server.server_port)
        def get(ident, auth=True):
            return opener.open(urllib.request.Request(base+'/v1/speech/hermes?id='+ident,
                headers={'Authorization': 'Bearer '+server.token} if auth else {}), timeout=3)
        try:
            with patch.dict(bridge.JOBS, hermes={'state':'done','text':'Hello','speech_id':'a'*32}), \
                 patch.object(bridge, 'synthesize', return_value=b'\x01\x02'*400) as synth:
                for ident, auth, code in [('a'*32,False,401),('b'*32,True,409)]:
                    with self.assertRaises(urllib.error.HTTPError) as error:get(ident,auth)
                    self.assertEqual(error.exception.code, code)
                synth.assert_not_called()
                with get('a'*32) as response:
                    self.assertEqual(response.read(), b'\x01\x02'*400)
                    self.assertIn('16000', response.headers['X-Audio-Format'])
                synth.assert_called_once_with('a'*32,'Hello')
        finally:
            server.shutdown();server.server_close();thread.join()

    def test_volume_navigation_and_reply_compaction(self):
        c = r'''
#include "passport_model.h"
#include "passport_reply.h"
#include <assert.h>
#include <string.h>
int main(void){
 passport_model_t m={.page=PAGE_MUSE};int delta=99;
 assert(!passport_assistant_volume_key(&m,1,true,&delta) && !m.volume_mode);
 assert(passport_assistant_volume_key(&m,2,false,&delta) && m.volume_mode && !delta);
 assert(passport_assistant_volume_key(&m,1,true,&delta) && delta==-10);
 assert(passport_assistant_volume_key(&m,0,false,&delta) && delta==10);
 assert(passport_assistant_volume_key(&m,2,false,&delta) && !m.volume_mode);
 m.volume_mode=true;assert(!passport_assistant_volume_key(&m,2,true,&delta) && !m.volume_mode);
 m.page=PAGE_RADIO;assert(!passport_assistant_volume_key(&m,2,false,&delta));
 passport_reply_select(PAGE_MUSE);strcpy(passport_reply_buffer(1),"Reply preserved");
 passport_reply_compact(1);assert(!strcmp(passport_reply_peek(1),"Reply preserved"));
 char *p=passport_reply_buffer(1);assert(p && !strcmp(p,"Reply preserved"));
 memset(p,'x',PASSPORT_REPLY_CAPACITY-1);p[PASSPORT_REPLY_CAPACITY-1]=0;
 passport_reply_compact(1);assert(strlen(passport_reply_peek(1))==PASSPORT_REPLY_CAPACITY-1);
 passport_reply_select(PAGE_HOME);assert(!passport_reply_peek(1));
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'test.c';source.write_text(c);exe=Path(directory)/'test'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                '-I'+str(ROOT/'main'),str(source),str(ROOT/'main/passport_model.c'),
                str(ROOT/'main/passport_reply.c'),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)


if __name__ == '__main__':unittest.main()
