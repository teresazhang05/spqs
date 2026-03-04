#!/usr/bin/env python3
"""Run Stage-6 baseline/gate benchmark and optionally Linux perf-gate collection."""

from __future__ import annotations

import argparse
import platform
import subprocess
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default="configs/default_block256_debug.yaml")
    parser.add_argument("--out-dir", default="results/manual_bench")
    parser.add_argument("--linux-gates", action="store_true")
    args = parser.parse_args()

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    bench_cmd = ["./build/bench_baselines", "--config", args.config, "--out-dir", str(out_dir)]
    with (out_dir / "bench_baselines.out").open("w", encoding="utf-8") as f:
        rc = subprocess.call(bench_cmd, stdout=f, stderr=subprocess.STDOUT)
    if rc != 0:
        return rc

    if args.linux_gates:
        gate_cmd = ["./tools/run_linux_perf_gates.sh", args.config, str(out_dir / "linux_gate")]
        return subprocess.call(gate_cmd)

    if platform.system() != "Linux":
        print("run_bench: non-Linux host detected; use --linux-gates on Linux for perf/strace/RAPL")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
