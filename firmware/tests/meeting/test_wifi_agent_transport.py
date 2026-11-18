import asyncio
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from meeting_wifi_agent_transport import send_request, BeforeSendError
from meeting_provision import SERVICE_UUID
from types import SimpleNamespace

class Security:
    def __init__(self,version,user,password,verbose):
        assert version==1 and user=='meeting' and password=='01'*32 and not verbose
class SecurityModule: Security2=Security
class Transport:
    def __init__(self,*args):self.closed=False
    async def connect(self,name):
        assert name=='PASSPORT-SETUP'
        assert list((await Bleak.BleakScanner.discover(return_adv=True)).keys())==['a']
    async def disconnect(self):self.closed=True
class TransportModule: Transport_BLE=Transport
class Scanner:
    @staticmethod
    async def discover(*args,**kwargs):return {}
class Bleak: BleakScanner=Scanner
device=SimpleNamespace(name='PASSPORT-SETUP')
adv=SimpleNamespace(service_uuids=[SERVICE_UUID],local_name='PASSPORT-SETUP')
discovery={'a':(device,adv)}
async def main():
    called=[]
    async def send(transport,security,payload):called.append(payload)
    async def poll(transport,security,payload):
        if payload==b'\x04':return b'\x13'
        assert payload==b'\x05';called.append(b'ack');return b'\x00'
    packet=b'MWC1\x02'+b'\x01'+bytes(15)
    result=await send_request(packet,b'\x01'*32,'PASSPORT-SETUP',SecurityModule,TransportModule,discovery=discovery,bleak_module=Bleak,send=send,poll=poll)
    assert result==19
    assert called==[packet,b'ack']
    stale=SimpleNamespace(name='soundcore Work')
    partial_adv=SimpleNamespace(service_uuids=[SERVICE_UUID],local_name=None)
    result=await send_request(packet,b'\x01'*32,'PASSPORT-SETUP',SecurityModule,TransportModule,discovery={'a':(stale,partial_adv)},bleak_module=Bleak,send=send,poll=poll)
    assert result==19 and stale.name=='PASSPORT-SETUP'
    wrong_adv=SimpleNamespace(service_uuids=[SERVICE_UUID],local_name='other-device')
    try:await send_request(packet,b'\x01'*32,'PASSPORT-SETUP',SecurityModule,TransportModule,discovery={'a':(stale,wrong_adv)},bleak_module=Bleak,send=send,poll=poll)
    except RuntimeError:pass
    else:raise AssertionError('conflicting current advertisement accepted')
    try:await send_request(packet,b'\0'*32,'PASSPORT-SETUP',SecurityModule,TransportModule,discovery=discovery,bleak_module=Bleak,send=send)
    except ValueError:pass
    else:raise AssertionError('zero secret accepted')
    try:await send_request(packet,b'\x01'*32,'PASSPORT-SETUP',SecurityModule,TransportModule,discovery={'a':(device,adv),'b':(device,adv)},bleak_module=Bleak,send=send)
    except RuntimeError:pass
    else:raise AssertionError('ambiguous devices accepted')
    original=Bleak.BleakScanner.discover
    class BrokenTransport(Transport):
        async def connect(self,name):raise RuntimeError('connection failed')
    class BrokenModule: Transport_BLE=BrokenTransport
    try:await send_request(packet,b'\x01'*32,'PASSPORT-SETUP',SecurityModule,BrokenModule,discovery=discovery,bleak_module=Bleak,send=send)
    except BeforeSendError:pass
    else:raise AssertionError('connection failure swallowed')
    assert Bleak.BleakScanner.discover is original
    class HangingTransport(Transport):
        instance=None
        def __init__(self,*args):super().__init__(*args);HangingTransport.instance=self
        async def connect(self,name):await asyncio.Event().wait()
    class HangingModule: Transport_BLE=HangingTransport
    try:
        await send_request(packet,b'\x01'*32,'PASSPORT-SETUP',SecurityModule,HangingModule,
            discovery=discovery,bleak_module=Bleak,send=send,timeout=.02)
    except asyncio.TimeoutError:pass
    else:raise AssertionError('hanging connection was not bounded')
    assert Bleak.BleakScanner.discover is original and HangingTransport.instance.closed
asyncio.run(main())
print('management agent transport contract: PASS')
