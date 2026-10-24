"""Builds the night's element map (yotei-work/porter/elements-map/index.html) from the sweep runs.

usage: python -I elem_build_map.py [--out DIR]

Inputs (yotei-work/runs-best): report-el2.json + plan-el2.txt + autoplan-el2.txt + shots-el2 (single elements),
plan-el3.txt + shots-el3 (floors, knobs, cumulative cuts), plan-el5.txt / plan-el6.txt (+ shots) for the lean
configurations and runtime switches, and the hand-written texts below. Screenshots are embedded as JPEG.
"""
import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from elem_spec import base_rates, read_plan  # noqa: E402

RUNS = r"C:\Users\Mubeen\Documents\Projects\yotei-work\runs-best"
OUT = r"C:\Users\Mubeen\Documents\Projects\yotei-work\porter\elements-map"


def plan(run):
    path = os.path.join(RUNS, f"plan-{run}.txt")
    return read_plan(path) if os.path.exists(path) else []


def shot(run, label):
    path = os.path.join(RUNS, f"shots-{run}", f"{label}.bmp")
    return path if os.path.exists(path) else None


def step(steps, label):
    return next((s for s in steps if s["label"] == label), None)


def fps(steps, label):
    s = step(steps, label)
    return s["rate"] / 10 if s and s["rate"] is not None else None


def config_card(run, steps, label, name, plain, base_label=None, details=None, rule=None):
    s = step(steps, label)
    if s is None:
        return None
    rates = base_rates(steps)
    index = steps.index(s)
    base = rates.get(index)
    if base_label is not None and step(steps, base_label):
        base = step(steps, base_label)["rate"]
    on = base / 10 if base else None
    off = s["rate"] / 10 if s["rate"] is not None else None
    gain = 100.0 * (off - on) / on if (on and off is not None) else None
    base_shot = None
    for candidate in ("measure", "base-1", "base-a", "base0"):
        base_shot = base_shot or shot(run, candidate)
    return {"name": name, "plain": plain, "group": f"run {run}", "fps_on": on, "fps_off": off, "gain": gain,
            "cost": f"{off:.2f} fps" if off is not None else "", "details": details or [], "rule": rule,
            "shots": [{"src": base_shot, "caption": "full game (base)"}, {"src": shot(run, label), "caption": name}]}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=OUT)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    spec_path = os.path.join(args.out, "spec-el2.json")
    subprocess.run([sys.executable, "-I", os.path.join(HERE, "elem_spec.py"), "--run", "el2", "--out", spec_path], check=True)
    spec = json.load(open(spec_path, encoding="utf-8"))
    texts = json.load(open(os.path.join(HERE, "elem_texts.json"), encoding="utf-8"))
    runs = {name: plan(name) for name in ("el3", "el5", "el6", "el7", "el9", "el10", "el11")}
    config_sections = []
    for section in texts["config_sections"]:
        cards = []
        for run, label, name, plain in section["cards"]:
            card = config_card(run, runs.get(run, []), label, name, plain)
            if card:
                cards.append(card)
        config_sections.append({"id": section["id"], "title": section["title"], "intro": section["intro"], "cards": cards})
    for key in ("title", "subtitle", "summary", "facts", "glossary", "footer", "breakdown", "breakdown_title", "breakdown_note", "breakdown_total_ms", "group_names"):
        if key in texts:
            spec[key] = texts[key]
    sections = []
    sections.extend(config_sections)
    sections.append({"id": "why", "title": "Why the game is slow here", "intro": texts["why_intro"], "paragraphs": texts["why"]})
    sections.extend(spec["sections"])
    sections.append({"id": "causes", "title": "Root causes, one by one", "intro": texts["causes_intro"], "table": texts["causes_table"]})
    sections.append({"id": "next", "title": "What would make it faster without cutting", "intro": texts["next_intro"], "paragraphs": texts["next"]})
    spec["sections"] = sections
    json.dump(spec, open(os.path.join(args.out, "spec.json"), "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    subprocess.run([sys.executable, "-I", os.path.join(HERE, "elem_html.py"), os.path.join(args.out, "spec.json"), os.path.join(args.out, "index.html")], check=True)


if __name__ == "__main__":
    main()
