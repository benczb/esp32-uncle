"""Spotify PKCE login and remote playback. Credentials remain on the companion."""
import base64
import hashlib
import json
import ipaddress
import os
from pathlib import Path
import secrets
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from library import atomic_json


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


class Spotify:
    def __init__(self, root):
        self.root = Path(root); self.root.mkdir(parents=True, exist_ok=True, mode=0o700)
        self.lock = threading.RLock(); self.pending = None; self.cached = None; self.due = 0
        self.client = os.environ.get('SPOTIFY_CLIENT_ID', '')
        self.redirect = os.environ.get('SPOTIFY_REDIRECT_URI', 'http://127.0.0.1:4188/spotify/callback')
        try: self.tokens = json.loads((self.root/'spotify.json').read_text())
        except (OSError, ValueError): self.tokens = {}

    def setup(self):
        url = urllib.parse.urlsplit(self.redirect)
        try: loopback = ipaddress.ip_address(url.hostname or '').is_loopback
        except ValueError: loopback = False
        valid = bool(url.hostname and not url.username and not url.password and not url.fragment
                     and (url.scheme == 'https' or (url.scheme == 'http' and loopback)))
        return {'configured': bool(self.client), 'callback_valid': valid,
                'computer_sign_in': loopback, 'connected': bool(self.tokens.get('refresh_token'))}

    def login(self):
        if not self.client: raise ValueError('Set SPOTIFY_CLIENT_ID on the companion')
        if not self.setup()['callback_valid']:
            raise ValueError('Spotify callback must use HTTPS, or a loopback IP on the companion computer')
        with self.lock:
            state = secrets.token_urlsafe(32); verifier = secrets.token_urlsafe(64)
            self.pending = (state, verifier, time.monotonic()+600)
        challenge = base64.urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest()).rstrip(b'=').decode()
        query = urllib.parse.urlencode({'client_id': self.client, 'response_type': 'code',
            'redirect_uri': self.redirect, 'scope': 'user-read-playback-state user-modify-playback-state',
            'state': state, 'code_challenge_method': 'S256', 'code_challenge': challenge})
        return {'url': 'https://accounts.spotify.com/authorize?' + query}

    def exchange(self, values):
        body = urllib.parse.urlencode({'client_id': self.client, **values}).encode()
        request = urllib.request.Request('https://accounts.spotify.com/api/token', data=body,
                    headers={'Content-Type': 'application/x-www-form-urlencoded'})
        with urllib.request.build_opener(NoRedirect()).open(request, timeout=10) as response:
            data = json.loads(response.read(32768))
        if not isinstance(data.get('access_token'), str): raise ValueError('Spotify sign-in failed')
        self.tokens = {'access_token': data['access_token'],
            'refresh_token': data.get('refresh_token', self.tokens.get('refresh_token', '')),
            'expires': time.time()+int(data.get('expires_in', 3600))-60}
        atomic_json(self.root/'spotify.json', self.tokens)

    def callback(self, state, code):
        with self.lock:
            pending = self.pending
            if not pending or pending[2] < time.monotonic() or not secrets.compare_digest(state, pending[0]) or not code:
                raise ValueError('Spotify login expired or invalid; start again')
            self.pending = None
            self.exchange({'grant_type': 'authorization_code', 'code': code,
                           'redirect_uri': self.redirect, 'code_verifier': pending[1]})
            self.due = 0

    def api(self, path, method='GET', retried=False):
        if self.tokens.get('expires', 0) <= time.time():
            if not self.tokens.get('refresh_token'): raise ValueError('Sign in to Spotify at /tools')
            self.exchange({'grant_type': 'refresh_token', 'refresh_token': self.tokens['refresh_token']})
        request = urllib.request.Request('https://api.spotify.com/v1/me/player'+path,
            method=method, headers={'Authorization': 'Bearer '+self.tokens['access_token']})
        try:
            with urllib.request.build_opener(NoRedirect()).open(request, timeout=8) as response:
                data = response.read(65536)
                return json.loads(data) if data else {}
        except urllib.error.HTTPError as error:
            if error.code == 401 and not retried:
                self.tokens['expires'] = 0
                return self.api(path, method, True)
            if error.code == 429:
                self.due = time.monotonic()+min(3600, max(5, int(error.headers.get('Retry-After', '30'))))
                raise ValueError('Spotify rate limited; retry later') from None
            message = {401: 'Spotify login expired; sign in again', 403: 'Spotify permission or Premium required',
                       404: 'Open Spotify and start playback on your phone'}
            raise ValueError(message.get(error.code, 'Spotify unavailable')) from None

    def status(self):
        with self.lock:
            if time.monotonic() < self.due:
                return self.cached or {'state': 'error', 'title': 'Spotify rate limited; retry later'}
            try:
                data = self.api(''); item = data.get('item') or {}; device = data.get('device') or {}
                result = {'state': 'playing' if data.get('is_playing') else 'paused' if item else 'idle',
                    'title': item.get('name', 'Start playback in Spotify')[:120],
                    'artist': ', '.join(a.get('name', '') for a in item.get('artists', []))[:120],
                    'device': device.get('name', '')[:80], 'progress_ms': data.get('progress_ms') or 0,
                    'duration_ms': item.get('duration_ms') or 0}
            except ValueError as error:
                result = {'state': 'error', 'title': str(error)[:120]}
            except (OSError, KeyError, TypeError):
                result = {'state': 'error', 'title': 'Spotify unavailable; check companion connection'}
            self.cached = result; self.due = max(self.due, time.monotonic()+5)
            return result

    def control(self, action):
        if action not in ('previous', 'next', 'toggle'): raise ValueError('Unknown Spotify action')
        with self.lock:
            if self.due > time.monotonic()+6: raise ValueError('Spotify rate limited; retry later')
            if action == 'toggle':
                state = self.api(''); self.api('/pause' if state.get('is_playing') else '/play', 'PUT')
            else: self.api('/'+action, 'POST')
            self.due = 0
            return self.status()
