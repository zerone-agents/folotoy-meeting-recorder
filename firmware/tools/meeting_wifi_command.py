"""Canonical binary management commands. Call only inside an authenticated session.

Do not log returned switch packets: they contain the Wi-Fi password.
"""
import hashlib

STATUS={17:'等待设备空闲',18:'正在连接新网络',19:'切换成功',20:'切换失败，已恢复原配置',21:'正在确认配置',22:'结果未知或已过期',23:'需要维护'}

def status_text(code):
    if code not in STATUS:
        raise ValueError('unknown management status')
    return STATUS[code]

def _id(value):
    if not isinstance(value,bytes) or len(value)!=16 or not any(value):
        raise ValueError('invalid operation ID')
    return value

def _network(ssid,password):
    if not isinstance(ssid,str) or not isinstance(password,str):
        raise ValueError('network fields must be strings')
    try:
        name=ssid.encode('utf-8'); secret=password.encode('utf-8')
    except UnicodeError as exc:
        raise ValueError('invalid UTF-8') from exc
    if not 1<=len(name)<=32 or len(secret)>63 or b'\0' in name or b'\0' in secret:
        raise ValueError('invalid network fields')
    return bytes((len(name),len(secret)))+name+secret

def encode_switch(operation_id,ssid,password):
    return b'MWC1\x01'+_id(operation_id)+_network(ssid,password)

def encode_query(operation_id):
    return b'MWC1\x02'+_id(operation_id)

def encode_ack(operation_id):
    return b'MWC1\x03'+_id(operation_id)

def request_digest(operation_id,ssid,password):
    return hashlib.sha256(b'MWIFI1\0'+_id(operation_id)+_network(ssid,password)).digest()
