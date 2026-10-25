"""Per-step summary of an elem-run log: presents (from the plan result), HTILE pre-syncs and queue-0 load.

usage: python -I elem_steps.py --run el15 [--runs-dir DIR]

The run log has one "[elem] rules <k>: ..." line per rules-file change; rules 1 is the empty file at start-up and
rules k (k >= 2) is plan step k-1. Each "[elem] round" line inside a step adds its 10 s counters.
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elem_spec import read_plan  # noqa: E402

RULES = re.compile(r"^\[elem\] rules (\d+):")
ROUND = re.compile(r"^\[elem\] round \d+ \(10 s\): \d+ labels active;(?: (\d+) deferred stores;)?(?: (\d+) depth pre-syncs (\d+) ms;)? per queue worker/commit ms:(.*)$")
QUEUE = re.compile(r" q([0-9a-f]+) (\d+)/(\d+);")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--run", required=True)
    ap.add_argument("--runs-dir", default=r"C:\Users\Mubeen\Documents\Projects\yotei-work\runs-best")
    args = ap.parse_args()
    steps = read_plan(os.path.join(args.runs_dir, f"plan-{args.run}.txt"))
    per = {}
    rules = 0
    with open(os.path.join(args.runs_dir, f"run-{args.run}.err.log"), encoding="utf-8", errors="replace") as log:
        for line in log:
            m = RULES.match(line)
            if m:
                rules = int(m.group(1))
                continue
            m = ROUND.match(line)
            if not m or rules < 2:
                continue
            row = per.setdefault(rules - 1, [0, 0, 0, 0, 0, 0])
            row[0] += 1
            row[1] += int(m.group(1) or 0)
            row[2] += int(m.group(2) or 0)
            row[3] += int(m.group(3) or 0)
            for q, worker, commit in QUEUE.findall(m.group(4)):
                if q == "0":
                    row[4] += int(worker)
                    row[5] += int(commit)
    print(f"{'step':<12}{'fps':>6}{'rounds':>8}{'defer/r':>9}{'presync/r':>11}{'ms/r':>8}{'q0 work/r':>11}{'q0 commit/r':>13}")
    groups = {}
    for index, step in enumerate(steps, start=1):
        row = per.get(index, [0, 0, 0, 0, 0, 0])
        n = max(row[0], 1)
        fps = step["rate"] / 10 if step["rate"] is not None else float("nan")
        print(f"{step['label']:<12}{fps:>6.2f}{row[0]:>8}{row[1] / n:>9.0f}{row[2] / n:>11.1f}{row[3] / n:>8.0f}{row[4] / n:>11.0f}{row[5] / n:>13.0f}")
        key = step["label"].rsplit("-", 1)[0] if step["label"][-1].isdigit() else step["label"]
        if step["rate"] is not None:
            groups.setdefault(key, []).append(fps)
    print()
    for key, values in groups.items():
        print(f"{key:<12} mean {sum(values) / len(values):.3f} fps over {len(values)}: " + " ".join(f"{v:.2f}" for v in values))


if __name__ == "__main__":
    main()
