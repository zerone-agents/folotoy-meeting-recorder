#!/usr/bin/env python3
"""Security 2 BLE transport for meeting configuration.

Durable receipt confirms configuration storage, not network connectivity.
"""
import argparse
import asyncio
import email.utils
import getpass
import hashlib
import json
import os
from pathlib import Path
import re
import ssl
import sys
import time
import urllib.request

from meeting_config_frames import MAX_CONFIG_BYTES, frames

SERVICE_UUID = "1775244d-6b43-439b-877c-060f2d9bed07"


def review_values(payload):
    def unique(pairs):
        values = {}
        for key, value in pairs:
            if key in values:
                raise ValueError("duplicate configuration key")
            values[key] = value
        return values

    config = json.loads(payload, object_pairs_hook=unique)
    if not isinstance(config, dict):
        raise ValueError("configuration must be an object")
    fingerprint = config.get("caSha256")
    utc = config.get("utc")
    if (not isinstance(fingerprint, str) or not re.fullmatch(r"[0-9a-f]{64}", fingerprint)
            or not isinstance(utc, int) or isinstance(utc, bool)
            or not 1735689600 <= utc <= 4102444800):
        raise ValueError("invalid review values")
    certificate = ssl.PEM_cert_to_DER_cert(config["caPem"])
    if hashlib.sha256(certificate).hexdigest() != fingerprint:
        raise ValueError("CA fingerprint does not match certificate")
    return fingerprint.upper(), utc


def check_freshness(utc, now=None):
    """Reject stale configuration; this does not establish clock trust."""
    current = time.time() if now is None else now
    if abs(current - utc) > 30:
        raise ValueError("configuration UTC differs from the computer clock by over 30 seconds")


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def check_server_clock(origin, ca_pem, opener=None, wall=time.time, monotonic=time.monotonic):
    """Compare local UTC with the configured HTTPS server's authenticated Date.

    This depends on that server's clock and the supplied trust anchor; neither
    a fresh config nor an ordinary TLS certificate alone proves accurate time.
    """
    if not isinstance(origin, str) or not re.fullmatch(r"https://[A-Za-z0-9.-]+(?::[0-9]{1,5})?", origin):
        raise ValueError("invalid HTTPS origin")
    if opener is None:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        # The explicitly configured CA can itself be a non-self-signed
        # intermediate. Match the device trust-anchor semantics without
        # loading the system trust store or disabling hostname verification.
        if not hasattr(ssl, "VERIFY_X509_PARTIAL_CHAIN"):
            raise RuntimeError("TLS partial-chain verification is unavailable")
        context.verify_flags |= ssl.VERIFY_X509_PARTIAL_CHAIN
        context.load_verify_locations(cadata=ca_pem)
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect(),
                                             urllib.request.HTTPSHandler(context=context))
    request = urllib.request.Request(origin + "/health", method="HEAD")
    start_wall, start_mono = wall(), monotonic()
    with opener.open(request, timeout=5) as response:
        dates = response.headers.get_all("Date", [])
        if response.status != 200 or len(dates) != 1:
            raise ValueError("server time response is unavailable or ambiguous")
        parsed = email.utils.parsedate_to_datetime(dates[0])
        if parsed.tzinfo is None:
            raise ValueError("server time has no timezone")
        server_utc = parsed.timestamp()
    end_wall, end_mono = wall(), monotonic()
    elapsed = end_mono - start_mono
    if elapsed < 0 or elapsed > 5 or abs((end_wall - start_wall) - elapsed) > 1:
        raise ValueError("clock changed during verification")
    if server_utc < start_wall - 10 or server_utc > end_wall + 10:
        raise ValueError("computer and server clocks differ")


