"""Durable, non-secret receipts for device management operations."""
import os
import sqlite3


class OperationStore:
    def __init__(self, path):
        fd = os.open(os.fspath(path), os.O_RDWR | os.O_CREAT | getattr(os, 'O_NOFOLLOW', 0), 0o600)
        os.close(fd)
        self.db = sqlite3.connect(os.fspath(path), isolation_level=None)
        self.db.execute('PRAGMA synchronous=FULL')
        self.db.execute('CREATE TABLE IF NOT EXISTS operations (device TEXT PRIMARY KEY, operation TEXT NOT NULL, result INTEGER)')

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.db.close()

    def load(self, device):
        row = self.db.execute('SELECT operation, result FROM operations WHERE device=?', (device,)).fetchone()
        if row is None:
            return None
        result = {'version': 1, 'operation_id': row[0], 'attempted': True}
        if row[1] is not None:
            result['result'] = row[1]
        return result

    def save(self, device, task):
        if not isinstance(device, str) or not device or not isinstance(task, dict):
            raise ValueError('invalid device or operation')
        op = task.get('operation_id')
        if task.get('version') != 1 or task.get('attempted') is not True or not isinstance(op, str):
            raise ValueError('invalid operation')
        try:
            decoded = bytes.fromhex(op)
        except ValueError as exc:
            raise ValueError('invalid operation ID') from exc
        if len(decoded) != 16 or not any(decoded) or op != decoded.hex():
            raise ValueError('invalid operation ID')
        result = task.get('result')
        if result is not None and result not in (19, 20):
            raise ValueError('invalid terminal result')
        self.db.execute('BEGIN IMMEDIATE')
        try:
            previous = self.load(device)
            if previous and (previous['operation_id'] != op or
                             ('result' in previous and previous['result'] != result)):
                raise ValueError('operation already recorded')
            self.db.execute('INSERT INTO operations(device,operation,result) VALUES(?,?,?) '
                            'ON CONFLICT(device) DO UPDATE SET result=excluded.result', (device, op, result))
            self.db.execute('COMMIT')
        except BaseException:
            self.db.execute('ROLLBACK')
            raise

    def release(self, device, operation_id, acknowledged):
        """Forget only a terminal result acknowledged by the device."""
        if acknowledged is not True:
            raise ValueError('device acknowledgement required')
        self.db.execute('BEGIN IMMEDIATE')
        try:
            previous = self.load(device)
            if not previous or previous['operation_id'] != operation_id or 'result' not in previous:
                raise ValueError('terminal operation not found')
            self.db.execute('DELETE FROM operations WHERE device=? AND operation=?', (device, operation_id))
            self.db.execute('COMMIT')
        except BaseException:
            self.db.execute('ROLLBACK')
            raise

    def abandon_unknown(self, device, operation_id, explicitly_abandoned, observed_status):
        """Forget an explicitly abandoned receipt only after a fresh unknown result."""
        if explicitly_abandoned is not True or observed_status != 22:
            raise ValueError('explicit abandonment and unknown device result required')
        self.db.execute('BEGIN IMMEDIATE')
        try:
            previous = self.load(device)
            if not previous or previous['operation_id'] != operation_id or 'result' in previous:
                raise ValueError('matching unresolved operation required')
            self.db.execute('DELETE FROM operations WHERE device=? AND operation=?', (device, operation_id))
            self.db.execute('COMMIT')
        except BaseException:
            self.db.execute('ROLLBACK')
            raise
