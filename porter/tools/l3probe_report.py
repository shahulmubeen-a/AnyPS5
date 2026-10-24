"""Per-frame view of an L3 probe run.

Reads the game's stderr log (the [hooks] 10 s lines of the guest-hook loader and the driver's
"[draw] driver phases (10 s, N draw packets" lines) and monitor-best.ps1's CSV (presented frames every
5 s), and prints hook calls per presented frame for each 10 s window.

usage: python -I l3probe_report.py <run-TAG.err.log> <monitor-TAG.csv> <launch HH:MM:SS>
"""
import bisect
import datetime
import re
import sys


def seconds(text):
    t = datetime.datetime.strptime(text, "%H:%M:%S")
    return t.hour * 3600 + t.minute * 60 + t.second


def main():
    log, csv, launch = sys.argv[1], sys.argv[2], seconds(sys.argv[3])
    hooks, draws = [], []
    with open(log, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("[hooks] (10 s):"):
                hooks.append({m.group(1): int(m.group(2)) for m in re.finditer(r" (\S+) (\d+) \(total", line)})
            elif line.startswith("[draw] driver phases (10 s, "):
                draws.append(int(re.search(r"\(10 s, (\d+) draw packets", line).group(1)))
    times, frames, fps = [], [], []
    with open(csv) as f:
        next(f)
        for row in f:
            cells = row.strip().split(",")
            if len(cells) >= 4 and cells[3].isdigit():
                times.append(seconds(cells[0][:8]))
                frames.append(int(cells[3]))
                fps.append(cells[2])

    def frames_at(t):
        i = bisect.bisect_left(times, t)
        if i == 0:
            return frames[0] if times else 0
        if i >= len(times):
            return frames[-1]
        t0, t1, f0, f1 = times[i - 1], times[i], frames[i - 1], frames[i]
        return f0 + (f1 - f0) * (t - t0) / max(1, t1 - t0)

    names = ["l3-drawitem", "l3-drawinstanced", "l3-drawinstanced-b", "l3-bindmaterial", "l3-passjob", "l3-dispatch", "l3-dispatchindirect"]
    print("window  clock     frames  " + "  ".join(f"{n[3:]:>13}" for n in names) + "   (calls per presented frame)")
    for i, counts in enumerate(hooks):
        end = launch + 10 * (i + 1)
        n = frames_at(end) - frames_at(end - 10)
        clock = str(datetime.timedelta(seconds=end))
        cells = "  ".join(f"{(counts.get(name, 0) / n if n > 0 else 0):13.1f}" for name in names)
        print(f"{i:6d}  {clock:>8}  {n:6.1f}  {cells}")
    if draws:
        print("driver draw packets per 10 s:", " ".join(str(d) for d in draws))


main()
