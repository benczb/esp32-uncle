"""Local, key-free reply speech. Text stays on the bridge and is passed on stdin."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import threading

_LOCK = threading.Lock()
_CACHE = ('', b'')
MAX_PCM = 16000 * 2 * 360


def executable():
    configured = os.environ.get('PASSPORT_ESPEAK', '')
    bundled = Path(__file__).resolve().parent.parent / '.local/tts-package/root/usr/bin/espeak-ng'
    path = configured or shutil.which('espeak-ng') or (str(bundled) if bundled.is_file() else '')
    if not path:
        raise RuntimeError('Install espeak-ng for local reply speech')
    return path


def synthesize(reply_id, text):
    """One bounded cached reply, shared by retries; never run shell commands."""
    global _CACHE
    if not re.fullmatch(r'[0-9a-f]{32}', reply_id) or not isinstance(text, str) or not text.strip() or len(text) > 6000:
        raise ValueError('Invalid speech reply')
    with _LOCK:
        if _CACHE[0] == reply_id:
            return _CACHE[1]
        voice = 'cmn' if re.search(r'[\u3400-\u9fff]', text) else 'en-us'
        wave = subprocess.run([executable(), '--stdout', '--stdin', '-v', voice],
                              input=text.encode('utf-8'), stdout=subprocess.PIPE,
                              stderr=subprocess.DEVNULL, timeout=30, check=True).stdout
        if len(wave) > MAX_PCM * 3:
            raise ValueError('Speech too long')
        pcm = subprocess.run(['ffmpeg', '-nostdin', '-loglevel', 'error', '-i', 'pipe:0',
                              '-ar', '16000', '-ac', '1', '-f', 's16le', 'pipe:1'],
                             input=wave, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                             timeout=30, check=True).stdout
        if not pcm or len(pcm) > MAX_PCM or len(pcm) % 2:
            raise ValueError('Invalid speech output')
        _CACHE = (reply_id, pcm)
        return pcm
