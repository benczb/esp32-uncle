"""Bounded, in-memory microphone uploads and optional local speech recognition."""
import hashlib
import hmac
import os
import secrets
import threading
import time

RATE = 16000
MAX_BYTES = RATE * 2 * 30
MAX_CHUNK = 4096


class VoiceError(Exception):
    def __init__(self, status, message):
        super().__init__(message)
        self.status = status


class VoiceSessions:
    def __init__(self, clock=time.monotonic):
        self.clock = clock
        self.lock = threading.Lock()
        self.session = ''
        self.audio = bytearray()
        self.started = 0
        self.sequence = 0
        self.last_digest = b''

    def _clear(self):
        self.audio.clear()
        self.session = ''
        self.sequence = 0
        self.last_digest = b''

    def _expire(self):
        if self.session and self.clock() - self.started > 35:
            self._clear()

    @property
    def active(self):
        with self.lock:
            self._expire()
            return bool(self.session)

    def start(self):
        with self.lock:
            self._expire()
            if self.session:
                raise VoiceError(409, 'Another recording is active')
            self.session = secrets.token_hex(16)
            self.started = self.clock()
            return {'session': self.session, 'max_seconds': 30,
                    'max_chunk': MAX_CHUNK, 'sample_rate': RATE}

    def _require(self, session):
        self._expire()
        if not isinstance(session, str) or not self.session or not hmac.compare_digest(session, self.session):
            raise VoiceError(404, 'Recording not found or expired')

    def chunk(self, session, sequence, pcm):
        with self.lock:
            self._require(session)
            if type(sequence) is not int or sequence < 0 or not pcm or len(pcm) > MAX_CHUNK or len(pcm) % 2:
                raise VoiceError(400, 'Invalid PCM chunk')
            digest = hashlib.sha256(pcm).digest()
            if sequence == self.sequence - 1 and hmac.compare_digest(digest, self.last_digest):
                return {'next_sequence': self.sequence, 'bytes': len(self.audio)}
            if sequence != self.sequence:
                raise VoiceError(409, 'PCM sequence mismatch')
            if len(self.audio) + len(pcm) > MAX_BYTES:
                self._clear()
                raise VoiceError(413, 'Recording exceeds thirty seconds')
            self.audio.extend(pcm)
            self.sequence += 1
            self.last_digest = digest
            return {'next_sequence': self.sequence, 'bytes': len(self.audio)}

    def finish(self, session):
        with self.lock:
            self._require(session)
            pcm = bytes(self.audio)
            self._clear()
            if len(pcm) < RATE * 2 // 5:
                raise VoiceError(400, 'Recording is too short')
            return pcm

    def cancel(self, session):
        with self.lock:
            self._require(session)
            self._clear()


_MODEL = None
_MODEL_LOCK = threading.Lock()


def transcribe(pcm):
    """Use an installed/cached model; never download or persist microphone audio."""
    global _MODEL
    import numpy as np
    from faster_whisper import WhisperModel
    with _MODEL_LOCK:
        if _MODEL is None:
            _MODEL = WhisperModel(os.environ.get('PASSPORT_STT_MODEL', 'small.en'),
                                  device='cpu', compute_type='int8',
                                  local_files_only=True, cpu_threads=4)
        language = os.environ.get('PASSPORT_STT_LANGUAGE', 'en')
        samples = np.frombuffer(pcm, dtype='<i2').astype(np.float32) / 32768.0
        segments, _ = _MODEL.transcribe(samples, language=None if language == 'auto' else language,
                                        beam_size=1, vad_filter=True)
        text = ' '.join(segment.text.strip() for segment in segments).strip()
    if not text:
        raise ValueError('No speech detected')
    if len(text) > 2000:
        raise ValueError('Speech message is too long')
    return text
