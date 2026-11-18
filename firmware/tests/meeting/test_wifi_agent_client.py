import asyncio
import io
import sys
import tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from meeting_wifi_agent_client import switch_or_resume, drive, abandon_unknown, discover, read_wifi_password, operation_exit_code
from meeting_wifi_agent_store import OperationStore
from meeting_wifi_agent_transport import BeforeSendError

async def main():
    assert operation_exit_code('switch',19)==0
    assert operation_exit_code('resume',20)==2
    assert operation_exit_code('switch',22)==3
    assert operation_exit_code('switch',23)==4
    assert operation_exit_code('abandon',22)==0
    assert operation_exit_code('abandon',19)!=0
    with tempfile.TemporaryDirectory() as directory:
        packets=[];events=[]
        async def before_send_retry(packet):
            packets.append(packet)
            if len(packets)==1:raise BeforeSendError('not connected')
            return 24 if packet[4]==3 else 19
        async def immediate_retry(_):pass
        with OperationStore(Path(directory)/'before-send.sqlite') as store:
            assert await drive(store,'device-a','ssid','password',before_send_retry,events.append,immediate_retry,lambda:b'\x07'*16)==19
            assert store.load('device-a') is None
        assert packets[0]==packets[1] and packets[0][4]==1
        assert packets[2]==b'MWC1\x03'+b'\x07'*16
        assert None in events and 'confirm_pending' not in events
    with tempfile.TemporaryDirectory() as directory:
        events=[]; attempts=[0]
        async def sender(packet):
            if packet[4]==3:
                attempts[0]+=1
                if attempts[0]<3:raise TimeoutError('lost ACK')
                return 24
            return 19
        async def immediate(_):pass
        with OperationStore(Path(directory)/'retry.sqlite') as store:
            assert await drive(store,'device-a','ssid','password',sender,events.append,immediate,lambda:b'\x05'*16)==19
            assert store.load('device-a') is None
        assert events.count(19)==1 and events.count('confirm_pending')==2
    with tempfile.TemporaryDirectory() as directory:
        events=[];ack_attempts=[0]
        async def ack_before_send(packet):
            if packet[4]==3:
                ack_attempts[0]+=1
                if ack_attempts[0]==1:raise BeforeSendError('not connected')
                return 24
            return 19
        with OperationStore(Path(directory)/'ack-before-send.sqlite') as store:
            assert await drive(store,'device-a','ssid','password',ack_before_send,events.append,immediate,lambda:b'\x08'*16)==19
        assert events==[19,'confirm_pending']
    class Scanner:
        @staticmethod
        async def discover(**kwargs):
            assert kwargs == {'timeout':1,'return_adv':True}
            return {'device':'advertisement'}
    class FakeBleak:BleakScanner=Scanner
    assert await discover(FakeBleak)=={'device':'advertisement'}
    assert read_wifi_password(True,io.StringIO('  password  \r\n'))=='  password  '
    assert read_wifi_password(True,io.StringIO('\n'))==''
    try:read_wifi_password(True,io.StringIO(''))
    except ValueError:pass
    else:raise AssertionError('missing password input was accepted')
    class StalledScanner:
        @staticmethod
        async def discover(**kwargs):await asyncio.Event().wait()
    class StalledBleak:BleakScanner=StalledScanner
    try:await discover(StalledBleak,timeout=.02)
    except asyncio.TimeoutError:pass
    else:raise AssertionError('stalled scan was not bounded')
    with tempfile.TemporaryDirectory() as directory:
        path=Path(directory)/'abandon.sqlite'; op='04'*16; packets=[]
        async def unknown(packet):packets.append(packet);return 22
        async def waiting(packet):packets.append(packet);return 17
        async def lost(packet):raise TimeoutError('lost query')
        with OperationStore(path) as store:
            store.save('device-a',{'version':1,'operation_id':op,'attempted':True})
            try:await abandon_unknown(store,'device-a','05'*16,unknown)
            except ValueError:pass
            else:raise AssertionError('wrong operation accepted')
            try:await abandon_unknown(store,'device-a',op,lost)
            except TimeoutError:pass
            else:raise AssertionError('lost query accepted')
            assert store.load('device-a')['operation_id']==op
            assert await abandon_unknown(store,'device-a',op,waiting)==17
            assert store.load('device-a')['operation_id']==op
            assert await abandon_unknown(store,'device-a',op,unknown)==22
            assert store.load('device-a') is None
            assert packets==[b'MWC1\x02'+bytes.fromhex(op)]*2
    with tempfile.TemporaryDirectory() as directory:
        path=Path(directory)/'tasks.sqlite';packets=[]
        async def sender(packet):
            packets.append(packet)
            if len(packets)==1:raise TimeoutError('lost response')
            return 19
        with OperationStore(path) as store:
            try:await switch_or_resume(store,'device-a','ssid','password',sender,lambda:b'\x01'*16)
            except TimeoutError:pass
            else:raise AssertionError('lost response accepted')
        with OperationStore(path) as store:
            assert await switch_or_resume(store,'device-a',None,None,sender)==19
            assert packets[0].startswith(b'MWC1\x01')
            assert packets[1]==b'MWC1\x02'+b'\x01'*16
            assert store.load('device-a')['result']==19
    with tempfile.TemporaryDirectory() as directory:
        packets=[];status=[]
        async def sender(packet):
            packets.append(packet)
            if len(packets)==1:raise TimeoutError('lost response')
            if len(packets)==2:return 17
            return 24 if packet[4]==3 else 19
        async def sleep(_):pass
        with OperationStore(Path(directory)/'tasks.sqlite') as store:
            assert await drive(store,'device-a','ssid','password',sender,status.append,sleep,lambda:b'\x01'*16)==19
        assert packets[0].startswith(b'MWC1\x01')
        assert packets[1:3]==[b'MWC1\x02'+b'\x01'*16]*2
        assert packets[3]==b'MWC1\x03'+b'\x01'*16
        assert 17 in status and 19 in status
    with tempfile.TemporaryDirectory() as directory:
        packets=[];status=[]
        async def unknown_sender(packet):
            packets.append(packet)
            if len(packets)==1:raise TimeoutError('response lost')
            return 22
        async def sleep(_):await asyncio.sleep(0)
        with OperationStore(Path(directory)/'tasks.sqlite') as store:
            assert await drive(store,'device-a','ssid','password',unknown_sender,status.append,sleep,lambda:b'\x02'*16)==22
            assert store.load('device-a')['operation_id']==(b'\x02'*16).hex()
        assert len(packets)==2 and packets[1]==b'MWC1\x02'+b'\x02'*16
        assert status[-1]==22
        resumed=[]
        async def resume_sender(packet):
            resumed.append(packet)
            return 24 if packet[4]==3 else 20
        with OperationStore(Path(directory)/'tasks.sqlite') as store:
            assert await drive(store,'device-a',None,None,resume_sender,status.append,sleep)==20
            assert store.load('device-a') is None
        assert resumed==[b'MWC1\x02'+b'\x02'*16,b'MWC1\x03'+b'\x02'*16]
    with tempfile.TemporaryDirectory() as directory:
        path=Path(directory)/'tasks.sqlite'; packets=[]
        async def lost_ack(packet):
            packets.append(packet)
            if packet[4]==3:raise TimeoutError('ack response lost')
            return 19
        events=[]
        async def interrupted(_):raise InterruptedError('agent stopped')
        with OperationStore(path) as store:
            try:await drive(store,'device-a','ssid','password',lost_ack,events.append,interrupted,lambda:b'\x03'*16)
            except InterruptedError:pass
            else:raise AssertionError('expected interrupted retry')
            assert store.load('device-a')['result']==19
            assert events==[19,'confirm_pending']
        async def acknowledge(packet):
            packets.append(packet)
            return 24
        with OperationStore(path) as store:
            assert await drive(store,'device-a',None,None,acknowledge,lambda _:None)==19
            assert store.load('device-a') is None
        assert packets==[b'MWC1\x01'+b'\x03'*16+b'\x04\x08ssidpassword',
                         b'MWC1\x03'+b'\x03'*16,b'MWC1\x03'+b'\x03'*16]
asyncio.run(main())
