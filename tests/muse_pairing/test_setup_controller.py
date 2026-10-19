"""Real setup controller callbacks with virtual app/transport/storage boundaries."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
class SetupControllerTest(unittest.TestCase):
    def test_confirmation_stale_work_and_provisioning(self):
        idf=Path(os.environ.get('IDF_PATH',ROOT/'.toolchains/esp-idf-v5.5.3'))
        cjson=idf/'components/json/cJSON'
        if not (cjson/'cJSON.c').is_file():
            self.skipTest('ESP-IDF cJSON source is required for this controller test')
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'setup'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                '-I'+str(ROOT/'main/muse'),'-I'+str(cjson),
                str(ROOT/'tests/muse_pairing/setup_controller_harness.c'),
                str(ROOT/'main/muse/setup_controller.c'),str(cjson/'cJSON.c'),
                '-Wl,--wrap=free','-lm','-o',str(binary)],check=True)
            result=subprocess.run([str(binary)],check=True,text=True,capture_output=True)
            self.assertIn('PASS official Muse setup controller',result.stdout)
