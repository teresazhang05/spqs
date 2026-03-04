#!/usr/bin/env python3
"""Parse `perf stat` output into structured JSON.

Supported input forms:
- perf CSV output (`perf stat -x, --no-big-num`)
- plain-text perf stat output (best-effort)
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any

NON_VALUE_TOKENS = {
    "<not supported>",
    "<not counted>",
    "not supported",
    "not counted",
}


def _to_value(raw: str) -> float | None:
    token = raw.strip()
    if not token or token.lower() in NON_VALUE_TOKENS:
        return None
    token = token.replace(" ", "")
    try:
        return float(token)
    except ValueError:
        return None


def parse_perf_csv(text: str) -> dict[str, Any]:
    events: dict[str, dict[str, Any]] = {}
    elapsed_sec: float | None = None

    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue

        parts = [p.strip() for p in line.split(",")]
        if len(parts) < 3:
            m = re.search(r"([0-9]+(?:\.[0-9]+)?)\s+seconds\s+time\s+elapsed", line)
            if m:
                elapsed_sec = float(m.group(1))
            continue

        value_raw, unit, event = parts[0], parts[1], parts[2]
        value = _to_value(value_raw)

        if "seconds time elapsed" in event:
            if value is not None:
                elapsed_sec = value
            continue

        if not event:
            continue

        entry = {
            "value": value,
            "unit": unit if unit else None,
            "raw": value_raw,
        }
        events[event] = entry

    return {"events": events, "elapsed_seconds": elapsed_sec}


def parse_perf_text(text: str) -> dict[str, Any]:
    events: dict[str, dict[str, Any]] = {}
    elapsed_sec: float | None = None

    event_re = re.compile(
        r"^\s*([0-9][0-9,\.]*|<not supported>|<not counted>)\s+([A-Za-z0-9_\-/\.]+)"
    )
    elapsed_re = re.compile(r"([0-9]+(?:\.[0-9]+)?)\s+seconds\s+time\s+elapsed")

    for line in text.splitlines():
        if not line.strip() or line.strip().startswith("#"):
            continue

        m = event_re.search(line)
        if m:
            raw_value = m.group(1)
            event = m.group(2)
            value = _to_value(raw_value.replace(",", ""))
            events[event] = {"value": value, "unit": None, "raw": raw_value}
            continue

        em = elapsed_re.search(line)
        if em:
            elapsed_sec = float(em.group(1))

    return {"events": events, "elapsed_seconds": elapsed_sec}


def main() -> int:
    parser = argparse.ArgumentParser(description="Parse perf stat output")
    parser.add_argument("--input", required=True, help="Input perf stat file")
    parser.add_argument("--output", required=True, help="Output JSON path")
    parser.add_argument(
        "--format",
        default="auto",
        choices=["auto", "csv", "text"],
        help="Input format",
    )
    args = parser.parse_args()

    in_path = Path(args.input)
    out_path = Path(args.output)

    text = in_path.read_text(encoding="utf-8")

    if args.format == "csv":
        parsed = parse_perf_csv(text)
    elif args.format == "text":
        parsed = parse_perf_text(text)
    else:
        parsed = parse_perf_csv(text) if "," in text else parse_perf_text(text)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(parsed, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(f"wrote {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
