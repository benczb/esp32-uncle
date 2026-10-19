from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from host_cjson import cjson_flags

ROOT = Path(__file__).resolve().parents[1]

class SettingsTest(unittest.TestCase):
    def test_ui_labels_have_font_inventory(self):
        inventory = set((ROOT/'assets/fonts/passport-symbols.txt').read_text())
        missing = set()
        for file in ['main/passport_ui.c','main/passport_network.c','main/passport_usage.c']:
            source = (ROOT/file).read_text()
            for literal in re.findall(r'"(?:[^"\\]|\\.)*"', source):
                missing.update(c for c in literal if ord(c) > 127 and c not in inventory)
        self.assertFalse(missing, 'Missing UI glyphs: ' + repr(missing))

    def test_relay_weather_refresh_without_network_worker(self):
        source=(ROOT/'main/passport_network.c').read_text()
        code=source[source.index('bool passport_weather_refresh_request(void)'):source.index('static atomic_uint audio_generation;')]
        harness=r'''#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>
typedef struct {unsigned kind,index;} control_t;
static void *control_queue;static atomic_uint weather_generation;static bool relay,accept;static unsigned queued;
#define pdTRUE 1
static bool passport_phone_relay_active(void){return relay;}
static int xQueueSend(void *queue,const control_t *request,unsigned wait){assert(queue&&request->kind==5&&!wait);queued++;return accept;}
'''
        checks=r'''
int main(void){
 relay=true;assert(passport_weather_refresh_request()&&weather_generation==1&&!queued);
 relay=false;assert(!passport_weather_refresh_request()&&weather_generation==1);
 control_queue=&queued;assert(!passport_weather_refresh_request()&&queued==1);
 accept=true;assert(passport_weather_refresh_request()&&queued==2&&weather_generation==1);
 relay=true;assert(passport_weather_refresh_request()&&weather_generation==2&&queued==2);return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cfile=Path(directory)/'refresh.c';binary=Path(directory)/'refresh';cfile.write_text(harness+code+checks)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(cfile),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    def test_bridge_response_allocation_and_truncation(self):
        source=(ROOT/'main/passport_network.c').read_text()
        code=source[source.index('static cJSON *bridge_request('):source.index('static const char *weather_name(')]
        harness=r'''#include "cJSON.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ESP_OK 0
#define HTTP_METHOD_POST 1
typedef void *esp_http_client_handle_t;
typedef struct {const char *url;int timeout_ms,buffer_size;bool keep_alive_enable;} esp_http_client_config_t;
static struct {char token[20];} passport_config={"test"};static bool passport_connected=true;
static const char *payload;static size_t used,allocated;static int64_t declared;static int response_status=200;
static void lock(void){} static void unlock(void){}
static const char *bridge_base(void){return "http://bridge.invalid";}
static void *tracked_malloc(size_t n){allocated=n;return malloc(n);}
#define malloc tracked_malloc
static esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c){assert(c->buffer_size==1024);used=0;return (void *)1;}
static void esp_http_client_set_header(void *h,const char *key,const char *value){(void)h;(void)key;(void)value;}
static void esp_http_client_set_method(void *h,int method){(void)h;assert(method==HTTP_METHOD_POST);}
static int esp_http_client_open(void *h,size_t n){(void)h;(void)n;return ESP_OK;}
static int esp_http_client_write(void *h,const char *p,size_t n){(void)h;(void)p;return (int)n;}
static int64_t esp_http_client_fetch_headers(void *h){(void)h;return declared;}
static int esp_http_client_get_status_code(void *h){(void)h;return response_status;}
static int esp_http_client_read(void *h,char *out,size_t cap){(void)h;size_t n=strlen(payload)-used;if(n>cap)n=cap;memcpy(out,payload+used,n);used+=n;return (int)n;}
static bool esp_http_client_is_complete_data_received(void *h){(void)h;return used==strlen(payload);}
static void esp_http_client_close(void *h){(void)h;}
static void esp_http_client_cleanup(void *h){(void)h;}
'''
        checks=r'''
