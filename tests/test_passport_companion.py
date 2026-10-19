import importlib.util
import sys
from pathlib import Path
import threading
import unittest
from unittest.mock import patch
import urllib.error
import urllib.request
import json

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'companion'))
spec = importlib.util.spec_from_file_location('bridge', Path(__file__).resolve().parents[1] / 'companion/server.py')
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)

class BridgeTest(unittest.TestCase):
    def test_athena_never_discloses_private_fields(self):
        result = bridge.athena_summary({'usage': [{'email': 'private@example.test',
            'codexHome': '/private', 'name': 'private', 'provider': 'codex',
            'status': 'ok', 'rateLimits': {'primary': {'usedPercent': 42}}}]})
        self.assertEqual(result[0]['primary']['usedPercent'], 42)
        self.assertNotIn('private', json.dumps(result))

    def test_usage_remaining_inputs_and_missing_claude(self):
        rows=bridge.athena_summary({'usage':[{'provider':'codex','status':'ok','rateLimits':{'primary':{'usedPercent':25,'resetsAt':1800000000,'email':'private'}}}]})
        self.assertEqual(rows[0]['primary'],{'usedPercent':25,'resetsAt':1800000000})
        self.assertEqual(rows[-1]['provider'],'claude')
        self.assertEqual(rows[-1]['status'],'not_connected')
        self.assertIsNone(rows[-1]['primary'])
        self.assertNotIn('private',json.dumps(rows))
        rows=bridge.athena_summary({'usage':[{'provider':'claude','status':'ok','rateLimits':{'five_hour':{'utilization':101,'resets_at':'2026-10-18T01:00:00Z'},'seven_day':{'usedPercent':float('nan')}}}]})
        self.assertNotIn('usedPercent',rows[0]['primary'])
        self.assertIsNone(rows[0]['secondary'])

    def test_lta_account_key_does_not_follow_redirects(self):
        seen=[]
        class Redirect(bridge.BaseHTTPRequestHandler):
            def do_GET(self):
                seen.append(self.path)
                if self.path=='/redirect':
                    self.send_response(302);self.send_header('Location','/target');self.end_headers()
                else:
                    self.send_response(200);self.end_headers();self.wfile.write(b'{}')
            def log_message(self,*args):pass
        server=bridge.ThreadingHTTPServer(('127.0.0.1',0),Redirect)
        thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        try:
            with self.assertRaises(urllib.error.HTTPError) as error:
                bridge.request_json('http://127.0.0.1:'+str(server.server_port)+'/redirect',headers={'AccountKey':'test-lta-key'})
            self.assertEqual(error.exception.code,302);self.assertEqual(seen,['/redirect'])
        finally:server.shutdown();server.server_close();thread.join()

    def test_muse_does_not_call_local_model(self):
        with patch.object(bridge,'request_json') as request:
            bridge.agent_job('muse','test')
        request.assert_not_called()
        self.assertIn('Muse Passport',bridge.JOBS['muse']['text'])

    def test_weather_keeps_valid_cache_on_failure(self):
        bridge.WEATHER['singapore'] = {'fetched_at': 0, 'current': {'temperature_2m': 30}}
        with patch.object(bridge, 'request_json', side_effect=TimeoutError):
            result = bridge.weather('singapore')
        self.assertTrue(result['stale'])
        self.assertEqual(result['current']['temperature_2m'], 30)
        with self.assertRaises(ValueError): bridge.weather('unknown')

    def test_agent_failure_does_not_expose_token(self):
        with patch.dict(bridge.os.environ, {'HERMES_URL': 'http://example.invalid/v1'}), \
             patch.object(bridge, 'request_json', side_effect=ValueError('secret-token')):
            bridge.agent_job('hermes', 'test')
        self.assertEqual(bridge.JOBS['hermes']['state'], 'error')
        self.assertNotIn('secret', bridge.JOBS['hermes']['text'])

    def test_phone_relay_cors_is_opt_in_and_still_requires_token(self):
        server = bridge.ThreadingHTTPServer(('127.0.0.1', 0), bridge.Handler)
        server.token = 'test-token-long-enough'
        server.phone_relay = True
        thread = threading.Thread(target=server.serve_forever, daemon=True);thread.start()
        base = 'http://127.0.0.1:' + str(server.server_port)
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        try:
            origin = 'http://192.168.43.100'
            with opener.open(urllib.request.Request(base + '/v1/chat/hermes', method='OPTIONS', headers={'Origin': origin})) as r:
                self.assertEqual(r.status, 204)
                self.assertEqual(r.headers['Access-Control-Allow-Origin'], origin)
            with self.assertRaises(urllib.error.HTTPError) as ctx:
                opener.open(urllib.request.Request(base + '/v1/status', headers={'Origin': origin}))
            self.assertEqual(ctx.exception.code, 401)
            for origin in ['https://evil.example', 'http://8.8.8.8', 'http://192.168.1.2/path']:
                with self.assertRaises(urllib.error.HTTPError) as ctx:
                    opener.open(urllib.request.Request(base, method='OPTIONS', headers={'Origin': origin}))
                self.assertEqual(ctx.exception.code, 403)
            server.phone_relay = False
            with self.assertRaises(urllib.error.HTTPError):
                opener.open(urllib.request.Request(base, method='OPTIONS', headers={'Origin': 'http://192.168.1.2'}))
        finally:
            server.shutdown();server.server_close();thread.join()

    def test_http_authorization_and_bounds(self):
        server = bridge.ThreadingHTTPServer(('127.0.0.1', 0), bridge.Handler)
        server.token = 'a-long-random-test-token'
        thread = threading.Thread(target=server.serve_forever, daemon=True);thread.start()
        base = 'http://127.0.0.1:' + str(server.server_port)
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        try:
            with self.assertRaises(urllib.error.HTTPError) as ctx: opener.open(base + '/v1/status')
            self.assertEqual(ctx.exception.code, 401)
            headers = {'Authorization': 'Bearer ' + server.token}
            result = json.load(opener.open(urllib.request.Request(base + '/v1/status', headers=headers)))
            self.assertEqual(result['stations'], len(bridge.STATIONS))
            with self.assertRaises(urllib.error.HTTPError) as ctx:
                opener.open(urllib.request.Request(base + '/v1/radio?station=-1', headers=headers))
            self.assertEqual(ctx.exception.code, 400)
        finally:
            server.shutdown();server.server_close();thread.join()

if __name__ == '__main__': unittest.main()
