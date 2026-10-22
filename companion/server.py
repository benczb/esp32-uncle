"""LAN companion. No credentials are embedded in the firmware or returned to clients."""
import argparse
import secrets
from library import Library, MAX_UPLOAD
from spotify import Spotify
from speech import synthesize
from haze import haze
from bus import bus_arrivals
from voice import VoiceSessions, VoiceError, transcribe
from accounts import AccountSetup, SetupError
import concurrent.futures
import hmac
import ipaddress
import json
import os
from pathlib import Path
import subprocess
import threading
import time
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

STATIONS = [
    ('sg-yes933', 'YES 933', 'SG', 'https://playerservices.streamtheworld.com/api/livestream-redirect/YES933_PREM.aac'),
    ('sg-love972', 'LOVE 972', 'SG', 'https://playerservices.streamtheworld.com/api/livestream-redirect/LOVE972FM_PREM.aac'),
    ('sg-capital958', 'CAPITAL 958', 'SG', 'https://playerservices.streamtheworld.com/api/livestream-redirect/CAPITAL958FM_PREM.aac'),
    ('sg-class95', 'CLASS 95', 'SG', 'https://playerservices.streamtheworld.com/api/livestream-redirect/CLASS95_PREM.aac'),
    ('sg-gold905', 'GOLD 905', 'SG', 'https://playerservices.streamtheworld.com/api/livestream-redirect/GOLD905_PREM.aac'),
    ('sg-987', '987', 'SG', 'https://playerservices.streamtheworld.com/api/livestream-redirect/987FM_PREM.aac'),
    ('my-my', 'MY FM', 'MY', 'https://stream.rcs.revma.com/hc3unrtbv4uvv'),
    ('my-melody', 'MELODY', 'MY', 'https://stream.rcs.revma.com/2u1n6dtbv4uvv'),
    ('my-hitz', 'HITZ', 'MY', 'https://stream.rcs.revma.com/488kt4sbv4uvv'),
    ('my-era', 'ERA', 'MY', 'https://stream.rcs.revma.com/crec9cmbv4uvv'),
]
CITIES = {'singapore': (1.3521, 103.8198), 'johor-bahru': (1.4927, 103.7414),
          'kuala-lumpur': (3.139, 101.6869), 'penang': (5.4141, 100.3288)}
LOCK = threading.Lock()
JOBS = {name: {'state': 'idle', 'text': ''} for name in ('hermes', 'muse')}
RADIO_SLOTS = threading.BoundedSemaphore(2)
POOL = concurrent.futures.ThreadPoolExecutor(max_workers=2)
WEATHER = {}
VOICE = VoiceSessions()


class NoCredentialRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def request_json(url, payload=None, token='', timeout=15, headers=None):
    hdr = {'User-Agent': 'PocketPassport/1.0', **(headers or {})}
    if token:
        hdr['Authorization'] = 'Bearer ' + token
    if payload is not None:
        hdr['Content-Type'] = 'application/json'
    req = urllib.request.Request(url, data=None if payload is None else json.dumps(payload).encode(), headers=hdr)
    # AccountKey is scoped to the fixed LTA endpoint, never forwarded on redirect.
    open_request=(urllib.request.build_opener(NoCredentialRedirect()).open
                  if any(k.lower()=='accountkey' for k in hdr) else urllib.request.urlopen)
    with open_request(req, timeout=timeout) as response:
        data = response.read(1024 * 1024 + 1)
        if len(data) > 1024 * 1024:
            raise ValueError('Upstream response too large')
        return json.loads(data)


from usage import summary as athena_summary


def weather(city):
    if city not in CITIES:
        raise ValueError('Unsupported city')
    cached = WEATHER.get(city)
    if cached and time.time() - cached['fetched_at'] < 600:
        return cached
    lat, lon = CITIES[city]
    query = urllib.parse.urlencode({'latitude': lat, 'longitude': lon,
        'current': 'temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m',
        'timezone': 'Asia/Singapore'})
    try:
        data = request_json('https://api.open-meteo.com/v1/forecast?' + query)
        result = {'city': city, 'current': data['current'], 'fetched_at': time.time(), 'stale': False}
        WEATHER[city] = result
        return result
    except Exception:
        if cached:
            return {**cached, 'stale': True}
        raise


