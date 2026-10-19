"""Firmware ADPCM encoder interoperates with the official Android upload codec."""
import base64
import ctypes
import importlib.util
import json
import math
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('official_muse_audio', Path(__file__).with_name('android_audio_codec.py'))
android = importlib.util.module_from_spec(spec)
spec.loader.exec_module(android)


class CodecState(ctypes.Structure):
    _fields_ = [('pred', ctypes.c_int16), ('index', ctypes.c_int8)]


class AudioInteropTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        library = str(Path(cls.directory.name) / 'codec.so')
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                        str(ROOT / 'main/muse/muse_adpcm.c'), '-o', library], check=True)
        cls.codec = ctypes.CDLL(library)
        cls.codec.muse_adpcm_encode_block.argtypes = [ctypes.POINTER(CodecState), ctypes.POINTER(ctypes.c_int16), ctypes.c_size_t, ctypes.POINTER(ctypes.c_uint8)]
        cls.codec.muse_adpcm_decode_block.argtypes = [ctypes.POINTER(CodecState), ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t, ctypes.POINTER(ctypes.c_int16)]

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_firmware_voice_blocks_preserve_android_wav_duration_and_pcm(self):
        encoder, decoder = CodecState(), CodecState()
        upload = android.AudioUpload()
        expected, encoded = bytearray(), []
        for sequence in range(50):
            samples = [int(14000 * math.sin(2 * math.pi * 440 * (sequence * 320 + i) / 16000)) for i in range(320)]
            pcm = (ctypes.c_int16 * 320)(*samples)
            block, decoded = (ctypes.c_uint8 * 160)(), (ctypes.c_int16 * 320)()
            header = struct.pack('<hBHI', encoder.pred, encoder.index, 320, sequence)
            self.codec.muse_adpcm_encode_block(ctypes.byref(encoder), pcm, 320, block)
            self.codec.muse_adpcm_decode_block(ctypes.byref(decoder), block, 320, decoded)
            expected.extend(bytes(decoded))
            encoded.append(upload.feed(header + bytes(block), end=sequence == 49))
        body = json.loads(android.HEAD + b''.join(encoded))
        wav = base64.b64decode(body['items'][0]['data_base64'])
        self.assertEqual(wav[:44], android.WAV)
        self.assertEqual(wav[44:], expected)
        self.assertEqual(upload.samples, 16000)
        self.assertEqual(len(wav[44:]), 32000)
        with self.assertRaises(ValueError):
            upload.feed(b'', end=True)

    def test_android_rejects_dropped_or_reordered_firmware_blocks(self):
        for sequence, count, index in [(1, 320, 0), (0, 319, 0), (0, 320, 89)]:
            with self.subTest(sequence=sequence, count=count, index=index):
                frame = struct.pack('<hBHI', 0, index, count, sequence) + bytes(160)
                with self.assertRaises(ValueError):
                    android.AudioUpload().feed(frame)
