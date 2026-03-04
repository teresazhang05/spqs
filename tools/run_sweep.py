#!/usr/bin/env python3
"""Convenience wrapper for PR5 sweep/eval bundle."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out-root", default="results/wow_eval_macos_equiv")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    cmd = [
        sys.executable,
        str(repo / "tools/run_wow_eval.py"),
        "--profile",
        "macos_equiv",
        "--out-root",
        args.out_root,
    ]
    return subprocess.call(cmd)


if __name__ == "__main__":
    raise SystemExit(main())
