#!/usr/bin/env bash
# bench.sh — Build-time benchmark baselines for Mellivora OS.
#
# Measures things we can time without a running QEMU instance:
#   1. Kernel-only rebuild (hot, no clean)
#   2. Single-program compile (hello.asm)
#   3. Full programs parallel compile
#   4. Filesystem population
#   5. Syscall / constant validation scripts
#
# Results are written to benchmark/results-<timestamp>.txt and compared
# against benchmark/baseline.txt when BENCH_CHECK=1.
#
# Usage:
#   bash tests/bench.sh            # run and store results
#   BENCH_CHECK=1 bash tests/bench.sh  # run and fail if regression > threshold
#
# Thresholds (BENCH_REGRESSION_PCT): default 25 %  (generous for CI variance)
set -uo pipefail

BENCH_DIR="benchmark"
mkdir -p "$BENCH_DIR"
BASELINE="$BENCH_DIR/baseline.txt"
TIMESTAMP=$(date +%Y%m%d-%H%M%S)
RESULTS="$BENCH_DIR/results-${TIMESTAMP}.txt"
RESULTS_JSON="$BENCH_DIR/results-${TIMESTAMP}.json"
SUMMARY_TXT="$BENCH_DIR/summary-${TIMESTAMP}.txt"
LATEST_TXT="$BENCH_DIR/latest-results.txt"
LATEST_JSON="$BENCH_DIR/latest-results.json"
LATEST_SUMMARY="$BENCH_DIR/latest-summary.txt"
BENCH_CHECK="${BENCH_CHECK:-0}"
REGRESSION_PCT="${BENCH_REGRESSION_PCT:-25}"
MIN_SLACK_MS="${BENCH_MIN_SLACK_MS:-10}"

PASS=0
FAIL=0

# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------
log() { printf "%s\n" "$*"; }
record() {
    local name="$1" elapsed="$2"
    printf "%-40s %s ms\n" "$name" "$elapsed" | tee -a "$RESULTS"
}

run_bench() {
    local name="$1"; shift
    local start end elapsed
    start=$(date +%s%3N)
    if ! "$@" >/dev/null 2>&1; then
        log "  SKIP  $name (command failed)"
        return
    fi
    end=$(date +%s%3N)
    elapsed=$(( end - start ))
    record "$name" "$elapsed"
}

emit_benchmark_outputs() {
    python3 - "$RESULTS" "$RESULTS_JSON" "$SUMMARY_TXT" "$BASELINE" \
        "$BENCH_CHECK" "$REGRESSION_PCT" "$MIN_SLACK_MS" "$PASS" "$FAIL" <<'PY'
import json
import re
import sys
from pathlib import Path

results_path = Path(sys.argv[1])
results_json_path = Path(sys.argv[2])
summary_path = Path(sys.argv[3])
baseline_path = Path(sys.argv[4])
bench_check = sys.argv[5] == "1"
regression_pct = int(sys.argv[6])
min_slack_ms = int(sys.argv[7])
pass_count = int(sys.argv[8])
fail_count = int(sys.argv[9])

line_re = re.compile(r"^(.*?)\s+([0-9]+)\s+ms$")

def parse_metrics(path: Path):
    metrics = []
    if not path.exists():
        return metrics
    for raw in path.read_text(encoding="utf-8").splitlines():
        m = line_re.match(raw.strip())
        if not m:
            continue
        metrics.append({"name": m.group(1), "elapsed_ms": int(m.group(2))})
    return metrics

results = parse_metrics(results_path)
baseline = parse_metrics(baseline_path) if bench_check and baseline_path.exists() else []
baseline_map = {m["name"]: m["elapsed_ms"] for m in baseline}

comparisons = []
for metric in results:
    name = metric["name"]
    if name not in baseline_map:
        continue
    baseline_ms = baseline_map[name]
    pct_slack = baseline_ms * regression_pct // 100
    slack = max(pct_slack, min_slack_ms)
    threshold = baseline_ms + slack
    current_ms = metric["elapsed_ms"]
    comparisons.append(
        {
            "name": name,
            "baseline_ms": baseline_ms,
            "current_ms": current_ms,
            "threshold_ms": threshold,
            "status": "pass" if current_ms <= threshold else "fail",
        }
    )

payload = {
    "benchmark": "mellivora-build-bench",
    "results_file": str(results_path),
    "bench_check": bench_check,
    "threshold_percent": regression_pct,
    "min_slack_ms": min_slack_ms,
    "metrics": results,
    "comparison": comparisons,
    "comparison_pass_count": pass_count,
    "comparison_fail_count": fail_count,
}
results_json_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")

summary_lines = []
summary_lines.append("Mellivora benchmark summary")
summary_lines.append(f"metrics: {len(results)}")
for metric in results:
    summary_lines.append(f"- {metric['name']}: {metric['elapsed_ms']} ms")

if bench_check:
    summary_lines.append("")
    summary_lines.append(
        f"regression check: {pass_count} passed, {fail_count} failed "
        f"(threshold {regression_pct}%, min slack {min_slack_ms}ms)"
    )
    for cmp_metric in comparisons:
        summary_lines.append(
            f"  [{cmp_metric['status']}] {cmp_metric['name']}: "
            f"{cmp_metric['current_ms']} ms (baseline {cmp_metric['baseline_ms']} ms, "
            f"threshold {cmp_metric['threshold_ms']} ms)"
        )

summary_path.write_text("\n".join(summary_lines) + "\n", encoding="utf-8")
PY

    cp "$RESULTS" "$LATEST_TXT"
    cp "$RESULTS_JSON" "$LATEST_JSON"
    cp "$SUMMARY_TXT" "$LATEST_SUMMARY"
}

