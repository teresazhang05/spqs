#!/usr/bin/env python3
"""Generate PR5 figures/tables from benchmark artifact directories."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
from typing import Dict, List


def parse_latency_samples_us(latency_hdr_path: Path) -> List[float]:
    if not latency_hdr_path.exists():
        return []
    vals: List[float] = []
    in_samples = False
    with latency_hdr_path.open("r", encoding="utf-8") as f:
        for raw in f:
            line = raw.strip()
            if line == "samples_ns_begin":
                in_samples = True
                continue
            if line == "samples_ns_end":
                in_samples = False
                break
            if in_samples and line:
                vals.append(float(line) / 1000.0)
    return vals


def load_runs(results_root: Path) -> List[Dict]:
    runs: List[Dict] = []
    for child in sorted(results_root.iterdir()):
        if not child.is_dir():
            continue
        gates_path = child / "gates.json"
        summary_path = child / "summary.json"
        if not gates_path.exists() or not summary_path.exists():
            continue
        with gates_path.open("r", encoding="utf-8") as f:
            gates = json.load(f)
        with summary_path.open("r", encoding="utf-8") as f:
            summary = json.load(f)
        runs.append(
            {
                "name": child.name,
                "dir": child,
                "gates": gates,
                "summary": summary,
                "latency_samples_us": parse_latency_samples_us(child / "latency.hdr"),
            }
        )
    return runs


def write_tables(runs: List[Dict], out_dir: Path) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)

    quant_csv = out_dir / "table_quantiles.csv"
    with quant_csv.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(
            [
                "workload",
                "count",
                "p50_us",
                "p95_us",
                "p99_us",
                "p99_9_us",
                "p99_99_us",
                "max_us",
                "fallback_rate",
            ]
        )
        for run in runs:
            lat = run["summary"]["latency"]
            w.writerow(
                [
                    run["name"],
                    lat.get("count", 0),
                    lat.get("p50_us", 0.0),
                    lat.get("p95_us", 0.0),
                    lat.get("p99_us", 0.0),
                    lat.get("p99_9_us", 0.0),
                    lat.get("p99_99_us", 0.0),
                    lat.get("max_us", 0.0),
                    run["summary"].get("fallback_rate", 0.0),
                ]
            )

    gate_csv = out_dir / "table_gates.csv"
    with gate_csv.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(
            [
                "workload",
                "G_stream_pass",
                "G1_oracle_match",
                "G2_feas_violations",
                "G3_p99_99_us",
                "G3_pass",
                "G4_status",
                "G4_pass",
                "G5_pass",
                "G6_pass",
                "oracle_init_tick_calls",
                "oracle_active_clears",
            ]
        )
        for run in runs:
            g = run["gates"]
            w.writerow(
                [
                    run["name"],
                    g.get("G_stream_pass", False),
                    g.get("G1_oracle_match", False),
                    g.get("G2_feas_violations", -1),
                    g.get("G3_p99_99_us", 0.0),
                    g.get("G3_pass", False),
                    g.get("G4_status", "UNKNOWN"),
                    g.get("G4_pass", False),
                    g.get("G5_pass", False),
                    g.get("G6_pass", False),
                    g.get("oracle_init_tick_calls", 0),
                    g.get("oracle_active_clears", 0),
                ]
            )


def write_plots(runs: List[Dict], out_dir: Path) -> None:
    try:
        import matplotlib.pyplot as plt
    except Exception as exc:  # pragma: no cover - optional plotting dep
        print(f"plot_all: matplotlib unavailable ({exc}); tables were still written")
        return

    out_dir.mkdir(parents=True, exist_ok=True)

    # Latency CDF.
    plt.figure(figsize=(8.5, 5.5))
    for run in runs:
        vals = sorted(run["latency_samples_us"])
        if not vals:
            continue
        n = len(vals)
        ys = [(i + 1) / n for i in range(n)]
        plt.plot(vals, ys, label=run["name"], linewidth=1.2)
    plt.xscale("log")
    plt.xlabel("Latency (us)")
    plt.ylabel("CDF")
    plt.title("Latency CDF")
    plt.grid(True, which="both", alpha=0.25)
    plt.legend(loc="lower right", fontsize=8)
    plt.tight_layout()
    plt.savefig(out_dir / "fig_latency_cdf.png", dpi=160)
    plt.close()

    # Fallback rate bars.
    names = [run["name"] for run in runs]
    fallback_rates = [run["summary"].get("fallback_rate", 0.0) for run in runs]
    plt.figure(figsize=(8.5, 4.2))
    plt.bar(names, fallback_rates, color="#2a9d8f")
    plt.ylabel("Fallback rate")
    plt.title("Fallback Rate by Workload")
    plt.xticks(rotation=30, ha="right")
    plt.tight_layout()
    plt.savefig(out_dir / "fig_fallback_rate.png", dpi=160)
    plt.close()

    # G4 absolute gap bars.
    g4_abs = [run["gates"].get("G4_obj_gap_abs_max", 0.0) for run in runs]
    plt.figure(figsize=(8.5, 4.2))
    plt.bar(names, g4_abs, color="#264653")
    plt.yscale("log")
    plt.ylabel("G4 abs gap max")
    plt.title("G4 Objective Gap (Abs, Log Scale)")
    plt.xticks(rotation=30, ha="right")
    plt.tight_layout()
    plt.savefig(out_dir / "fig_g4_abs_gap.png", dpi=160)
    plt.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--results-root", required=True, help="Directory containing per-workload run dirs")
    parser.add_argument("--out-dir", default="", help="Output directory for tables/figures")
    args = parser.parse_args()

    results_root = Path(args.results_root).resolve()
    out_dir = Path(args.out_dir).resolve() if args.out_dir else (results_root / "figures")
    if not results_root.exists():
        raise SystemExit(f"results root does not exist: {results_root}")

    runs = load_runs(results_root)
    if not runs:
        raise SystemExit(f"no run directories with summary.json+gates.json found under {results_root}")

    write_tables(runs, out_dir)
    write_plots(runs, out_dir)
    print(f"plot_all: wrote tables/figures to {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
