# streaming-proj-qpsolver

Standalone v2 implementation scaffold for streaming projection QP with:
- block-sparse structure contract and deterministic mode routing
- certified floating-point feasibility primitives
- hierarchical violator oracle (incremental local blocks + tournament tree + global full rescan)
- streaming workload generator (iid + correlated block-sparse + burst variants)
- gate-integrity hardening (claim preconditions, G_stream, G6, alloc tracing)

Current implementation status:
- Stage 0: complete (repo/build/config scaffolding)
- Stage 1: complete (data model + structure validation + dedicated RHS)
- Stage 2: complete (certified arithmetic + brute-force reference scan)
- Stage 3: complete (hierarchical oracle + incremental update path + 100k correctness gate)
- Stage 4: complete (active-set core with add/remove loop, Cholesky-based solves, touched-block oracle integration, projector correctness/churn tests)
- Stage 5: complete (strict-interior shrink policy, tau-required safety checks, fail-closed ray-scale fallback, kappa validation and safety tests)
- Stage 6: complete (baseline harness + G_stream/G1/G2/G3/G4/G5/G6 + concrete artifact schema)
- Stage 7+: in progress (evaluation + figures/tables runbook)

## Build

```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j
ctest --output-on-failure
```

## Smoke run

```bash
./bench_projector --config ../configs/default_block256_debug.yaml
./bench_oracle_correctness --config ../configs/default_block256_debug.yaml --trials 100000 --max-updated-blocks 4
./bench_baselines --config ../configs/default_block256_debug.yaml --out-dir ../results/stage6_smoke --warmup-ticks 100 --latency-ticks 2000 --feas-ticks 5000 --sample-ticks 200
../tools/run_stage6_gates.sh ../configs/default_block256_debug.yaml ../results/stage6_gate_smoke 0 0 0 50
```

`bench_baselines` writes:
- `summary.json`
- `gates.json`
- `tick_samples.csv`
- `latency.hdr`

Notes:
- OSQP comparison is wired via baseline API and reported in `gates.json` (`osqp_available` + `g4_reference_backend`).
- Claim-setting runs (`--claim-setting 1`) are hard-gated to Linux x86_64 + pinned CPU + non-`iid_dense` stream.
- `run_stage6_gates.sh` supports macOS-equivalent G3 checks via `MACOS_EQUIV=1`:
  - `./tools/run_stage6_gates.sh <cfg> <out> 1 0 0 50 1 140 750`
  - This enforces p95/p99 latency bounds while still requiring G_stream/G1/G2/G4/G5/G6.

## PR5 Workload Bundle

Run the full macOS-equivalent workload suite (W1/W2/W3/W4/W5) and generate tables/figures:

```bash
./tools/run_wow_eval.py --profile macos_equiv --out-root results/pr5_wow_macos_equiv_v2
```

Artifacts:
- `manifest.json`
- `table_gate_summary.csv`
- `table_gate_summary.md`
- `figures/fig_latency_cdf.png`
- `figures/fig_fallback_rate.png`
- `figures/fig_g4_abs_gap.png`
- `figures/table_quantiles.csv`
- `figures/table_gates.csv`

## Linux Performance Tooling Gates

Claim-setting runs must be Linux x86_64. The repo now includes:
- `tools/run_linux_perf_gates.sh` (or equivalent fallback on non-Linux)
- `tools/parse_perf_stat.py` (perf CSV/text to JSON)
- `tools/check_zero_alloc.sh` (strace syscall tracing on Linux; equivalent process-memory gate elsewhere)

Run:

```bash
./tools/run_linux_perf_gates.sh configs/default_block256.yaml results/linux_perf_gate
```
