"""Writes the hand-made parts of the element map (summary, frame breakdown, root causes, configurations, next steps)
as an --extra JSON for elem_spec.py. Numbers come from the night's runs (el2-el6); see porter/docs/elements-night-log.md.

usage: python -I elem_extra.py <out.json> [--glossary porter/docs/elements-glossary.json] [--shots-dir DIR]
"""
import argparse
import json
import os

RUNS = r"C:\Users\Mubeen\Documents\Projects\yotei-work\runs-best"


def shot(run, label):
    return os.path.join(RUNS, f"shots-{run}", f"{label}.bmp")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--glossary", default=r"C:\Users\Mubeen\Documents\Projects\ps5-porter\porter\docs\elements-glossary.json")
    ap.add_argument("--results", help="JSON with measured configuration results to merge (written by hand or a script)")
    args = ap.parse_args()
    results = json.load(open(args.results, encoding="utf-8")) if args.results else {}
    extra = json.load(open(args.glossary, encoding="utf-8"))
    extra.update(results.get("top", {}))
    extra["breakdown_title"] = "Where one frame goes (gameplay, 553 ms per frame)"
    extra["breakdown_note"] = ("Timeline trace of 8 frames (run el4). The emulator turns the game's GPU orders into PC graphics work on a "
                               "few threads. The main one, the <b>queue-0 worker</b>, is the frame's backbone: the bar splits its 500 ms "
                               "per frame. Waiting is time it spends blocked on another thread or on the graphics card.")
    extra["group_names"] = {"draw": "translating draws", "compute": "translating compute jobs", "wait": "waiting for the draw committer", "gpu": "waiting for the GPU or other queues", "other": "other"}
    extra["breakdown"] = results.get("breakdown", [
        {"name": "Waiting for the draw committer to finish (before compute jobs)", "ms": 158, "group": "wait"},
        {"name": "Translating draws (state capture, shader lookup, stages)", "ms": 121, "group": "draw"},
        {"name": "Translating compute jobs", "ms": 73, "group": "compute"},
        {"name": "Committer queue full (back-pressure)", "ms": 52, "group": "wait"},
        {"name": "Waiting for async queues and GPU labels", "ms": 41, "group": "gpu"},
        {"name": "Waiting for the shared GPU lock", "ms": 30, "group": "gpu"},
        {"name": "Waiting for the next frame's orders", "ms": 18, "group": "other"},
        {"name": "Other packet handling", "ms": 28, "group": "other"},
    ])
    extra["breakdown_total_ms"] = 521
    sections_before = results.get("sections_before", [])
    sections_after = results.get("sections_after", [])
    extra["sections_before"] = sections_before
    extra["sections_after"] = sections_after
    json.dump(extra, open(args.out, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
