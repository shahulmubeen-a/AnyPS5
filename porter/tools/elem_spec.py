"""Builds the element-map spec (JSON for elem_html.py) from a sweep run of elem-run.ps1.

usage: python -I elem_spec.py --run el2 [--runs-dir DIR] --out spec.json [--extra extra.json]

Reads runs-best/report-<run>.json (the measurement, elem_report.py), runs-best/plan-<run>.txt and
runs-best/autoplan-<run>.txt (the sweep and its rules), runs-best/shots-<run>/*.bmp (the newest frame of each step)
and, from the run log, the [elem-name] / [elem-tech] lines. --extra merges a hand-written JSON (summary text, root
causes, the 5 fps configuration, extra sections) into the spec.
"""
import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elem_names import describe  # noqa: E402

KIND_TEXT = {
    "kind-item": ("Single objects (DrawItem)", "Every object the engine draws one at a time: rocks, houses, props, characters' parts."),
    "kind-instanced": ("Instanced objects (DrawInstanced)", "Objects drawn many times in one call: grass clumps, bushes, fences, repeated props."),
    "kind-instanced-b": ("Instanced objects, second path", "The engine's second instanced-draw path (special cases, indirect instancing)."),
}
VA_TEXT = {
    "0xdfb200": ("Shadow-cascade batch draws", "Batches of depth-only draws for the sun's shadow cascades."),
    "0x157a3ab": ("GPU-driven batches", "Batches the GPU itself decided to draw (GPU-driven rendering, mostly vegetation and clutter)."),
    "0x13dbac0": ("One-draw helper", "An engine helper that issues exactly one draw per call (full-screen or utility draws)."),
    "0xba7e70": ("World batch path A", "A world-only batch path, about 33 draws per call."),
    "0xdf9e60": ("World batch path B", "A world-only batch path, about 14 draws per call."),
    "0x1268630": ("Indirect multi-draw cluster", "GPU-driven indirect multi-draws (draw lists written by compute shaders)."),
    "0x13dbec0": ("Low-rate indirect draws", "Indirect draws whose argument lists come from the GPU."),
    "0x126cb10": ("Low-rate indirect draws B", "Indirect draws whose argument lists come from the GPU."),
    "0x1266b10": ("Low-rate indirect draws C", "Indirect draws whose argument lists come from the GPU."),
}


def read_plan(path):
    steps = []
    for line in open(path, encoding="utf-8"):
        if line.startswith("#") or line.startswith("PLAN DONE") or not line.strip():
            continue
        parts = line.split()
        label = parts[0]
        rate = None if parts[2] == "?" else float(parts[2])
        steps.append({"label": label, "seconds": int(parts[1]), "rate": rate, "frames": parts[3], "dump": parts[5] if len(parts) > 5 else ""})
    return steps


