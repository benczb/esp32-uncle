"""Account setup with native-provider process doubles, never real account login."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request
from http.server import ThreadingHTTPServer

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'companion'))
from accounts import AccountSetup, SetupError, provider_url
import server

FAKE = r'''
import json, os, sys, time
if os.environ.get('FAKE_PROVIDER') == 'claude':
 print('private-log-never-returned',flush=True)
 print('https://evil.example/auth?token=private-token',flush=True)
 print('https://claude.ai/oauth/authorize?state=test-state',flush=True)
 time.sleep(30)
else:
 for line in sys.stdin:
  j=json.loads(line)
  if j.get('method')=='initialize': print(json.dumps({'id':j['id'],'result':{}}),flush=True)
  elif j.get('method')=='account/login/start':
   assert j['params']=={'type':'chatgptDeviceCode'}
   print(json.dumps({'id':j['id'],'result':{'verificationUrl':'https://auth.openai.com/codex/device','userCode':'ABCD-1234'}}),flush=True)
   if os.environ.get('FAKE_WAIT')=='1':time.sleep(30)
   else:
    time.sleep(.2)
    fd=os.open(os.path.join(os.environ['CODEX_HOME'],'auth.json'),os.O_WRONLY|os.O_CREAT,0o600)
    with os.fdopen(fd,'w') as f:json.dump({'test_fixture_only':True},f)
    print(json.dumps({'method':'account/login/completed','params':{'success':True}}),flush=True)
    break
'''


class AccountSetupTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.script = self.root / 'native-provider-double.py'
        self.script.write_text(FAKE)
        self.sent, self.children = [], []
        self.wait = False
        self.manager = AccountSetup(self.tracker, self.root / 'profiles', self.popen, lambda name: '/official/'+name)

    def tracker(self, path, body=None):
        self.sent.append((path, body))
        return {'account': {'id': 'tracker-account-'+str(len(self.sent))}}

    def popen(self, args, **kwargs):
        self.children.append((args, kwargs['env'].copy()))
        kwargs['env']['FAKE_PROVIDER'] = 'claude' if args[0].endswith('claude') else 'codex'
        kwargs['env']['FAKE_WAIT'] = '1' if self.wait else '0'
        return subprocess.Popen([sys.executable, str(self.script)], **kwargs)

    def tearDown(self):
        for key in list(self.manager.sessions):
            self.manager.cancel(key)
        deadline = time.monotonic()+3
        while any(s['process'].poll() is None for s in self.manager.sessions.values()) and time.monotonic()<deadline:
            time.sleep(.02)
        self.temp.cleanup()

    def until(self, key, state):
        deadline = time.monotonic()+4
        while time.monotonic()<deadline:
            result = self.manager.status(key)
            if result['state'] == state:
                return result
            time.sleep(.02)
        self.fail(str(self.manager.status(key)))

    def test_device_login_isolated_saved_and_no_secrets_in_status(self):
        key = self.manager.add('codex', 'My OpenAI')['id']
        self.assertEqual(self.sent[0][0], '/api/accounts')
        self.assertEqual(self.sent[0][1]['codexHome'], str(self.root/'profiles'/key))
        os.environ['PASSPORT_TEST_SECRET'] = 'never-inherit'
        try:
            self.manager.start(key)
        finally:
            os.environ.pop('PASSPORT_TEST_SECRET')
        waiting = self.until(key, 'waiting')
        self.assertEqual(waiting['userCode'], 'ABCD-1234')
        self.assertEqual(waiting['authUrl'], 'https://auth.openai.com/codex/device')
        self.until(key, 'done')
        self.assertNotIn('userCode', self.manager.status(key))
        self.assertNotIn('authUrl', self.manager.status(key))
        self.assertNotIn('PASSPORT_TEST_SECRET', self.children[0][1])
        self.assertIn('cli_auth_credentials_store="file"', self.children[0][0])
        listing = json.dumps(self.manager.list())
        self.assertNotIn(str(self.root), listing)
        self.assertNotIn('test_fixture_only', listing)
        self.assertEqual((self.root/'profiles').stat().st_mode & 0o777, 0o700)
        self.assertEqual((self.root/'profiles/accounts.json').stat().st_mode & 0o777, 0o600)
        restored = AccountSetup(self.tracker, self.root/'profiles')
        self.assertEqual(restored.status(key)['state'], 'done')

    def test_cancel_is_terminal_and_claude_uses_only_native_login(self):
        key = self.manager.add('claude', 'My Claude')['id']
        self.manager.start(key)
        waiting = self.until(key, 'waiting')
        deadline = time.monotonic()+2
        while not self.manager.status(key).get('authUrl') and time.monotonic()<deadline:
            time.sleep(.02)
        result = self.manager.status(key)
        self.assertEqual(result['authUrl'], 'https://claude.ai/oauth/authorize?state=test-state')
        self.assertNotIn('private', json.dumps(result))
        self.assertEqual(self.children[0][0], ['/official/claude','auth','login','--claudeai'])
        self.assertIn('CLAUDE_CONFIG_DIR', self.children[0][1])
        self.manager.cancel(key)
        time.sleep(.1)
        self.assertEqual(self.manager.status(key)['state'], 'cancelled')
        self.assertNotIn('authUrl', self.manager.status(key))

    def test_provider_validation_traversal_single_login_and_expiry(self):
        for url in ['http://auth.openai.com/x','https://auth.openai.com.evil/x','https://a@auth.openai.com/x','https://auth.openai.com:444/x','https://auth.openai.com/x?access_token=secret','https://auth.openai.com/x#secret']:
            self.assertFalse(provider_url(url, 'codex'))
        with self.assertRaises(SetupError): self.manager.add('sh', 'no')
        with self.assertRaises(SetupError): self.manager.status('../other')
        first = self.manager.add('codex', 'first')['id']
        second = self.manager.add('codex', 'second')['id']
        self.wait = True
        self.manager.start(first)
        self.until(first, 'waiting')
        with self.assertRaises(SetupError): self.manager.start(second)
        self.manager._expire(first, self.manager.sessions[first])
        self.assertEqual(self.manager.status(first)['state'], 'error')

    def test_http_requires_bridge_auth_and_rejects_unknown_account(self):
        previous = server.ACCOUNT_SETUP
        server.ACCOUNT_SETUP = self.manager
        http = ThreadingHTTPServer(('127.0.0.1',0), server.Handler)
        http.token = 'test-owner-only-token'
        http.phone_relay = False
        thread = threading.Thread(target=http.serve_forever, daemon=True)
        thread.start()
        def call(path, data=None, authorized=True):
            headers={'Content-Type':'application/json'}
            if authorized: headers['Authorization']='Bearer '+http.token
            req=urllib.request.Request('http://127.0.0.1:'+str(http.server_port)+path, data=None if data is None else json.dumps(data).encode(),headers=headers)
            try:
                with urllib.request.urlopen(req,timeout=3) as r:return r.status,json.load(r)
            except urllib.error.HTTPError as e:return e.code,json.load(e)
        try:
            self.assertEqual(call('/v1/accounts',authorized=False)[0],401)
            code,result=call('/v1/accounts',{'provider':'codex','name':'OpenAI'})
            self.assertEqual(code,201)
            self.assertEqual(call('/v1/accounts/'+result['id']+'/status')[1]['state'],'idle')
            self.assertEqual(call('/v1/accounts/'+'0'*32+'/login',{})[0],404)
            self.assertEqual(call('/v1/accounts',{'provider':'codex','name':'\ninvalid'})[0],400)
            self.assertFalse(self.children)
        finally:
            http.shutdown();http.server_close();thread.join();server.ACCOUNT_SETUP=previous


if __name__=='__main__':
    unittest.main()
