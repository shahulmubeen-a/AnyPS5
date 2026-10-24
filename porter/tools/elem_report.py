"""Summarizes the porter element-cost lines ([elem], [elem-pass], [elem-engine], [elem-mat], [elem-prog]) of a run log.

usage: python -I elem_report.py <run-*.err.log> [--rounds A-B | --last N] [--top N] [--json out.json] [--group-depth D]

Each 10 s report round lists the top elements by worker + committer time. The script sums the chosen rounds and
prints, per element, the average per 10 s: worker ms (queue worker wall time of the element's packets, drains and
waits included), commit ms (draw committer), prepare ms (draw preparer), wait ms (WAIT_REG_MEM inside it), draws,
dispatches and skipped packets. --group-depth D folds pass labels to their first D marker levels.
"""
import argparse
import collections
import json
import re

ROUND = re.compile(r"^\[elem\] round (\d+) \(10 s\): (\d+) labels active; per queue worker/commit ms:(.*)$")
PASS = re.compile(r"^\[elem-pass\] (\d+) ([\d.]+) ([\d.]+) ([\d.]+) ([\d.]+) (\d+) (\d+) (\d+) (\d+) (.*)$")
ENGINE = re.compile(r"^\[elem-engine\] (\d+) ([\d.]+) ([\d.]+) ([\d.]+) (\d+) (\d+) kind=(\d+) va=(0x[0-9a-f]+)$")
MAT = re.compile(r"^\[elem-mat\] (\d+) ([\d.]+) ([\d.]+) ([\d.]+) (\d+) (\d+) (0x[0-9a-f]+) (.*)$")
PROG = re.compile(r"^\[elem-prog\] (\d+) ([\d.]+) ([\d.]+) ([\d.]+) (\d+) (\d+) (\S) (0x[0-9a-f]+) (0x[0-9a-f]+) (.*)$")
PRESENT = re.compile(r"^\[present\] (\d+) presents over 10 s.*?GPU busy ([\d.]+) ms, idle gaps ([\d.]+) ms")
KINDS = {1: "item", 2: "instanced", 3: "instanced-b", 10: "other"}


