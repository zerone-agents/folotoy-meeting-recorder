import hashlib
import sys
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from meeting_wifi_command import encode_switch,encode_query,request_digest,status_text

operation=bytes.fromhex('01'+'00'*15)
packet=encode_switch(operation,'net','pass')
assert packet==b'MWC1\x01'+operation+b'\x03\x04netpass'
assert encode_query(operation)==b'MWC1\x02'+operation
assert request_digest(operation,'net','pass')==hashlib.sha256(b'MWIFI1\x00'+operation+b'\x03\x04netpass').digest()
assert request_digest(operation,'net','pass')!=request_digest(operation,'net','Pass')
assert status_text(19)=='切换成功' and status_text(22)=='结果未知或已过期'
try:status_text(2)
except ValueError:pass
else:raise AssertionError('provisioning receipt misread as management result')
for args in [(bytes(16),'net','pass'),(operation,'','pass'),(operation,'a'*33,'pass'),(operation,'net','a'*64),(operation,'net\x00','pass')]:
    try:encode_switch(*args)
    except ValueError:pass
    else:raise AssertionError('invalid request accepted')
print('agent wifi command format: PASS')
