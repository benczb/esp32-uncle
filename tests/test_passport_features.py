from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'companion'))
import io
import json
import os
import tempfile
import threading
import time
import unittest
from unittest.mock import patch
import urllib.request
import urllib.error
import urllib.parse
import zipfile
from library import Library, Text, convert
from spotify import Spotify
import server


def epub():
    result = io.BytesIO()
    with zipfile.ZipFile(result, 'w') as z:
        z.writestr('META-INF/container.xml', '<container><rootfiles><rootfile full-path="OPS/book.opf"/></rootfiles></container>')
        z.writestr('OPS/book.opf', '<package xmlns="http://www.idpf.org/2007/opf"><manifest><item id="a" href="one.xhtml"/><item id="b" href="two.xhtml"/></manifest><spine><itemref idref="b"/><itemref idref="a"/></spine></package>')
        z.writestr('OPS/one.xhtml', '<p>First file</p><script>hidden secret</script>')
        z.writestr('OPS/two.xhtml', '<h1>Second file</h1><p>A &amp; B</p>')
    return result.getvalue()


class Features(unittest.TestCase):
    def test_epub_spine_order_and_no_script(self):
        text = convert('test.epub', epub())
        self.assertLess(text.index('Second'), text.index('First'))
        self.assertIn('A & B', text); self.assertNotIn('hidden', text)
        with self.assertRaises(ValueError): convert('test.pdf', b'abc')
        with self.assertRaises(ValueError): convert('test.txt', b'\xff')
        with self.assertRaises(ValueError): convert('test.txt', b'x'*(4194305))

    def test_reading_survives_restart_and_font_change(self):
        with tempfile.TemporaryDirectory() as root:
            lib = Library(root); self.assertEqual(lib.snapshot()['state'], 'empty')
            lib.add('one.txt', ('Hello world. '*100).encode())
            lib.add('two.epub', epub()); view = lib.control('open')
            self.assertEqual(view['state'], 'reading')
            for _ in range(3): view = lib.control('next')
            self.assertEqual(view['page'], 3)
            reloaded = Library(root); self.assertEqual(reloaded.control('open')['page'], 3)
            view = reloaded.control('size'); self.assertTrue(view['large'])
            self.assertTrue(view['text']); self.assertLessEqual(len(view['text'].splitlines()), 6)
            before = view['id']; reloaded.control('shelf'); self.assertNotEqual(reloaded.control('next')['id'], before)
            for p in Path(root).glob('*.json'): self.assertEqual(p.stat().st_mode & 0o777, 0o600)
            with self.assertRaises(ValueError): reloaded.control('unknown')

    def test_pkce_state_expiry_and_single_use(self):
        with tempfile.TemporaryDirectory() as root, patch.dict(os.environ, {'SPOTIFY_CLIENT_ID': 'test-client'}):
            spotify = Spotify(root); login = spotify.login(); query = urllib.parse.parse_qs(urllib.parse.urlsplit(login['url']).query)
            self.assertEqual(query['code_challenge_method'], ['S256']); self.assertNotIn('code_verifier', query)
            with self.assertRaises(ValueError): spotify.callback('wrong', 'code')
            with patch.object(spotify, 'exchange') as exchange:
                self.assertEqual(spotify.callback(query['state'][0], 'code'), 'en'); exchange.assert_called_once()
            with self.assertRaises(ValueError): spotify.callback(query['state'][0], 'code')
            spotify.login('zh-CN')
            with patch.object(spotify, 'exchange'):
                self.assertEqual(spotify.callback(spotify.pending[0], 'code'), 'zh-CN')
            spotify.login(); spotify.pending = (*spotify.pending[:2], 0)
            with self.assertRaises(ValueError): spotify.callback(spotify.pending[0], 'code')

    def test_phone_selects_exact_book_and_resumes_position(self):
        with tempfile.TemporaryDirectory() as root:
            lib = Library(root)
            first = lib.add('A.txt', b'First book. ' * 100)
            second = lib.add('B.txt', b'Second book. ' * 100)
            self.assertEqual(lib.select(second['id'])['id'], second['id'])
            lib.control('next'); lib.control('next')
            lib.select(first['id'])
            view = lib.select(second['id'])
            self.assertEqual(view['page'], 2)
            self.assertEqual(view['state'], 'reading')
            self.assertEqual(lib.select(second['id'])['state'], 'reading')
            for invalid in ('../../outside', '', None, ['bad']):
                with self.assertRaises(ValueError): lib.select(invalid)
            self.assertEqual(Library(root).select(second['id'])['page'], 2)

    def test_epub_repeated_spine_is_bounded(self):
        repeated = io.BytesIO()
        with zipfile.ZipFile(io.BytesIO(epub())) as original, zipfile.ZipFile(repeated, 'w') as output:
            for name in original.namelist():
                data = original.read(name)
                if name.endswith('.opf'):
                    data = data.replace(b'<itemref idref="b"/>', b'<itemref idref="b"/>' * 20)
                output.writestr(name, data)
        with patch('library.MAX_TEXT', 100), patch('library.Text', wraps=Text) as parser:
            with self.assertRaisesRegex(ValueError, 'Book text too large'):
                convert('book.epub', repeated.getvalue())
            self.assertLess(parser.call_count, 20)  # Stop parsing before allocation grows further.

    def test_spotify_setup_checks_callback_without_exposing_tokens(self):
        with tempfile.TemporaryDirectory() as root, patch.dict(os.environ, {'SPOTIFY_CLIENT_ID': 'test-client'}):
            spotify = Spotify(root)
            spotify.tokens = {'refresh_token': 'private-fixture'}
            for uri, valid, computer in (
                    ('http://127.0.0.1:4188/spotify/callback', True, True),
                    ('http://[::1]:4188/spotify/callback', True, True),
                    ('https://companion.example/spotify/callback', True, False),
                    ('http://192.168.1.2:4188/spotify/callback', False, False),
                    ('http://localhost:4188/spotify/callback', False, False),
                    ('https://user:pass@companion.example/callback', False, False)):
                spotify.redirect = uri
                setup = spotify.setup()
                self.assertEqual(setup['callback_valid'], valid)
                self.assertEqual(setup['computer_sign_in'], computer)
                self.assertNotIn('private-fixture', json.dumps(setup))
                if not valid:
                    with self.assertRaises(ValueError): spotify.login()

    def test_spotify_controls_and_cache(self):
        with tempfile.TemporaryDirectory() as root:
            spotify = Spotify(root)
            with patch.object(spotify, 'api', return_value={'is_playing': True, 'item': {'name': 'Track', 'artists': [{'name':'Artist'}]}}) as api:
                view = spotify.status(); self.assertEqual(view['state'], 'playing'); spotify.status(); self.assertEqual(api.call_count,1)
                spotify.control('toggle'); self.assertIn(unittest.mock.call('/pause','PUT'),api.call_args_list)
            with self.assertRaises(ValueError): spotify.control('hack')
            spotify.due = time.monotonic()+60
            with patch.object(spotify, 'api') as api:
                with self.assertRaises(ValueError): spotify.control('next')
                api.assert_not_called()

    def test_http_auth_import_and_control(self):
        with tempfile.TemporaryDirectory() as root:
            library = Library(root); spotify = Spotify(root)
            http = server.ThreadingHTTPServer(('127.0.0.1',0), server.Handler); http.token = 'test-token'; http.phone_relay = False
            thread = threading.Thread(target=http.serve_forever,daemon=True);thread.start()
            base = 'http://127.0.0.1:'+str(http.server_port)
            def request(path, body=None, auth=True):
                req=urllib.request.Request(base+path, data=body, headers={'Authorization':'Bearer test-token'} if auth else {})
                with urllib.request.urlopen(req) as response: return json.load(response)
            try:
                with patch.object(server,'features',return_value=(library,spotify)):
                    for path in ['/v1/library','/v1/reader','/v1/spotify','/v1/setup']:
                        with self.assertRaises(urllib.error.HTTPError) as failure: request(path, auth=False)
                        self.assertEqual(failure.exception.code,401)
                    added = request('/v1/library/import?name=book.txt', b'Hello from a real HTTP upload')
                    self.assertIn('spotify', request('/v1/setup'))
                    self.assertEqual(len(request('/v1/library')['books']),1)
                    self.assertEqual(request('/v1/reader/control',b'{"action":"open"}')['state'],'reading')
                    self.assertIn('Hello',request('/v1/reader')['text'])
                    self.assertEqual(request('/v1/reader/control', json.dumps({'action':'select','id':added['id']}).encode())['id'], added['id'])
                    with self.assertRaises(urllib.error.HTTPError) as failure:
                        request('/v1/reader/control', b'{"action":"select","id":"../../outside"}')
                    self.assertEqual(failure.exception.code,400)
                    with self.assertRaises(urllib.error.HTTPError) as failure: request('/v1/reader/control',b'{"action":"invalid"}')
                    self.assertEqual(failure.exception.code,400)
                    with urllib.request.urlopen(base+'/spotify/callback?state=zh.wrong&code=code') as response:
                        self.assertIn('lang=zh-CN&spotify=failed', response.url)
                    with patch.object(spotify, 'callback', return_value='zh-CN'):
                        with urllib.request.urlopen(base+'/spotify/callback?state=test&code=code') as response:
                            self.assertIn('lang=zh-CN&spotify=connected', response.url)
            finally: http.shutdown();http.server_close();thread.join()


if __name__ == '__main__': unittest.main()
