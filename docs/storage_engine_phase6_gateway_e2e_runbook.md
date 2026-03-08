# StorageEngine Phase6 Gateway E2E Runbook

## Goal

Use a mock client to run a real `gateway -> logic -> storage` end-to-end drill:

1. Login through `mir2_gateway`
2. Create role
3. Select role
4. Move once
5. Disconnect
6. Verify persistence

The current pass criteria accept either:

1. `login/create_role/select_role/move/disconnect` all succeed
2. `baseline_version` is captured before `MoveReq`
3. `snapshot_version >= baseline_version + 1`
4. `version_delta >= 1`
5. `actual_x == move_target_x`
6. `actual_y == move_target_y`
7. verify tries L2 first, then PostgreSQL fallback, inside one 5-second window

## Prerequisites

1. Required binaries built:

```bash
cmake --build --preset vcpkg-wsl-debug \
  --target mir2_gateway mir2_logic mir2_storage_admin \
           mir2_storage_engine_phase6_fault_driver \
           mir2_storage_engine_phase6_gateway_mock_client \
  -j$(nproc)
```

2. PostgreSQL container is running:

```bash
docker ps --format '{{.Names}}'
```

Expected to include:

```text
legend2-postgres
```

3. Database connection values:

```bash
export PHASE6_DB_HOST=127.0.0.1
export PHASE6_DB_PORT=5432
export PHASE6_DB_USER=mir2
export PHASE6_DB_PASSWORD=mir2_password
export PHASE6_DB_NAME=mir2_game
```

## Run

```bash
ROOT="$(mktemp -d /tmp/mir2_phase6_gateway_e2e_XXXXXX)"

bash scripts/run_storage_engine_phase6_gateway_e2e_drill.sh \
  --scenario login_select_move_disconnect \
  --gateway-bin ./build-wsl/bin/mir2_gateway \
  --logic-bin ./build-wsl/bin/mir2_logic \
  --mock-client-bin ./build-wsl/bin/mir2_storage_engine_phase6_gateway_mock_client \
  --fault-driver-bin ./build-wsl/bin/mir2_storage_engine_phase6_fault_driver \
  --admin-bin ./build-wsl/bin/mir2_storage_admin \
  --gateway-e2e-gate-script scripts/run_storage_engine_phase6_gateway_e2e_gate.sh \
  --config-template-gateway config/gateway.yaml \
  --config-template-logic config/logic.yaml \
  --db-host "${PHASE6_DB_HOST}" \
  --db-port "${PHASE6_DB_PORT}" \
  --db-user "${PHASE6_DB_USER}" \
  --db-password "${PHASE6_DB_PASSWORD}" \
  --db-name "${PHASE6_DB_NAME}" \
  --db-path "${ROOT}/db" \
  --backend-state-path "${ROOT}/backend_state.txt" \
  --report-file "${ROOT}/drill.report.txt" \
  --acceptance-csv "${ROOT}/acceptance.csv"
```

## Expected Success Signals

1. Drill output:

```text
phase6_gateway_e2e_drill_result status=pass scenario=login_select_move_disconnect
```

2. Gate output:

```text
phase6_gateway_e2e_gate_result status=pass scenario=login_select_move_disconnect
```

3. Client report contains:

```text
status=ok
login_rsp_code=ERR_OK
create_role_rsp_code=ERR_OK
select_role_rsp_code=ERR_OK
move_rsp_code=ERR_OK
disconnect_sent=true
player_id=<dynamic>
move_target_x=101
move_target_y=100
```

4. `storage_admin validate` remains clean:

```text
total_corrupted=0
```

## Verification Semantics

The drill now verifies persistence with a pre-move barrier:

1. The mock client logs in, creates/selects the role, writes `pre_move.ready`,
   and waits for `pre_move.continue`.
2. The drill reads `player_id` from `pre_move.ready` and captures `baseline_version`
   before the move is released.
3. After disconnect, verify runs 25 rounds with 200 ms spacing:
   `L2 -> PostgreSQL fallback -> sleep`
4. L2 reads come from:
   `mir2_storage_engine_phase6_fault_driver --scenario login_select_move_disconnect_verify --key char:<player_id>`
5. PostgreSQL fallback reads:
   `kv_store.version` plus `encode(data, 'hex')` where `is_deleted = FALSE`
6. Snapshot position decoding comes from:
   `mir2_storage_admin decode-character-snapshot --hex <hex> --expected-x <x> --expected-y <y>`

```text
phase6_gateway_e2e_verify_start player_id=<id> baseline_version=<n> move_target_x=101 move_target_y=100
phase6_gateway_e2e_verify_result verify_result=PASS|FAIL verify_stage=l2|postgres_fallback snapshot_source=l2|postgres l2_snapshot_present=true|false postgres_snapshot_present=true|false baseline_version=<n> snapshot_version=<n> version_delta=<n> move_target_x=101 move_target_y=100 actual_x=<x> actual_y=<y>
```

## Artifacts

The drill writes:

1. `phase6_gateway_e2e_login_select_move_disconnect.client.report.txt`
2. `phase6_gateway_e2e_login_select_move_disconnect.validate.txt`
3. `phase6_gateway_e2e_login_select_move_disconnect.logic.log`
4. `phase6_gateway_e2e_login_select_move_disconnect.gateway.log`
5. `phase6_gateway_e2e_login_select_move_disconnect.gate.report.txt`
6. `phase6_gateway_e2e_login_select_move_disconnect.verify.report.txt`
7. `phase6_gateway_e2e_login_select_move_disconnect.pre_move.ready`
8. `phase6_gateway_e2e_login_select_move_disconnect.pre_move.continue`
9. `acceptance.csv`
10. `drill.report.txt`

## Current Notes

1. The drill auto-seeds `phase6_user` into PostgreSQL.
2. The drill auto-applies the `kv_store.is_deleted` schema fix when missing.
3. The default role name is `phase6role`.
4. The default move target is `(101, 100)` to avoid out-of-range failures from the
   default spawn `(100, 100)`.