int main(void){
 payload="{\"text\":\"ok\"}";declared=strlen(payload);
 cJSON *j=bridge_request("/v1/reply/hermes",NULL);assert(j&&allocated==strlen(payload)+1);cJSON_Delete(j);
 declared=0;j=bridge_request("/v1/usage",NULL);assert(j&&allocated==10000);cJSON_Delete(j);
 j=bridge_request("/v1/weather?city=singapore",NULL);assert(j&&allocated==2048);cJSON_Delete(j);
 declared=3;j=bridge_request("/v1/reply/hermes",NULL);assert(!j&&allocated==4);
 char oversized[10001];memset(oversized,'x',10000);oversized[10000]=0;payload=oversized;declared=10000;
 assert(!bridge_request("/v1/reply/hermes",NULL)&&allocated==10000);
 response_status=401;allocated=0;assert(!bridge_request("/v1/reply/hermes",NULL)&&!allocated);
 passport_connected=false;assert(!bridge_request("/v1/reply/hermes",NULL));return 0;
}
'''
        cflags,ldflags=cjson_flags()
        with tempfile.TemporaryDirectory() as directory:
            cfile=Path(directory)/'bridge.c';binary=Path(directory)/'bridge';cfile.write_text(harness+code+checks)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',*cflags,str(cfile),*ldflags,'-lm','-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    def test_weather_tls_failure_uses_bridge(self):
        source=(ROOT/'main/passport_network.c').read_text()
        code=source[source.index('static cJSON *fetch_weather('):source.index('static bool copy_weather(cJSON *j){')]
        harness=r'''#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef struct {int unused;} cJSON;
static cJSON direct_value,bridge_value;static bool direct_ok,bridge_ok;static unsigned direct_calls,bridge_calls;static char requested[64];
static cJSON *passport_direct_weather(const char *city){assert(city);direct_calls++;return direct_ok?&direct_value:NULL;}
static cJSON *bridge_request(const char *path,const char *body){assert(!body);bridge_calls++;snprintf(requested,sizeof(requested),"%s",path);return bridge_ok?&bridge_value:NULL;}
'''
        checks=r'''
