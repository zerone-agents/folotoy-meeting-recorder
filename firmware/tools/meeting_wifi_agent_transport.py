"""Send a management request over ESP-IDF Security 2.

Caller must provide a fresh discovery snapshot and retain the operation until
it queries a durable result. Frame receipt is not completion. ESP-IDF scans by
name, so this module serializes its own scanner overrides; other scanner users
in the same process must also be serialized externally.
"""
import asyncio
from meeting_provision import SERVICE_UUID, send_frames, command, unique_setup_discovery
_scan_lock=asyncio.Lock()

class BeforeSendError(RuntimeError):
    """The connection failed before any management payload was sent."""

async def send_request(payload,secret,device_name,security_module,transport_module,*,discovery,bleak_module,send=send_frames,poll=command,sleep=asyncio.sleep,max_polls=30,timeout=60,disconnect_timeout=5,peer_name=None):
    if not isinstance(payload,bytes) or not 21<=len(payload)<=118 or payload[:4]!=b'MWC1':
        raise ValueError('invalid management message')
    if not isinstance(secret,bytes) or len(secret)!=32 or not any(secret):
        raise ValueError('invalid management credential')
    if not isinstance(device_name,str) or not device_name:
        raise ValueError('invalid target name')
    # CoreBluetooth may return a name cached from an older firmware when the
    # current advertisement omits it. Selection is still unique by service;
    # Security 2 authenticates the target before any management command.
    selected=unique_setup_discovery(discovery,device_name,allow_stale_cache=True)
    security=security_module.Security2(1,'meeting',secret.hex(),False)
    transport=transport_module.Transport_BLE(SERVICE_UUID,{'prov-session':'ff51','meeting-config':'ff52'})
    async def exchange():
        async with _scan_lock:
            scanner=bleak_module.BleakScanner
            original=scanner.discover
            async def discover_selected(*args,**kwargs):
                if not kwargs.get('return_adv'):
                    raise RuntimeError('unexpected scan contract')
                return selected
            scanner.discover=discover_selected
            try:
                try:
                    await transport.connect(device_name)
                except Exception as exc:
                    raise BeforeSendError('connection failed before management send') from exc
            finally:
                scanner.discover=original
        await send(transport,security,payload)
        if peer_name:
            name=''.join(c for c in peer_name if ' '<=c<='~').strip()[:32]
            if name:
                # Older firmware can reject the optional display hint.
                await poll(transport,security,b'\x06'+name.encode('ascii'))
        for _ in range(max_polls):
            result=await poll(transport,security,b'\x04')
            if len(result)!=1:
                raise RuntimeError('invalid device status')
            if 16<=result[0]<=31:
                # This acknowledges the response frame only. It does not
                # authorize erasing the durable transaction result.
                try:
                    await poll(transport,security,b'\x05')
                except (OSError, RuntimeError, TimeoutError):
                    pass
                return result[0]
            if result!=b'\x00':
                raise RuntimeError('device rejected management request')
            await sleep(1)
        raise TimeoutError('device result not confirmed')
    try:
        # BLE backends may hang on connect, write or read. Bound the whole
        # exchange; cancellation still restores the scanner override below.
        return await asyncio.wait_for(exchange(),timeout)
    finally:
        await asyncio.wait_for(transport.disconnect(),disconnect_timeout)
