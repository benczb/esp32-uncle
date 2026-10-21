from pathlib import Path
import importlib.util
import json
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('private_defaults', ROOT/'tools/generate_private_defaults.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class PrivateDefaultsTest(unittest.TestCase):
    def test_clean_checkout_has_no_private_profiles(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            module.generate(path/'absent.json', path/'generated.h')
            self.assertIn('PASSPORT_PRIVATE_WIFI_COUNT 0', (path/'generated.h').read_text())
            self.assertIn('PASSPORT_PRIVATE_BRIDGE_TOKEN ""', (path/'generated.h').read_text())
            self.assertIn('PASSPORT_PRIVATE_USB_TEST_CONTROL 0', (path/'generated.h').read_text())
            self.assertIn('PASSPORT_PRIVATE_PSI_REGION ""', (path/'generated.h').read_text())

    def test_bridge_defaults_do_not_change_wifi_revision(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            source, header = path/'private.json', path/'generated.h'
            data = {'wifi_profiles':[{'id':'home','ssid':'test','password':'testpass'}]}
            source.write_text(json.dumps(data));module.generate(source,header)
            before=header.read_text().split('WIFI_REVISION ')[1].splitlines()[0]
            data.update(bridge_url='http://192.168.1.2:4188/',bridge_token='test-token',bridge_lan_url='http://192.168.1.3:4188')
            source.write_text(json.dumps(data));module.generate(source,header)
            after=header.read_text()
            self.assertEqual(before,after.split('WIFI_REVISION ')[1].splitlines()[0])
            self.assertIn('PASSPORT_PRIVATE_BRIDGE_TOKEN',after)
            self.assertNotIn('test-token',after)
            for key,value in [('bridge_url','http://user:secret@example.test'),('bridge_url','http://example.test:bad'),('bridge_lan_url','http://user:secret@example.test'),('bridge_token','bad\r\nheader')]:
                bad={**data,key:value};source.write_text(json.dumps(bad))
                with self.assertRaisesRegex(ValueError,'Invalid private bridge'):
                    module.generate(source,header)

    def test_private_region_is_validated_without_changing_wifi_revision(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory);source=path/'private.json';header=path/'generated.h'
            data={'wifi_profiles':[{'id':'home','ssid':'test','password':'testpass'}]}
            source.write_text(json.dumps(data));module.generate(source,header)
            before=header.read_text().split('WIFI_REVISION ')[1].splitlines()[0]
            for region in ('central','north','south','east','west',''):
                data['psi_region']=region;source.write_text(json.dumps(data));module.generate(source,header)
                self.assertEqual(before,header.read_text().split('WIFI_REVISION ')[1].splitlines()[0])
            for region in ('auto','unset','elsewhere',None,7):
                data['psi_region']=region;source.write_text(json.dumps(data))
                with self.assertRaisesRegex(ValueError,'^Invalid private PSI region$'):
                    module.generate(source,header)

    def test_profiles_preserve_literals_and_changes_update_revision(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            source, header = path/'private.json', path/'generated.h'
            profiles=[{'id':'home', 'ssid':'Home " network', 'password':'test\\pass'},
                      {'id':'outside', 'ssid':'Outside test', 'password':'otherpass'}]
            source.write_text(json.dumps({'wifi_profiles':profiles,'default_profile':'home'}))
            module.generate(source, header)
            first=header.read_text()
            self.assertEqual(header.stat().st_mode & 0o777, 0o600)
            self.assertNotIn('test\\pass', first)
            program=path/'test.c'
            program.write_text('#include "generated.h"\n#include <string.h>\n#include <assert.h>\nstruct P{const char *id,*ssid,*pass;};\nstruct P profiles[]=PASSPORT_PRIVATE_WIFI_PROFILES;\nint main(void){assert(PASSPORT_PRIVATE_WIFI_COUNT==2);assert(PASSPORT_PRIVATE_WIFI_DEFAULT==0);assert(!strcmp(profiles[0].ssid,"Home \\" network"));assert(strlen(profiles[0].pass)==9);assert(!strcmp(profiles[1].id,"outside"));}\n')
            subprocess.run(['cc','-std=c11',str(program),'-o',str(path/'test')],check=True,capture_output=True)
            subprocess.run([str(path/'test')],check=True,capture_output=True)
            profiles[1]['password']='changedtest'
            source.write_text(json.dumps({'wifi_profiles':profiles}))
            module.generate(source,header)
            self.assertNotEqual(first.split('REVISION')[1],header.read_text().split('REVISION')[1])

    def test_traffic_qr_links_are_optional_validated_and_escaped(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory);source=path/'private.json';header=path/'generated.h'
            # No local file at all: every traffic link defaults to empty.
            module.generate(path/'absent.json',header)
            text=header.read_text()
            for key in ('WECHAT','LINKEDIN','WHATSAPP'):
                self.assertIn(f'PASSPORT_PRIVATE_TRAFFIC_QR_{key} ""',text)
            # A data-only local file (no Wi-Fi profiles) is valid.
            data={'traffic_qr':{'whatsapp':'https://wa.me/6512345678',
                                'linkedin':'https://www.linkedin.com/in/example',
                                'wechat':''}}
            source.write_text(json.dumps(data));module.generate(source,header)
            text=header.read_text()
            self.assertIn('PASSPORT_PRIVATE_WIFI_COUNT 0',text)
            self.assertIn('PASSPORT_PRIVATE_TRAFFIC_QR_WHATSAPP',text)
            self.assertNotIn('wa.me/6512345678',text)  # octal-escaped literal
            program=path/'traffic.c'
            program.write_text('#include "generated.h"\n#include <string.h>\n#include <assert.h>\nint main(void){assert(!strcmp(PASSPORT_PRIVATE_TRAFFIC_QR_WHATSAPP,"https://wa.me/6512345678"));assert(!strcmp(PASSPORT_PRIVATE_TRAFFIC_QR_LINKEDIN,"https://www.linkedin.com/in/example"));assert(!strcmp(PASSPORT_PRIVATE_TRAFFIC_QR_WECHAT,""));}\n')
            subprocess.run(['cc','-std=c11',str(program),'-o',str(path/'traffic')],check=True,capture_output=True)
            subprocess.run([str(path/'traffic')],check=True,capture_output=True)
            for bad in ({'traffic_qr':{'whatsapp':'ftp://example.test'}},
                        {'traffic_qr':{'linkedin':'https://example.test/a b'}},
                        {'traffic_qr':{'wechat':'https://example.test/'+'x'*250}},
                        {'traffic_qr':['not-a-dict']},
                        {'traffic_qr':{'whatsapp':7}}):
                source.write_text(json.dumps(bad))
                with self.assertRaisesRegex(ValueError,'^Invalid private traffic QR links$'):
                    module.generate(source,header)

    def test_bad_profiles_fail_without_echoing_credentials(self):
        bad=[[],[{'id':'other','ssid':'valid','password':'testpass'}],
             [{'id':'home','ssid':'x'*33,'password':'privatepass'}],
             [{'id':'home','ssid':'valid','password':'short'}]]
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)
            for profiles in bad:
                (path/'private.json').write_text(json.dumps({'wifi_profiles':profiles}))
                with self.assertRaisesRegex(ValueError,'^Invalid private Wi-Fi profiles'):
                    module.generate(path/'private.json',path/'out.h')


if __name__ == '__main__':
    unittest.main()
