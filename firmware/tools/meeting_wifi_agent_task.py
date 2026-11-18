"""Keep a non-secret operation receipt across Agent restarts.

The caller must durably save the supplied dictionary before returning from
``save`` and obtain the management credential from its secure store. Once a
send has been attempted, only query the same operation; a timeout proves
neither receipt nor failure and must never cause a new operation ID.
"""
from meeting_wifi_command import encode_query, encode_switch, status_text

_TERMINAL = frozenset((19, 20))


async def begin(operation_id, ssid, password, save, send):
    packet = encode_switch(operation_id, ssid, password)
    task = {'version': 1, 'operation_id': operation_id.hex(), 'attempted': True}
    save(task.copy())
    result = await send(packet)
    status_text(result)
    if result in _TERMINAL:
        task['result'] = result
        save(task.copy())
    return result


async def resume(task, save, send):
    if not isinstance(task, dict) or task.get('version') != 1 or task.get('attempted') is not True:
        raise ValueError('invalid persisted operation')
    try:
        operation_id = bytes.fromhex(task['operation_id'])
        packet = encode_query(operation_id)
    except (KeyError, TypeError, ValueError) as exc:
        raise ValueError('invalid persisted operation') from exc
    if 'result' in task:
        status_text(task['result'])
        if task['result'] not in _TERMINAL:
            raise ValueError('invalid terminal result')
        return task['result']
    result = await send(packet)
    status_text(result)
    if result in _TERMINAL:
        task['result'] = result
        save(task.copy())
    return result
