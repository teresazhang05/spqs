#!/usr/bin/env bash
set -euo pipefail

CONFIG="${1:-configs/default_block256.yaml}"
OUT_DIR="${2:-results/linux_perf_gate}"
BENCH="./build/bench_projector"
BENCH_GATES="./build/bench_baselines"

mkdir -p "$OUT_DIR"

if [[ ! -x "$BENCH" ]]; then
  echo "Missing $BENCH. Build first:"
  echo "  mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build . -j"
  exit 2
fi
if [[ ! -x "$BENCH_GATES" ]]; then
  echo "Missing $BENCH_GATES. Build first:"
  echo "  mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build . -j"
  exit 2
fi

OS_NAME="$(uname -s)"
echo "[perf-gate] host_os=$OS_NAME"

echo "[perf-gate] running projector benchmark"
"$BENCH" --config "$CONFIG" | tee "$OUT_DIR/bench_projector.out"

if [[ "$OS_NAME" == "Linux" ]]; then
  if command -v perf >/dev/null 2>&1; then
    echo "[perf-gate] collecting perf counters"
    perf stat --no-big-num -x, \
      -e cycles,instructions,branches,branch-misses,LLC-loads,LLC-load-misses,cache-misses \
      -o "$OUT_DIR/perf_stat.csv" \
      -- "$BENCH" --config "$CONFIG" >/dev/null 2>&1

    ./tools/parse_perf_stat.py --input "$OUT_DIR/perf_stat.csv" --output "$OUT_DIR/perf_stat.json" --format csv

    echo "[perf-gate] attempting RAPL energy collection"
    if perf stat --no-big-num -x, -e power/energy-pkg/ -o "$OUT_DIR/rapl_energy.csv" -- "$BENCH" --config "$CONFIG" >/dev/null 2>&1; then
      ./tools/parse_perf_stat.py --input "$OUT_DIR/rapl_energy.csv" --output "$OUT_DIR/rapl_energy.json" --format csv
      echo "[perf-gate] RAPL energy collected"
    else
      echo "[perf-gate] RAPL not available on this host/kernel permissions"
    fi
  else
    echo "[perf-gate] perf not found; skip perf counters"
  fi

  echo "[perf-gate] running in-process allocation gate"
  ./tools/check_zero_alloc.sh --out "$OUT_DIR/alloc_gate.log" -- \
    "$BENCH_GATES" --config "$CONFIG" --out-dir "$OUT_DIR/alloc_gate_run" \
    --warmup-ticks 100000 --latency-ticks 50000 --feas-ticks 50000 --sample-ticks 100 --claim-setting 0
else
  echo "[perf-gate] non-Linux host: running equivalent memory/time gate"
  ./tools/check_zero_alloc.sh --out "$OUT_DIR/equivalent_mem_time.log" -- \
    "$BENCH_GATES" --config "$CONFIG" --out-dir "$OUT_DIR/alloc_gate_run" \
    --warmup-ticks 100000 --latency-ticks 50000 --feas-ticks 50000 --sample-ticks 100 --claim-setting 0
fi

echo "[perf-gate] complete; artifacts in $OUT_DIR"
