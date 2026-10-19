"""Owner-local usage account setup. Official CLIs own OAuth and their credentials."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import threading
import time
import urllib.parse
import uuid


class SetupError(Exception):
    def __init__(self, status, message):
        super().__init__(message)
        self.status = status


def provider_url(value, provider):
    try:
        u = urllib.parse.urlsplit(value)
        hosts = {'codex': {'auth.openai.com', 'chatgpt.com'},
                 'claude': {'claude.ai', 'console.anthropic.com', 'platform.claude.com'}}
        query = {key.lower() for key in urllib.parse.parse_qs(u.query)}
        secret_fields = {'access_token','refresh_token','id_token','token','code'}
        return (not any(ord(c) < 32 for c in value) and not u.fragment and not query.intersection(secret_fields) and u.scheme == 'https' and u.hostname in hosts[provider] and
                not u.username and not u.password and u.port in (None, 443) and len(value) <= 8192)
    except (ValueError, KeyError):
        return False


class AccountSetup:
    def __init__(self, request, root=None, popen=subprocess.Popen, which=shutil.which):
        self.request = request
        self.root = Path(root or Path(__file__).parent / '.local/usage-accounts').resolve()
        self.popen, self.which = popen, which
        self.lock = threading.RLock()
        self.records, self.sessions = {}, {}
        registry = self.root / 'accounts.json'
        if registry.is_file():
            records = json.loads(registry.read_text())
            if not isinstance(records, dict):
                raise ValueError('Invalid local usage account registry')
            for key, value in records.items():
                if (not re.fullmatch(r'[0-9a-f]{32}', key) or not isinstance(value, dict) or
                    value.get('provider') not in ('codex', 'claude') or
                    not isinstance(value.get('name'), str) or not 1 <= len(value['name']) <= 48 or
                    not re.fullmatch(r'[a-zA-Z0-9_-]{1,80}', str(value.get('tracker_id', '')))):
                    raise ValueError('Invalid local usage account registry')
            self.records = records

    def _save(self):
        self.root.mkdir(parents=True, exist_ok=True, mode=0o700)
        os.chmod(self.root, 0o700)
        tmp = self.root / 'accounts.tmp'
        fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, 'w') as f:
            json.dump(self.records, f)
        os.replace(tmp, self.root / 'accounts.json')

    def _account(self, key):
        if not isinstance(key, str) or not re.fullmatch(r'[0-9a-f]{32}', key) or key not in self.records:
            raise SetupError(404, 'Account not found')
        return self.records[key]

    def list(self):
        with self.lock:
            return {'accounts': [{'id': key, 'provider': value['provider'], 'name': value['name'],
                                  **self.status(key)} for key, value in self.records.items()],
                    'available': {'codex': bool(self.which('codex')), 'claude': bool(self.which('claude'))}}

    def add(self, provider, name):
        if provider not in ('codex', 'claude') or not isinstance(name, str) or not 1 <= len(name.strip()) <= 48 or any(ord(c) < 32 for c in name):
            raise SetupError(400, 'Choose a provider and a name of 1 to 48 characters')
        with self.lock:
            if len(self.records) >= 6:
                raise SetupError(409, 'At most six setup accounts')
            key = uuid.uuid4().hex
            home = self.root / key
            home.mkdir(parents=True, mode=0o700)
            os.chmod(self.root, 0o700)
            body = {'provider': provider, 'name': name.strip(),
                    'codexHome' if provider == 'codex' else 'claudeHome': str(home)}
            try:
                result = self.request('/api/accounts', body)
                tracker_id = result['account']['id']
                if not isinstance(tracker_id, str) or not re.fullmatch(r'[a-zA-Z0-9_-]{1,80}', tracker_id):
                    raise ValueError('Invalid tracker account')
            except Exception:
                raise SetupError(503, 'Usage tracker unavailable; check the bridge setup') from None
            self.records[key] = {'provider': provider, 'name': name.strip(), 'tracker_id': tracker_id}
            self._save()
            return {'id': key, 'provider': provider, 'name': name.strip()}

    def status(self, key):
        with self.lock:
            self._account(key)
            session = self.sessions.get(key)
            if not session:
                return {'state': 'done' if self.records[key].get('connected') else 'idle', 'message': 'Account connected' if self.records[key].get('connected') else 'Sign in to connect this account'}
            fields = ('state', 'message', 'authUrl', 'userCode') if session['state'] in ('starting', 'waiting') else ('state', 'message')
            return {k: session[k] for k in fields if k in session}

    def _update(self, key, session, **values):
        with self.lock:
            if self.sessions.get(key) is session and session['state'] != 'cancelled':
                session.update(values)
                if values.get('state') == 'done':
                    self.records[key]['connected'] = True
                    self._save()

    def _rpc(self, session, method, params=None):
        session['request_id'] += 1
        message = {'id': session['request_id'], 'method': method}
        if params is not None:
            message['params'] = params
        session['process'].stdin.write(json.dumps(message) + '\n')
        session['process'].stdin.flush()
        return message['id']

    def start(self, key):
        with self.lock:
            record = self._account(key)
            old = self.sessions.get(key)
            if old and old['state'] in ('starting', 'waiting'):
                return self.status(key)
            # A bounded single login avoids opening several native callback servers.
            if any(s['state'] in ('starting', 'waiting') for s in self.sessions.values()):
                raise SetupError(409, 'Finish or cancel the other sign-in first')
            command = self.which(record['provider'])
            if not command:
                raise SetupError(503, 'Install the official provider CLI on the bridge computer')
            allowed = {'PATH','HOME','USER','USERNAME','LANG','LC_ALL','TERM','SHELL','TMPDIR','TEMP','TMP','SYSTEMROOT','APPDATA','LOCALAPPDATA','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR','DISPLAY','WAYLAND_DISPLAY','DBUS_SESSION_BUS_ADDRESS','XDG_CURRENT_DESKTOP','SSL_CERT_FILE','SSL_CERT_DIR','NODE_EXTRA_CA_CERTS','HTTP_PROXY','HTTPS_PROXY','ALL_PROXY','NO_PROXY','http_proxy','https_proxy','all_proxy','no_proxy'}
            env = {name: value for name, value in os.environ.items() if name in allowed or name.startswith('LC_')}
            for name in ('OPENAI_API_KEY', 'CODEX_API_KEY', 'CODEX_HOME', 'ANTHROPIC_API_KEY', 'CLAUDE_CODE_OAUTH_TOKEN', 'CLAUDE_CONFIG_DIR'):
                env.pop(name, None)
            env['CODEX_HOME' if record['provider'] == 'codex' else 'CLAUDE_CONFIG_DIR'] = str(self.root / key)
            args = [command, '-c', 'cli_auth_credentials_store="file"', 'app-server', '--listen', 'stdio://'] if record['provider'] == 'codex' else [command, 'auth', 'login', '--claudeai']
            session = {'state': 'starting', 'message': 'Starting official provider sign-in', 'request_id': 0}
            try:
                session['process'] = self.popen(args, env=env, stdin=subprocess.PIPE,
                                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT if record['provider'] == 'claude' else subprocess.DEVNULL,
                                                text=True, bufsize=1, start_new_session=True)
            except OSError:
                raise SetupError(503, 'Unable to start the official provider CLI') from None
            self.sessions[key] = session
            threading.Thread(target=self._run, args=(key, record['provider'], session), daemon=True).start()
            return self.status(key)

    def _run(self, key, provider, session):
        proc = session['process']
        deadline = time.monotonic() + 600
        timer = threading.Timer(600, self._expire, args=(key, session))
        timer.daemon = True
        timer.start()
        try:
            if provider == 'codex':
                self._rpc(session, 'initialize', {'clientInfo': {'name': 'pocket_passport_usage', 'title': 'Pocket Passport usage setup', 'version': '1.0'}})
            else:
                self._update(key, session, state='waiting', message='Complete Claude Code sign-in on the bridge computer')
            for line in iter(lambda: proc.stdout.readline(16385), ''):
                if len(line) > 16384 or time.monotonic() > deadline:
                    raise ValueError('Invalid provider response')
                if provider == 'claude':
                    # Only a validated provider URL may leave the native CLI; no tokens or raw logs.
                    for value in re.findall(r'https://[^\s<>"\x1b]+', line):
                        if provider_url(value, provider):
                            self._update(key, session, authUrl=value)
                    continue
                try:
                    value = json.loads(line)
                except ValueError:
                    continue
                if value.get('method') == 'account/login/completed':
                    done = value.get('params', {})
                    self._update(key, session, state='done' if done.get('success') is True else 'error',
                                 message='Account connected' if done.get('success') is True else 'Sign-in did not complete')
                    break
                if value.get('id') == 1:
                    if 'error' in value:
                        raise ValueError('Provider initialization failed')
                    proc.stdin.write('{"method":"initialized"}\n')
                    proc.stdin.flush()
                    self._rpc(session, 'account/login/start', {'type': 'chatgptDeviceCode'})
                elif value.get('id') == 2:
                    result = value.get('result', {})
                    url, code = result.get('verificationUrl', ''), result.get('userCode', '')
                    if not provider_url(url, provider) or not isinstance(code, str) or not re.fullmatch(r'[A-Za-z0-9-]{4,32}', code):
                        raise ValueError('Provider did not supply device login')
                    self._update(key, session, state='waiting', message='Open the provider page and enter the code', authUrl=url, userCode=code)
            if provider == 'claude':
                code = proc.wait(timeout=3)
                self._update(key, session, state='done' if code == 0 else 'error', message='Account connected' if code == 0 else 'Finish sign-in using Claude Code on the bridge computer')
            elif session['state'] in ('starting', 'waiting'):
                self._update(key, session, state='error', message='Sign-in ended before completion')
        except Exception:
            self._update(key, session, state='error', message='Sign-in unavailable; check the official CLI and retry')
        finally:
            timer.cancel()
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
            for stream in (proc.stdin, proc.stdout):
                if stream:
                    stream.close()
            with self.lock:
                session.pop('authUrl', None)
                session.pop('userCode', None)

    def _expire(self, key, session):
        if session['state'] not in ('starting', 'waiting'):
            return
        self._update(key, session, state='error', message='Sign-in expired; start again')
        proc = session['process']
        if proc.poll() is None:
            proc.terminate()

    def cancel(self, key):
        with self.lock:
            self._account(key)
            session = self.sessions.get(key)
            if session and session['state'] in ('starting', 'waiting'):
                session.update(state='cancelled', message='Sign-in cancelled')
                proc = session['process']
                if proc.poll() is None:
                    proc.terminate()
            return self.status(key)