def fold(label, depth):
    if depth is None:
        return label
    queue, _, path = label.partition("|")
    parts = path.split("/") if path else []
    return queue + "|" + "/".join(parts[:depth])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--rounds")
    ap.add_argument("--last", type=int)
    ap.add_argument("--top", type=int, default=40)
    ap.add_argument("--json")
    ap.add_argument("--group-depth", type=int)
    args = ap.parse_args()
    lines = open(args.log, encoding="utf-8", errors="replace").read().splitlines()
    rounds = sorted({int(m.group(1)) for line in lines if (m := ROUND.match(line))})
    if args.rounds:
        first, last = (int(x) for x in args.rounds.split("-"))
        chosen = {r for r in rounds if first <= r <= last}
    elif args.last:
        chosen = set(rounds[-args.last:])
    else:
        chosen = set(rounds)
    count = max(1, len(chosen))
    queues = collections.defaultdict(lambda: [0.0, 0.0])
    passes = collections.defaultdict(lambda: [0.0] * 8)
    engines = collections.defaultdict(lambda: [0.0] * 5)
    mats = collections.defaultdict(lambda: [0.0] * 5)
    mat_label = {}
    progs = collections.defaultdict(lambda: [0.0] * 5)
    prog_label = {}
    presents = []
    current = None
    for line in lines:
        if m := ROUND.match(line):
            current = int(m.group(1))
            if current in chosen:
                for item in m.group(3).split(";"):
                    item = item.strip()
                    if not item:
                        continue
                    name, values = item.split(" ")
                    worker, commit = values.split("/")
                    queues[name][0] += float(worker)
                    queues[name][1] += float(commit)
            continue
        if m := PRESENT.match(line):
            if current in chosen:
                presents.append((int(m.group(1)), float(m.group(2)), float(m.group(3))))
            continue
        if m := PASS.match(line):
            if int(m.group(1)) not in chosen:
                continue
            key = fold(m.group(10), args.group_depth)
            row = passes[key]
            for i in range(8):
                row[i] += float(m.group(2 + i))
        elif m := ENGINE.match(line):
            if int(m.group(1)) not in chosen:
                continue
            kind = int(m.group(7))
            key = KINDS.get(kind, str(kind)) + ("" if m.group(8) == "0x0" else " " + m.group(8))
            row = engines[key]
            for i in range(5):
                row[i] += float(m.group(2 + i))
        elif m := MAT.match(line):
            if int(m.group(1)) not in chosen:
                continue
            row = mats[m.group(7)]
            for i in range(5):
                row[i] += float(m.group(2 + i))
            mat_label[m.group(7)] = m.group(8)
        elif m := PROG.match(line):
            if int(m.group(1)) not in chosen:
                continue
            key = (m.group(7), m.group(8), m.group(9))
            row = progs[key]
            for i in range(5):
                row[i] += float(m.group(2 + i))
            prog_label[key] = m.group(10)
    frames = sum(p[0] for p in presents) / max(1, len(presents)) if presents else 0
    print(f"{len(chosen)} rounds; presents per 10 s {frames:.1f}; GPU busy per frame {sum(p[1] for p in presents) / max(1, len(presents)):.0f} ms" if presents else f"{len(chosen)} rounds")
    print("per queue (ms per 10 s, worker/commit): " + ", ".join(f"{q} {w / count:.0f}/{c / count:.0f}" for q, (w, c) in sorted(queues.items(), key=lambda kv: -kv[1][0])))

    def per(value):
        return value / count

    print(f"\npasses (avg per 10 s): worker commit prepare wait draws dispatches packets skipped label")
    rows = sorted(passes.items(), key=lambda kv: -(kv[1][0] + kv[1][1] + kv[1][2]))
    for key, r in rows[:args.top]:
        print(f"{per(r[0]):8.1f} {per(r[1]):8.1f} {per(r[2]):7.1f} {per(r[3]):7.1f} {per(r[4]):7.0f} {per(r[5]):7.0f} {per(r[6]):7.0f} {per(r[7]):6.0f}  {key}")
    print(f"\nengine functions (avg per 10 s): worker commit prepare draws skipped kind")
    for key, r in sorted(engines.items(), key=lambda kv: -(kv[1][0] + kv[1][1]))[:args.top]:
        print(f"{per(r[0]):8.1f} {per(r[1]):8.1f} {per(r[2]):7.1f} {per(r[3]):7.0f} {per(r[4]):6.0f}  {key}")
    print(f"\nmaterials (avg per 10 s): worker commit prepare draws skipped material first-seen-label")
    for key, r in sorted(mats.items(), key=lambda kv: -(kv[1][0] + kv[1][1]))[:args.top]:
        print(f"{per(r[0]):8.1f} {per(r[1]):8.1f} {per(r[2]):7.1f} {per(r[3]):7.0f} {per(r[4]):6.0f}  {key} {mat_label.get(key, '')}")
    print(f"\nprograms (avg per 10 s): worker commit prepare count skipped type first last label")
    for key, r in sorted(progs.items(), key=lambda kv: -(kv[1][0] + kv[1][1]))[:args.top]:
        print(f"{per(r[0]):8.1f} {per(r[1]):8.1f} {per(r[2]):7.1f} {per(r[3]):7.0f} {per(r[4]):6.0f}  {key[0]} {key[1]} {key[2]} {prog_label.get(key, '')}")
    if args.json:
        out = {
            "rounds": sorted(chosen),
            "presents_per_10s": frames,
            "queues": {q: [w / count, c / count] for q, (w, c) in queues.items()},
            "passes": {k: [v / count for v in r] for k, r in passes.items()},
            "engines": {k: [v / count for v in r] for k, r in engines.items()},
            "materials": {k: [v / count for v in r] + [mat_label.get(k, "")] for k, r in mats.items()},
            "programs": {" ".join(k): [v / count for v in r] + [prog_label.get(k, "")] for k, r in progs.items()},
        }
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(out, f, indent=1)


if __name__ == "__main__":
    main()
