"""Bind one device's durable operation to the authenticated BLE transport."""
import argparse
import asyncio
import getpass
import secrets
import socket
import subprocess
import sys
import time
from meeting_wifi_agent_task import begin, resume
from meeting_wifi_command import encode_ack, encode_query
from meeting_wifi_agent_transport import BeforeSendError


async def discover(bleak_module, scan_seconds=1, timeout=6):
    """Bound an individual scan even if the platform scanner stalls."""
    return await asyncio.wait_for(
        bleak_module.BleakScanner.discover(timeout=scan_seconds, return_adv=True), timeout)


def read_wifi_password(from_stdin, stream=None):
    """Read a password without requiring a terminal or exposing CLI arguments."""
    if not from_stdin:
        return getpass.getpass('Wi-Fi password: ')
    line = (stream or sys.stdin).readline()
    if not line:
        raise ValueError('Wi-Fi password input is missing')
    # Keep spaces in the password; remove only the line terminator.
    return line.removesuffix('\n').removesuffix('\r')


def operation_exit_code(action, status):
    """Only an actual switch success or confirmed abandonment exits zero."""
    if action == 'abandon':
        return 0 if status == 22 else 3
    return {19: 0, 20: 2, 22: 3, 23: 4}.get(status, 3)


async def abandon_unknown(store, device_id, operation_id, send):
    """Explicitly abandon only the identified receipt after an authenticated query."""
    saved = store.load(device_id)
    if not saved or saved['operation_id'] != operation_id or 'result' in saved:
        raise ValueError('matching unresolved operation required')
    status = await send(encode_query(bytes.fromhex(operation_id)))
    if status != 22:
        return status
    store.abandon_unknown(device_id, operation_id, True, status)
    return status


async def switch_or_resume(store, device_id, ssid, password, send, new_id=lambda: secrets.token_bytes(16)):
    current = store.load(device_id)
    save = lambda task: store.save(device_id, task)
    if current is not None:
        if ssid is not None or password is not None:
            raise ValueError('existing operation must be resolved first')
        return await resume(current, save, send)
    if ssid is None or password is None:
        raise ValueError('no saved operation to resume')
    return await begin(new_id(), ssid, password, save, send)


async def drive(store, device_id, ssid, password, send, report, sleep=asyncio.sleep,
                new_id=lambda: secrets.token_bytes(16)):
    """Submit once, then reconnect and query; never replay an uncertain write."""
    class TransportUnavailable(Exception):
        pass

    async def guarded_send(packet):
        while True:
            try:
                return await send(packet)
            except BeforeSendError:
                # This process still holds the original packet; retry only when
                # transport proves that no management frame was attempted.
                # A retry while querying or acknowledging a sent operation
                # must not suggest that the operation has not been submitted.
                report(None if packet[4]==1 else 'confirm_pending')
                await sleep(2)
            except Exception as exc:
                raise TransportUnavailable from exc

    first = True
    reported_terminal = None
    while True:
        try:
            result = await switch_or_resume(store, device_id, ssid if first else None,
                                            password if first else None, guarded_send, new_id)
            if result not in (19, 20) or result != reported_terminal:
                report(result)
                if result in (19, 20):
                    reported_terminal = result
            if result in (19, 20):
                # Terminal status has already been durably saved locally;
                # an uncertain ACK is retried with the same operation ID.
                saved=store.load(device_id)
                if await guarded_send(encode_ack(bytes.fromhex(saved['operation_id']))) != 24:
                    raise TransportUnavailable()
                store.release(device_id,saved['operation_id'],True)
                return result
            # Unknown is not success or failure. Stop polling and retain the
            # receipt so a later resume uses the same operation ID.
            if result in (22,23):
                return result
        except TransportUnavailable:
            pending = store.load(device_id)
            # A saved operation may have reached the device even when its
            # response was lost. Do not claim it was received or succeeded.
            report('confirm_pending' if pending else None)
        first = False
        await sleep(2)