# ---------------------------------------------------------------------------
log "=== Mellivora OS Benchmark ==="
log "timestamp: $TIMESTAMP"
log ""
printf "" > "$RESULTS"

# 1. Kernel-only rebuild (assumes kernel.bin already exists)
if [[ -f kernel.bin ]]; then
    run_bench "kernel-only rebuild" make kernel-only
else
    log "  SKIP  kernel-only rebuild (no kernel.bin)"
fi

# 2. Single program compile
run_bench "single program (hello.asm)" make programs/hello.bin

# 3. Full programs build (parallel, no-op if up to date)
run_bench "programs up-to-date check" make programs

# 4. Filesystem population (populate only, no rebuild)
if [[ -f mellivora.img ]]; then
    run_bench "filesystem population" python3 populate.py mellivora.img programs
else
    log "  SKIP  filesystem population (no mellivora.img)"
fi

# 5. Validation scripts
run_bench "validate-constants" python3 tools/validate_constants.py
run_bench "validate-syscalls"  python3 tools/validate_syscalls.py
run_bench "check-syscalls-json" python3 tools/generate_syscalls_json.py --check-json
run_bench "check-syscalls-md"   python3 tools/generate_syscalls_json.py --check-md

log ""
log "Results written to $RESULTS"

# ---------------------------------------------------------------------------
# Regression check
# ---------------------------------------------------------------------------
REGRESSION_FAILED=0
if [[ "$BENCH_CHECK" == "1" ]]; then
    if [[ ! -f "$BASELINE" ]]; then
        log "  INFO  No baseline found at $BASELINE — storing current run as baseline"
        cp "$RESULTS" "$BASELINE"
        log "  Baseline saved."
    else
        log ""
        log "=== Regression check (threshold ${REGRESSION_PCT}%) ==="

        while IFS= read -r line; do
            name=$(echo "$line" | awk '{$NF=""; $(NF-1)=""; print}' | sed 's/[[:space:]]*$//')
            baseline_ms=$(echo "$line" | awk '{print $(NF-1)}')
            [[ "$baseline_ms" =~ ^[0-9]+$ ]] || continue

            current_ms=$(grep -F "$name" "$RESULTS" | awk '{print $(NF-1)}')
            [[ "$current_ms" =~ ^[0-9]+$ ]] || continue

            pct_slack=$(( baseline_ms * REGRESSION_PCT / 100 ))
            slack=$pct_slack
            if [[ "$slack" -lt "$MIN_SLACK_MS" ]]; then
                slack=$MIN_SLACK_MS
            fi
            threshold=$(( baseline_ms + slack ))
            if [[ "$current_ms" -gt "$threshold" ]]; then
                printf "  \033[31mFAIL\033[0m  %s: %s ms (baseline %s ms, threshold %s ms)\n" \
                    "$name" "$current_ms" "$baseline_ms" "$threshold"
                (( FAIL++ ))
            else
                printf "  \033[32mPASS\033[0m  %s: %s ms (baseline %s ms)\n" \
                    "$name" "$current_ms" "$baseline_ms"
                (( PASS++ ))
            fi
        done < "$BASELINE"

        log ""
        log "Regression check: $PASS passed, $FAIL failed"
        if [[ $FAIL -ne 0 ]]; then
            REGRESSION_FAILED=1
        fi
    fi
fi

emit_benchmark_outputs

if [[ "$BENCH_CHECK" == "1" && "$REGRESSION_FAILED" -ne 0 ]]; then
    exit 1
fi
