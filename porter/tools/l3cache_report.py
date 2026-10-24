"""A/B view of two L3 runs (porter/tools/ab-l3cache.ps1): per 10 s window, the presented frames (fps), the
engine draws, the driver's draw-cache hits, the queue-0 worker's draw cost, the committer's commits and the
[l3-cache] outcomes; then menu and world averages for both runs side by side.

usage: python -I l3cache_report.py <runs-best dir> <tagA> <tagB>
"""
import bisect
import datetime
import os
import re
import sys


def seconds(text):
    t = datetime.datetime.strptime(text[:8], "%H:%M:%S")
    return t.hour * 3600 + t.minute * 60 + t.second


def number(pattern, line, default=0.0):
    m = re.search(pattern, line)
    return float(m.group(1)) if m else default


class Run:
    def __init__(self, runs, tag):
        self.tag = tag
        self.launch = self.continued = None
        with open(os.path.join(runs, f"porter-{tag}.log"), encoding="utf-8", errors="replace") as f:
            for line in f:
                if "launched pid" in line and self.launch is None:
                    self.launch = seconds(line)
                if "continue pressed" in line:
                    self.continued = seconds(line)
        self.times, self.frames = [], []
        with open(os.path.join(runs, f"monitor-{tag}.csv")) as f:
            next(f)
            for row in f:
                cells = row.strip().split(",")
                if len(cells) >= 4 and cells[3].isdigit():
                    self.times.append(seconds(cells[0]))
                    self.frames.append(int(cells[3]))
        self.windows = []  # one dict per [l3] 10 s report, filled from the other 10 s lines in order
        keys = {"l3": [], "draw": [], "cache": [], "regkey": [], "pipe": [], "l3cache": [], "q0": []}
        with open(os.path.join(runs, f"run-{tag}.err.log"), encoding="utf-8", errors="replace") as f:
            for line in f:
                if line.startswith("[l3] (10 s): draws "):
                    keys["l3"].append({"draws": number(r"draws (\d+)", line), "foreign": number(r"foreign (\d+) direct", line) + number(r"\+ (\d+) indirect", line), "item": number(r"written item=(\d+)", line), "instanced": number(r" instanced=(\d+)", line), "instancedb": number(r"instanced-b=(\d+)", line), "other": number(r" other=(\d+)", line)})
                elif line.startswith("[draw] driver phases (10 s, "):
                    keys["draw"].append({"packets": number(r"\(10 s, (\d+) draw packets", line), "captures": number(r"drawn, (\d+) captures", line), "us": number(r"total ([\d.]+) \(", line), "ms": number(r"total [\d.]+ \(([\d.]+) ms", line), "capture_us": number(r" capture ([\d.]+)", line), "validate_us": number(r"key/lookup/validate ([\d.]+)", line), "recompile_us": number(r"recompile ([\d.]+)", line)})
                elif line.startswith("[draw-cache] ") and " lookups (10 s): " in line:
                    keys["cache"].append({"lookups": number(r"\] (\d+) lookups", line), "noentry": number(r": (\d+) no entry", line), "hits": number(r": (\d+) hits \(every", line), "inserts": number(r"; (\d+) inserts", line), "evictions": number(r", (\d+) evictions in total", line)})
                elif line.startswith("[draw-cache] register-key misses"):
                    keys["regkey"].append({"shape": number(r"shape found \(data entry\) (\d+)", line), "reused": number(r"data stages reused (\d+)", line), "compiled": number(r"compiled (\d+)", line)})
                elif line.startswith("[draw] pipelined (10 s"):
                    keys["pipe"].append({"commits": number(r": (\d+) commits at", line), "commit_us": number(r"commits at ([\d.]+) us", line), "busy": number(r"\(([\d.]+) % busy", line), "drain_ms": number(r"drains: packet \d+ \(([\d.]+) ms", line)})
                elif line.startswith("[l3-cache] (10 s)"):
                    keys["l3cache"].append({"calls": number(r"total (\d+) calls", line), "served": number(r"(\d+) served by the alias", line), "hits": number(r"(\d+) validated hits", line), "relocated": sum(float(m) for m in re.findall(r"/(\d+)/\d+ \(\d+\)/\d+;", line)), "unmoved": sum(float(m) for m in re.findall(r"/\d+/(\d+)/\d+/\d+ \(\d+\)/\d+;", line)), "exact": sum(float(m) for m in re.findall(r" (?:item|instanced|instanced-b|other) \d+/(\d+)/", line)), "datahits": sum(float(m) for m in re.findall(r"\((\d+)\)/\d+;", line)), "refused": number(r"refused (\d+); moved registers", line), "allowed": number(r"variants allowed (\d+)", line), "line": line.strip()})
                elif line.startswith("[packets] queue 0x0 "):
                    keys["q0"].append({"dispatch_ms": number(r"DISPATCH_DIRECT x\d+ (\d+)ms", line), "draw2_ms": number(r"DRAW_INDEX_2 x\d+ (\d+)ms", line), "wait_ms": number(r"WAIT_REG_MEM x\d+ (\d+)ms", line)})
        self.keys = keys

    def frames_at(self, t):
        i = bisect.bisect_left(self.times, t)
        if not self.times:
            return 0
        if i == 0:
            return self.frames[0]
        if i >= len(self.times):
            return self.frames[-1]
        t0, t1, f0, f1 = self.times[i - 1], self.times[i], self.frames[i - 1], self.frames[i]
        return f0 + (f1 - f0) * (t - t0) / max(1, t1 - t0)

    def window_rows(self):
        rows = []
        n = len(self.keys["l3"])
        for i in range(n):
            end = self.launch + 10 * (i + 1)
            row = {"end": end, "clock": str(datetime.timedelta(seconds=end)), "frames": self.frames_at(end) - self.frames_at(end - 10)}
            for key, items in self.keys.items():
                row[key] = items[i] if i < len(items) else {}
            rows.append(row)
        return rows


