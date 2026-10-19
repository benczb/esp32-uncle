"""Run the adapted upstream v5 handshake against IDF 5.5's real host crypto.

The included upstream harness uses throwaway test keys and virtual board/time
interfaces. No eFuse is read, no radio is started, and no account is contacted.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class MusePairingCryptoTest(unittest.TestCase):
    def test_community_handshake_replay_timeout_and_session_binding(self):
        idf = Path(os.environ.get('IDF_PATH', ROOT / '.toolchains/esp-idf-v5.5.3'))
        crypto = idf / 'components/mbedtls/mbedtls'
        cjson = idf / 'components/json/cJSON'
        if not crypto.joinpath('CMakeLists.txt').is_file():
            self.skipTest('ESP-IDF source is required for the real-crypto host test')
        build = ROOT / 'build/muse-host-crypto'
        def run(command):
            result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout[-6000:] + result.stderr[-6000:])
            return result.stdout
        run(['cmake', '-S', str(crypto), '-B', str(build), '-DENABLE_TESTING=OFF',
             '-DENABLE_PROGRAMS=OFF', '-DGEN_FILES=OFF', '-DMBEDTLS_FATAL_WARNINGS=OFF',
             '-DCMAKE_BUILD_TYPE=Release'])
        run(['cmake', '--build', str(build), '--target', 'mbedcrypto', '-j', '4'])
        with tempfile.TemporaryDirectory() as directory:
            binary = str(Path(directory) / 'handshake')
            run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                 '-Wno-unused-function', '-Wno-unused-variable', '-DTEST_PAIRING_EFUSE_AUTH=0',
                 '-I'+str(ROOT / 'tests/muse_pairing/pairing_fakes'),
                 '-I'+str(ROOT / 'main/muse'), '-I'+str(crypto / 'include'), '-I'+str(cjson),
                 str(ROOT / 'tests/muse_pairing/link_pairing_handshake_harness.c'),
                 str(ROOT / 'main/muse/pairing_transcript.c'),
                 str(ROOT / 'main/muse/pairing_signer_policy.c'), str(cjson / 'cJSON.c'),
                 str(build / 'library/libmbedcrypto.a'), '-lm', '-o', binary])
            self.assertIn('PASS actual pairing', run([binary]))