async def send_frames(transport, security, payload):
    """Establish Security 2 and send ordered frames. Returns no apply receipt."""
    response = None
    while True:
        request = security.security_session(response)
        if request is None:
            break
        response = await transport.send_data("prov-session", request)
        if response is None:
            raise RuntimeError("secure session failed")
    for frame in frames(payload):
        ciphertext = security.encrypt_data(frame)
        response = await transport.send_data("meeting-config", ciphertext.decode("latin-1"))
        if response is None or security.decrypt_data(response.encode("latin-1")) != b"\x00":
            raise RuntimeError("device rejected configuration frame")


async def command(transport, security, data):
    ciphertext = security.encrypt_data(data)
    response = await transport.send_data("meeting-config", ciphertext.decode("latin-1"))
    if response is None:
        raise RuntimeError("device did not respond")
    return security.decrypt_data(response.encode("latin-1"))


def load_idf_modules():
    root = os.environ.get("IDF_PATH")
    if not root:
        raise RuntimeError("activate ESP-IDF 5.5.3 first")
    root = Path(root)
    version = root / "components" / "esp_common" / "include" / "esp_idf_version.h"
    if not version.is_file():
        raise RuntimeError("ESP-IDF 5.5.3 is required")
    text = version.read_text(encoding="utf-8")
    actual = [re.search(rf"^#define\s+ESP_IDF_VERSION_{part}\s+(\d+)", text, re.MULTILINE)
              for part in ("MAJOR", "MINOR", "PATCH")]
    if any(match is None for match in actual) or tuple(int(match.group(1)) for match in actual) != (5, 5, 3):
        raise RuntimeError("ESP-IDF 5.5.3 is required")
    sys.path.insert(0, str(root / "components" / "protocomm" / "python"))
    sys.path.insert(0, str(root / "tools" / "esp_prov"))
    try:
        import bleak  # noqa: F401; do not fall back to the console transport
        import security
        import transport
    except ImportError as exc:
        raise RuntimeError("ESP-IDF provisioning dependencies and bleak are required") from exc
    return security, transport


def unique_setup_discovery(discovery, device_name, *, allow_stale_cache=False):
    """Select the single advertised provisioning service, never a name alone."""
    matches = [(key, device, adv) for key, (device, adv) in discovery.items()
               if SERVICE_UUID in (uuid.lower() for uuid in adv.service_uuids)]
    if len(matches) != 1:
        raise RuntimeError("expected exactly one provisioning service")
    key, device, adv = matches[0]
    # CoreBluetooth may retain a previous application's device.name after a
    # firmware switch. A current local name is stronger evidence than that
    # cache; an explicit conflicting advertisement must still be rejected.
    advertised_name = getattr(adv, "local_name", None)
    if advertised_name and advertised_name != device_name:
        raise RuntimeError("provisioning device name mismatch")
    if not advertised_name and device.name and device.name != device_name and not allow_stale_cache:
        raise RuntimeError("provisioning device name mismatch")
    device.name = device_name  # ESP-IDF transport selects the scan by name.
    return {key: (device, adv)}