def read_rules(path):
    rules = {}
    if not os.path.exists(path):
        return rules
    for line in open(path, encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        parts = line.strip().split(None, 2)
        if len(parts) > 2:
            rules[parts[0]] = parts[2]
    return rules


def base_rates(steps):
    """For each step index, the mean rate of the nearest base steps before and after it."""
    bases = [(i, s["rate"]) for i, s in enumerate(steps) if (s["label"].startswith("base") or s["label"] == "measure") and s["rate"] is not None]
    out = {}
    for i, s in enumerate(steps):
        before = [r for j, r in bases if j < i]
        after = [r for j, r in bases if j > i]
        near = ([before[-1]] if before else []) + ([after[0]] if after else [])
        out[i] = sum(near) / len(near) if near else None
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--run", required=True)
    ap.add_argument("--runs-dir", default=r"C:\Users\Mubeen\Documents\Projects\yotei-work\runs-best")
    ap.add_argument("--out", required=True)
    ap.add_argument("--extra")
    args = ap.parse_args()
    d = args.runs_dir
    report = json.load(open(os.path.join(d, f"report-{args.run}.json"), encoding="utf-8"))
    steps = read_plan(os.path.join(d, f"plan-{args.run}.txt"))
    rules = read_rules(os.path.join(d, f"autoplan-{args.run}.txt"))
    shots = os.path.join(d, f"shots-{args.run}")
    presents = report.get("presents_per_10s") or 0
    frames = presents if presents else 1
    bases = base_rates(steps)
    base_shot = None
    for s in steps:
        if (s["label"].startswith("base") or s["label"] == "measure") and os.path.exists(os.path.join(shots, s["label"] + ".bmp")):
            base_shot = os.path.join(shots, s["label"] + ".bmp")
            break
    cards = {"pass": [], "engine": [], "compute": [], "material": []}
    for i, s in enumerate(steps):
        label = s["label"]
        if label.startswith("base") or label == "measure":
            continue
        base = bases.get(i)
        gain = (100.0 * (s["rate"] - base) / base) if (base and s["rate"] is not None) else None
        shot = os.path.join(shots, label + ".bmp")
        card = {"fps_on": base / 10 if base else None, "fps_off": s["rate"] / 10 if s["rate"] is not None else None, "gain": gain,
                "rule": rules.get(label, ""), "shots": [{"src": base_shot, "caption": "with it (base)"}, {"src": shot, "caption": "without it"}]}
        if label.startswith("pass-"):
            rule = rules.get(label, "")
            target = rule.split(";;")[0].replace("passx ", "").strip()
            queue, name, text = describe(target)
            row = [0.0] * 8
            for key, values in report["passes"].items():
                if key == target or key.startswith(target + "/"):
                    row = [a + b for a, b in zip(row, values[:8])]
            work = max(0.0, row[0] - row[3])
            card.update({"name": name, "plain": text, "group": queue,
                         "cost": f"{(work + row[1] + row[2]) / frames:.0f} ms of CPU per frame",
                         "details": [f"Marker path: <code>{target}</code>", f"Per frame: {row[4] / frames:.0f} draws, {row[5] / frames:.0f} dispatches", f"Queue worker {work / frames:.1f} ms (+ {row[3] / frames:.1f} ms waiting), draw committer {row[1] / frames:.1f} ms, preparer {row[2] / frames:.1f} ms per frame"]})
            cards["pass"].append(card)
        elif label.startswith("kind-") or label.startswith("va-"):
            key = label[5:] if label.startswith("va-") else None
            name, text = (KIND_TEXT.get(label) or VA_TEXT.get(key) or (label, ""))
            engine_key = ("other " + key) if key else label[5:]
            row = report["engines"].get(engine_key, [0, 0, 0, 0, 0])
            card.update({"name": name, "plain": text, "group": "engine draw function" + (f" {key}" if key else ""),
                         "cost": f"{(row[0] + row[1] + row[2]) / frames:.0f} ms of CPU per frame, {row[3] / frames:.0f} draws"})
            cards["engine"].append(card)
        elif label.startswith("prog-"):
            address = label[5:]
            row, where = None, ""
            for key, values in report["programs"].items():
                parts = key.split()
                if parts[0] == "c" and parts[1] == address:
                    row, where = values, values[5] if len(values) > 5 else ""
            queue, name, text = describe(where) if where else ("", address, "")
            cost = f"{(row[0] + row[1] + row[2]) / frames:.0f} ms of CPU per frame, {row[3] / frames:.0f} dispatches" if row else ""
            card.update({"name": f"{name} (compute {address})", "plain": text, "group": queue, "cost": cost, "details": [f"Pass: <code>{where}</code>"] if where else []})
            cards["compute"].append(card)
        elif label.startswith("mat"):
            material = label.split("-", 1)[1]
            row = report["materials"].get(material)
            where = row[5] if row and len(row) > 5 else ""
            queue, pname, _ = describe(where) if where else ("", "", "")
            card.update({"name": f"Material {material}", "plain": f"One material (surface type) first seen in: {pname}." if pname else "One material (surface type).",
                         "group": "material", "cost": f"{(row[0] + row[1] + row[2]) / frames:.0f} ms of CPU per frame, {row[3] / frames:.0f} draws" if row else ""})
            cards["material"].append(card)
    for group in cards.values():
        group.sort(key=lambda c: -(c["gain"] if c["gain"] is not None else -1e9))
    spec = {
        "title": "Ghost of Yōtei: what each part of the world costs",
        "page_title": "Yotei element map",
        "subtitle": f"Run {args.run}, gameplay at HOME (Ezo save), 1080p. Baseline {presents / 10:.2f} fps.",
        "sections": [
            {"id": "passes", "title": "Render passes", "intro": "Each card removes one pass of the frame for 40 seconds. The two pictures show the scene with the pass and without it; the numbers are frames per second before and after.", "cards": cards["pass"]},
            {"id": "engine", "title": "Engine draw functions", "intro": "The same scene with one of the game's own draw functions switched off at the emulator.", "cards": cards["engine"]},
            {"id": "compute", "title": "Compute jobs", "intro": "Single compute programs (GPU calculations that draw nothing themselves).", "cards": cards["compute"]},
            {"id": "materials", "title": "Materials", "intro": "The costliest materials, each removed at the game itself (its draw calls return at once).", "cards": cards["material"]},
        ],
    }
    if args.extra:
        extra = json.load(open(args.extra, encoding="utf-8"))
        for key, value in extra.items():
            if key == "sections_before":
                spec["sections"] = value + spec["sections"]
            elif key == "sections_after":
                spec["sections"] = spec["sections"] + value
            else:
                spec[key] = value
    json.dump(spec, open(args.out, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    print(f"wrote {args.out}: {sum(len(v) for v in cards.values())} cards")


if __name__ == "__main__":
    main()
