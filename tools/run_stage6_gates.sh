#!/usr/bin/env bash
set -euo pipefail

CONFIG="${1:-configs/default_block256_debug.yaml}"
OUT_DIR="${2:-results/stage6_gate}"
ENFORCE_G3="${3:-0}"
CLAIM_SETTING="${4:-0}"
ALLOW_G4_SKIP="${5:-0}"
G3_TARGET_US="${6:-50.0}"
MACOS_EQUIV="${7:-0}"
G3_EQUIV_P95_MAX="${8:-140.0}"
G3_EQUIV_P99_MAX="${9:-750.0}"
BENCH="./build/bench_baselines"

mkdir -p "$OUT_DIR"

if [[ ! -x "$BENCH" ]]; then
  echo "Missing $BENCH. Build first:"
  echo "  mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build . -j"
  exit 2
fi

echo "[stage6] running bench_baselines"
"$BENCH" --config "$CONFIG" --out-dir "$OUT_DIR" \
  --claim-setting "$CLAIM_SETTING" --allow-g4-skip "$ALLOW_G4_SKIP" \
  --g3-target-us "$G3_TARGET_US"

if [[ ! -f "$OUT_DIR/gates.json" ]]; then
  echo "[stage6] missing gates.json"
  exit 3
fi

python3 - "$OUT_DIR/gates.json" "$OUT_DIR/summary.json" "$ENFORCE_G3" "$CLAIM_SETTING" "$ALLOW_G4_SKIP" "$MACOS_EQUIV" "$G3_EQUIV_P95_MAX" "$G3_EQUIV_P99_MAX" <<'PY'
import json
import sys

path = sys.argv[1]
summary_path = sys.argv[2]
enforce_g3 = sys.argv[3] == "1"
claim_setting = sys.argv[4] == "1"
allow_g4_skip = sys.argv[5] == "1"
macos_equiv = sys.argv[6] == "1"
g3_equiv_p95_max = float(sys.argv[7])
g3_equiv_p99_max = float(sys.argv[8])
with open(path, "r", encoding="utf-8") as f:
    g = json.load(f)

summary = {}
if summary_path:
    try:
        with open(summary_path, "r", encoding="utf-8") as f:
            summary = json.load(f)
    except FileNotFoundError:
        summary = {}

required = [
    ("G_stream_pass", bool),
    ("G1_oracle_match", bool),
    ("G2_feas_violations", int),
    ("G3_pass", bool),
    ("G4_status", str),
    ("G4_pass", bool),
    ("G5_pass", bool),
    ("G6_pass", bool),
    ("alloc_calls_during_loop", int),
    ("bytes_allocated_during_loop", int),
    ("structure_valid", bool),
]
for key, typ in required:
    if key not in g:
        raise SystemExit(f"[stage6] missing key in gates.json: {key}")
    if not isinstance(g[key], typ):
        raise SystemExit(f"[stage6] invalid type for {key}: expected {typ.__name__}")

gate_ok = (
    bool(g["G1_oracle_match"]) and
    int(g["G2_feas_violations"]) == 0 and
    bool(g["G5_pass"]) and
    bool(g["G6_pass"])
)
if claim_setting:
    gate_ok = gate_ok and bool(g["G_stream_pass"])
g4_status = g["G4_status"]
if claim_setting:
    if g4_status == "SKIPPED":
        gate_ok = gate_ok and allow_g4_skip and bool(g["G4_pass"])
    else:
        gate_ok = gate_ok and bool(g["G4_pass"]) and g4_status == "PASS"
else:
    if g4_status == "FAIL":
        gate_ok = False
    elif g4_status == "PASS":
        gate_ok = gate_ok and bool(g["G4_pass"])

if bool(g["structure_valid"]) and enforce_g3:
    if macos_equiv:
        lat = summary.get("latency", {})
        g3_equiv_pass = (
            float(lat.get("p95_us", float("inf"))) <= g3_equiv_p95_max and
            float(lat.get("p99_us", float("inf"))) <= g3_equiv_p99_max
        )
        gate_ok = gate_ok and g3_equiv_pass
    else:
        gate_ok = gate_ok and bool(g["G3_pass"])

print("[stage6] gate summary:",
      "G_stream=", g["G_stream_pass"],
      "G1=", g["G1_oracle_match"],
      "G2_viol=", g["G2_feas_violations"],
      "G3=", g["G3_pass"],
      "G5=", g["G5_pass"],
      "G6=", g["G6_pass"],
      "alloc_calls=", g["alloc_calls_during_loop"],
      "alloc_bytes=", g["bytes_allocated_during_loop"],
      "G4_status=", g["G4_status"],
      "G4=", g["G4_pass"],
      "macos_equiv=", macos_equiv,
      "mode=", g.get("mode"),
      "backend=", g.get("g4_reference_backend"))
if not gate_ok:
    raise SystemExit(4)
PY

echo "[stage6] PASS"