def agent_job(agent, text):
    try:
        if agent == 'muse':
            with LOCK:
                JOBS[agent] = {'state': 'error', 'text': 'Muse uses Muse Passport and the official Muse app. Bluetooth integration into this superapp is pending; the local model route is disabled.'}
            return
        prefix = agent.upper()
        base = os.environ.get(prefix + '_URL', '')
        if not base:
            raise ValueError(agent + ' endpoint is not configured')
        model = os.environ.get(prefix + '_MODEL', 'hermes-agent')
        result = request_json(base.rstrip('/') + '/chat/completions',
            {'model': model, 'messages': [{'role': 'user', 'content': text}], 'stream': False,
             'max_tokens': 700}, os.environ.get(prefix + '_TOKEN', ''), timeout=180,
             headers={'X-Hermes-Session-Id': 'pocket-passport'} if agent == 'hermes' else {})
        answer = result['choices'][0]['message']['content']
        if not isinstance(answer, str):
            raise ValueError('Unsupported reply format')
        outcome = {'state': 'done', 'text': answer[:6000], 'speech_id': secrets.token_hex(16)}
    except Exception as exc:
        # Never include upstream bodies/URLs that might contain credentials.
        outcome = {'state': 'error', 'text': 'Connection failed: ' + type(exc).__name__}
    with LOCK:
        JOBS[agent] = outcome


def voice_job(pcm):
    try:
        text = transcribe(pcm)
    except Exception:
        with LOCK:
            JOBS['hermes'] = {'state': 'error', 'text': 'Speech not recognised. Check local voice setup and try again.'}
        return
    agent_job('hermes', text)


ACCOUNT_SETUP = None

def account_setup():
    global ACCOUNT_SETUP
    with LOCK:
        if ACCOUNT_SETUP is None:
            def tracker(path, body=None):
                return request_json(os.environ.get('ATHENA_URL', 'http://127.0.0.1:4177').rstrip('/') + path, body)
            try:
                ACCOUNT_SETUP = AccountSetup(tracker)
            except Exception:
                raise SetupError(503, 'Local usage setup needs repair') from None
        return ACCOUNT_SETUP


FEATURE_LOCK = threading.Lock()
FEATURES = None

def features():
    global FEATURES
    with FEATURE_LOCK:
        if FEATURES is None:
            root = Path(os.environ.get('PASSPORT_DATA_DIR', str(Path.home()/'.local/share/pocket-passport')))
            FEATURES = (Library(root/'books'), Spotify(root))
        return FEATURES


