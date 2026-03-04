#!/usr/bin/env python3
"""Run PR5 workload suite and emit aggregate tables/plots."""

from __future__ import annotations

import argparse
import csv
import json
import platform
import subprocess
import sys
from pathlib import Path
from typing import Dict, List


def write_config(
    path: Path,
    *,
    run_name: str,
    n: int,
    b: int,
    block_size: int,
    m_local_per_block: int,
    m_global: int,
    stream_mode: str,
    burst_enabled: bool,
    t_warmup: int,
    t_latency: int,
    t_feas: int,
    b_margin: float = 10.0,
    b_noise_std: float = 0.20,
    ar_rho: float = 0.995,
    k_small: int = 1,
    k_small_alt: int = 2,
    p_k_small_alt: float = 0.02,
    delta_small: float = 0.20,
    p_jump: float = 0.001,
    k_jump: int = 4,
    delta_jump: float = 2.0,
    burst_every_ticks: int = 2000000,
    burst_length_ticks: int = 20000,
    burst_k: int = 6,
    burst_delta: float = 2.0,
    burst_p_jump: float = 0.01,
    burst_jump_k: int = 10,
    burst_jump_delta: float = 8.0,
) -> None:
    block_sizes = ",".join([str(block_size)] * b)
    text = f"""run:
  run_name: "{run_name}"
  seed: 12345
  out_dir: "results/{run_name}"
  threads: 1
  pin_cpu: 2
  disable_turbo: false
  disable_smt: false

problem:
  n: {n}
  a_max: 96
  I_max: 200

structure:
  mode: "block_sparse"
  B: {b}
  block_sizes: [{block_sizes}]
  m_local_per_block: {m_local_per_block}
  m_global: {m_global}

generator:
  A_local_type: "dense_in_block_normalized"
  A_global_type: "factor_plus_gross"
  factors: 8
  row_norm: "l2_unit"
  b_margin: {b_margin}
  b_noise_std: {b_noise_std}

solver:
  warm_start: true
  bland_rule: true
  feasibility: "certified"
  q_anchor: "zero"
  strict_interior: true
  kappa_min: 1.0e-6
  tau_abs_scale: 8.0
  tau_shrink_min: 0.0
  tau_shrink_max: 1.0e-3
  fallback_enabled: true
  fallback_mode: "ray_scale_to_anchor"

stream:
  mode: "{stream_mode}"
  seed_stream: 424242
  T_warmup: {t_warmup}
  T_latency: {t_latency}
  T_feas: {t_feas}
  ar_rho: {ar_rho}
  K_small: {k_small}
  K_small_alt: {k_small_alt}
  p_K_small_alt: {p_k_small_alt}
  delta_small: {delta_small}
  p_jump: {p_jump}
  K_jump: {k_jump}
  delta_jump: {delta_jump}
  clamp_inf: 100.0
  burst:
    enabled: {"true" if burst_enabled else "false"}
    every_ticks: {burst_every_ticks}
    length_ticks: {burst_length_ticks}
    K_burst: {burst_k}
    delta_burst: {burst_delta}
    p_jump_in_burst: {burst_p_jump}
    K_jump_in_burst: {burst_jump_k}
    delta_jump_in_burst: {burst_jump_delta}
  q_small: 1.0
  q_big: 50.0
  p_big: 0.005

instrumentation:
  latency_clock: "rdtsc"
  perf:
    enabled: false
  energy:
    enabled: false
"""
    path.write_text(text, encoding="utf-8")


def run_cmd(cmd: List[str], log_path: Path) -> int:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8") as f:
        proc = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT)
    return proc.returncode


def evaluate_gate_pass(g: Dict, summary: Dict, w: Dict, profile: str) -> bool:
    lat = summary.get("latency", {})
    g3_equiv_pass = bool(g.get("G3_pass", False))
    if profile == "macos_equiv":
        p95 = float(lat.get("p95_us", 0.0))
        p99 = float(lat.get("p99_us", 0.0))
        g3_equiv_pass = (p95 <= float(w["g3_equiv_p95_max"])) and (p99 <= float(w["g3_equiv_p99_max"]))
    return (
        bool(g.get("G_stream_pass", False))
        and bool(g.get("G1_oracle_match", False))
        and int(g.get("G2_feas_violations", 1)) == 0
        and g3_equiv_pass
        and g.get("G4_status", "FAIL") == "PASS"
        and bool(g.get("G4_pass", False))
        and bool(g.get("G5_pass", False))
        and bool(g.get("G6_pass", False))
    )