def phase_average(rows, lo, hi, pick):
    values = [pick(r) for r in rows if lo <= r["end"] < hi and r["frames"] > 0 and r["l3"].get("draws", 0) > 0]
    values = [v for v in values if v is not None]
    return sum(values) / len(values) if values else float("nan")


def main():
    runs, tagA, tagB = sys.argv[1], sys.argv[2], sys.argv[3]
    for tag in (tagA, tagB):
        run = Run(runs, tag)
        rows = run.window_rows()
        print(f"=== {tag}: launched {run.launch and str(datetime.timedelta(seconds=run.launch))}, CONTINUE at {run.continued and str(datetime.timedelta(seconds=run.continued))}")
        print("  clock     frames   draws  foreign | cache lookups no-entry  hits | worker us/draw captures | commits us/commit busy% drain ms | l3-cache calls served hits (data) exact")
        for r in rows:
            c, d, p, l, lc = r["cache"], r["draw"], r["pipe"], r["l3"], r["l3cache"]
            print(f"  {r['clock']:>8} {r['frames']:7.1f} {l.get('draws', 0):7.0f} {l.get('foreign', 0):8.0f} | {c.get('lookups', 0):13.0f} {c.get('noentry', 0):8.0f} {c.get('hits', 0):5.0f} | {d.get('us', 0):14.1f} {d.get('captures', 0):8.0f} | {p.get('commits', 0):7.0f} {p.get('commit_us', 0):9.1f} {p.get('busy', 0):5.1f} {p.get('drain_ms', 0):8.0f} | {lc.get('calls', 0):7.0f} {lc.get('served', 0):6.0f} {lc.get('hits', 0):4.0f} ({lc.get('datahits', 0):.0f}) {lc.get('exact', 0):5.0f}")
        for item in run.keys["l3cache"][-3:]:
            print("  " + item["line"][:1400])
        run.rows = rows
        if tag == tagA:
            A = run
        else:
            B = run
    print()
    print("=== phase averages per 10 s window (menu = after the menu appeared until CONTINUE; world = 60 s after CONTINUE to the end)")
    header = f"{'metric':44} {'A menu':>10} {'B menu':>10} {'A world':>10} {'B world':>10}"
    print(header)
    metrics = [
        ("presented frames per 10 s", lambda r: r["frames"]),
        ("engine draws per 10 s ([l3])", lambda r: r["l3"].get("draws")),
        ("draw packets per 10 s ([draw])", lambda r: r["draw"].get("packets")),
        ("worker us per draw packet", lambda r: r["draw"].get("us")),
        ("  of it capture us", lambda r: r["draw"].get("capture_us")),
        ("  of it key/lookup/validate us", lambda r: r["draw"].get("validate_us")),
        ("  of it recompile us", lambda r: r["draw"].get("recompile_us")),
        ("worker draw ms per 10 s", lambda r: r["draw"].get("ms")),
        ("captures per 10 s", lambda r: r["draw"].get("captures")),
        ("draw-cache lookups per 10 s", lambda r: r["cache"].get("lookups")),
        ("draw-cache no entry per 10 s", lambda r: r["cache"].get("noentry")),
        ("draw-cache hits per 10 s", lambda r: r["cache"].get("hits")),
        ("draw-cache inserts per 10 s", lambda r: r["cache"].get("inserts")),
        ("stages compiled per 10 s", lambda r: r["regkey"].get("compiled")),
        ("committer commits per 10 s", lambda r: r["pipe"].get("commits")),
        ("committer us per commit", lambda r: r["pipe"].get("commit_us")),
        ("committer busy %", lambda r: r["pipe"].get("busy")),
        ("packet drains ms per 10 s", lambda r: r["pipe"].get("drain_ms")),
        ("queue 0 DISPATCH_DIRECT ms per 10 s", lambda r: r["q0"].get("dispatch_ms")),
        ("queue 0 DRAW_INDEX_2 ms per 10 s", lambda r: r["q0"].get("draw2_ms")),
        ("queue 0 WAIT_REG_MEM ms per 10 s", lambda r: r["q0"].get("wait_ms")),
        ("l3-cache calls per 10 s", lambda r: r["l3cache"].get("calls")),
        ("l3-cache exact hits per 10 s", lambda r: r["l3cache"].get("exact")),
        ("l3-cache served by alias per 10 s", lambda r: r["l3cache"].get("served")),
        ("l3-cache validated hits per 10 s", lambda r: r["l3cache"].get("hits")),
        ("l3-cache data hits per 10 s", lambda r: r["l3cache"].get("datahits")),
        ("l3-cache variants allowed per 10 s", lambda r: r["l3cache"].get("allowed")),
        ("l3-cache variants refused per 10 s", lambda r: r["l3cache"].get("refused")),
    ]
    for name, pick in metrics:
        cells = []
        for run in (A, B):
            menu_lo = run.continued - 50 if run.continued else 0
            menu_hi = run.continued if run.continued else 10 ** 9
            world_lo = run.continued + 60 if run.continued else 10 ** 9
            cells.append(phase_average(run.rows, menu_lo, menu_hi, pick))
            cells.append(phase_average(run.rows, world_lo, 10 ** 9, pick))
        print(f"{name:44} {cells[0]:10.1f} {cells[2]:10.1f} {cells[1]:10.1f} {cells[3]:10.1f}")


main()