async def _cli(args):
    from meeting_provision import load_idf_modules, unique_setup_discovery
    from meeting_wifi_agent_store import OperationStore
    from meeting_wifi_agent_transport import send_request
    from meeting_wifi_command import status_text
    import bleak

    security, transport = load_idf_modules()
    try:
        encoded = subprocess.run(['/usr/bin/security', 'find-generic-password', '-s',
                                  'meeting-recorder-management', '-a', args.device, '-w'],
                                 check=True, capture_output=True, text=True).stdout.strip()
        secret = bytes.fromhex(encoded)
    except (subprocess.SubprocessError, ValueError) as exc:
        raise RuntimeError('management credential unavailable in Keychain') from exc
    if len(secret) != 32 or not any(secret):
        raise RuntimeError('invalid management credential')
    # Do not create an attempted operation merely because the device was not
    # advertising. Once transport begins, however, failure is ambiguous.
    discovery = None
    started = time.monotonic()
    warned = False
    print('等待设备空闲或连接', flush=True)
    while discovery is None:
        try:
            found = await discover(bleak)
            unique_setup_discovery(found, args.name, allow_stale_cache=True)
            discovery = found
        except (RuntimeError, asyncio.TimeoutError):
            if not warned and time.monotonic()-started >= 30:
                print('仍在等待设备空闲或连接', flush=True)
                warned = True
    initial = [discovery]

    async def send(packet):
        try:
            discovery = initial.pop() if initial else await discover(bleak)
            unique_setup_discovery(discovery, args.name, allow_stale_cache=True)
        except Exception as exc:
            raise BeforeSendError('device discovery failed before management send') from exc
        print('正在认证设备', flush=True)
        return await send_request(packet, secret, args.name, security, transport,
                                  discovery=discovery, bleak_module=bleak, peer_name=socket.gethostname())

    with OperationStore(args.store) as store:
        if args.action == 'abandon':
            status = await abandon_unknown(store, args.device, args.operation_id, send)
            if status == 22:
                print('已放弃未知结果；下次换网将使用新的操作 ID', flush=True)
            else:
                print('未放弃：'+status_text(status), flush=True)
            return operation_exit_code(args.action, status)
        if args.action == 'switch':
            password = read_wifi_password(args.password_stdin)
            ssid = args.ssid
        else:
            ssid = password = None
        last = [object()]
        def report(result):
            if result != last[0]:
                message = ('等待设备空闲或连接' if result is None else
                           '结果待确认' if result == 'confirm_pending' else status_text(result))
                print(message, flush=True)
                if result == 22:
                    pending = store.load(args.device)
                    if pending:
                        print('待查操作 ID：'+pending['operation_id'], flush=True)
                last[0] = result
        result = await drive(store, args.device, ssid, password, send, report)
        if result in (19, 20):
            print('结果已确认', flush=True)
        return operation_exit_code(args.action, result)


def main():
    parser = argparse.ArgumentParser(description='Switch a nearby passport Wi-Fi or query an existing operation')
    parser.add_argument('--device', required=True, help='provisioned device identity')
    parser.add_argument('--name', default='PASSPORT-SETUP', help='BLE management advertisement name')
    parser.add_argument('--store', required=True, help='path to local operation database')
    actions = parser.add_subparsers(dest='action', required=True)
    switch = actions.add_parser('switch')
    switch.add_argument('--ssid', required=True)
    switch.add_argument('--password-stdin', action='store_true', help='read Wi-Fi password from standard input')
    actions.add_parser('resume')
    abandon = actions.add_parser('abandon', help='explicitly abandon an unknown operation result')
    abandon.add_argument('--operation-id', required=True, help='exact pending operation ID')
    args = parser.parse_args()
    raise SystemExit(asyncio.run(_cli(args)))


if __name__ == '__main__':
    main()
