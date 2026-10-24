#!/usr/bin/env python3
"""test_parity_scorecard.py - prove the scorecard cannot flatter the tree.

tools/parity_scorecard.py computes the fourteen rows of docs/ROADMAP.md from
evidence in the tree. This test holds it to three promises:

- the table is the fourteen jobs, numbered once each, and every row states a
  criterion and names evidence that would make it pass;
- the committed baseline agrees with the tree, so a capability cannot be lost
  without the change that loses it being noticed;
- a baseline that claims more than the tree delivers fails, which is the only
  thing that makes `--check` worth running.
"""

from __future__ import annotations

import importlib.util
import io
import json
import sys
import tempfile
from contextlib import redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOL_PATH = ROOT / "tools" / "parity_scorecard.py"

PASS = 0
FAIL = 0


def check(description, condition):
    global PASS, FAIL
    if condition:
        PASS += 1
        print("  PASS  %s" % description)
    else:
        FAIL += 1
        print("  FAIL  %s" % description)


def load_tool():
    spec = importlib.util.spec_from_file_location("parity_scorecard", TOOL_PATH)
    module = importlib.util.module_from_spec(spec)
    # dataclasses resolve their own module by name while the class is created.
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def quiet(function, *args):
    with redirect_stdout(io.StringIO()):
        return function(*args)


def main():
    tool = load_tool()
    rows = tool.score(tool.Tree())
    summary = tool.summarise(rows)

    check("the scorecard has the fourteen jobs", len(rows) == 14)
    check("the jobs are numbered 1..14 once each",
          [row["number"] for row in rows] == list(range(1, 15)))
    check("every row states what would make it pass",
          all(row["criteria"] for row in rows))
    check("every row that is not passing names what is missing",
          all(row["missing"] for row in rows if row["status"] != "pass"))
    check("every row that is passing has nothing missing",
          all(not row["missing"] for row in rows if row["status"] == "pass"))
    check("no row has an unknown status",
          all(row["status"] in tool.STATUS_RANK for row in rows))
    check("the summary counts the rows",
          summary["pass"] + summary["partial"] + summary["fail"] == summary["total"] == 14)

    # The committed baseline must match the tree: a row that quietly improved
    # is as much a baseline error as one that regressed.
    baseline = json.loads(tool.BASELINE.read_text(encoding="utf-8"))
    recorded = {row["number"]: row["status"] for row in baseline["rows"]}
    actual = {row["number"]: row["status"] for row in rows}
    check("docs/parity-baseline.json lists every row", recorded.keys() == actual.keys())
    check("the committed baseline matches the tree", recorded == actual)

    # main() reads argv, so the check path is exercised through check().
    check("--check passes on the current tree",
          quiet(tool.check, rows) == 0)

    # A baseline that overstates the tree has to fail, or the gate is decorative.
    saved = tool.BASELINE
    try:
        directory = Path(tempfile.mkdtemp(prefix="parity-baseline-"))
        overstated = {"generated_by": "test",
                      "rows": [{"number": row["number"], "job": row["job"],
                                "status": "pass"} for row in rows]}
        tool.BASELINE = directory / "parity-baseline.json"
        tool.BASELINE.write_text(json.dumps(overstated), encoding="utf-8")
        check("a baseline that overstates the tree fails the check",
              quiet(tool.check, rows) == 1)

        missing_row = {"generated_by": "test", "rows": baseline["rows"][:-1]}
        tool.BASELINE.write_text(json.dumps(missing_row), encoding="utf-8")
        check("a baseline missing a row fails the check",
              quiet(tool.check, rows) == 1)

        tool.BASELINE.unlink()
        check("a missing baseline fails the check", quiet(tool.check, rows) == 1)
    finally:
        tool.BASELINE = saved

    print()
    if FAIL:
        print("  %d passed, %d failed" % (PASS, FAIL))
        return 1
    print("  %d passed" % PASS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
