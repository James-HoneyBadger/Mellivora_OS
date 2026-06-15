#!/usr/bin/env python3
"""Aggregate benchmark run history into trend artifacts.

Reads benchmark/results-*.json files and writes:
- benchmark/trend-latest.json
- benchmark/trend-latest.txt
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from statistics import mean


def load_result(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    metrics = {m["name"]: int(m["elapsed_ms"]) for m in data.get("metrics", [])}
    return {
        "file": path.name,
        "metrics": metrics,
    }


def build_trend(runs: list[dict]) -> dict:
    all_names: set[str] = set()
    for run in runs:
        all_names.update(run["metrics"].keys())

    metric_rows = []
    for name in sorted(all_names):
        samples = [run["metrics"][name] for run in runs if name in run["metrics"]]
        if not samples:
            continue
        first = samples[0]
        latest = samples[-1]
        delta = latest - first
        delta_pct = (delta / first * 100.0) if first else 0.0
        recent_window = samples[-3:] if len(samples) >= 3 else samples
        recent_avg = round(mean(recent_window), 2)
        metric_rows.append(
            {
                "name": name,
                "samples": len(samples),
                "series_ms": samples,
                "first_ms": first,
                "latest_ms": latest,
                "min_ms": min(samples),
                "max_ms": max(samples),
                "avg_ms": round(mean(samples), 2),
                "recent_avg_ms": recent_avg,
                "delta_ms": delta,
                "delta_pct": round(delta_pct, 2),
            }
        )

    regressions = sorted(
        [row for row in metric_rows if row["delta_ms"] > 0],
        key=lambda row: (row["delta_pct"], row["delta_ms"]),
        reverse=True,
    )
    improvements = sorted(
        [row for row in metric_rows if row["delta_ms"] < 0],
        key=lambda row: (row["delta_pct"], row["delta_ms"]),
    )

    return {
        "runs_analyzed": len(runs),
        "run_files": [run["file"] for run in runs],
        "metrics": metric_rows,
        "top_regressions": regressions[:5],
        "top_improvements": improvements[:5],
    }


def render_text(trend: dict) -> str:
    lines = []
    lines.append("Mellivora benchmark trend")
    lines.append(f"runs analyzed: {trend['runs_analyzed']}")
    lines.append("")
    if not trend["metrics"]:
        lines.append("No benchmark metrics available.")
        return "\n".join(lines) + "\n"

    lines.append("metric | samples | first | latest | avg | min | max | delta")
    lines.append("------ | ------- | ----- | ------ | --- | --- | --- | -----")
    for row in trend["metrics"]:
        sign = "+" if row["delta_ms"] >= 0 else ""
        lines.append(
            f"{row['name']} | {row['samples']} | {row['first_ms']} ms | "
            f"{row['latest_ms']} ms | {row['avg_ms']} ms | {row['min_ms']} ms | "
            f"{row['max_ms']} ms | {sign}{row['delta_ms']} ms ({sign}{row['delta_pct']}%)"
        )

    if trend["top_regressions"]:
        lines.append("")
        lines.append("Top regressions")
        for row in trend["top_regressions"]:
            lines.append(
                f"- {row['name']}: +{row['delta_ms']} ms (+{row['delta_pct']}%) "
                f"latest {row['latest_ms']} ms"
            )

    if trend["top_improvements"]:
        lines.append("")
        lines.append("Top improvements")
        for row in trend["top_improvements"]:
            lines.append(
                f"- {row['name']}: {row['delta_ms']} ms ({row['delta_pct']}%) "
                f"latest {row['latest_ms']} ms"
            )

    lines.append("")
    return "\n".join(lines)


def run_trend_check(trend: dict, min_runs: int, max_regression_pct: float) -> tuple[int, list[str]]:
    findings: list[str] = []
    if trend["runs_analyzed"] < min_runs:
        return 0, [
            f"INFO insufficient runs for trend check: {trend['runs_analyzed']} < {min_runs}"
        ]

    failures = 0
    for row in trend["metrics"]:
        if row["samples"] < min_runs:
            continue
        if row["delta_pct"] > max_regression_pct:
            failures += 1
            findings.append(
                f"FAIL {row['name']}: drifted +{row['delta_pct']}% "
                f"({row['first_ms']} ms -> {row['latest_ms']} ms)"
            )

    if failures == 0:
        findings.append(
            f"PASS no metric exceeded +{max_regression_pct}% drift across {trend['runs_analyzed']} runs"
        )
    return failures, findings


def main() -> int:
    parser = argparse.ArgumentParser(description="Build benchmark trend artifacts")
    parser.add_argument("--dir", default="benchmark", help="Benchmark directory")
    parser.add_argument("--limit", type=int, default=20, help="Max recent runs to analyze")
    parser.add_argument("--check", action="store_true", help="Fail on sustained regressions")
    parser.add_argument("--min-runs", type=int, default=5, help="Minimum runs required for trend checks")
    parser.add_argument(
        "--max-regression-pct",
        type=float,
        default=25.0,
        help="Maximum allowed first-to-latest slowdown percentage per metric",
    )
    args = parser.parse_args()

    bench_dir = Path(args.dir)
    bench_dir.mkdir(parents=True, exist_ok=True)

    result_files = sorted(bench_dir.glob("results-*.json"))
    if args.limit > 0:
        result_files = result_files[-args.limit :]

    runs = [load_result(path) for path in result_files]
    trend = build_trend(runs)

    trend_json_path = bench_dir / "trend-latest.json"
    trend_txt_path = bench_dir / "trend-latest.txt"

    trend_json_path.write_text(json.dumps(trend, indent=2) + "\n", encoding="utf-8")
    trend_txt_path.write_text(render_text(trend), encoding="utf-8")

    print(f"[bench-trend] WROTE {trend_json_path}")
    print(f"[bench-trend] WROTE {trend_txt_path}")
    print(f"[bench-trend] runs analyzed: {trend['runs_analyzed']}")

    if args.check:
        failures, findings = run_trend_check(trend, args.min_runs, args.max_regression_pct)
        for finding in findings:
            print(f"[bench-trend-check] {finding}")
        return 1 if failures else 0

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