class Handler(BaseHTTPRequestHandler):
    server_version = 'PocketPassport/1.0'

    def setup(self):
        super().setup()
        self.connection.settimeout(15)

    def log_message(self, fmt, *args):
        pass  # Paths/headers may contain private data.

    def relay_origin(self):
        if not getattr(self.server, 'phone_relay', False):
            return None
        origin = self.headers.get('Origin', '')
        try:
            parsed = urllib.parse.urlsplit(origin)
            ip = ipaddress.ip_address(parsed.hostname)
            allowed = any(ip in ipaddress.ip_network(net) for net in
                          ('10.0.0.0/8', '172.16.0.0/12', '192.168.0.0/16'))
            if parsed.scheme == 'http' and allowed and not parsed.username and not parsed.path and not parsed.query and not parsed.fragment:
                return origin
        except ValueError:
            pass
        return None

    def end_headers(self):
        origin = self.relay_origin()
        if origin:
            self.send_header('Access-Control-Allow-Origin', origin)
            self.send_header('Vary', 'Origin')
        super().end_headers()

    def do_OPTIONS(self):
        if not self.relay_origin():
            return self.send_json({'error': 'Origin not allowed'}, 403)
        self.send_response(204)
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Authorization, Content-Type')
        self.send_header('Access-Control-Allow-Private-Network', 'true')
        self.send_header('Access-Control-Max-Age', '300')
        self.send_header('Content-Length', '0')
        self.end_headers()

    def send_json(self, data, status=200):
        body = json.dumps(data, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def authorized(self):
        supplied = self.headers.get('Authorization', '')
        return hmac.compare_digest(supplied, 'Bearer ' + self.server.token)

    def do_GET(self):
        public_path = urllib.parse.urlsplit(self.path)
        if public_path.path == '/tools':
            body = Path(__file__).with_name('tools.html').read_bytes()
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers(); self.wfile.write(body); return
        if public_path.path == '/spotify/callback':
            query = urllib.parse.parse_qs(public_path.query)
            try:
                features()[1].callback(query.get('state', [''])[0], query.get('code', [''])[0])
                return self.send_json({'status': 'Spotify connected. Return to /tools or open Spotify on Passport.'})
            except Exception:
                return self.send_json({'error': 'Spotify sign-in failed. Start again at /tools.'}, 400)
        if not self.authorized():
            return self.send_json({'error': 'Unauthorized'}, 401)
        url = urllib.parse.urlsplit(self.path)
        query = urllib.parse.parse_qs(url.query)
        try:
            if url.path == '/v1/setup':
                return self.send_json({'spotify': features()[1].setup()})
            if url.path == '/v1/spotify':
                return self.send_json(features()[1].status())
            if url.path == '/v1/library':
                return self.send_json({'books': features()[0].books()})
            if url.path == '/v1/reader':
                return self.send_json(features()[0].snapshot())
            if url.path == '/v1/accounts':
                return self.send_json(account_setup().list())
            if url.path.startswith('/v1/accounts/'):
                parts = url.path.split('/')
                if len(parts) == 5 and parts[4] == 'status':
                    return self.send_json(account_setup().status(parts[3]))
                return self.send_json({'error': 'Not found'}, 404)
            if url.path == '/v1/status':
                return self.send_json({'time': int(time.time()), 'stations': len(STATIONS),
                    'hermes_configured': bool(os.environ.get('HERMES_URL')),
                    'muse_configured': False, 'muse_integration': 'muse-passport-ble-pending'})
            if url.path in ('/v1/athena', '/v1/usage'):
                return self.send_json({'usage': athena_summary(request_json(
                    os.environ.get('ATHENA_URL', 'http://127.0.0.1:4177') + '/api/usage')),
                    'fetched_at': int(time.time())})
            if url.path == '/v1/bus':
                return self.send_json(bus_arrivals(request_json, query.get('stop', [''])[0],
                    page=int(query.get('page', ['0'])[0])))
            if url.path == '/v1/haze':
                return self.send_json(haze(request_json,
                    city=query.get('city', ['singapore'])[0],
                    region=query.get('region', ['unset'])[0],
                    latitude=query.get('lat', [None])[0], longitude=query.get('lon', [None])[0]))
            if url.path == '/v1/weather':
                return self.send_json(weather(query.get('city', ['singapore'])[0]))
            if url.path == '/v1/stations':
                return self.send_json({'stations': [{'id': s[0], 'name': s[1], 'country': s[2]} for s in STATIONS]})
            if url.path.startswith('/v1/reply/'):
                agent = url.path.rsplit('/', 1)[-1]
                if agent not in JOBS:
                    return self.send_json({'error': 'Unknown agent'}, 404)
                with LOCK:
                    result = dict(JOBS[agent])
                return self.send_json(result)
            if url.path == '/v1/speech/hermes':
                reply_id = query.get('id', [''])[0]
                with LOCK:
                    job = dict(JOBS['hermes'])
                if job.get('state') != 'done' or not reply_id or reply_id != job.get('speech_id'):
                    return self.send_json({'error': 'Reply expired'}, 409)
                pcm = synthesize(reply_id, job['text'])
                self.send_response(200)
                self.send_header('Content-Type', 'application/octet-stream')
                self.send_header('X-Audio-Format', 's16le; rate=16000; channels=1')
                self.send_header('Content-Length', str(len(pcm)))
                self.end_headers()
                self.wfile.write(pcm)
                return
            if url.path == '/v1/radio':
                index = int(query.get('station', ['0'])[0])
                if not 0 <= index < len(STATIONS):
                    raise ValueError('Station out of range')
                if not RADIO_SLOTS.acquire(blocking=False):
                    return self.send_json({'error': 'Radio busy'}, 503)
                try:
                    return self.stream_radio(STATIONS[index][3])
                finally:
                    RADIO_SLOTS.release()
            self.send_json({'error': 'Not found'}, 404)
        except SetupError as error:
            self.send_json({'error': str(error)}, error.status)
        except (ValueError, KeyError, TypeError):
            self.send_json({'error': 'Invalid request'}, 400)
        except (BrokenPipeError, ConnectionResetError):
            pass
        except Exception:
            self.send_json({'error': 'Upstream unavailable'}, 502)

    def do_POST(self):
        if not self.authorized():
            return self.send_json({'error': 'Unauthorized'}, 401)
        url = urllib.parse.urlsplit(self.path)
        if url.path in ('/v1/spotify/login', '/v1/spotify/control', '/v1/library/import', '/v1/reader/control'):
            try:
                length = int(self.headers.get('Content-Length', '0'))
                limit = MAX_UPLOAD if url.path == '/v1/library/import' else 256
                if not 0 < length <= limit: raise ValueError('Invalid request size')
                data = self.rfile.read(length)
                if len(data) != length: raise ValueError('Incomplete upload')
                library, spotify = features()
                if url.path == '/v1/library/import':
                    query = urllib.parse.parse_qs(url.query)
                    return self.send_json(library.add(query.get('name', [''])[0], data), 201)
                body = json.loads(data)
                if not isinstance(body, dict): raise ValueError('Invalid request')
                if url.path == '/v1/spotify/login': return self.send_json(spotify.login())
                if url.path == '/v1/spotify/control': return self.send_json(spotify.control(body.get('action')))
                if body.get('action') == 'select': return self.send_json(library.select(body.get('id')))
                return self.send_json(library.control(body.get('action')))
            except ValueError as error:
                return self.send_json({'error': str(error)[:160]}, 400)
            except Exception:
                return self.send_json({'error': 'Service unavailable; check book format or Spotify configuration'}, 502)
        if url.path == '/v1/accounts' or url.path.startswith('/v1/accounts/'):
            try:
                length = int(self.headers.get('Content-Length', '0'))
                if not 0 < length <= 512:
                    raise SetupError(400, 'Invalid setup request')
                body = json.loads(self.rfile.read(length))
                if not isinstance(body, dict):
                    raise SetupError(400, 'Invalid setup request')
                manager = account_setup()
                if url.path == '/v1/accounts':
                    return self.send_json(manager.add(body.get('provider'), body.get('name')), 201)
                parts = url.path.split('/')
                if len(parts) == 5 and parts[4] in ('login', 'cancel'):
                    return self.send_json(getattr(manager, 'start' if parts[4] == 'login' else 'cancel')(parts[3]))
                return self.send_json({'error': 'Not found'}, 404)
            except SetupError as error:
                return self.send_json({'error': str(error)}, error.status)
            except (ValueError, TypeError):
                return self.send_json({'error': 'Invalid setup request'}, 400)
        if url.path.startswith('/v1/voice/'):
            try:
                return self.voice_post(url.path, urllib.parse.parse_qs(url.query))
            except VoiceError as error:
                return self.send_json({'error': str(error)}, error.status)
            except (ValueError, KeyError, TypeError, OSError):
                return self.send_json({'error': 'Invalid voice request'}, 400)
        agent = self.path.rsplit('/', 1)[-1]
        if self.path != '/v1/chat/' + agent or agent not in JOBS:
            return self.send_json({'error': 'Not found'}, 404)
        try:
            length = int(self.headers.get('Content-Length', '0'))
            if not 0 < length <= 8192:
                raise ValueError()
            text = json.loads(self.rfile.read(length))['text']
            if not isinstance(text, str) or not text.strip() or len(text) > 2000:
                raise ValueError()
            if not os.environ.get(agent.upper() + '_URL'):
                return self.send_json({'error': 'Endpoint not configured'}, 503)
            with LOCK:
                if JOBS[agent]['state'] == 'working' or (agent == 'hermes' and VOICE.active):
                    return self.send_json({'error': 'Agent busy'}, 409)
                JOBS[agent] = {'state': 'working', 'text': ''}
            POOL.submit(agent_job, agent, text)
            self.send_json({'state': 'working'}, 202)
        except (ValueError, KeyError, TypeError):
            self.send_json({'error': 'Invalid request'}, 400)

    def voice_post(self, path, query):
        length = int(self.headers.get('Content-Length', '0'))
        if path == '/v1/voice/chunk':
            if not 0 < length <= 4096 or length % 2:
                raise VoiceError(400, 'Invalid PCM chunk')
            data = self.rfile.read(length)
            if len(data) != length:
                raise VoiceError(400, 'Incomplete PCM chunk')
            result = VOICE.chunk(query.get('session', [''])[0],
                                 int(query.get('seq', ['-1'])[0]), data)
            return self.send_json(result)
        if path not in ('/v1/voice/start', '/v1/voice/finish', '/v1/voice/cancel'):
            raise VoiceError(404, 'Unknown voice operation')
        if not 0 < length <= 512:
            raise VoiceError(400, 'Invalid voice request')
        data = json.loads(self.rfile.read(length))
        if not isinstance(data, dict):
            raise VoiceError(400, 'Invalid voice request')
        if path == '/v1/voice/start':
            if data.get('agent') != 'hermes':
                raise VoiceError(503, 'Muse voice requires the official Muse connection')
            if data.get('sample_rate') != 16000 or data.get('channels') != 1 or data.get('format') != 's16le':
                raise VoiceError(400, 'Use 16 kHz mono signed little-endian PCM')
            if not os.environ.get('HERMES_URL'):
                raise VoiceError(503, 'Hermes is not configured')
            with LOCK:
                if JOBS['hermes']['state'] == 'working':
                    raise VoiceError(409, 'Hermes is busy')
                result = VOICE.start()
            return self.send_json(result)
        if path == '/v1/voice/cancel':
            VOICE.cancel(data.get('session'))
            return self.send_json({'ok': True})
        with LOCK:
            pcm = VOICE.finish(data.get('session'))
            JOBS['hermes'] = {'state': 'working', 'text': ''}
        POOL.submit(voice_job, pcm)
        return self.send_json({'state': 'working'}, 202)

    def stream_radio(self, source):
        # Decode on the host, delivering bounded 16kHz mono PCM to the no-PSRAM board.
        proc = subprocess.Popen(['ffmpeg', '-nostdin', '-loglevel', 'error', '-rw_timeout', '10000000',
            '-i', source, '-vn', '-ar', '16000', '-ac', '1', '-f', 's16le', 'pipe:1'],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        try:
            first = proc.stdout.read(2048)
            if not first:
                return self.send_json({'error': 'Station unavailable'}, 502)
            self.send_response(200)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('X-Audio-Format', 's16le; rate=16000; channels=1')
            self.send_header('Connection', 'close')
            self.end_headers()
            self.wfile.write(first)
            while chunk := proc.stdout.read(2048):
                self.wfile.write(chunk)
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
            proc.stdout.close()
            self.close_connection = True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--bind', action='append')
    parser.add_argument('--phone-relay', action='store_true')
    parser.add_argument('--port', type=int, default=4188)
    args = parser.parse_args()
    token = os.environ.get('PASSPORT_BRIDGE_TOKEN', '')
    if len(token) < 16:
        parser.error('Set PASSPORT_BRIDGE_TOKEN to a random token of at least 16 characters')
    servers = []
    try:
        for bind in args.bind or ['127.0.0.1']:
            instance = ThreadingHTTPServer((bind, args.port), Handler)
            instance.daemon_threads = True
            instance.token = token
            instance.phone_relay = args.phone_relay
            servers.append(instance)
        for instance in servers[1:]:
            threading.Thread(target=instance.serve_forever, daemon=True).start()
        print('Passport bridge listening on ' + ', '.join(str(s.server_address) for s in servers), flush=True)
        servers[0].serve_forever()
    finally:
        for instance in servers:
            instance.server_close()

if __name__ == '__main__':
    main()