async def run(path, device_name, discovery=None):
    # Do not include the file contents, PIN or sensitive transport exceptions in logs.
    payload = path.read_bytes()
    if not payload or len(payload) > MAX_CONFIG_BYTES:
        raise RuntimeError("configuration file size is invalid")
    fingerprint, _ = review_values(payload)
    config = json.loads(payload)
    await asyncio.to_thread(check_server_clock, config["origin"], config["caPem"])
    security_module, transport_module = load_idf_modules()
    code = os.environ.get("MEETING_TEST_PAIRING_CODE") or getpass.getpass("Device pairing code: ")
    if len(code) != 12 or not code.isascii() or not code.isdigit():
        raise RuntimeError("pairing code must be 12 digits")
    security = security_module.Security2(1, "meeting", code, False)
    code = None
    transport = transport_module.Transport_BLE(SERVICE_UUID, {"prov-session": "ff51", "meeting-config": "ff52"})
    try:
        # CoreBluetooth can expose the service UUID while omitting the local
        # name. The stock IDF client only matches by name. Use the unique
        # service match without guessing when more than one device advertises.
        import bleak
        discover = bleak.BleakScanner.discover

        async def discover_with_service_name(*args, **kwargs):
            result = discovery if discovery is not None else await discover(*args, **kwargs)
            return unique_setup_discovery(result, device_name) if kwargs.get("return_adv") else result

        bleak.BleakScanner.discover = discover_with_service_name
        try:
            await transport.connect(device_name)
        finally:
            bleak.BleakScanner.discover = discover
        await asyncio.to_thread(check_server_clock, config["origin"], config["caPem"])
        # Refresh UTC in memory immediately before transfer; never rewrite the
        # credential-bearing file or use a timestamp that went stale at a prompt.
        config["utc"] = int(time.time())
        payload = json.dumps(config, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        if len(payload) > MAX_CONFIG_BYTES:
            raise RuntimeError("configuration file size is invalid")
        _, utc = review_values(payload)
        check_freshness(utc)
        await send_frames(transport, security, payload)
        if os.environ.get("MEETING_TEST_AUTO_CONFIRM") == "1":
            print("Prototype automatic setup submitted; awaiting durable confirmation.", flush=True)
            for _ in range(30):
                if await command(transport, security, b"\x04") == b"\x02":
                    break
                await asyncio.sleep(1)
            else:
                raise RuntimeError("device did not confirm configuration")
        else:
            print(f"Compare on the device: CA SHA-256 {fingerprint}; UTC {utc}.")
            print("Confirm only if both values match.")
            await asyncio.to_thread(input, "After confirming on the device, press Enter: ")
        if await command(transport, security, b"\x04") != b"\x02":
            raise RuntimeError("device has not confirmed durable storage")
        if await command(transport, security, b"\x05") != b"\x00":
            raise RuntimeError("device did not accept acknowledgement")
    finally:
        await transport.disconnect()


async def watch(path, device_name, scan=None, runner=run, sleep=asyncio.sleep):
    """Prototype companion: re-establish the configured device after reboot.

    A unique service and successful Security 2 authentication are both
    required; this does not establish a tamper-resistant hardware identity.
    """
    if os.environ.get("MEETING_TEST_AUTO_CONFIRM") != "1" or not os.environ.get("MEETING_TEST_PAIRING_CODE"):
        raise RuntimeError("automatic prototype credentials are required")
    if scan is None:
        load_idf_modules()
        import bleak
        scan = bleak.BleakScanner.discover
    reported_problem = None
    while True:
        try:
            discovery = await scan(timeout=5, return_adv=True)
            selected = unique_setup_discovery(discovery, device_name)
        except asyncio.CancelledError:
            raise
        except Exception:
            if reported_problem != "discovery":
                print("No unique matching provisioning device; retrying.", flush=True)
                reported_problem = "discovery"
            await sleep(10)
            continue
        try:
            await runner(path, device_name, discovery=selected)
            print("Device configuration confirmed; watching for reconnect.", flush=True)
            reported_problem = None
        except asyncio.CancelledError:
            raise
        except Exception:
            # No identifiers, credentials or provider exception contents.
            if reported_problem != "transfer":
                print("Provisioning did not complete; retrying.", flush=True)
                reported_problem = "transfer"
        await sleep(10)


def main():
    parser = argparse.ArgumentParser(description="Transfer a meeting configuration over an authenticated BLE session")
    parser.add_argument("config", type=Path, help="path to the configuration JSON; keep it outside the repository")
    parser.add_argument("--device", default="PASSPORT-SETUP", help="BLE advertisement name")
    parser.add_argument("--watch", action="store_true", help="prototype: automatically reconfigure after device reboot")
    args = parser.parse_args()
    try:
        asyncio.run(watch(args.config, args.device) if args.watch else run(args.config, args.device))
    except Exception:
        print("Provisioning transfer failed; check the device, IDF dependencies and configuration.", file=sys.stderr)
        return 1
    print("Device confirmed durable configuration; network activation is separate.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
