# Gateway-Logic IPC Deployment Matrix

## Recommended Matrix

| Scenario | Recommended Config | Expected Transport | Notes |
|---|---|---|---|
| Single-host deployment (Gateway + Logic on same machine) | `services.logic.transport: auto` + non-empty `services.logic.uds_path` | UDS (preferred), TCP fallback on UDS failure | Lowest local IPC overhead, keeps availability via fallback |
| Cross-host deployment (Gateway and Logic on different machines) | `services.logic.transport: tcp` | TCP | UDS is local-only; use explicit TCP for clarity |

## Minimal Config Examples

### Single-host (recommended)
```yaml
services:
  logic:
    host: "127.0.0.1"
    port: 8002
    transport: "auto"
    uds_path: "/tmp/mir2_logic.sock"
```

### Cross-host (recommended)
```yaml
services:
  logic:
    host: "10.0.0.25"
    port: 8002
    transport: "tcp"
```

## Failure-mode Behavior

- `transport: auto` (Gateway side):
  - If host is loopback (`127.0.0.1`, `::1`, `localhost`) and `uds_path` is set, Gateway tries UDS first.
  - If UDS connect fails, Gateway logs a warning and falls back to TCP.
- `transport: auto` (Logic side):
  - If `uds_path` is set, Logic tries UDS listen first.
  - If UDS listen fails, Logic logs a warning and falls back to TCP listen.
- `transport: uds`:
  - Requires valid `uds_path`.
  - No automatic TCP fallback; failure is fail-fast.
- Non-UNIX platforms:
  - UDS is unsupported; use `transport: tcp`.