int main(void){
 direct_ok=true;assert(fetch_weather("singapore",false)==&direct_value&&direct_calls==1&&!bridge_calls);
 direct_ok=false;bridge_ok=true;assert(fetch_weather("penang",false)==&bridge_value&&direct_calls==2&&bridge_calls==1);assert(!strcmp(requested,"/v1/weather?city=penang"));
 assert(fetch_weather("kuala-lumpur",true)==&bridge_value&&direct_calls==2&&bridge_calls==2);assert(!strcmp(requested,"/v1/weather?city=kuala-lumpur"));
 bridge_ok=false;assert(!fetch_weather("singapore",false));return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cfile=Path(directory)/'weather-fallback.c';binary=Path(directory)/'weather-fallback'
            cfile.write_text(harness+code+checks)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(cfile),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    def test_haze_tls_failure_preserves_location(self):
        source=(ROOT/'main/passport_network.c').read_text()
        code=source[source.index('static cJSON *fetch_haze('):source.index('static void poll_haze(void){')]
        harness=r'''#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef struct {int unused;} cJSON;
typedef struct {char region[12];double latitude,longitude;unsigned has_location;} haze_location_t;
static cJSON direct_value,bridge_value;static bool direct_ok;static unsigned bridge_calls;static char requested[160];
static cJSON *passport_direct_haze(const char *city,const char *region,double lat,double lon,bool location){(void)lat;(void)lon;(void)location;assert(city&&region);return direct_ok?&direct_value:NULL;}
static cJSON *bridge_request(const char *path,const char *body){assert(!body);bridge_calls++;snprintf(requested,sizeof(requested),"%s",path);return &bridge_value;}
'''
        checks=r'''
int main(void){
 haze_location_t loc={.region="east"};direct_ok=true;assert(fetch_haze("singapore",&loc)==&direct_value&&!bridge_calls);
 direct_ok=false;assert(fetch_haze("singapore",&loc)==&bridge_value&&bridge_calls==1);assert(!strcmp(requested,"/v1/haze?city=singapore&region=east"));
 strcpy(loc.region,"auto");loc.has_location=1;loc.latitude=1.35;loc.longitude=103.9;fetch_haze("singapore",&loc);assert(strstr(requested,"region=auto&lat=1.350000&lon=103.900000"));return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cfile=Path(directory)/'haze-fallback.c';binary=Path(directory)/'haze-fallback'
            cfile.write_text(harness+code+checks)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(cfile),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    def test_weather_payload_validation(self):
        source=(ROOT/'main/passport_network.c').read_text()
        start=source.index('static bool copy_weather(cJSON *j){')
        end=source.index('static const char *str(',start)
        harness=r'''#include "cJSON.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#define T(zh,en) en
static char passport_weather[320];static bool passport_dirty;
static void lock(void){} static void unlock(void){}
static const char *weather_name(int code){(void)code;return "Cloudy";}
'''
        checks=r'''
int main(void){
 assert(!copy_weather(NULL));
 cJSON *j=cJSON_Parse("{\"current\":{\"temperature_2m\":30.5,\"relative_humidity_2m\":70,\"wind_speed_10m\":5,\"weather_code\":3}}");
 assert(copy_weather(j)&&passport_dirty&&strstr(passport_weather,"30.5")&&strstr(passport_weather,"RH 70%"));
 passport_dirty=false;cJSON *c=cJSON_GetObjectItem(j,"current");
 cJSON_SetNumberValue(cJSON_GetObjectItem(c,"relative_humidity_2m"),101);
 assert(!copy_weather(j)&&!passport_dirty);
 cJSON_SetNumberValue(cJSON_GetObjectItem(c,"relative_humidity_2m"),70);
 cJSON_SetNumberValue(cJSON_GetObjectItem(c,"wind_speed_10m"),-1);assert(!copy_weather(j));
 cJSON_SetNumberValue(cJSON_GetObjectItem(c,"wind_speed_10m"),5);
 cJSON_AddBoolToObject(j,"stale",true);assert(copy_weather(j)&&strstr(passport_weather,"cached"));
 cJSON_DeleteItemFromObject(c,"temperature_2m");assert(!copy_weather(j));cJSON_Delete(j);return 0;
}
'''
        cflags,ldflags=cjson_flags()
        with tempfile.TemporaryDirectory() as directory:
            cfile=Path(directory)/'weather.c';binary=Path(directory)/'weather'
            cfile.write_text(harness+source[start:end]+checks)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',*cflags,str(cfile),*ldflags,'-lm','-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    def test_settings_upload_rgb565_and_config(self):
        html = (ROOT/'main/passport_settings.html').read_text()
        script = html.split('<script>',1)[1].split('</script>',1)[0]
        harness = '''
const assert=require('assert');const elements={};let sent=[];
const photoPixels=new Uint8Array(80*112*4).fill(255);
photoPixels.set([255,0,0,255,0,255,0,255,0,0,255,255]);
global.document={getElementById(id){return elements[id]??=(
 {value: id==='theme'?'0':'', files:[],textContent:'',getContext(){return {getImageData(){return {data:photoPixels}},drawImage(){}}}})}};
global.fetch=async(path,options)=>{sent.push({path,options});return {ok:true,text:async()=>'{"ok":true}'}};
global.createImageBitmap=async()=>({width:100,height:100});
'''
        checks = '''
(async()=>{
 elements.code.value='123456';elements.photo.files=[{size:1000}];
 await elements.photo.onchange();await elements.upload.onclick();
 assert.equal(sent[0].path,'/avatar');
 assert.equal(sent[0].options.headers['X-Passport-Code'],'123456');
 assert.equal(sent[0].options.body.length,17920);
 assert.deepEqual([...sent[0].options.body.slice(0,6)],[0x00,0xf8,0xe0,0x07,0x1f,0x00], 'Save original red, green and blue even when the selected theme is neon');
 await elements.code.onchange();sent.pop();sent.pop();
 for(const id of ['name','tagline','contact','ssid','password','bridge','token','city'])document.getElementById(id).value='';
 elements.name.value='测试';document.getElementById('lta_api_key').value='test-lta-key';await elements.save.onclick();
 assert.equal(JSON.parse(sent[1].options.body).lta_api_key,'test-lta-key');assert.equal(elements.lta_api_key.value,'');assert.equal(elements.lta_key_status.textContent,'LTA 密钥已配置');
 assert.equal(JSON.parse(sent[1].options.body).name,'测试');
 assert.equal(elements.password.value,'');
 elements.lta_api_key.value='bad\\r\\nHeader';let countBefore=sent.length;await elements.save.onclick();assert.equal(sent.length,countBefore);elements.lta_api_key.value='';
 document.getElementById('bus_stops').value='00481 | Test Stop\\n82009 | Eunos Int';
 await elements.save.onclick();
 assert.equal(JSON.parse(sent[2].options.body).bus_stops[0].code,'00481');
 sent.pop();
 globalThis.isSecureContext=false;elements.locate.onclick();
 assert.match(elements.status.textContent,/安全定位/);
 elements.psi_region.value='auto';document.getElementById('latitude').value='1.42';document.getElementById('longitude').value='103.82';
 await elements.save.onclick();
 assert.equal(JSON.parse(sent[2].options.body).latitude,1.42);
 assert.equal(JSON.parse(sent[2].options.body).longitude,103.82);
 assert.equal(JSON.parse(sent[2].options.body).psi_region,'auto');
 elements.psi_region.value='central';elements.bus_stops.value='82009 | Eunos Int';
 elements.wifi_profile.disabled=false;elements.wifi_profile.value='outside';elements.wifi_profile.onchange();
 assert.equal(elements.ssid.disabled,true);assert.equal(elements.password.disabled,false);
 await elements.save.onclick();
 assert.equal(JSON.parse(sent[sent.length-1].options.body).wifi_profile,'outside');
 assert.equal(elements.save_status.textContent,'设置已保存。');
 elements.language.value='1';elements.qr_url.value='https://example.test/profile';await elements.save.onclick();
 assert.equal(elements.save_status.textContent,'Settings saved.');
 assert.equal(JSON.parse(sent[sent.length-1].options.body).qr_url,'https://example.test/profile');
 assert.equal(JSON.parse(sent[sent.length-1].options.body).language,1);
 elements.language.value='0';
 elements.wifi_profile.value='custom';elements.wifi_profile.onchange();
 assert.equal(elements.ssid.disabled,false);assert.equal(elements.password.disabled,false);
 await elements.save.onclick();assert.equal(JSON.parse(sent[sent.length-1].options.body).wifi_profile,undefined);
 console.log('Settings upload size, RGB565, auth, Wi-Fi profile and save status: PASS');
})().catch(e=>{console.error(e);process.exitCode=1});
'''
        with tempfile.NamedTemporaryFile(suffix='.js',mode='w') as file:
            file.write(harness + script + checks);file.flush()
            result = subprocess.run(['node',file.name],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_android_phone_relay(self):
        script = (ROOT/'main/passport_settings.html').read_text().split('<script>',1)[1].split('</script>',1)[0]
        harness = """
const assert=require('assert');const elements={};const sent=[];let activePage=0;
global.document={getElementById(id){return elements[id]??={value:'',checked:false,textContent:''}}};
global.setTimeout=fn=>{fn();return 1};
let assistantOpen=true;global.fetch=async(path,options)=>{sent.push({path,options});const ok=assistantOpen||path!=='/relay';const j=!ok?{error:'Open the assistant on Passport first'}:path.includes('/v1/reply/')?{state:'done',text:'Hermes reply'}:{ok:true};return {ok,text:async()=>JSON.stringify(j),json:async()=>j}};
"""
        checks = """
(async()=>{
 document.getElementById('agent').value='hermes';document.getElementById('phone_relay').checked=true;
 document.getElementById('relay_url').value='http://100.64.0.10:4188';document.getElementById('token').value='test-token';
 document.getElementById('code').value='123456';document.getElementById('message').value='Hello';await elements.send.onclick();
 assert.equal(sent[0].path,'/relay');assert.equal(sent[1].path,'http://100.64.0.10:4188/v1/chat/hermes');
 assert.equal(sent[1].options.headers.Authorization,'Bearer test-token');
 const delivery=sent.find(s=>s.path==='/relay'&&JSON.parse(s.options.body).text==='Hermes reply');assert(delivery);
 assert.equal(delivery.options.headers['X-Passport-Code'],'123456');
 assistantOpen=false;sent.length=0;await elements.send.onclick();
 assert(!sent.some(s=>s.path.startsWith('http:')),'Inactive assistant must not send or poll through the phone');
 assert.equal(elements.send.disabled,false);
 console.log('Android phone relay: PASS');
})().catch(e=>{console.error(e);process.exitCode=1});
"""
        with tempfile.NamedTemporaryFile(suffix='.js', mode='w') as file:
            file.write(harness+script+checks);file.flush()
            result = subprocess.run(['node',file.name], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)

    def test_phone_radio_preserves_pcm_and_handles_backpressure(self):
        script=(ROOT/'main/passport_settings.html').read_text().split('<script>',1)[1].split('</script>',1)[0]
        harness="""
const assert=require('assert');const elements={};const packets=[];const controls=[];
global.document={getElementById(id){return elements[id]??={value:'',checked:false,textContent:''}}};
let full=true,conflict=false,cancelled=false;
const chunks=[new Uint8Array([1,2,3]),new Uint8Array([4,5,6,7]),new Uint8Array([8])];
global.fetch=async(path,options)=>{
 if(path.includes('/v1/radio'))return {ok:true,body:{getReader(){return {read:async()=>chunks.length?{done:false,value:chunks.shift()}:{done:true},cancel:async()=>{cancelled=true}}}}};
 if(options.headers['Content-Type']==='application/octet-stream'){
  assert.equal(options.headers['X-Passport-Code'],'123456');
  if(conflict)return {ok:false,status:409};
  if(full){full=false;return {ok:false,status:503}};
  packets.push(new Uint8Array(options.body));return {ok:true};
 }
 controls.push(JSON.parse(options.body));return {ok:true,text:async()=>'{"ok":true}'};
};
"""
        checks="""
(async()=>{
 document.getElementById('code').value='123456';
 const session={closed:false,remote:{base:'http://100.64.0.10:4188',headers:{Authorization:'Bearer test'}}};
 await pumpPhoneRadio(session,{station:0,generation:0x12345678},new AbortController());
 assert(cancelled);assert.equal(packets.length,3);
 const pcm=[];for(const packet of packets){assert.equal(new DataView(packet.buffer).getUint32(0,true),0x12345678);pcm.push(...packet.slice(4));}
 assert.deepEqual(pcm,[1,2,3,4,5,6,7,8]);
 assert.equal(controls[0].action,'radio_error');assert.equal(controls[0].generation,0x12345678);
 conflict=true;await assert.rejects(sendRadioFrame(session,1,new Uint8Array([1,2]),new AbortController().signal),/rejected/);
 const aborted=new AbortController();aborted.abort();await assert.rejects(sendRadioFrame(session,1,new Uint8Array([1,2]),aborted.signal),/cancelled/);
 console.log('Phone radio odd boundaries, backpressure, generation and cancellation: PASS');
})().catch(e=>{console.error(e);process.exitCode=1});
"""
        with tempfile.NamedTemporaryFile(suffix='.js',mode='w') as file:
            file.write(harness+script+checks);file.flush()
            result=subprocess.run(['node',file.name],capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_usage_oauth_setup_authorized_requests_and_safe_signin_link(self):
        script=(ROOT/'main/passport_settings.html').read_text().split('<script>',1)[1].split('</script>',1)[0]
        harness="""
const assert=require('assert');const elements={},sent=[];
function element(){return {value:'',checked:false,textContent:'',hidden:true,children:[],dataset:{},removeAttribute(name){delete this[name]},replaceChildren(){this.children=[];this.value=''},appendChild(child){this.children.push(child);if(!this.value)this.value=child.value}};}
global.document={getElementById(id){return elements[id]??=element()},createElement(){return element()}};
global.setTimeout=()=>1;global.clearTimeout=()=>{};
const id='a'.repeat(32);
global.fetch=async(path,options)=>{
 sent.push({path,options});
 let data=path.endsWith('/status')||path.endsWith('/login')?{state:'waiting',authUrl:'https://auth.openai.com/codex/device',userCode:'ABCD-1234'}:path.endsWith('/cancel')?{state:'cancelled'}:options.method==='POST'?{id,provider:'codex',name:'My OpenAI'}:{accounts:[{id,provider:'codex',name:'My OpenAI',state:'idle'}]};
 return {ok:true,json:async()=>data};
};
"""
        checks="""
(async()=>{
 el('bridge').value='http://192.168.1.2:4188';el('token').value='owner-token';el('usage_provider').value='codex';el('usage_name').value='My OpenAI';
 await elements.usage_add.onclick();assert.equal(elements.usage_accounts.value,id);
 assert.equal(sent[0].options.headers.Authorization,'Bearer owner-token');assert(!sent[0].path.includes('owner-token'));
 assert.deepEqual(JSON.parse(sent[0].options.body),{provider:'codex',name:'My OpenAI'});
 await elements.usage_login.onclick();assert.equal(elements.usage_auth_link.hidden,false);assert.equal(elements.usage_auth_link.href,'https://auth.openai.com/codex/device');assert(elements.usage_setup_status.textContent.includes('ABCD-1234'));
 showUsageSetup({state:'waiting',authUrl:'https://evil.example/auth'});assert.equal(elements.usage_auth_link.hidden,true);
 await elements.usage_cancel.onclick();assert.equal(elements.usage_auth_link.hidden,true);assert(!elements.usage_setup_status.textContent.includes('ABCD-1234'));
 console.log('Usage OAuth setup: authorized requests, safe provider link and cancellation PASS');
})().catch(e=>{console.error(e);process.exitCode=1});
"""
        with tempfile.NamedTemporaryFile(suffix='.js',mode='w') as file:
            file.write(harness+script+checks);file.flush()
            result=subprocess.run(['node',file.name],capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_phone_voice_forwarding_and_retry_ownership(self):
        source=(ROOT/'main/passport_settings.html').read_text()
        code=source[source.index('async function forwardPhoneVoice('):source.index('async function sendRadioFrame(')]
        harness=r'''
const assert=require('assert');let request={},acks=[],posts=0,failAck=false;
const session={remote:{base:'http://100.64.0.10:4188',headers:{Authorization:'Bearer test-token'}},closed:false};
async function relayControl(body){if(body.action==='voice_poll')return request;if(failAck){failAck=false;throw Error('ack lost');}acks.push(body);return {};}
global.fetch=async(url,options)=>{posts++;assert(url.startsWith(session.remote.base+'/v1/voice/'));assert.equal(options.headers.Authorization,'Bearer test-token');assert.deepEqual([...options.body],[1,2,3,4]);return {status:200,text:async()=>'{}'};};
'''
        checks=r'''
(async()=>{
 assert.equal(await forwardPhoneVoice(session),false);
 request={id:1,path:'/v1/voice/chunk?session='+('a'.repeat(32))+'&seq=0',type:'application/octet-stream',body:btoa(String.fromCharCode(1,2,3,4))};
 failAck=true;await assert.rejects(()=>forwardPhoneVoice(session));assert.equal(posts,1);
 assert(await forwardPhoneVoice(session));assert.equal(posts,1,'Lost acknowledgement must not duplicate remote upload');assert.equal(acks[0].id,1);
 request={...request,id:2,path:'/v1/chat/hermes'};await assert.rejects(()=>forwardPhoneVoice(session));assert.equal(posts,1,'Only voice endpoints may be forwarded');
 request={...request,id:3,path:'/v1/voice/start',body:btoa('x'.repeat(4097))};await forwardPhoneVoice(session);assert.equal(posts,1);assert.equal(acks.at(-1).status,502);
 session.closed=true;request={...request,id:4,body:btoa(String.fromCharCode(1,2,3,4))};let n=acks.length;await forwardPhoneVoice(session);assert.equal(acks.length,n,'Closed sessions must not acknowledge stale work');
})().catch(e=>{console.error(e);process.exitCode=1});
'''
        with tempfile.NamedTemporaryFile(suffix='.js',mode='w') as file:
            file.write(harness+code+checks);file.flush()
            result=subprocess.run(['node',file.name],capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_phone_service_start_usage_and_stop(self):
        script=(ROOT/'main/passport_settings.html').read_text().split('<script>',1)[1].split('</script>',1)[0]
        harness="""
const assert=require('assert');const elements={};const sent=[];let activePage=0,weatherGeneration=0,weatherCity='singapore',weatherFail=false;
global.document={getElementById(id){return elements[id]??={value:'',checked:false,textContent:''}}};
global.fetch=async(path,options)=>{
 sent.push({path,options});
 if(path.includes('/v1/weather')&&weatherFail)throw new Error('Weather offline');
 const data=path.includes('/v1/stations')?{stations:[{name:'YES 933',country:'SG'}]}:path.includes('/v1/usage')?{usage:[{provider:'codex',status:'ok',primary:{usedPercent:20}}]}:path.includes('/v1/weather')?{current:{temperature_2m:30,relative_humidity_2m:70,wind_speed_10m:5,weather_code:3}}:{playing:false,page:activePage,weather_generation:weatherGeneration,city:weatherCity};
 return {ok:true,json:async()=>data,text:async()=>JSON.stringify(data)};
};
"""
        checks="""
(async()=>{
 assert.throws(phoneRemote,/服务地址无效/);
 document.getElementById('relay_url').value='http://100.64.0.10:4188';
 assert.throws(phoneRemote,/设置码/);
 document.getElementById('code').value='123456';assert.throws(phoneRemote,/令牌/);
 document.getElementById('token').value='test-token';
 await startPhoneServices();assert(phoneSession);assert.equal(elements.relay_start.disabled,true);assert.equal(elements.relay_stop.disabled,false);
 const start=sent.find(s=>s.path==='/relay'&&JSON.parse(s.options.body).action==='start');assert.equal(JSON.parse(start.options.body).stations[0].name,'YES 933');
 await pause(30);assert(!sent.some(s=>s.path.includes('/v1/usage')),'Inactive usage must not poll');
 const weather=sent.find(s=>s.path==='/relay'&&JSON.parse(s.options.body).action==='weather');assert(weather);assert.equal(JSON.parse(weather.options.body).data.current.temperature_2m,30);
 const weatherCount=sent.filter(s=>s.path.includes('/v1/weather')).length;
 await pause(400);assert.equal(sent.filter(s=>s.path.includes('/v1/weather')).length,weatherCount,'Weather requests are bounded');
 weatherGeneration++;await pause(400);assert.equal(sent.filter(s=>s.path.includes('/v1/weather')).length,weatherCount+1,'Explicit refresh bypasses interval');
 weatherCity='penang';await pause(400);assert(sent.some(s=>s.path.endsWith('/v1/weather?city=penang')),'City changes refresh weather');
 weatherFail=true;weatherGeneration++;await pause(400);assert(phoneSession&&!phoneSession.closed,'Weather errors must not stop audio relay');
 weatherFail=false;
 activePage=3;await pause(450);
 const usage=sent.find(s=>s.path==='/relay'&&JSON.parse(s.options.body).action==='usage');assert(usage);assert.equal(JSON.parse(usage.options.body).data.usage[0].primary.usedPercent,20);
 activePage=0;phoneSession.usageAt=0;const usageCount=sent.filter(s=>s.path.includes('/v1/usage')).length;await pause(450);
 assert.equal(sent.filter(s=>s.path.includes('/v1/usage')).length,usageCount,'Leaving usage stops polling');
 assert.equal(sent[0].options.headers.Authorization,'Bearer test-token');assert.equal(start.options.headers['X-Passport-Code'],'123456');
 await stopPhoneServices();assert.equal(phoneSession,null);assert.equal(elements.relay_start.disabled,false);assert.equal(elements.relay_stop.disabled,true);
 console.log('Phone service validation, authenticated start, usage and stop: PASS');
})().catch(e=>{console.error(e);process.exitCode=1});
"""
        with tempfile.NamedTemporaryFile(suffix='.js',mode='w') as file:
            file.write(harness+script+checks);file.flush()
            result=subprocess.run(['node',file.name],capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

if __name__ == '__main__': unittest.main()
