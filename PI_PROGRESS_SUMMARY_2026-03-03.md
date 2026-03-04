# SPQS v2 Progress Summary (PI Handoff)

Date: 2026-03-03
Prepared by: Codex run assistant

## What has been implemented so far

This snapshot includes implementation and validation work across Stage/PR tracks, including:

- PR0 integrity and safety gating hardening:
  - real in-process allocation counters/gates
  - strict G4 behavior semantics (no false PASS when OSQP unavailable)
  - expanded gate/log schema fields for claim/dev mode consistency
- PR1 inactive-only oracle behavior:
  - active masks in hierarchical oracle
  - structured fast path uses inactive-only oracle result
  - brute-force inactive escape hatch removed from structured loop
- PR2 rank-1 active-set maintenance:
  - rank-1 Cholesky insert/remove integration
  - persistent active-set factor reuse across ticks
- PR3 allocation-free hot-loop improvements:
  - hot-path scratch reuse/preallocation
  - reduced dynamic allocations to zero during measured loop after warmup
- PR4 one-pass fallback:
  - one-pass ray-scale fallback (no iterative bisection loop in fallback path)
- PR5 strict-interior safety path updates:
  - strict interiorization logic retained with no extra post-convergence full scan
  - bound/certification refinements and stability checks
- PR6 measurement and baseline reliability:
  - OSQP baseline integration wired and running (not stub)
  - claim/dev benchmark behavior updates

Key touched files include:
- `CMakeLists.txt`
- `benches/bench_baselines.cpp`
- `baselines/osqp_wrapper.cpp`
- `src/projector.cpp`
- `include/spqs/projector.hpp`
- `src/violator_oracle.cpp`
- `include/spqs/violator_oracle.hpp`
- `src/safefallback.cpp`
- `src/chol_rank1.cpp`
- `include/spqs/chol_rank1.hpp`
- `src/linalg_small.cpp`
- `include/spqs/linalg_small.hpp`
- `tools/run_stage6_gates.sh`
- `tools/run_linux_perf_gates.sh`
- `tools/check_zero_alloc.sh`

## Current run status and what we are doing now

The long canonical macOS-equivalent run has completed, and this packet is now being prepared for PI handoff.

Current activity (right now):
- no benchmark process is currently running
- run artifacts have been validated on disk
- this summary and full-project zip are being produced

## Current results (specific)

### A) Full long macOS-equivalent run (completed)
Artifact dir:
- `results/stage6_full_default_postpr56b_20260303/`

Gate file:
- `results/stage6_full_default_postpr56b_20260303/gates.json`

Results:
- G1: PASS
- G2: PASS (`feas_violations=0`, `audit_failures=0`)
- G3: FAIL (`p99.99_us = 10327.208`)
- G4: PASS (`abs_max = 9.9095969965175397e-23`, `rel_max = 9.9095969965175397e-23`)
- G5: PASS (`alloc_calls_during_loop=0`, `bytes_allocated_during_loop=0`)

### B) Claim-mode smoke validation (strict thresholds)
Artifact dir:
- `results/pr56d_claim_smoke/`

Gate file:
- `results/pr56d_claim_smoke/gates.json`

Results:
- G1/G2/G5: PASS
- G3: FAIL (`p99.99_us = 10572.208`)
- G4: close but FAIL (`abs_max = 3.2360958357458003e-10`, threshold `1e-10`)

## G3 failure explanation (including p95 ~ 40.7us)

Measured from long-run sampled ticks (`tick_samples.csv`):
- p95: ~40.667 us
- p99: ~72.292 us
- p99.9: ~2475.791 us
- p99.99: ~10535.125 us
- max sampled outlier: ~40997.292 us

Interpretation:
- Most ticks are fast (p95 around 40.7 us), which indicates core-path performance is strong.
- G3 fails because rare expensive events dominate extreme tail percentiles:
  - fallback/high-iteration events appear with `iters ~ 96-98`, often touching all blocks
  - fallback sample rate is low (~0.43%) but still far too high for a 99.99% tail target
  - at p99.99, even very rare slow events can dominate the metric

Likely issue:
- Primarily project-path behavior (rare heavy solver/fallback episodes), not just environment noise.
- macOS scheduling/jitter contributes additional outliers, but the recurring high-iteration/fallback pattern is algorithmic/runtime behavior that must be reduced for true tail-latency claims.

## Practical interpretation for PI

- Feasibility/correctness and no-allocation claims are in good shape.
- OSQP comparison can pass strongly in dev-equivalent long runs.
- The remaining major blocker is G3 tail latency robustness under rare hard ticks.
- Priority next work is reducing frequency/cost of high-iteration/fallback events, then validating on Linux claim machine.

