#!/usr/bin/env bash
set -euo pipefail

OUT_FILE="results/alloc_gate.log"
STRICT=1

usage() {
  cat <<USAGE
Usage: $0 [--out PATH] [--strict|--non-strict] [--] <command...>

Examples:
  $0 -- ./build/bench_baselines --config configs/default_block256_debug.yaml --out-dir results/alloc_gate
  $0 --non-strict -- ./build/bench_projector --config configs/default_block256_debug.yaml
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --out)
      OUT_FILE="$2"
      shift 2
      ;;
    --strict)
      STRICT=1
      shift
      ;;
    --non-strict)
      STRICT=0
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    *)
      break
      ;;
  esac
done

if [[ $# -eq 0 ]]; then
  CMD=(
    "./build/bench_baselines"
    "--config" "configs/default_block256_debug.yaml"
    "--out-dir" "results/alloc_gate_default"
    "--warmup-ticks" "1000"
    "--latency-ticks" "20000"
    "--feas-ticks" "20000"
    "--sample-ticks" "100"
    "--claim-setting" "0"
  )
else
  CMD=("$@")
fi

mkdir -p "$(dirname "$OUT_FILE")"
echo "[zero-alloc] running: ${CMD[*]}"
if ! "${CMD[@]}" >"$OUT_FILE" 2>&1; then
  echo "[zero-alloc] command failed, see $OUT_FILE"
  exit 2
fi

ALLOC_CALLS="$(grep -Eo 'alloc_calls_during_loop=[0-9]+' "$OUT_FILE" | tail -n1 | cut -d= -f2 || true)"
ALLOC_BYTES="$(grep -Eo 'bytes_allocated_during_loop=[0-9]+' "$OUT_FILE" | tail -n1 | cut -d= -f2 || true)"

if [[ -z "${ALLOC_CALLS}" || -z "${ALLOC_BYTES}" ]]; then
  OUT_DIR=""
  for ((i = 0; i + 1 < ${#CMD[@]}; ++i)); do
    if [[ "${CMD[$i]}" == "--out-dir" ]]; then
      OUT_DIR="${CMD[$((i + 1))]}"
      break
    fi
  done

  if [[ -n "$OUT_DIR" && -f "$OUT_DIR/gates.json" ]]; then
    read -r ALLOC_CALLS ALLOC_BYTES < <(python3 - "$OUT_DIR/gates.json" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as f:
    g = json.load(f)
print(int(g.get("alloc_calls_during_loop", -1)), int(g.get("bytes_allocated_during_loop", -1)))
PY
)
  fi
fi

if [[ -z "${ALLOC_CALLS}" || -z "${ALLOC_BYTES}" ]]; then
  echo "[zero-alloc] failed to extract allocation counters from output/gates.json"
  echo "[zero-alloc] output file: $OUT_FILE"
  exit 3
fi

echo "[zero-alloc] alloc_calls_during_loop=$ALLOC_CALLS"
echo "[zero-alloc] bytes_allocated_during_loop=$ALLOC_BYTES"
echo "[zero-alloc] log=$OUT_FILE"

if [[ "$STRICT" -eq 1 ]] && { [[ "$ALLOC_CALLS" -ne 0 ]] || [[ "$ALLOC_BYTES" -ne 0 ]]; }; then
  echo "[zero-alloc] STRICT mode failed: loop allocations detected"
  exit 1
fi

echo "[zero-alloc] PASS"
