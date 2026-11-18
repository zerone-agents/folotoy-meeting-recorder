#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--firmware]" >&2
}

run_static_checks() {
    local actionlint_bin
    local test_dir
    local store_test_parent
    local gc_sections=-Wl,--gc-sections
    if [[ "$(uname -s)" == Darwin ]]; then
        gc_sections=-Wl,-dead_strip
    fi

    python3 tools/check_repo.py

    actionlint_bin="${ACTIONLINT_BIN:-}"
    if [[ -z "${actionlint_bin}" ]]; then
        actionlint_bin="$(command -v actionlint || true)"
    fi
    if [[ -z "${actionlint_bin}" || ! -x "${actionlint_bin}" ]]; then
        actionlint_bin="$(./tools/install-actionlint.sh)"
    fi
    "${actionlint_bin}" -color .github/workflows/*.yml

    test_dir="$(mktemp -d /tmp/ai-passport-host-tests.XXXXXX)"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_stack_lease.c main/meeting_stack_lease.c -o "${test_dir}/test_stack_lease"
    "${test_dir}/test_stack_lease"
    cc -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_idle_coordinator.c main/meeting_idle_coordinator.c -o "${test_dir}/test_idle_coordinator"
    "${test_dir}/test_idle_coordinator"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_idle_window.c main/meeting_idle_window.c -o "${test_dir}/test_idle_window"
    "${test_dir}/test_idle_window"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_wifi_switch.c main/meeting_wifi_switch.c -o "${test_dir}/test_wifi_switch"
    "${test_dir}/test_wifi_switch"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_wifi_switch_nvs.c main/meeting_wifi_switch.c main/meeting_wifi_switch_nvs.c -o "${test_dir}/test_wifi_switch_nvs"
    "${test_dir}/test_wifi_switch_nvs"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_wifi_slots.c main/meeting_wifi_slots.c main/meeting_wifi_slot_proof.c main/meeting_wifi_switch.c -o "${test_dir}/test_wifi_slots"
    "${test_dir}/test_wifi_slots"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_wifi_slots_nvs.c main/meeting_wifi_slots.c main/meeting_wifi_slots_nvs.c -o "${test_dir}/test_wifi_slots_nvs"
    "${test_dir}/test_wifi_slots_nvs"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_wifi_slot_apply.c main/meeting_wifi_slot_apply.c main/meeting_wifi_slots.c -o "${test_dir}/test_wifi_slot_apply"
    "${test_dir}/test_wifi_slot_apply"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_wifi_trial.c main/meeting_wifi_trial.c -o "${test_dir}/test_wifi_trial"
    "${test_dir}/test_wifi_trial"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_wifi_trial_sta.c main/meeting_wifi_trial.c main/meeting_wifi_trial_sta.c -o "${test_dir}/test_wifi_trial_sta"
    "${test_dir}/test_wifi_trial_sta"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_wifi_operation.c main/meeting_wifi_operation.c main/meeting_wifi_trial.c main/meeting_wifi_slots.c \
        main/meeting_wifi_slot_proof.c main/meeting_wifi_switch.c main/meeting_management_dispatch.c main/meeting_management_ack.c main/meeting_wifi_command.c main/meeting_wifi_status.c -o "${test_dir}/test_wifi_operation"
    "${test_dir}/test_wifi_operation"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -DCONFIG_MEETING_SUPERVISED_TEST_LONG_WIFI_TRIAL=1 -Itests/meeting/stubs -Imain \
        tests/meeting/test_wifi_operation.c main/meeting_wifi_operation.c main/meeting_wifi_trial.c main/meeting_wifi_slots.c \
        main/meeting_wifi_slot_proof.c main/meeting_wifi_switch.c main/meeting_management_dispatch.c main/meeting_management_ack.c main/meeting_wifi_command.c main/meeting_wifi_status.c -o "${test_dir}/test_wifi_operation_power_test"
    "${test_dir}/test_wifi_operation_power_test"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_wifi_command.c main/meeting_wifi_command.c -o "${test_dir}/test_wifi_command"
    "${test_dir}/test_wifi_command"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_wifi_status.c main/meeting_wifi_status.c -o "${test_dir}/test_wifi_status"
    "${test_dir}/test_wifi_status"
    PYTHONDONTWRITEBYTECODE=1 python3 tests/meeting/test_wifi_command_agent.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/meeting/test_wifi_agent_transport.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/meeting/test_wifi_agent_task.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/meeting/test_wifi_agent_store.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/meeting/test_wifi_agent_client.py
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_agent_mgmt.c main/meeting_agent_mgmt.c main/meeting_management_credential.c -o "${test_dir}/test_agent_mgmt"
    "${test_dir}/test_agent_mgmt"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_record_preempt_async.c main/meeting_record_preempt_async.c -o "${test_dir}/test_record_preempt_async"
    "${test_dir}/test_record_preempt_async"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_management_session.c main/meeting_management_session.c -o "${test_dir}/test_management_session"
    "${test_dir}/test_management_session"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_management_controller.c main/meeting_management_controller.c main/meeting_upload_priority.c main/meeting_upload_activity.c -o "${test_dir}/test_management_controller"
    "${test_dir}/test_management_controller"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_management_inbox.c main/meeting_management_inbox.c main/meeting_wifi_command.c -o "${test_dir}/test_management_inbox"
    "${test_dir}/test_management_inbox"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_management_query.c main/meeting_management_query.c main/meeting_wifi_switch.c main/meeting_wifi_status.c -o "${test_dir}/test_management_query"
    "${test_dir}/test_management_query"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_management_dispatch.c main/meeting_management_dispatch.c main/meeting_wifi_command.c main/meeting_wifi_status.c -o "${test_dir}/test_management_dispatch"
    "${test_dir}/test_management_dispatch"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread -Imain \
        tests/meeting/test_stack_lease_threads.c main/meeting_stack_lease.c -o "${test_dir}/test_stack_lease_threads"
    "${test_dir}/test_stack_lease_threads"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_management_gate.c main/meeting_management_gate.c main/meeting_management_credential.c main/meeting_pairing_session.c -o "${test_dir}/test_management_gate"
    "${test_dir}/test_management_gate"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_management_password.c main/meeting_management_password.c -o "${test_dir}/test_management_password"
    "${test_dir}/test_management_password"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_management_ble.c main/meeting_management_ble.c main/meeting_management_credential.c -o "${test_dir}/test_management_ble"
    "${test_dir}/test_management_ble"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_management_credential.c main/meeting_management_credential.c -o "${test_dir}/test_management_credential"
    "${test_dir}/test_management_credential"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_management_credential_nvs.c main/meeting_management_credential.c main/meeting_management_credential_nvs.c -o "${test_dir}/test_management_credential_nvs"
    "${test_dir}/test_management_credential_nvs"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_credential_reset.c main/meeting_credential_reset.c -o "${test_dir}/test_credential_reset"
    "${test_dir}/test_credential_reset"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_credential_reset_nvs.c main/meeting_credential_reset.c main/meeting_credential_reset_nvs.c \
        -o "${test_dir}/test_credential_reset_nvs"
    "${test_dir}/test_credential_reset_nvs"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs \
        tests/meeting/test_tls_fragment.c main/meeting_tls_fragment.c -o "${test_dir}/test_tls_fragment"
    "${test_dir}/test_tls_fragment"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ui_pixel_math.c main/ui_pixel_math.c \
        -o "${test_dir}/test_ui_pixel_math"
    "${test_dir}/test_ui_pixel_math"
    "${CC:-cc}" -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_file.c main/meeting_scr1.c main/meeting_scr1_file.c \
        -o "${test_dir}/test_meeting_file"
    "${test_dir}/test_meeting_file"
    "${CC:-cc}" -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_create_file.c main/meeting_create_file.c -o "${test_dir}/test_meeting_create_file"
    "${test_dir}/test_meeting_create_file"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_scan.c main/meeting_scr1.c -o "${test_dir}/test_meeting_scan"
    "${test_dir}/test_meeting_scan"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_scr1.c main/meeting_scr1.c -o "${test_dir}/test_meeting_projection"
    "${test_dir}/test_meeting_projection"
    "${CC:-cc}" -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_projection_file.c main/meeting_scr1.c main/meeting_scr1_file.c -o "${test_dir}/test_projection_file"
    "${test_dir}/test_projection_file"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_snapshot.c main/meeting_snapshot.c -o "${test_dir}/test_meeting_snapshot"
    "${test_dir}/test_meeting_snapshot"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_snapshot_forward_compat.c main/meeting_snapshot.c -o "${test_dir}/test_snapshot_forward_compat"
    "${test_dir}/test_snapshot_forward_compat"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_record.c main/meeting_record.c -o "${test_dir}/test_meeting_record"
    "${test_dir}/test_meeting_record"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_seal.c main/meeting_seal.c -o "${test_dir}/test_meeting_seal"
    "${test_dir}/test_meeting_seal"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_seal_nvs.c main/meeting_seal.c main/meeting_seal_nvs.c -o "${test_dir}/test_meeting_seal_nvs"
    "${test_dir}/test_meeting_seal_nvs"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_frame_barrier.c main/meeting_frame_barrier.c -o "${test_dir}/test_frame_barrier"
    "${test_dir}/test_frame_barrier"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_writer_signal.c main/meeting_writer_signal.c -o "${test_dir}/test_writer_signal"
    "${test_dir}/test_writer_signal"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_deletion.c main/meeting_deletion.c -o "${test_dir}/test_meeting_deletion"
    "${test_dir}/test_meeting_deletion"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain tests/meeting/test_delete_workflow.c \
        main/meeting_deletion.c main/meeting_delete_workflow.c -o "${test_dir}/test_delete_workflow"
    "${test_dir}/test_delete_workflow"
    "${CC:-cc}" -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_delete_proof.c main/meeting_delete_proof.c main/meeting_confirmation.c main/meeting_receipt.c \
        main/meeting_upload.c main/meeting_snapshot_file.c main/meeting_scr1.c main/meeting_scr1_file.c \
        -o "${test_dir}/test_delete_proof"
    "${test_dir}/test_delete_proof"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_delete_proof_nvs.c main/meeting_delete_proof_nvs.c main/meeting_confirmation.c main/meeting_receipt.c \
        -o "${test_dir}/test_delete_proof_nvs"
    "${test_dir}/test_delete_proof_nvs"
    "${CC:-cc}" -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_delete_flow_integration.c main/meeting_deletion.c main/meeting_delete_workflow.c main/meeting_delete_proof.c \
        main/meeting_confirmation.c main/meeting_receipt.c main/meeting_upload.c main/meeting_snapshot_file.c main/meeting_scr1.c main/meeting_scr1_file.c \
        -o "${test_dir}/test_delete_flow_integration"
    "${test_dir}/test_delete_flow_integration"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_deletion_nvs.c main/meeting_deletion.c main/meeting_deletion_nvs.c \
        -o "${test_dir}/test_meeting_deletion_nvs"
    "${test_dir}/test_meeting_deletion_nvs"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_deletion_store.c main/meeting_deletion.c main/meeting_deletion_nvs.c main/meeting_deletion_store.c \
        -o "${test_dir}/test_deletion_store"
    "${test_dir}/test_deletion_store"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_delete_offline.c main/meeting_delete_offline.c -o "${test_dir}/test_delete_offline"
    "${test_dir}/test_delete_offline"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_delete_ui.c main/meeting_delete_ui.c -o "${test_dir}/test_delete_ui"
    "${test_dir}/test_delete_ui"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_delete_job.c main/meeting_delete_job.c -o "${test_dir}/test_delete_job"
    "${test_dir}/test_delete_job"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_record_nvs.c main/meeting_record.c main/meeting_record_nvs.c -o "${test_dir}/test_meeting_record_nvs"
    "${test_dir}/test_meeting_record_nvs"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_file_sequence.c main/meeting_file_sequence.c -o "${test_dir}/test_meeting_file_sequence"
    "${test_dir}/test_meeting_file_sequence"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_file_sequence_nvs.c main/meeting_file_sequence.c main/meeting_file_sequence_nvs.c \
        -o "${test_dir}/test_meeting_file_sequence_nvs"
    "${test_dir}/test_meeting_file_sequence_nvs"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_snapshot_slots.c main/meeting_snapshot.c -o "${test_dir}/test_meeting_slots"
    "${test_dir}/test_meeting_slots"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_snapshot_nvs.c main/meeting_snapshot.c main/meeting_snapshot_nvs.c \
        -o "${test_dir}/test_meeting_snapshot_nvs"
    "${test_dir}/test_meeting_snapshot_nvs"
    "${CC:-cc}" -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_snapshot_file.c main/meeting_scr1.c main/meeting_scr1_file.c main/meeting_snapshot_file.c \
        -o "${test_dir}/test_meeting_snapshot_file"
    "${test_dir}/test_meeting_snapshot_file"
    "${CC:-cc}" -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_recovery.c main/meeting_scr1.c main/meeting_scr1_file.c \
        main/meeting_snapshot.c main/meeting_snapshot_file.c main/meeting_recovery.c main/meeting_seal.c \
        -o "${test_dir}/test_meeting_recovery"
    "${test_dir}/test_meeting_recovery"
    "${CC:-cc}" -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_closed_recovery.c main/meeting_scr1.c main/meeting_scr1_file.c \
        main/meeting_snapshot.c main/meeting_snapshot_file.c main/meeting_recovery.c main/meeting_seal.c \
        -o "${test_dir}/test_meeting_closed"
    "${test_dir}/test_meeting_closed"
    "${CC:-cc}" -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_sealed_recovery.c main/meeting_scr1.c main/meeting_scr1_file.c \
        main/meeting_snapshot.c main/meeting_snapshot_file.c main/meeting_recovery.c main/meeting_seal.c \
        -o "${test_dir}/test_sealed_recovery"
    "${test_dir}/test_sealed_recovery"
    "${CC:-cc}" -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_upload.c main/meeting_scr1.c main/meeting_scr1_file.c \
        main/meeting_snapshot_file.c main/meeting_upload.c -o "${test_dir}/test_meeting_upload"
    "${test_dir}/test_meeting_upload"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_receipt.c main/meeting_receipt.c -o "${test_dir}/test_meeting_receipt"
    "${test_dir}/test_meeting_receipt"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_confirmation.c main/meeting_confirmation.c main/meeting_receipt.c \
        -o "${test_dir}/test_meeting_confirmation"
    "${test_dir}/test_meeting_confirmation"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_confirmation_nvs.c main/meeting_confirmation_nvs.c \
        main/meeting_confirmation.c main/meeting_receipt.c -o "${test_dir}/test_meeting_confirmation_nvs"
    "${test_dir}/test_meeting_confirmation_nvs"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_cleanup.c main/meeting_cleanup.c main/meeting_confirmation.c main/meeting_receipt.c \
        -o "${test_dir}/test_meeting_cleanup"
    "${test_dir}/test_meeting_cleanup"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_retirement.c main/meeting_retirement.c -o "${test_dir}/test_meeting_retirement"
    "${test_dir}/test_meeting_retirement"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_retirement_nvs.c main/meeting_retirement.c main/meeting_retirement_nvs.c \
        -o "${test_dir}/test_meeting_retirement_nvs"
    "${test_dir}/test_meeting_retirement_nvs"
    "${CC:-cc}" -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_cleanup_files.c main/meeting_cleanup_files.c main/meeting_cleanup.c \
        main/meeting_confirmation.c main/meeting_receipt.c -o "${test_dir}/test_meeting_cleanup_files"
    "${test_dir}/test_meeting_cleanup_files"
    "${CC:-cc}" -D_POSIX_C_SOURCE=200809L -Dunlink=meeting_test_unlink -std=c11 -Wall -Wextra -Werror \
        -ffunction-sections -fdata-sections -Itests/meeting/stubs -Imain \
        -c main/meeting_cleanup_files.c -o "${test_dir}/meeting_cleanup_files.o"
    store_test_parent="$(mktemp -d /tmp/ms.XXXXXX)"
    "${CC:-cc}" -D_POSIX_C_SOURCE=200809L -DSC_STORE_TEST "-DSC_STORE_BASE_PATH=\"${store_test_parent}/s\"" \
        -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -Itests/meeting/stubs -Imain \
        tests/meeting/test_store_cleanup_integration.c main/sc_store.c main/sc_proto.c main/sc_ring.c main/meeting_create_file.c \
        main/meeting_record.c main/meeting_record_nvs.c main/meeting_snapshot.c main/meeting_snapshot_nvs.c \
        main/meeting_recovery.c main/meeting_seal.c main/meeting_seal_nvs.c main/meeting_deletion.c main/meeting_deletion_nvs.c main/meeting_deletion_store.c main/meeting_delete_proof.c main/meeting_delete_proof_nvs.c main/meeting_snapshot_file.c main/meeting_scr1.c main/meeting_scr1_file.c \
        main/meeting_receipt.c main/meeting_confirmation.c main/meeting_confirmation_nvs.c \
        main/meeting_cleanup.c main/meeting_retirement.c main/meeting_retirement_nvs.c main/meeting_upload.c main/meeting_store_upload.c \
        "${test_dir}/meeting_cleanup_files.o" "${gc_sections}" \
        -o "${test_dir}/test_meeting_store_cleanup_integration"
    "${test_dir}/test_meeting_store_cleanup_integration"
    rmdir "${store_test_parent}"
    bash tests/meeting/test_lfs_export.sh
    PYTHONDONTWRITEBYTECODE=1 python3 tests/meeting/test_device_export.py
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_upload_priority.c main/meeting_upload_priority.c -o "${test_dir}/test_meeting_upload_priority"
    "${test_dir}/test_meeting_upload_priority"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread -Imain \
        tests/meeting/test_upload_activity.c main/meeting_upload_activity.c \
        -o "${test_dir}/test_meeting_upload_activity"
    "${test_dir}/test_meeting_upload_activity"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_upload_protocol.c main/meeting_receipt.c main/meeting_upload_protocol.c \
        -o "${test_dir}/test_meeting_upload_protocol"
    "${test_dir}/test_meeting_upload_protocol"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_window_resume.c main/meeting_receipt.c main/meeting_upload_protocol.c \
        -o "${test_dir}/test_window_resume"
    "${test_dir}/test_window_resume"
    PYTHONDONTWRITEBYTECODE=1 python3 tests/meeting/test_security2_rng_cleanup.py main/meeting_security2_idf553.c
    PYTHONDONTWRITEBYTECODE=1 python3 tests/meeting/test_srp_short_salt.py main/meeting_srp_idf553.c
    PYTHONDONTWRITEBYTECODE=1 python3 tests/meeting/test_srp_padding.py main/meeting_srp_idf553.c
    PYTHONDONTWRITEBYTECODE=1 python3 tests/meeting/test_srp_session_faults.py main/meeting_srp_idf553.c
    PYTHONDONTWRITEBYTECODE=1 python3 tests/meeting/test_srp_pubkey_faults.py main/meeting_srp_idf553.c
    PYTHONDONTWRITEBYTECODE=1 python3 tests/meeting/test_srp_mpi_cleanup.py main/meeting_srp_mpi_idf553.c
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_legacy_resume.c main/meeting_receipt.c main/meeting_upload_protocol.c \
        -o "${test_dir}/test_legacy_resume"
    "${test_dir}/test_legacy_resume"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_time_http.c main/meeting_time_http.c -o "${test_dir}/test_meeting_time_http"
    "${test_dir}/test_meeting_time_http"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_dns.c main/meeting_dns.c -o "${test_dir}/test_meeting_dns"
    "${test_dir}/test_meeting_dns"
    if [[ -n "${IDF_PATH:-}" && -f "${IDF_PATH}/components/json/cJSON/cJSON.c" ]]; then
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain -I"${IDF_PATH}/components/json/cJSON" \
            tests/meeting/test_receipt_json.c main/meeting_receipt.c main/meeting_receipt_json.c \
            "${IDF_PATH}/components/json/cJSON/cJSON.c" -lm -o "${test_dir}/test_meeting_receipt_json"
        "${test_dir}/test_meeting_receipt_json"
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain -I"${IDF_PATH}/components/json/cJSON" \
            tests/meeting/test_time_response.c main/meeting_time_response.c "${IDF_PATH}/components/json/cJSON/cJSON.c" -lm -o "${test_dir}/test_meeting_time_response"
        "${test_dir}/test_meeting_time_response"
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain -I"${IDF_PATH}/components/json/cJSON" \
            tests/meeting/test_cancel_request.c main/meeting_cancel_request.c main/meeting_receipt.c main/meeting_receipt_json.c \
            "${IDF_PATH}/components/json/cJSON/cJSON.c" -lm -o "${test_dir}/test_cancel_request"
        "${test_dir}/test_cancel_request"
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain -I"${IDF_PATH}/components/json/cJSON" \
            tests/meeting/test_upload_e2e.c main/meeting_receipt.c main/meeting_receipt_json.c \
            main/meeting_upload_protocol.c "${IDF_PATH}/components/json/cJSON/cJSON.c" -lm \
            -o "${test_dir}/test_meeting_upload_e2e"
        "${test_dir}/test_meeting_upload_e2e"
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain -I"${IDF_PATH}/components/json/cJSON" \
            tests/meeting/test_window_resume_json.c main/meeting_receipt.c main/meeting_receipt_json.c \
            main/meeting_upload_protocol.c "${IDF_PATH}/components/json/cJSON/cJSON.c" -lm \
            -o "${test_dir}/test_window_resume_json"
        "${test_dir}/test_window_resume_json"
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain -I"${IDF_PATH}/components/json/cJSON" \
            tests/meeting/test_window_low_memory.c main/meeting_receipt.c main/meeting_receipt_json.c \
            "${IDF_PATH}/components/json/cJSON/cJSON.c" -lm -o "${test_dir}/test_window_low_memory"
        "${test_dir}/test_window_low_memory"
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain -I"${IDF_PATH}/components/json/cJSON" \
            tests/meeting/test_config.c tests/meeting/config_sha256_stub.c main/meeting_config.c "${IDF_PATH}/components/json/cJSON/cJSON.c" -lm \
            -o "${test_dir}/test_meeting_config"
        "${test_dir}/test_meeting_config"
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain -I"${IDF_PATH}/components/json/cJSON" \
            tests/meeting/test_credential_identity.c tests/meeting/config_sha256_stub.c main/meeting_config.c \
            main/meeting_credential_identity.c main/meeting_credential_reset.c \
            "${IDF_PATH}/components/json/cJSON/cJSON.c" -lm -o "${test_dir}/test_credential_identity"
        "${test_dir}/test_credential_identity"
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain -I"${IDF_PATH}/components/json/cJSON" \
            tests/meeting/test_credential_reset_config_nvs.c tests/meeting/config_sha256_stub.c \
            main/meeting_config.c main/meeting_config_store.c main/meeting_config_store_nvs.c \
            main/meeting_credential_identity.c main/meeting_credential_reset.c main/meeting_credential_reset_nvs.c \
            main/meeting_credential_reset_config_nvs.c "${IDF_PATH}/components/json/cJSON/cJSON.c" -lm \
            -o "${test_dir}/test_credential_reset_config_nvs"
        "${test_dir}/test_credential_reset_config_nvs"
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain -I"${IDF_PATH}/components/json/cJSON" \
            tests/meeting/test_config_store.c tests/meeting/config_sha256_stub.c main/meeting_config.c main/meeting_config_store.c \
            "${IDF_PATH}/components/json/cJSON/cJSON.c" -lm -o "${test_dir}/test_meeting_config_store"
        "${test_dir}/test_meeting_config_store"
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain -I"${IDF_PATH}/components/json/cJSON" \
            tests/meeting/test_provision_apply.c tests/meeting/config_sha256_stub.c main/meeting_provision_apply.c main/meeting_clock.c \
            main/meeting_config.c main/meeting_config_store.c "${IDF_PATH}/components/json/cJSON/cJSON.c" -lm \
            -o "${test_dir}/test_meeting_provision_apply"
        "${test_dir}/test_meeting_provision_apply"
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
            tests/meeting/test_config_store_nvs.c main/meeting_config_store_nvs.c -o "${test_dir}/test_meeting_config_store_nvs"
        "${test_dir}/test_meeting_config_store_nvs"
    else
        echo "SKIP meeting upload JSON integration: ESP-IDF cJSON unavailable"
    fi
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_https.c main/meeting_https.c -o "${test_dir}/test_meeting_https"
    "${test_dir}/test_meeting_https"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/audio_stubs -Itests/meeting/stubs \
        -Icomponents/bsp/include -Imain tests/meeting/test_sc_audio.c -o "${test_dir}/test_sc_audio"
    "${test_dir}/test_sc_audio"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_sta.c main/meeting_sta.c -o "${test_dir}/test_meeting_sta"
    "${test_dir}/test_meeting_sta"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_upload_session.c main/meeting_upload_session.c main/meeting_upload_activity.c -o "${test_dir}/test_meeting_upload_session"
    "${test_dir}/test_meeting_upload_session"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_online_session.c main/meeting_online_session.c -o "${test_dir}/test_meeting_online_session"
    "${test_dir}/test_meeting_online_session"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_store_upload.c main/meeting_store_upload.c -o "${test_dir}/test_meeting_store_upload"
    "${test_dir}/test_meeting_store_upload"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -DCONFIG_MEETING_SUPERVISED_TEST_REPLAY_UPLOAD=1 \
        -DCONFIG_MEETING_SUPERVISED_TEST_REPLAY_FILE=7 -DCONFIG_MEETING_SUPERVISED_TEST_REPLAY_RUN=1 \
        -Itests/meeting/stubs -Imain tests/meeting/test_replay_upload.c main/meeting_store_upload.c \
        -o "${test_dir}/test_meeting_replay_upload"
    "${test_dir}/test_meeting_replay_upload"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_arbitration.c main/meeting_arbitration.c -o "${test_dir}/test_meeting_arbitration"
    "${test_dir}/test_meeting_arbitration"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_reset_coordinator.c main/meeting_reset_coordinator.c main/meeting_credential_reset.c main/meeting_arbitration.c -o "${test_dir}/test_meeting_reset_coordinator"
    "${test_dir}/test_meeting_reset_coordinator"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_runtime.c main/meeting_runtime.c main/meeting_arbitration.c -o "${test_dir}/test_meeting_runtime"
    "${test_dir}/test_meeting_runtime"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_worker.c main/meeting_worker.c main/meeting_upload_activity.c -o "${test_dir}/test_meeting_worker"
    "${test_dir}/test_meeting_worker"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_ca_compat.c main/meeting_ca_compat.c -o "${test_dir}/test_ca_compat"
    "${test_dir}/test_ca_compat"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_network.c main/meeting_clock.c main/meeting_wifi_slot_apply.c main/meeting_wifi_slots.c main/meeting_ca_compat.c -o "${test_dir}/test_meeting_network"
    "${test_dir}/test_meeting_network"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_wifi_boot.c main/meeting_wifi_boot.c main/meeting_wifi_switch.c main/meeting_wifi_slots.c main/meeting_wifi_slot_apply.c -o "${test_dir}/test_meeting_wifi_boot"
    "${test_dir}/test_meeting_wifi_boot"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_restore.c -o "${test_dir}/test_meeting_restore"
    "${test_dir}/test_meeting_restore"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        -DCONFIG_MEETING_AGENT_TIME_PROVISION=1 -DCONFIG_MEETING_AGENT_TIME_ORIGIN_SHA256='"origin"' \
        -DCONFIG_MEETING_AGENT_TIME_PUBLIC_KEY='"key"' tests/meeting/test_restore.c -o "${test_dir}/test_meeting_restore_provision"
    "${test_dir}/test_meeting_restore_provision"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        -DCONFIG_MEETING_AGENT_TIME_PROVISION=1 -DCONFIG_MEETING_AGENT_TEST_ANCHOR_NAMESPACE=1 \
        -DCONFIG_MEETING_AGENT_TIME_VERIFY_ONLY=1 -DCONFIG_MEETING_AGENT_TIME_ORIGIN_SHA256='"origin"' \
        -DCONFIG_MEETING_AGENT_TIME_PUBLIC_KEY='"key"' tests/meeting/test_restore.c -o "${test_dir}/test_meeting_restore_readback"
    "${test_dir}/test_meeting_restore_readback"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_agent_anchor.c main/meeting_agent_anchor.c main/meeting_time_anchor.c -o "${test_dir}/test_agent_anchor"
    "${test_dir}/test_agent_anchor"
    PYTHONDONTWRITEBYTECODE=1 python3 tests/meeting/test_prepare_time_anchor.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/meeting/test_ui_chinese.py
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_clock.c main/meeting_clock.c -o "${test_dir}/test_meeting_clock"
    "${test_dir}/test_meeting_clock"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_time_proof.c main/meeting_time_proof.c main/meeting_clock.c -o "${test_dir}/test_meeting_time_proof"
    "${test_dir}/test_meeting_time_proof"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_time_anchor.c main/meeting_time_anchor.c -o "${test_dir}/test_meeting_time_anchor"
    "${test_dir}/test_meeting_time_anchor"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_time_anchor_nvs.c main/meeting_time_anchor.c main/meeting_time_anchor_nvs.c -o "${test_dir}/test_meeting_time_anchor_nvs"
    "${test_dir}/test_meeting_time_anchor_nvs"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_clock_nvs.c main/meeting_clock_nvs.c -o "${test_dir}/test_meeting_clock_nvs"
    "${test_dir}/test_meeting_clock_nvs"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        '-DCONFIG_MEETING_SUPERVISED_DEVICE_MAC="001122334455"' \
        tests/meeting/test_commission.c main/meeting_commission.c -o "${test_dir}/test_meeting_commission"
    "${test_dir}/test_meeting_commission"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_pairing_session.c main/meeting_pairing_session.c -o "${test_dir}/test_meeting_pairing_session"
    "${test_dir}/test_meeting_pairing_session"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_config_fragments.c main/meeting_config_fragments.c -o "${test_dir}/test_meeting_config_fragments"
    "${test_dir}/test_meeting_config_fragments"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_config_protocol.c main/meeting_config_fragments.c main/meeting_config_protocol.c \
        -o "${test_dir}/test_meeting_config_protocol"
    "${test_dir}/test_meeting_config_protocol"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_config_ble.c main/meeting_config_ble.c main/meeting_config_fragments.c \
        main/meeting_config_protocol.c -o "${test_dir}/test_meeting_config_ble"
    "${test_dir}/test_meeting_config_ble"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/meeting/stubs -Imain \
        tests/meeting/test_setup.c main/meeting_setup.c -o "${test_dir}/test_meeting_setup"
    "${test_dir}/test_meeting_setup"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/meeting/test_setup_retry.c main/meeting_setup_retry.c -o "${test_dir}/test_meeting_setup_retry"
    "${test_dir}/test_meeting_setup_retry"
    PYTHONDONTWRITEBYTECODE=1 python3 tests/meeting/test_config_frames.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/meeting/test_provision_client.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/meeting/test_prepare_identity.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/meeting/test_revocation_outbox.py
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_demo_navigation.c main/demo_navigation.c \
        -o "${test_dir}/test_demo_navigation"
    "${test_dir}/test_demo_navigation"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_key_guard.c -o "${test_dir}/test_key_guard"
    "${test_dir}/test_key_guard"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_display_rounding.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_display_rounding"
    "${test_dir}/test_bsp_display_rounding"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_es8311_sleep_check.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_es8311_sleep_check"
    "${test_dir}/test_bsp_es8311_sleep_check"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_button.c -o "${test_dir}/test_bsp_button"
    "${test_dir}/test_bsp_button"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_lvgl_init.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_lvgl_init"
    "${test_dir}/test_bsp_lvgl_init"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/audio_stubs -Icomponents/bsp/include -Icomponents/bsp/src \
        tests/test_bsp_audio_recovery.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_audio_recovery"
    "${test_dir}/test_bsp_audio_recovery"
    for demo in audio low_power ble wifi; do
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
            -ffunction-sections -fdata-sections -Itests/demo_stubs -Imain \
            "tests/test_demo_${demo}_runtime.c" "${gc_sections}" \
            -o "${test_dir}/test_demo_${demo}_runtime"
        "${test_dir}/test_demo_${demo}_runtime"
    done
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_sc_proto.c main/sc_proto.c -o "${test_dir}/test_sc_proto"
    "${test_dir}/test_sc_proto"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_sc_ring.c main/sc_ring.c -o "${test_dir}/test_sc_ring"
    "${test_dir}/test_sc_ring"
    # Builds the firmware protocol/crypto modules against host mbedtls and checks them
    # with a port of the Soundcore client; skipped when mbedtls or cryptography is absent.
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_sc_client_compat.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_deep_sleep_contract.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_key_wake_contract.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_check_repo.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_verify_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_archive_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_install_passport_skills.py
    rm -rf "${test_dir}"
    echo "Host tests: PASS"
}

run_firmware_checks() (
    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py is not available; activate ESP-IDF 5.5.3 first." >&2
        return 1
    fi

    validation_build_dir="$(mktemp -d /tmp/ai-passport-firmware.XXXXXX)"
    trap 'rm -rf -- "$validation_build_dir"' EXIT

    SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults${MEETING_EXTRA_SDKCONFIG_DEFAULTS:+;$MEETING_EXTRA_SDKCONFIG_DEFAULTS}" \
        idf.py -B "${validation_build_dir}" \
        -D "SDKCONFIG=${validation_build_dir}/sdkconfig" build
    idf.py -B "${validation_build_dir}" merge-bin \
        -o "${validation_build_dir}/FoloToy-AI-Passport-full.bin"
    python3 tools/verify_firmware.py "${validation_build_dir}"
    PYTHONDONTWRITEBYTECODE=1 python3 tools/archive_firmware.py create \
        "${validation_build_dir}" --archive-root "${repo_root}/build/firmware"
    mkdir -p "${repo_root}/build"
    install -m 0644 \
        "${validation_build_dir}/FoloToy-AI-Passport-full.bin" \
        "${repo_root}/build/FoloToy-AI-Passport-full.bin"
    echo "Firmware build: PASS"
)

cd "${repo_root}"
case "${mode}" in
    --all)
        run_static_checks
        run_firmware_checks
        ;;
    --static)
        run_static_checks
        ;;
    --firmware)
        run_firmware_checks
        ;;
    *)
        usage
        exit 2
        ;;
esac
