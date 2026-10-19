from datetime import datetime, timezone
from pathlib import Path
import copy
import sys
import time
import unittest
from unittest.mock import Mock
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'companion'))
import haze

def fixture():
    return {'code': 0, 'data': {'regionMetadata': [
        {'name': name, 'labelLocation': {'latitude': lat, 'longitude': lon}}
        for name, lat, lon in [('central',1.35735,103.82),('north',1.41803,103.82),
        ('south',1.29587,103.82),('east',1.35735,103.94),('west',1.35735,103.7)]],
        'items': [{'timestamp': datetime.now(timezone.utc).isoformat(),
            'readings': {'psi_twenty_four_hourly': {'north': 50, 'south': 100,
                'east': 200, 'west': 300, 'central': 184}}}]}}

class HazeTest(unittest.TestCase):
    def setUp(self): haze._CACHE = None

    def test_all_nea_category_boundaries(self):
        for value, expected in [(0,'good'),(50,'good'),(51,'moderate'),(100,'moderate'),
            (101,'unhealthy'),(200,'unhealthy'),(201,'very_unhealthy'),(300,'very_unhealthy'),(301,'hazardous')]:
            self.assertEqual(haze.psi_category(value),expected)
        for value in [-1,True,float('nan'),float('inf'),'100']:
            with self.assertRaises(ValueError): haze.psi_category(value)

    def test_manual_region_and_cache(self):
        request = Mock(return_value=fixture())
        result = haze.haze(request,region='west')
        self.assertEqual(result['value'],300);self.assertEqual(result['region'],'west')
        self.assertEqual(result['period'],'24h');self.assertFalse(result['stale'])
        self.assertEqual(haze.haze(request,region='central')['value'],184)
        request.assert_called_once()

    def test_coordinate_mapping_and_no_location_assumptions(self):
        request=Mock(return_value=fixture())
        for name,lat,lon in [('north',1.42,103.82),('south',1.29,103.82),('east',1.36,103.94),('west',1.36,103.70),('central',1.35,103.82)]:
            result=haze.haze(request,region='auto',latitude=lat,longitude=lon)
            self.assertEqual(result['region'],name)
            self.assertEqual(result['selection'],'nearest_reporting_centre')
        self.assertEqual(haze.haze(request)['reason'],'location_required')
        self.assertEqual(haze.haze(request,region='auto')['reason'],'location_required')
        self.assertEqual(haze.haze(request,city='johor-bahru',region='north')['reason'],'outside_singapore')
        self.assertEqual(haze.haze(request,region='auto',latitude=3.139,longitude=101.69)['reason'],'outside_singapore')
        with self.assertRaises(ValueError):haze.haze(request,region='auto',latitude='nan',longitude=103.82)

    def test_invalid_nea_response_does_not_replace_cache(self):
        request=Mock(return_value=fixture());haze.haze(request,region='central')
        haze._CACHE['fetched_at']=0
        bad=fixture();bad['data']['items'][0]['readings']['psi_twenty_four_hourly']['central']=-1
        request.return_value=bad
        result=haze.haze(request,region='central')
        self.assertTrue(result['stale']);self.assertEqual(result['value'],184)
        haze._CACHE=None
        self.assertFalse(haze.haze(request,region='central')['available'])

    def test_old_published_reading_is_stale(self):
        p=fixture();p['data']['items'][0]['timestamp']='2020-01-01T01:00:00+08:00'
        self.assertTrue(haze.haze(Mock(return_value=p),region='central')['stale'])

if __name__=='__main__':unittest.main()
