"""Start a private local bridge; load only the Hermes API key from its .env file."""
import argparse
import os
from pathlib import Path
import secrets
import sys
import server

p = argparse.ArgumentParser()
p.add_argument('--bind', action='append')
p.add_argument('--phone-relay', action='store_true')
p.add_argument('--port', type=int, default=4188)
p.add_argument('--hermes-env', type=Path)
p.add_argument('--hermes-url', default='http://127.0.0.1:8642/v1')
p.add_argument('--muse-url', help=argparse.SUPPRESS) # Accepted for old launch commands; no longer used.
a = p.parse_args()
runtime = Path(__file__).resolve().parent / '.local'
runtime.mkdir(mode=0o700, exist_ok=True)
usage_file = runtime / 'usage-url'
if not os.environ.get('ATHENA_URL') and usage_file.is_file():
    usage_url = usage_file.read_text().strip()
    if usage_url:
        os.environ['ATHENA_URL'] = usage_url
token_file = runtime / 'bridge-token'
if not token_file.exists():
    with token_file.open('x') as f:
        os.chmod(token_file, 0o600)
        f.write(secrets.token_urlsafe(32))
os.environ['PASSPORT_BRIDGE_TOKEN'] = token_file.read_text().strip()
if a.hermes_env:
    for line in a.hermes_env.read_text().splitlines():
        if line.startswith('API_SERVER_KEY='):
            os.environ['HERMES_TOKEN'] = line.split('=', 1)[1].strip().strip('\'"')
            break
os.environ['HERMES_URL'] = a.hermes_url
print('Bridge token is stored privately in ' + str(token_file), flush=True)
sys.argv = [sys.argv[0], '--port', str(a.port)]
for bind in a.bind or ['127.0.0.1']:
    sys.argv.extend(['--bind', bind])
if a.phone_relay:
    sys.argv.append('--phone-relay')
server.main()
