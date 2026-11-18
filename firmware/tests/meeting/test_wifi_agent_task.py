import asyncio
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from meeting_wifi_agent_task import begin, resume

async def main():
    store={};sent=[];op=b'\x01'*16
    def save(value): store.clear();store.update(value)
    async def transport(packet):
        sent.append(packet)
        if len(sent)==1: raise TimeoutError('response lost')
        return 19
    try: await begin(op,'wifi','password',save,transport)
    except TimeoutError: pass
    else: raise AssertionError('timeout must not imply success')
    assert store['operation_id']==op.hex() and store['attempted'] is True
    assert 'password' not in str(store)
    assert await resume(dict(store),save,transport)==19
    assert sent[1]==b'MWC1\x02'+op
    assert store['result']==19
    assert await resume(dict(store),save,transport)==19 and len(sent)==2
    store.clear();pending={'version':1,'operation_id':op.hex(),'attempted':True}
    async def unknown(packet):return 22
    assert await resume(pending,save,unknown)==22 and 'result' not in store
asyncio.run(main())
