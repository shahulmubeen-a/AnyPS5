"""Summarise the [l3-stage] lines of a run (APS5_PORTER_L3_DUMP_STAGES=1).

Usage: python -I l3stage_report.py <run.err.log> [word word ...]

For every engine-draw shader the driver printed its stage, user-data base register, the resource walk's
asBase/asValue masks and the request file it wrote. This groups the shaders by stage and shows, for the
listed user words (default: the world's refused GS words 2, 6, 10, 14, 18, 24, 28), whether the word is
read as a base (B), as a value (V), both (X) or not at all (-), so the shaders to disassemble can be picked.
"""
import re
import sys
from collections import Counter, defaultdict

STAGES = {0: "compute", 1: "vertex", 2: "hull", 3: "domain", 4: "geometry", 5: "fragment", 6: "local", 7: "mesh"}
LINE = re.compile(r"\[l3-stage\] 0x([0-9a-f]+) stage (\d+) userDataBase 0x([0-9a-f]+) firstUser (\d+) hidden (\d+) words (\d+) (\S+): asBase 0x([0-9a-f]+) asValue 0x([0-9a-f]+) reads (\d+)")


def main() -> None:
    path = sys.argv[1]
    words = [int(w) for w in sys.argv[2:]] or [2, 6, 10, 14, 18, 24, 28]
    rows = []
    with open(path, "r", encoding="utf-8", errors="replace") as file:
        for line in file:
            m = LINE.search(line)
            if m:
                rows.append((int(m[1], 16), int(m[2]), int(m[3], 16), int(m[4]), int(m[5]), int(m[6]), m[7], int(m[8], 16), int(m[9], 16), int(m[10])))
    print(f"{len(rows)} engine-draw shaders")
    by_stage = defaultdict(list)
    for row in rows:
        by_stage[row[1]].append(row)
    for stage, items in sorted(by_stage.items()):
        print(f"\n== {STAGES.get(stage, stage)}: {len(items)} shaders, userDataBase " + ", ".join(f"0x{b:x}" for b in sorted({r[2] for r in items})))
        patterns = Counter()
        for code, _, base, first, hidden, count, name, as_base, as_value, reads in items:
            pattern = ""
            for w in words:
                b = (as_base >> w) & 1
                v = (as_value >> w) & 1
                pattern += "X" if b and v else "B" if b else "V" if v else "-"
            patterns[pattern] += 1
        print("  words " + " ".join(f"{w:>2}" for w in words) + "   shaders  (B base, V value, X both, - unread)")
        for pattern, n in patterns.most_common():
            print("        " + "  ".join(pattern) + f"   {n}")
        print("  shaders with a V among the listed words:")
        for code, _, base, first, hidden, count, name, as_base, as_value, reads in items:
            bad = [w for w in words if (as_value >> w) & 1]
            if bad:
                print(f"    0x{code:x} {name} firstUser {first} hidden {hidden} words {count} reads {reads} value-words {bad} asBase 0x{as_base:x} asValue 0x{as_value:x}")


if __name__ == "__main__":
    main()
