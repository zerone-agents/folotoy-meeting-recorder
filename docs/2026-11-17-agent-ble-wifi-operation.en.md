# Agent Bluetooth Wi-Fi Switching

This guide describes the recording-first, idle Bluetooth switching prototype. Management Bluetooth is unavailable during recording and becomes discoverable when the device is idle. Device tests have covered switching between two networks, one physical power loss during a candidate trial, and power loss before and after committing a same-network candidate. Three paired uploads of the same 30-minute recording met the throughput thresholds; cross-network power loss and multi-run stability remain unverified. This guide is not a release-readiness claim.

When idle without an effective upload, management windows use approximately 25 seconds of network opportunity and 8 seconds of advertising. An effective upload can hold the network for up to 120 seconds, so discovery during upload can take longer than 30 seconds. After 45 seconds without new server-confirmed progress, the device returns to ordinary scheduling. Draining, scanning and authentication add time, so these are not discovery guarantees. Recording always takes priority. A waiting message does not establish whether the device is recording, uploading or offline. See the device record for the three paired long-audio measurements and their conditions.

## Preparation

- Use a device with its own provisioned management identity and matching credential. The current CLI reads it from the macOS Keychain service `meeting-recorder-management`, with the device management ID as the account.
- Activate the ESP-IDF 5.5.3 Python environment with its provisioning dependencies and `bleak`. Enable Bluetooth and keep the powered device nearby. The target must be a 2.4 GHz network the device can join.
- Choose a persistent local SQLite path for `--store`. It contains non-secret pending operation IDs and results, not Wi-Fi passwords. Do not change or remove it while an operation is unresolved.

## Switch networks

From the repository root, replace the management ID, database path and SSID:

```sh
python3 firmware/tools/meeting_wifi_agent_client.py \
  --device '<device management ID>' --store '<local pending database path>' \
  switch --ssid '<target Wi-Fi SSID>'
```

The password is read interactively by default. Automation can add `--password-stdin` after `switch` to read one line from standard input, for example from a credential manager. Do not put the password in command arguments or logs. The CLI waits until the device is available, discovers and authenticates it, and submits once. Waiting or disconnecting does not establish that the device received the request. Only a queried terminal result establishes success or restoration of the previous configuration. Authentication or DHCP failure restores the previous configuration; if that network is also unavailable, recordings remain offline.

When this process can establish that discovery or connection failed before any management payload was sent, it retries using the same operation ID. Once transmission has begun, or its status is uncertain, it only queries that operation to avoid duplicate submission. A connection failure while querying or acknowledging remains “result pending”. The Wi-Fi password is not persisted or recovered after a process restart; the saved operation can only be queried.

## Resume a pending operation

Use the same device ID and database:

```sh
python3 firmware/tools/meeting_wifi_agent_client.py \
  --device '<device management ID>' --store '<local pending database path>' resume
```

The CLI queries the same operation ID and clears its receipt only after the device acknowledges the terminal result. For an unknown or expired result it displays the pending operation ID and retains the receipt. To explicitly abandon that unknown result near the device:

```sh
python3 firmware/tools/meeting_wifi_agent_client.py \
  --device '<device management ID>' --store '<local pending database path>' \
  abandon --operation-id '<displayed pending operation ID>'
```

The command reauthenticates and queries again. It removes the matching local receipt only if the device still reports unknown or expired; connection failures, an operation in progress and terminal results preserve it. It does not infer success or failure, or overwrite other unacknowledged device transactions. A later switch receives a new operation ID.

Automation can inspect the exit status: switch success is 0, rollback is 2, an unknown or unconfirmed result is 3, and maintenance required is 4; confirmed abandonment of an unknown result is 0. Connection and other execution errors also exit nonzero.

## Maintenance and limitations

Management credential rotation, revocation and recovery currently use a USB maintenance build restricted to the target device. The build contains a secret: protect it locally, restore the normal firmware afterward and remove temporary artifacts. Remote credential rotation is not implemented. This device has neither secure boot nor flash encryption enabled; the prototype does not resist someone able to read or modify device flash.

See the [idle switching design](superpowers/specs/2026-11-16-idle-ble-wifi-switch-design.md), [upload-priority scheduling revision](superpowers/specs/2026-11-17-upload-priority-idle-switch-design.md), and [device test record](../research/2026-11-18-idle-wifi-validation.md) for acceptance thresholds and outstanding work.
