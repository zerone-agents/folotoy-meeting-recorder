import sys
import tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from meeting_wifi_agent_store import OperationStore

with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)/'pending.sqlite'
    task={'version':1,'operation_id':'01'*16,'attempted':True}
    with OperationStore(path) as store:
        store.save('device-a',task)
        for explicit,status in ((False,22),(True,17)):
            try:store.abandon_unknown('device-a',task['operation_id'],explicit,status)
            except ValueError:pass
            else:raise AssertionError('unconfirmed abandonment accepted')
        try:store.save('device-a',dict(task,operation_id='02'*16))
        except ValueError:pass
        else:raise AssertionError('pending operation was overwritten')
    with OperationStore(path) as store:
        assert store.load('device-a')==task
        assert store.load('device-b') is None
        store.save('device-a',dict(task,result=19))
    with OperationStore(path) as store:
        assert store.load('device-a')['result']==19
        try:store.abandon_unknown('device-a',task['operation_id'],True,22)
        except ValueError:pass
        else:raise AssertionError('terminal operation was abandoned without ACK')
        try:store.release('device-a',task['operation_id'],False)
        except ValueError:pass
        else:raise AssertionError('unacknowledged operation was removed')
        store.release('device-a',task['operation_id'],True)
        store.save('device-a',dict(task,operation_id='02'*16))
