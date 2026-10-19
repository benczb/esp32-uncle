"""NEA 24-hour PSI, retaining official timestamps and region metadata."""
from datetime import datetime
import math
import threading
import time

PSI_URL = 'https://api-open.data.gov.sg/v2/real-time/api/psi'
REGIONS = ('north', 'south', 'east', 'west', 'central')
_CACHE = None
_LOCK = threading.Lock()


def psi_category(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0:
        raise ValueError('Invalid PSI')
    if value <= 50: return 'good'
    if value <= 100: return 'moderate'
    if value <= 200: return 'unhealthy'
    if value <= 300: return 'very_unhealthy'
    return 'hazardous'


def parse_psi(payload):
    if payload.get('code') != 0:
        raise ValueError('NEA API error')
    data = payload['data']
    items = data['items']
    item = max(items, key=lambda x: datetime.fromisoformat(x['timestamp']).timestamp())
    timestamp = datetime.fromisoformat(item['timestamp'])
    if timestamp.tzinfo is None:
        raise ValueError('NEA timestamp has no timezone')
    readings = item['readings']['psi_twenty_four_hourly']
    for region in REGIONS:
        psi_category(readings[region])
    metadata = {}
    for region in data['regionMetadata']:
        if region['name'] in REGIONS:
            lat = float(region['labelLocation']['latitude'])
            lon = float(region['labelLocation']['longitude'])
            if not math.isfinite(lat) or not math.isfinite(lon):
                raise ValueError('Invalid metadata')
            metadata[region['name']] = (lat, lon)
    if set(metadata) != set(REGIONS):
        raise ValueError('Incomplete NEA regions')
    return {'readings': {r: readings[r] for r in REGIONS}, 'metadata': metadata,
            'timestamp': item['timestamp'], 'updated_at': item.get('updatedTimestamp', item['timestamp'])}


def haze(request_json, city='singapore', region='unset', latitude=None, longitude=None):
    global _CACHE
    if city != 'singapore':
        return {'available': False, 'reason': 'outside_singapore', 'source': 'NEA', 'period': '24h'}
    if region not in (*REGIONS, 'auto', 'unset'):
        raise ValueError('Invalid PSI region')
    point = None
    if region == 'auto':
        if latitude is None or longitude is None:
            return {'available': False, 'reason': 'location_required', 'source': 'NEA', 'period': '24h'}
        point = (float(latitude), float(longitude))
        if not all(math.isfinite(x) for x in point):
            raise ValueError('Invalid coordinates')
        # Conservative application coverage guard, not an official boundary polygon.
        if not (1.15 <= point[0] <= 1.48 and 103.60 <= point[1] <= 104.10):
            return {'available': False, 'reason': 'outside_singapore', 'source': 'NEA', 'period': '24h'}
    if region == 'unset':
        return {'available': False, 'reason': 'location_required', 'source': 'NEA', 'period': '24h'}
    failed_refresh = False
    with _LOCK:
        if _CACHE is None or time.time() - _CACHE['fetched_at'] >= 300:
            try:
                parsed = parse_psi(request_json(PSI_URL, timeout=10))
                _CACHE = {**parsed, 'fetched_at': time.time()}
            except Exception:
                if _CACHE is None:
                    return {'available': False, 'reason': 'upstream_unavailable', 'source': 'NEA', 'period': '24h'}
                failed_refresh = True
        cached = _CACHE
    selected = region
    if point:
        selected = min(REGIONS, key=lambda r: (cached['metadata'][r][0]-point[0])**2 +
            ((cached['metadata'][r][1]-point[1])*math.cos(math.radians(point[0])))**2)
    value = cached['readings'][selected]
    age = time.time() - datetime.fromisoformat(cached['timestamp']).timestamp()
    return {'available': True, 'value': value, 'category': psi_category(value), 'region': selected,
            'selection': 'nearest_reporting_centre' if point else 'manual_region',
            'timestamp': cached['timestamp'], 'updated_at': cached['updated_at'],
            'stale': failed_refresh or age > 7200, 'source': 'NEA', 'period': '24h'}
