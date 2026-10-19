"""LTA DataMall v3 adapter for the Passport's small paged display.

Protocol reference: https://github.com/awesomelionel/bus-aunty-mini (MIT).
This adapter is independently implemented for the existing local bridge.
"""
from datetime import datetime, timedelta, timezone
import math
import os
import hashlib
import re
import threading
import time

BASE_URL = 'https://datamall2.mytransport.sg/ltaodataservice/v3/BusArrival?BusStopCode='
_CACHE = {}
_LOCK = threading.Lock()
PAGE_SIZE = 4


def valid_stop(code):
    return isinstance(code, str) and re.fullmatch(r'[0-9]{3,5}', code) is not None


def epoch(stamp):
    if not isinstance(stamp, str) or not stamp:
        return None
    try:
        parsed = datetime.fromisoformat(stamp.replace('Z', '+00:00'))
        if parsed.tzinfo is None:
            return None
        value = parsed.timestamp()
        return value if math.isfinite(value) else None
    except (ValueError, OverflowError):
        return None


def eta_text(target, now):
    if target is None or target-now < -120:
        return '--'
    diff = target-now
    if diff <= 60:
        return 'Arr'
    minutes = int(diff//60)
    return '60+' if minutes > 60 else str(minutes)


def parse_arrivals(payload, code, fetched=None):
    if isinstance(payload, dict) and 'Services' in payload:
        if str(payload.get('BusStopCode','')) != code or fetched is None:
            raise ValueError('Invalid LTA bus response')
        if not isinstance(payload['Services'],list):
            raise ValueError('Invalid LTA services')
        services=[]
        for service in payload['Services']:
            if not isinstance(service,dict): continue
            copied=dict(service)
            for slot in ('NextBus','NextBus2','NextBus3'):
                arrival=service.get(slot)
                if isinstance(arrival,dict):
                    destination=str(arrival.get('DestinationCode') or '')[:5]
                    copied[slot]={**arrival,'Label':'To stop '+destination if destination else ''}
            services.append(copied)
        payload={'busStops':[{'BusStopCode':code,'Services':services,
            'UpdatedAt':datetime.fromtimestamp(fetched,timezone.utc).isoformat()}]}
    if not isinstance(payload, dict) or not isinstance(payload.get('busStops'), list):
        raise ValueError('Invalid bus response')
    stop = next((s for s in payload['busStops'] if isinstance(s, dict) and
                 str(s.get('BusStopCode', '')).zfill(len(code)) == code), None)
    if stop is None or not isinstance(stop.get('Services'), list):
        raise ValueError('Bus stop not found')
    updated = epoch(stop.get('UpdatedAt'))
    if updated is None:
        raise ValueError('Missing source timestamp')
    groups = {}
    for entry, service in enumerate(stop['Services'][:48]):
        if not isinstance(service, dict):
            continue
        number = service.get('ServiceNo')
        if not isinstance(number, str) or not re.fullmatch(r'[A-Za-z0-9/.-]{1,12}', number):
            continue
        for slot in ('NextBus', 'NextBus2', 'NextBus3'):
            bus = service.get(slot)
            if not isinstance(bus, dict):
                continue
            arrival = epoch(bus.get('EstimatedArrival'))
            if arrival is None:
                continue
            label = bus.get('Label') or ''
            if not isinstance(label, str): label = ''
            label = label[:60]
            visit = str(bus.get('VisitNumber') or '')[:8]
            # Opposite directions / different visits must not share a countdown row.
            key = (number, label, visit, None if label else entry)
            row = groups.setdefault(key, {'service': number, 'destination': label,
                'visit': visit, 'arrivals': []})
            item = {'at': arrival, 'load': bus.get('Load', ''), 'type': bus.get('Type', ''),
                    'terminating': bus.get('Terminating') is True}
            if item not in row['arrivals']: row['arrivals'].append(item)
    rows = list(groups.values())[:96]
    for row in rows: row['arrivals'].sort(key=lambda a: a['at'])
    return {'rows': rows, 'updated_at': datetime.fromtimestamp(updated, timezone(timedelta(hours=8))).isoformat(), 'updated_epoch': updated}


def bus_arrivals(request_json, code, page=0, now=None):
    if not valid_stop(code) or not isinstance(page, int) or not 0 <= page <= 95:
        raise ValueError('Invalid stop or page')
    now = time.time() if now is None else now
    key=os.environ.get('LTA_ACCOUNT_KEY','')
    if not key or len(key)>128 or any(ord(c)<=32 or ord(c)>=127 for c in key):
        return {'available':False,'stop':code,'reason':'lta_key_required','rows':[],'page':0,'pages':1}
    credential=hashlib.sha256(key.encode()).digest()
    with _LOCK:
        entry = _CACHE.get(code)
        if entry and entry.get('credential')!=credential: entry=None
        if entry is None or now >= entry['next_attempt']:
            try:
                parsed = parse_arrivals(request_json(BASE_URL+code, timeout=3, headers={'AccountKey':key,'Accept':'application/json'}), code, now)
                entry = {**parsed, 'failed': False, 'failures': 0, 'fetched_at': now,
                         'next_attempt': now+60}
            except Exception:
                previous = entry or {}
                failures = previous.get('failures', 0)+1
                entry = {**previous, 'failed': True, 'failures': failures,
                         'next_attempt': now+(120 if failures == 1 else 240 if failures == 2 else 300)}
            entry['credential']=credential
            _CACHE[code] = entry
            # At most 32 stops retained, independent of arbitrary client requests.
            if len(_CACHE) > 32:
                oldest = next(iter(_CACHE))
                if oldest != code: _CACHE.pop(oldest)
        data = entry
    if 'rows' not in data:
        return {'available': False, 'stop': code, 'reason': 'upstream_unavailable',
                'rows': [], 'page': 0, 'pages': 1}
    stale = data['failed'] or now-data['updated_epoch'] > 180
    pages = max(1, math.ceil(len(data['rows'])/PAGE_SIZE))
    page = min(page, pages-1)
    rows = []
    for row in data['rows'][page*PAGE_SIZE:(page+1)*PAGE_SIZE]:
        arrivals = [a for a in row['arrivals'] if a['at']-now >= -120][:3]
        rows.append({'service': row['service'], 'destination': row['destination'],
            'visit': row['visit'], 'eta': ['--' if stale else eta_text(a['at'],now) for a in arrivals],
            'load': [a['load'] for a in arrivals], 'type': [a['type'] for a in arrivals],
            'terminating': [a['terminating'] for a in arrivals]})
    return {'available': True, 'stop': code, 'rows': rows, 'page': page, 'pages': pages,
            'stale': stale, 'updated_at': data['updated_at'], 'source': 'LTA DataMall'}
