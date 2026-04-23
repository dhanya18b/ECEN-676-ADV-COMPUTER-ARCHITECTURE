#!/usr/bin/env python3
"""
Extract HW1 metrics (IPC, branch MPKI, branch accuracy) from ChampSim outputs.

Usage:
  python extract_hw1_metrics.py --root Results --runs-csv runs.csv --summary-csv summary.csv
"""
from __future__ import annotations

import argparse
import csv
import math
import re
from pathlib import Path
from typing import Dict, List

IPC_RE = re.compile(
    r"CPU\s+(?P<cpu>\d+)\s+cumulative IPC:\s+(?P<ipc>[0-9.]+)\s+instructions:\s+(?P<inst>\d+)\s+cycles:\s+(?P<cycles>\d+)",
    re.MULTILINE,
)
BP_RE = re.compile(
    r"CPU\s+(?P<cpu>\d+)\s+Branch Prediction Accuracy:\s+(?P<acc>[0-9.]+)%\s+MPKI:\s+(?P<mpki>[0-9.]+)",
    re.MULTILINE,
)


def geomean(values: List[float]) -> float:
    vals = [v for v in values if v is not None]
    if not vals:
        return float("nan")
    if any(v == 0 for v in vals):
        return 0.0
    return math.exp(sum(math.log(v) for v in vals) / len(vals))


def parse_run(path: Path) -> Dict:
    text = path.read_text(errors="ignore")
    ipc_by_cpu = {}
    for m in IPC_RE.finditer(text):
        ipc_by_cpu[int(m["cpu"])] = float(m["ipc"])  # keep last match per CPU
    bp_by_cpu = {}
    for m in BP_RE.finditer(text):
        bp_by_cpu[int(m["cpu"])] = (float(m["acc"]), float(m["mpki"]))  # last per CPU

    if not ipc_by_cpu or not bp_by_cpu:
        return {}

    return {
        "benchmark": path.stem,
        "config": path.parent.name,
        "ipc_mean": sum(ipc_by_cpu.values()) / len(ipc_by_cpu),
        "bp_accuracy_mean": sum(a for a, _ in bp_by_cpu.values()) / len(bp_by_cpu),
        "bp_mpki_geomean": geomean([m for _, m in bp_by_cpu.values()]),
        "ipc_by_cpu": ipc_by_cpu,
        "bp_by_cpu": bp_by_cpu,
    }


def collect_runs(root: Path) -> List[Dict]:
    runs = []
    for txt in root.glob("*/*.txt"):
        rec = parse_run(txt)
        if rec:
            runs.append(rec)
    return sorted(runs, key=lambda r: (r["config"], r["benchmark"]))


def summarize_by_config(runs: List[Dict]) -> List[Dict]:
    summary = []
    for cfg in sorted({r["config"] for r in runs}):
        subset = [r for r in runs if r["config"] == cfg]
        summary.append(
            {
                "config": cfg,
                "benchmarks": len(subset),
                "ipc_mean": sum(r["ipc_mean"] for r in subset) / len(subset),
                "bp_accuracy_mean": sum(r["bp_accuracy_mean"] for r in subset) / len(subset),
                "bp_mpki_geomean": geomean([r["bp_mpki_geomean"] for r in subset]),
            }
        )
    return summary


def write_csv(path: Path, rows: List[Dict], fieldnames: List[str]) -> None:
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="Results", help="Root folder containing config subfolders")
    ap.add_argument("--runs-csv", help="Optional path to write per-run metrics")
    ap.add_argument("--summary-csv", help="Optional path to write per-config summary")
    args = ap.parse_args()

    root = Path(args.root)
    runs = collect_runs(root)
    summary = summarize_by_config(runs)

    print("benchmark,config,ipc,branch_accuracy_pct,branch_mpki")
    for r in runs:
        print(f"{r['benchmark']},{r['config']},{r['ipc_mean']:.4f},{r['bp_accuracy_mean']:.2f},{r['bp_mpki_geomean']:.2f}")

    print("\n# per-config (IPC arithmetic mean, MPKI geomean, accuracy arithmetic mean)")
    for s in summary:
        print(
            f"{s['config']}: benches={s['benchmarks']}, "
            f"ipc_mean={s['ipc_mean']:.4f}, mpki_geomean={s['bp_mpki_geomean']:.2f}, "
            f"accuracy_mean={s['bp_accuracy_mean']:.2f}%"
        )

    if args.runs_csv:
        write_csv(Path(args.runs_csv), runs, ["benchmark", "config", "ipc_mean", "bp_accuracy_mean", "bp_mpki_geomean"])
    if args.summary_csv:
        write_csv(Path(args.summary_csv), summary, ["config", "benchmarks", "ipc_mean", "bp_accuracy_mean", "bp_mpki_geomean"])


if __name__ == "__main__":
    main()