def write_aggregate_tables(records: List[Dict], out_root: Path) -> None:
    csv_path = out_root / "table_gate_summary.csv"
    with csv_path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(
            [
                "workload",
                "pass",
                "G_stream_pass",
                "G1_oracle_match",
                "G2_feas_violations",
                "G3_p99_99_us",
                "G3_pass",
                "G3_equiv_pass",
                "G4_status",
                "G4_pass",
                "G5_pass",
                "G6_pass",
                "fallback_rate",
                "out_dir",
            ]
        )
        for rec in records:
            g = rec["gates"]
            s = rec["summary"]
            w.writerow(
                [
                    rec["name"],
                    rec["pass"],
                    g.get("G_stream_pass", False),
                    g.get("G1_oracle_match", False),
                    g.get("G2_feas_violations", 0),
                    g.get("G3_p99_99_us", 0.0),
                    g.get("G3_pass", False),
                    rec.get("g3_equiv_pass", False),
                    g.get("G4_status", "UNKNOWN"),
                    g.get("G4_pass", False),
                    g.get("G5_pass", False),
                    g.get("G6_pass", False),
                    s.get("fallback_rate", 0.0),
                    rec["out_dir"],
                ]
            )

    md_path = out_root / "table_gate_summary.md"
    with md_path.open("w", encoding="utf-8") as f:
        f.write("| workload | pass | G3 p99.99us | G4 status | fallback_rate |\n")
        f.write("|---|---:|---:|---|---:|\n")
        for rec in records:
            g = rec["gates"]
            s = rec["summary"]
            f.write(
                f"| {rec['name']} | {str(rec['pass']).lower()} | "
                f"{g.get('G3_p99_99_us', 0.0):.3f} | {g.get('G4_status', 'UNKNOWN')} | "
                f"{s.get('fallback_rate', 0.0):.6g} |\n"
            )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", choices=["macos_equiv"], default="macos_equiv")
    parser.add_argument("--out-root", default="results/wow_eval_macos_equiv")
    parser.add_argument("--bench", default="./build/bench_baselines")
    parser.add_argument("--reuse-existing", action="store_true")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    out_root = (repo / args.out_root).resolve()
    out_root.mkdir(parents=True, exist_ok=True)

    bench = (repo / args.bench).resolve()
    if not bench.exists():
        raise SystemExit(f"missing bench binary: {bench}")

    claim_setting = "0"
    if platform.system() != "Linux":
        print("run_wow_eval: running macOS-equivalent mode (CLAIM=0)")

    generated_cfg_dir = out_root / "generated_configs"
    generated_cfg_dir.mkdir(parents=True, exist_ok=True)

    write_config(
        generated_cfg_dir / "w2_burst_macos.yaml",
        run_name="w2_burst_macos",
        n=256,
        b=16,
        block_size=16,
        m_local_per_block=64,
        m_global=32,
        stream_mode="correlated_block_sparse_burst",
        burst_enabled=True,
        t_warmup=2000,
        t_latency=12000,
        t_feas=50000,
        ar_rho=0.997,
        k_small=1,
        k_small_alt=2,
        p_k_small_alt=0.02,
        delta_small=0.15,
        p_jump=0.0002,
        k_jump=3,
        delta_jump=0.9,
        burst_every_ticks=400000,
        burst_length_ticks=1500,
        burst_k=3,
        burst_delta=0.7,
        burst_p_jump=0.002,
        burst_jump_k=4,
        burst_jump_delta=1.4,
    )
    write_config(
        generated_cfg_dir / "w3_degeneracy_macos.yaml",
        run_name="w3_degeneracy_macos",
        n=256,
        b=16,
        block_size=16,
        m_local_per_block=128,
        m_global=32,
        stream_mode="correlated_block_sparse",
        burst_enabled=False,
        t_warmup=1000,
        t_latency=10000,
        t_feas=30000,
        b_margin=6.0,
        b_noise_std=0.05,
        ar_rho=0.997,
        k_small=1,
        k_small_alt=2,
        p_k_small_alt=0.01,
        delta_small=0.12,
        p_jump=0.0002,
        k_jump=3,
        delta_jump=0.8,
    )

    # W4 scaling configs.
    scale_mlocal_vals = [64, 128, 256]
    for v in scale_mlocal_vals:
        write_config(
            generated_cfg_dir / f"scale_mlocal_{v}.yaml",
            run_name=f"w4_scale_mlocal_{v}",
            n=256,
            b=16,
            block_size=16,
            m_local_per_block=v,
            m_global=32,
            stream_mode="correlated_block_sparse",
            burst_enabled=False,
            t_warmup=2000,
            t_latency=8000,
            t_feas=20000,
        )

    # W5 scale-n configs.
    scale_n_specs = [(128, 8), (256, 16), (512, 32)]
    for n, b in scale_n_specs:
        write_config(
            generated_cfg_dir / f"scale_n_{n}.yaml",
            run_name=f"w5_scale_n_{n}",
            n=n,
            b=b,
            block_size=16,
            m_local_per_block=64,
            m_global=32,
            stream_mode="correlated_block_sparse",
            burst_enabled=False,
            t_warmup=2000,
            t_latency=8000,
            t_feas=20000,
        )

    workloads: List[Dict] = [
        {
            "name": "w1_correlated",
            "config": repo / "configs/claim_block256_correlated.yaml",
            "g3_target": "200.0",
            "g3_equiv_p95_max": "70.0",
            "g3_equiv_p99_max": "180.0",
            "warmup": "2000",
            "latency": "12000",
            "feas": "50000",
            "sample": "2000",
        },
        {
            "name": "w2_burst",
            "config": generated_cfg_dir / "w2_burst_macos.yaml",
            "g3_target": "200.0",
            "g3_equiv_p95_max": "80.0",
            "g3_equiv_p99_max": "220.0",
            "warmup": "2000",
            "latency": "12000",
            "feas": "50000",
            "sample": "2000",
        },
        {
            "name": "w3_degeneracy",
            "config": generated_cfg_dir / "w3_degeneracy_macos.yaml",
            "g3_target": "220.0",
            "g3_equiv_p95_max": "90.0",
            "g3_equiv_p99_max": "250.0",
            "warmup": "1000",
            "latency": "10000",
            "feas": "30000",
            "sample": "1500",
        },
    ]
    workloads.extend(
        [
            {
                "name": f"w4_scale_mlocal_{v}",
                "config": generated_cfg_dir / f"scale_mlocal_{v}.yaml",
                "g3_target": "300.0",
                "g3_equiv_p95_max": "140.0",
                "g3_equiv_p99_max": "750.0",
                "warmup": "1000",
                "latency": "6000",
                "feas": "12000",
                "sample": "800",
            }
            for v in scale_mlocal_vals
        ]
    )
    workloads.extend(
        [
            {
                "name": f"w5_scale_n_{n}",
                "config": generated_cfg_dir / f"scale_n_{n}.yaml",
                "g3_target": "350.0",
                "g3_equiv_p95_max": "140.0",
                "g3_equiv_p99_max": "750.0",
                "warmup": "1000",
                "latency": "6000",
                "feas": "12000",
                "sample": "800",
            }
            for n, _ in scale_n_specs
        ]
    )

    records: List[Dict] = []
    any_fail = False
    for w in workloads:
        out_dir = out_root / w["name"]
        out_dir.mkdir(parents=True, exist_ok=True)
        if not args.reuse_existing:
            cmd = [
                str(bench),
                "--config",
                str(w["config"]),
                "--out-dir",
                str(out_dir),
                "--claim-setting",
                claim_setting,
                "--allow-g4-skip",
                "0",
                "--warmup-ticks",
                w["warmup"],
                "--latency-ticks",
                w["latency"],
                "--feas-ticks",
                w["feas"],
                "--sample-ticks",
                w["sample"],
                "--g3-target-us",
                w["g3_target"],
            ]
            log_path = out_dir / "bench.out"
            rc = run_cmd(cmd, log_path)
            if rc != 0:
                any_fail = True
                records.append(
                    {
                        "name": w["name"],
                        "pass": False,
                        "summary": {},
                        "gates": {},
                        "out_dir": str(out_dir),
                        "error": f"bench exit code {rc}",
                    }
                )
                continue
        elif not (out_dir / "summary.json").exists() or not (out_dir / "gates.json").exists():
            any_fail = True
            records.append(
                {
                    "name": w["name"],
                    "pass": False,
                    "summary": {},
                    "gates": {},
                    "out_dir": str(out_dir),
                    "error": "missing existing summary/gates for --reuse-existing",
                }
            )
            continue

        with (out_dir / "summary.json").open("r", encoding="utf-8") as f:
            summary = json.load(f)
        with (out_dir / "gates.json").open("r", encoding="utf-8") as f:
            gates = json.load(f)
        lat = summary.get("latency", {})
        g3_equiv_pass = bool(gates.get("G3_pass", False))
        if args.profile == "macos_equiv":
            g3_equiv_pass = (
                float(lat.get("p95_us", 0.0)) <= float(w["g3_equiv_p95_max"])
                and float(lat.get("p99_us", 0.0)) <= float(w["g3_equiv_p99_max"])
            )
        run_pass = evaluate_gate_pass(gates, summary, w, args.profile)
        any_fail = any_fail or (not run_pass)
        records.append(
            {
                "name": w["name"],
                "pass": run_pass,
                "g3_equiv_pass": g3_equiv_pass,
                "summary": summary,
                "gates": gates,
                "out_dir": str(out_dir),
            }
        )
        print(
            f"run_wow_eval: {w['name']} pass={run_pass} "
            f"p99.99={gates.get('G3_p99_99_us', 0.0):.3f}us "
            f"p95={lat.get('p95_us', 0.0):.3f}us p99={lat.get('p99_us', 0.0):.3f}us "
            f"G3_equiv={g3_equiv_pass} "
            f"G4={gates.get('G4_status', 'UNKNOWN')} "
            f"fallback_rate={summary.get('fallback_rate', 0.0):.6g}"
        )

    manifest_path = out_root / "manifest.json"
    manifest_path.write_text(json.dumps({"profile": args.profile, "runs": records}, indent=2), encoding="utf-8")
    write_aggregate_tables(records, out_root)

    plot_cmd = [sys.executable, str(repo / "tools/plot_all.py"), "--results-root", str(out_root)]
    plot_rc = subprocess.call(plot_cmd)
    if plot_rc != 0:
      any_fail = True

    print(f"run_wow_eval: artifacts in {out_root}")
    return 1 if any_fail else 0


if __name__ == "__main__":
    raise SystemExit(main())
