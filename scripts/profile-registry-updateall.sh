#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  scripts/profile-registry-updateall.sh \
    --logic-pid <pid> \
    --metrics-url <http://127.0.0.1:9091/metrics> \
    [--duration-sec 180] \
    [--sample-interval-sec 1] \
    [--out-dir artifacts/profile_registry_updateall_<ts>] \
    [--perf-freq 99]

Description:
  Collects logic tick metrics relevant to RegistryManager::UpdateAll bottleneck checks:
    - logic_tick_duration_ms
    - logic_tick_phase_ecs_registry_update_ms
    - logic_tick_phase_ecs_world_systems_ms

  Optionally runs perf sampling against the logic process during the same window.
USAGE
}

LOGIC_PID=""
METRICS_URL=""
DURATION_SEC=180
SAMPLE_INTERVAL_SEC=1
OUT_DIR=""
PERF_FREQ=99

while [[ $# -gt 0 ]]; do
  case "$1" in
    --logic-pid)
      LOGIC_PID="$2"
      shift 2
      ;;
    --metrics-url)
      METRICS_URL="$2"
      shift 2
      ;;
    --duration-sec)
      DURATION_SEC="$2"
      shift 2
      ;;
    --sample-interval-sec)
      SAMPLE_INTERVAL_SEC="$2"
      shift 2
      ;;
    --out-dir)
      OUT_DIR="$2"
      shift 2
      ;;
    --perf-freq)
      PERF_FREQ="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ -z "$LOGIC_PID" || -z "$METRICS_URL" ]]; then
  echo "--logic-pid and --metrics-url are required." >&2
  usage
  exit 1
fi

if ! kill -0 "$LOGIC_PID" 2>/dev/null; then
  echo "logic pid not running: $LOGIC_PID" >&2
  exit 1
fi

if [[ -z "$OUT_DIR" ]]; then
  OUT_DIR="artifacts/profile_registry_updateall_$(date -u +%Y%m%d_%H%M%S)"
fi
mkdir -p "$OUT_DIR"

CSV_PATH="$OUT_DIR/registry_updateall_metrics.csv"
SUMMARY_PATH="$OUT_DIR/profiling_summary.txt"
PERF_DATA_PATH="$OUT_DIR/perf.data"
PERF_REPORT_PATH="$OUT_DIR/perf_report.txt"

fetch_metric() {
  local metrics_text="$1"
  local metric_name="$2"
  awk -v k="$metric_name" '$1 == k {print $2; found=1; exit} END {if (!found) print 0}' <<<"$metrics_text"
}

calc_p95_from_stream() {
  local values="$1"
  if [[ -z "$values" ]]; then
    echo "0"
    return
  fi
  awk '{print $1}' <<<"$values" | sort -n | awk '
    {vals[NR]=$1}
    END {
      if (NR == 0) { print 0; exit }
      idx = int((NR - 1) * 0.95) + 1
      print vals[idx]
    }
  '
}

perf_pid=""
if command -v perf >/dev/null 2>&1; then
  (
    perf record -F "$PERF_FREQ" -g -p "$LOGIC_PID" -o "$PERF_DATA_PATH" -- sleep "$DURATION_SEC" >/dev/null 2>&1 || true
  ) &
  perf_pid="$!"
else
  echo "perf not found, metrics-only profiling." >&2
fi

printf 'unix_ts,logic_tick_duration_ms,logic_tick_phase_ecs_registry_update_ms,logic_tick_phase_ecs_world_systems_ms\n' > "$CSV_PATH"

end_ts=$(( $(date +%s) + DURATION_SEC ))
while [[ $(date +%s) -lt $end_ts ]]; do
  now_ts="$(date +%s)"
  metrics_text="$(curl -fsS --max-time 2 "$METRICS_URL" || true)"

  tick_ms="0"
  registry_ms="0"
  world_systems_ms="0"

  if [[ -n "$metrics_text" ]]; then
    tick_ms="$(fetch_metric "$metrics_text" "logic_tick_duration_ms")"
    registry_ms="$(fetch_metric "$metrics_text" "logic_tick_phase_ecs_registry_update_ms")"
    world_systems_ms="$(fetch_metric "$metrics_text" "logic_tick_phase_ecs_world_systems_ms")"
  fi

  printf '%s,%s,%s,%s\n' "$now_ts" "$tick_ms" "$registry_ms" "$world_systems_ms" >> "$CSV_PATH"
  sleep "$SAMPLE_INTERVAL_SEC"
done

if [[ -n "$perf_pid" ]]; then
  wait "$perf_pid" || true
  if [[ -f "$PERF_DATA_PATH" ]]; then
    perf report --stdio -i "$PERF_DATA_PATH" > "$PERF_REPORT_PATH" 2>/dev/null || true
  fi
fi

tick_values="$(awk -F, 'NR > 1 {print $2}' "$CSV_PATH")"
registry_values="$(awk -F, 'NR > 1 {print $3}' "$CSV_PATH")"
world_values="$(awk -F, 'NR > 1 {print $4}' "$CSV_PATH")"
share_values="$(awk -F, 'NR > 1 && $2 > 0 {print ($3 / $2) * 100.0}' "$CSV_PATH")"

p95_tick="$(calc_p95_from_stream "$tick_values")"
p95_registry="$(calc_p95_from_stream "$registry_values")"
p95_world="$(calc_p95_from_stream "$world_values")"
p95_share="$(calc_p95_from_stream "$share_values")"

cat > "$SUMMARY_PATH" <<EOF_SUMMARY
profile_target=RegistryManager::UpdateAll
logic_pid=$LOGIC_PID
metrics_url=$METRICS_URL
sample_duration_sec=$DURATION_SEC
sample_interval_sec=$SAMPLE_INTERVAL_SEC
metrics_csv=$CSV_PATH
perf_data=${PERF_DATA_PATH}
perf_report=${PERF_REPORT_PATH}
logic_tick_duration_ms_p95=$p95_tick
logic_tick_phase_ecs_registry_update_ms_p95=$p95_registry
logic_tick_phase_ecs_world_systems_ms_p95=$p95_world
registry_update_share_of_tick_p95_percent=$p95_share
EOF_SUMMARY

cat "$SUMMARY_PATH"
