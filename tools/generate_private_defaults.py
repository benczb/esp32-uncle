#!/usr/bin/env python3
"""Turn optional ignored local Wi-Fi profiles into a private build header."""
import argparse
import hashlib
import json
from pathlib import Path
from urllib.parse import urlsplit


def generate(source, output):
    profiles, default, document = [], 0, {}
    if source.exists():
        try:
            document = json.loads(source.read_text())
            profiles = document['wifi_profiles']
            if not isinstance(profiles, list) or not 1 <= len(profiles) <= 2:
                raise ValueError()
            ids = []
            for profile in profiles:
                kind, ssid, password = profile['id'], profile['ssid'], profile['password']
                if kind not in ('home', 'outside') or kind in ids:
                    raise ValueError()
                ids.append(kind)
                if not isinstance(ssid, str) or not isinstance(password, str):
                    raise ValueError()
                if not 1 <= len(ssid.encode()) <= 32 or '\0' in ssid or '\0' in password:
                    raise ValueError()
                if not password.isascii() or not 8 <= len(password) <= 63:
                    raise ValueError()
            default = ids.index(document.get('default_profile', ids[0]))
        except (ValueError, KeyError, TypeError, OSError):
            raise ValueError('Invalid private Wi-Fi profiles; check IDs, SSID and WPA2 password lengths') from None
    revision = hashlib.sha256(json.dumps([profiles, default], sort_keys=True, ensure_ascii=False).encode()).hexdigest()[:16] if profiles else ''

    def literal(text):
        return '"' + ''.join('\\%03o' % b for b in text.encode()) + '"'

    qr_url = document.get('qr_url', '') if source.exists() else ''
    if not isinstance(qr_url,str) or len(qr_url.encode())>256 or (qr_url and not qr_url.startswith(('https://','http://','weixin://'))):
        raise ValueError('Invalid private profile QR URL')
    rows = ['{' + ','.join(literal(p[k]) for k in ('id', 'ssid', 'password')) + '}' for p in profiles]
    bridge = document.get('bridge_url', '')
    lan_bridge = document.get('bridge_lan_url', '')
    token = document.get('bridge_token', '')
    for url in (bridge,lan_bridge):
        if not isinstance(url, str) or len(url.encode()) > 159 or any(ord(c) < 33 for c in url):
            raise ValueError('Invalid private bridge URL')
        try:
            parsed = urlsplit(url)
            if url and (parsed.scheme not in ('http', 'https') or not parsed.hostname or parsed.username or parsed.password or parsed.query or parsed.fragment):
                raise ValueError()
            parsed.port
        except ValueError:
            raise ValueError('Invalid private bridge URL') from None
    if not isinstance(token, str) or len(token.encode()) > 96 or any(ord(c) < 33 or ord(c) > 126 for c in token):
        raise ValueError('Invalid private bridge token')
    psi_region = document.get('psi_region', '')
    if not isinstance(psi_region, str) or psi_region not in ('', 'central', 'north', 'south', 'east', 'west'):
        raise ValueError('Invalid private PSI region')
    text = '#pragma once\n/* Generated private build input. Never commit or publish. */\n'
    text += '#define PASSPORT_PRIVATE_WIFI_COUNT %d\n' % len(profiles)
    text += '#define PASSPORT_PRIVATE_WIFI_DEFAULT %d\n' % default
    text += '#define PASSPORT_PRIVATE_WIFI_REVISION %s\n' % literal(revision)
    text += '#define PASSPORT_PRIVATE_WIFI_PROFILES {%s}\n' % ','.join(rows or ['{"","",""}'])
    text += '#define PASSPORT_PRIVATE_PSI_REGION %s\n' % literal(psi_region)
    text += '#define PASSPORT_PRIVATE_QR_URL %s\n' % literal(qr_url)
    text += '#define PASSPORT_PRIVATE_BRIDGE_URL %s\n' % literal(bridge.rstrip('/'))
    text += '#define PASSPORT_PRIVATE_BRIDGE_TOKEN %s\n' % literal(token)
    text += '#define PASSPORT_PRIVATE_LAN_BRIDGE_URL %s\n' % literal(lan_bridge.rstrip('/'))
    usb_test = document.get('usb_test_control', False)
    if not isinstance(usb_test, bool):
        raise ValueError('Invalid private USB test control flag')
    text += '#define PASSPORT_PRIVATE_USB_TEST_CONTROL %d\n' % usb_test
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text)
    output.chmod(0o600)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        generate(args.input, args.output)
    except ValueError as exc:
        parser.exit(1, str(exc) + '\n')
