import json
from pathlib import Path
import sys
import threading
import unittest
from unittest.mock import patch
import urllib.error
import urllib.request

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'companion'))
from voice import VoiceSessions, VoiceError, MAX_BYTES
import server as bridge


class SessionTest(unittest.TestCase):
    def test_order_retries_and_cancel_prevent_duplicate_or_stale_audio(self):
        sessions = VoiceSessions()
        sid = sessions.start()['session']
        first = b'\x01\x02' * 2048
        self.assertEqual(sessions.chunk(sid, 0, first)['bytes'], 4096)
        self.assertEqual(sessions.chunk(sid, 0, first)['bytes'], 4096)
        for sequence, data in ((0, b'\x03\x04'), (2, first)):
            with self.assertRaises(VoiceError):
                sessions.chunk(sid, sequence, data)
        sessions.chunk(sid, 1, first)
        self.assertEqual(sessions.finish(sid), first * 2)
        with self.assertRaises(VoiceError):
            sessions.finish(sid)
        sid = sessions.start()['session']
        sessions.cancel(sid)
        with self.assertRaises(VoiceError):
            sessions.chunk(sid, 0, first)

    def test_bounds_expiry_and_single_recording(self):
        now = [0]
        sessions = VoiceSessions(lambda: now[0])
        sid = sessions.start()['session']
        with self.assertRaises(VoiceError):
            sessions.start()
        for data in (b'', b'x', b'xx' * 2049):
            with self.assertRaises(VoiceError):
                sessions.chunk(sid, 0, data)
        sessions.chunk(sid, 0, b'xx')
        with self.assertRaises(VoiceError):
            sessions.finish(sid)
        self.assertFalse(sessions.active)
        sid = sessions.start()['session']
        now[0] = 36
        self.assertFalse(sessions.active)
        with self.assertRaises(VoiceError):
            sessions.chunk(sid, 0, b'xx')
        sid = sessions.start()['session']
        for sequence in range(MAX_BYTES // 4000):
            sessions.chunk(sid, sequence, b'xx' * 2000)
        with self.assertRaises(VoiceError) as error:
            sessions.chunk(sid, MAX_BYTES // 4000, b'xx')
        self.assertEqual(error.exception.status, 413)
        self.assertFalse(sessions.active)


class VoiceHttpTest(unittest.TestCase):
    def test_authenticated_upload_transcribes_once_and_submits_to_hermes(self):
        server = bridge.ThreadingHTTPServer(('127.0.0.1', 0), bridge.Handler)
        server.token = 'test-voice-token'
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        base = 'http://127.0.0.1:' + str(server.server_port)
        completed = threading.Event()

        def agent(agent, text):
            self.assertEqual((agent, text), ('hermes', 'say hello'))
            with bridge.LOCK:
                bridge.JOBS['hermes'] = {'state': 'done', 'text': 'Hello'}
            completed.set()

        def post(path, data, token=True):
            headers = {'Content-Type': 'application/octet-stream' if isinstance(data, bytes) else 'application/json'}
            if token:
                headers['Authorization'] = 'Bearer ' + server.token
            body = data if isinstance(data, bytes) else json.dumps(data).encode()
            with opener.open(urllib.request.Request(base + path, data=body, headers=headers), timeout=3) as response:
                return response.status, json.load(response)

        options = dict(agent='hermes', sample_rate=16000, channels=1, format='s16le')
        try:
            with patch.object(bridge, 'VOICE', VoiceSessions()), \
                 patch.dict(bridge.os.environ, {'HERMES_URL': 'http://example.invalid/v1'}), \
                 patch.object(bridge, 'transcribe', return_value='say hello') as speech, \
                 patch.object(bridge, 'agent_job', side_effect=agent):
                bridge.JOBS['hermes'] = {'state': 'idle', 'text': ''}
                with self.assertRaises(urllib.error.HTTPError) as error:
                    post('/v1/voice/start', options, token=False)
                self.assertEqual(error.exception.code, 401)
                _, opened = post('/v1/voice/start', options)
                sid = opened['session']
                with self.assertRaises(urllib.error.HTTPError) as error:
                    post('/v1/chat/hermes', {'text': 'competing message'})
                self.assertEqual(error.exception.code, 409)
                for sequence in range(2):
                    post('/v1/voice/chunk?session=' + sid + '&seq=' + str(sequence), b'\x01\x02' * 2048)
                status, result = post('/v1/voice/finish', {'session': sid})
                self.assertEqual((status, result['state']), (202, 'working'))
                self.assertTrue(completed.wait(3))
                speech.assert_called_once_with(b'\x01\x02' * 4096)
                with self.assertRaises(urllib.error.HTTPError):
                    post('/v1/voice/finish', {'session': sid})
                _, opened = post('/v1/voice/start', options)
                post('/v1/voice/cancel', {'session': opened['session']})
                self.assertFalse(bridge.VOICE.active)
                with self.assertRaises(urllib.error.HTTPError) as error:
                    post('/v1/voice/start', {**options, 'agent': 'muse'})
                self.assertEqual(error.exception.code, 503)
        finally:
            server.shutdown()
            server.server_close()
            thread.join()

    def test_speech_backend_error_does_not_leak_private_details(self):
        with patch.object(bridge, 'transcribe', side_effect=ValueError('private-secret-model-path')), \
             patch.object(bridge, 'agent_job') as agent:
            bridge.voice_job(b'xx')
        agent.assert_not_called()
        self.assertEqual(bridge.JOBS['hermes']['state'], 'error')
        self.assertNotIn('private', bridge.JOBS['hermes']['text'])


if __name__ == '__main__':
    unittest.main()
