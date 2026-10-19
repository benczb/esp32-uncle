"""Privacy-preserving quota adapter. Missing data is unknown, never a zero balance."""
import datetime
import math


def finite(value, minimum, maximum):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) and minimum <= value <= maximum


def window(raw):
    if not isinstance(raw, dict):
        return None
    result = {}
    used = raw.get('usedPercent', raw.get('used_percent', raw.get('utilization')))
    if finite(used, 0, 100):
        result['usedPercent'] = used
    minutes = raw.get('windowDurationMins')
    if finite(minutes, 1, 525600):
        result['windowDurationMins'] = minutes
    reset = raw.get('resetsAt', raw.get('resets_at'))
    if isinstance(reset, str):
        try:
            parsed = datetime.datetime.fromisoformat(reset.replace('Z', '+00:00'))
            reset = parsed.timestamp() if parsed.tzinfo else None
        except ValueError:
            reset = None
    if finite(reset, 1700000000, 4102444800):
        result['resetsAt'] = reset
    return result or None


def summary(payload):
    rows = []
    items = payload.get('usage', []) if isinstance(payload, dict) else []
    for item in items if isinstance(items, list) else []:
        if not isinstance(item, dict):
            continue
        provider = str(item.get('provider', '')).lower()
        if provider not in ('codex', 'claude'):
            continue
        rates = item.get('rateLimits') or {}
        if not isinstance(rates, dict):
            rates = {}
        status = item.get('status')
        status = status if status in ('ok', 'error', 'not_connected') else 'unknown'
        rows.append({'provider': provider, 'status': status,
                     'primary': window(rates.get('primary', rates.get('five_hour'))),
                     'secondary': window(rates.get('secondary', rates.get('seven_day')))})
        if len(rows) == 6:
            break
    for provider in ('codex', 'claude'):
        if not any(row['provider'] == provider for row in rows):
            rows.append({'provider': provider, 'status': 'not_connected', 'primary': None, 'secondary': None})
    return rows
