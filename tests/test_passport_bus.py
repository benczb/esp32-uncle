from datetime import datetime,timezone
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock,patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'companion'))
import bus

NOW=1700000000

def stamp(offset=0):return datetime.fromtimestamp(NOW+offset,timezone.utc).isoformat()
def fixture():
    return {'busStops':[{'BusStopCode':'82009','UpdatedAt':stamp(), 'Services':[
        {'ServiceNo':'22','NextBus':{'EstimatedArrival':stamp(90),'Label':'To A','VisitNumber':'1','Type':'DD','Load':'SEA'},
         'NextBus2':{'EstimatedArrival':stamp(600),'Label':'To B','VisitNumber':'2','Terminating':True}},
        {'ServiceNo':'60','NextBus':{'EstimatedArrival':stamp(30),'Label':'To Bedok','VisitNumber':'1'}}]}]}

class BusTest(unittest.TestCase):
    def setUp(self):
        bus._CACHE.clear()
        environment=patch.dict(bus.os.environ,{'LTA_ACCOUNT_KEY':'test-lta-key'})
        environment.start();self.addCleanup(environment.stop)

    def test_lta_headers_missing_key_and_destination(self):
        payload={'BusStopCode':'82009','Services':[{'ServiceNo':'22','NextBus':{
            'EstimatedArrival':stamp(120),'DestinationCode':'54009','VisitNumber':'1'}}]}
        request=Mock(return_value=payload)
        result=bus.bus_arrivals(request,'82009',now=NOW)
        self.assertEqual(result['source'],'LTA DataMall')
        self.assertEqual(result['rows'][0]['destination'],'To stop 54009')
        self.assertEqual(result['rows'][0]['eta'],['2'])
        self.assertNotIn('test-lta-key',str(result))
        request.assert_called_once_with(bus.BASE_URL+'82009',timeout=3,headers={'AccountKey':'test-lta-key','Accept':'application/json'})
        with patch.dict(bus.os.environ,{'LTA_ACCOUNT_KEY':''}):
            self.assertEqual(bus.bus_arrivals(request,'82009',now=NOW)['reason'],'lta_key_required')
        self.assertEqual(request.call_count,1)
        with self.assertRaises(ValueError):bus.parse_arrivals(payload,'00481',NOW)

    def test_changed_key_invalidates_cache(self):
        request=Mock(return_value=fixture());bus.bus_arrivals(request,'82009',now=NOW)
        with patch.dict(bus.os.environ,{'LTA_ACCOUNT_KEY':'replacement-test-key'}):
            bus.bus_arrivals(request,'82009',now=NOW+1)
        self.assertEqual(request.call_count,2)

    def test_stop_validation_preserves_leading_zero(self):
        for code in ['00481','82009','123']:self.assertTrue(bus.valid_stop(code))
        for code in ['1','123456','1234x','../','１２３４５',12345]:self.assertFalse(bus.valid_stop(code))
        with self.assertRaises(ValueError):bus.bus_arrivals(Mock(),'../')

    def test_arrival_boundaries(self):
        for delta,expected in [(-121,'--'),(-120,'Arr'),(0,'Arr'),(60,'Arr'),(61,'1'),(210,'3'),(3660,'60+')]:
            self.assertEqual(bus.eta_text(NOW+delta,NOW),expected)
        self.assertEqual(bus.eta_text(None,NOW),'--')
        self.assertIsNone(bus.epoch('not-a-date'))
        self.assertIsNone(bus.epoch('2026-10-17T15:00:00'))

    def test_directions_visits_and_terminating_kept_separate(self):
        result=bus.bus_arrivals(Mock(return_value=fixture()),'82009',now=NOW)
        self.assertEqual(len(result['rows']),3)
        self.assertEqual(result['rows'][0]['eta'],['1'])
        self.assertEqual(result['rows'][0]['type'],['DD'])
        self.assertEqual(result['rows'][1]['visit'],'2')
        self.assertTrue(result['rows'][1]['terminating'][0])
        self.assertEqual(result['updated_at'][-6:],'+08:00')

    def test_cache_recalculates_eta_without_repolling(self):
        request=Mock(return_value=fixture())
        bus.bus_arrivals(request,'82009',now=NOW)
        result=bus.bus_arrivals(request,'82009',now=NOW+35)
        self.assertEqual(result['rows'][0]['eta'],['Arr'])
        request.assert_called_once()

    def test_error_cache_and_backoff(self):
        request=Mock(return_value=fixture());bus.bus_arrivals(request,'82009',now=NOW)
        request.side_effect=TimeoutError
        result=bus.bus_arrivals(request,'82009',now=NOW+60)
        self.assertTrue(result['stale']);self.assertEqual(result['rows'][0]['eta'],['--'])
        bus.bus_arrivals(request,'82009',now=NOW+70)
        self.assertEqual(request.call_count,2)
        bus.bus_arrivals(request,'82009',now=NOW+180)
        self.assertEqual(bus._CACHE['82009']['next_attempt'],NOW+420)
        bus._CACHE.clear();self.assertFalse(bus.bus_arrivals(request,'82009',now=NOW)['available'])

    def test_paging_and_empty_and_malformed(self):
        p=fixture();p['busStops'][0]['Services']=[{'ServiceNo':str(i),'NextBus':{'EstimatedArrival':stamp(120)}} for i in range(9)]
        result=bus.bus_arrivals(Mock(return_value=p),'82009',page=2,now=NOW)
        self.assertEqual(result['pages'],3);self.assertEqual(len(result['rows']),1)
        with self.assertRaises(ValueError):bus.parse_arrivals({},'82009')
        p['busStops'][0]['Services']=[]
        result=bus.parse_arrivals(p,'82009');self.assertEqual(result['rows'],[])
        p['busStops'][0]['BusStopCode']=481
        self.assertEqual(bus.parse_arrivals(p,'00481')['rows'],[])

if __name__=='__main__':unittest.main()
