"""Renders the element map (a JSON spec) into one self-contained local HTML page with embedded JPEG screenshots.

usage: python -I elem_html.py <spec.json> <out.html>

Spec keys (all optional except title): title, subtitle, summary (list of paragraphs), facts (list of {label, value,
note}), breakdown (list of {name, ms, group}), breakdown_total_ms, breakdown_note, sections (list of {id, title, intro,
cards: [{name, plain, group, cost, fps_on, fps_off, gain, shots: [{src, caption}], details: [..], rule}],
table: {columns: [...], rows: [[...]]}, paragraphs: [...]}), glossary (list of {term, text}), footer.
Image paths may be BMP or PNG; they are scaled to at most 960 px wide and embedded as JPEG.
"""
import base64
import html
import io
import json
import os
import sys

from PIL import Image

CACHE = {}


def embed(path, width=960):
    if not path or not os.path.exists(path):
        return None
    key = (path, width)
    if key in CACHE:
        return CACHE[key]
    image = Image.open(path).convert("RGB")
    if image.width > width:
        image = image.resize((width, int(image.height * width / image.width)), Image.LANCZOS)
    buffer = io.BytesIO()
    image.save(buffer, "JPEG", quality=82, optimize=True)
    CACHE[key] = "data:image/jpeg;base64," + base64.b64encode(buffer.getvalue()).decode("ascii")
    return CACHE[key]


def esc(text):
    return html.escape(str(text)) if text is not None else ""


GROUP_COLORS = {
    "draw": "var(--c-draw)",
    "compute": "var(--c-compute)",
    "wait": "var(--c-wait)",
    "commit": "var(--c-commit)",
    "other": "var(--c-other)",
    "gpu": "var(--c-gpu)",
}

CSS = """
:root{--bg:#f6f5f2;--panel:#ffffff;--ink:#1d1d1f;--muted:#5d5f66;--line:#e2e0da;--accent:#9b2c2c;--good:#1f7a4d;--bad:#a33a2b;
--c-draw:#3b6ea8;--c-compute:#c07a1e;--c-wait:#8a8f98;--c-commit:#6b4fa0;--c-other:#b5b0a4;--c-gpu:#2f8f83;--shadow:0 1px 2px rgba(0,0,0,.06),0 4px 14px rgba(0,0,0,.05)}
@media (prefers-color-scheme: dark){:root:not([data-theme="light"]){--bg:#141416;--panel:#1d1e21;--ink:#ececee;--muted:#a3a6ad;--line:#2e3035;--accent:#e07a6b;--good:#5cc28e;--bad:#e8826f;--c-other:#6f6b63;--shadow:none}}
:root[data-theme="dark"]{--bg:#141416;--panel:#1d1e21;--ink:#ececee;--muted:#a3a6ad;--line:#2e3035;--accent:#e07a6b;--good:#5cc28e;--bad:#e8826f;--c-other:#6f6b63;--shadow:none}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.55 "Segoe UI",system-ui,-apple-system,sans-serif}
.wrap{max-width:1180px;margin:0 auto;padding:28px 16px 80px}
h1{font-size:30px;line-height:1.2;margin:0 0 6px;letter-spacing:-.01em}h2{font-size:22px;margin:42px 0 8px}h3{font-size:16px;margin:0 0 4px}
.sub{color:var(--muted);margin:0 0 18px}.muted{color:var(--muted)}
nav{display:flex;flex-wrap:wrap;gap:8px;margin:14px 0 6px}nav a{color:var(--ink);text-decoration:none;border:1px solid var(--line);background:var(--panel);padding:4px 10px;border-radius:999px;font-size:13px}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:18px 20px;box-shadow:var(--shadow)}
.facts{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:12px;margin:16px 0}
.fact{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:12px 14px}.fact .v{font-size:24px;font-weight:650}.fact .l{color:var(--muted);font-size:13px}.fact .n{color:var(--muted);font-size:12px;margin-top:2px}
.bar{display:flex;height:34px;border-radius:8px;overflow:hidden;border:1px solid var(--line);margin:10px 0 6px}
.bar span{display:block;height:100%;min-width:2px}
.legend{display:flex;flex-wrap:wrap;gap:6px 14px;font-size:13px;color:var(--muted)}.legend i{display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:5px;vertical-align:-1px}
.rows{margin-top:10px}.row{display:grid;grid-template-columns:minmax(150px,280px) 1fr 70px;gap:10px;align-items:center;padding:3px 0;font-size:13px}
.row .track{background:var(--bg);border-radius:4px;height:12px;overflow:hidden}.row .fill{height:100%}
.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(340px,1fr));gap:16px;margin-top:14px}
.card{background:var(--panel);border:1px solid var(--line);border-radius:12px;overflow:hidden;box-shadow:var(--shadow);display:flex;flex-direction:column}
.card .body{padding:14px 16px}.card .plain{margin:4px 0 8px}
.chip{display:inline-block;font-size:12px;padding:1px 8px;border-radius:999px;border:1px solid var(--line);color:var(--muted);margin-right:6px}
.gain{font-weight:650}.gain.pos{color:var(--good)}.gain.neg{color:var(--bad)}
.shots{display:grid;grid-template-columns:1fr 1fr;gap:2px;background:var(--line)}.shots.one{grid-template-columns:1fr}
.shots figure{margin:0;background:var(--panel)}.shots img{display:block;width:100%;height:auto;cursor:zoom-in}.shots figcaption{font-size:12px;color:var(--muted);padding:4px 8px}
details{margin-top:6px}summary{cursor:pointer;color:var(--muted);font-size:13px}ul.det{margin:6px 0 0 18px;padding:0;font-size:13px}
code{font-family:"Cascadia Mono",Consolas,monospace;font-size:12.5px;background:var(--bg);padding:1px 4px;border-radius:4px;word-break:break-all}
table{border-collapse:collapse;width:100%;font-size:13px;margin-top:10px}th,td{text-align:left;padding:6px 8px;border-bottom:1px solid var(--line);vertical-align:top}th{color:var(--muted);font-weight:600}
td.num,th.num{text-align:right;font-variant-numeric:tabular-nums;white-space:nowrap}
.tablewrap{overflow-x:auto}
dl.gl{display:grid;grid-template-columns:minmax(120px,190px) 1fr;gap:6px 16px;margin:10px 0 0}dl.gl dt{font-weight:600}dl.gl dd{margin:0;color:var(--muted)}
#zoom{position:fixed;inset:0;background:rgba(0,0,0,.86);display:none;align-items:center;justify-content:center;padding:16px;cursor:zoom-out;z-index:9}#zoom img{max-width:100%;max-height:100%}
.toggle{float:right;border:1px solid var(--line);background:var(--panel);color:var(--ink);border-radius:999px;padding:4px 10px;font-size:13px;cursor:pointer}
@media (max-width:600px){.row{grid-template-columns:1fr 60px}.row .track{grid-column:1/-1}h1{font-size:24px}.cards{grid-template-columns:1fr}}
"""

JS = """
document.addEventListener('click',e=>{const z=document.getElementById('zoom');if(e.target.matches('.shots img')){z.querySelector('img').src=e.target.src;z.style.display='flex'}else if(e.target.closest('#zoom')){z.style.display='none'}});
document.getElementById('theme').addEventListener('click',()=>{const r=document.documentElement;const dark=r.dataset.theme?r.dataset.theme==='dark':matchMedia('(prefers-color-scheme: dark)').matches;r.dataset.theme=dark?'light':'dark'});
"""


def gain_html(gain):
    if gain is None:
        return ""
    cls = "pos" if gain > 0 else "neg" if gain < 0 else ""
    return f'<span class="gain {cls}">{gain:+.0f}%</span>'


def render(spec):
    out = []
    out.append("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">")
    out.append(f"<title>{esc(spec.get('page_title', 'Yotei element map'))}</title><style>{CSS}</style></head><body><div class=\"wrap\">")
    out.append('<button class="toggle" id="theme" type="button">Light / dark</button>')
    out.append(f"<h1>{esc(spec['title'])}</h1><p class=\"sub\">{esc(spec.get('subtitle', ''))}</p>")
    sections = spec.get("sections", [])
    out.append("<nav>" + "".join(f'<a href="#{esc(s["id"])}">{esc(s["title"])}</a>' for s in sections) + ("<a href=\"#glossary\">Words used here</a>" if spec.get("glossary") else "") + "</nav>")
    if spec.get("facts"):
        out.append('<div class="facts">' + "".join(f'<div class="fact"><div class="v">{esc(f["value"])}</div><div class="l">{esc(f["label"])}</div>' + (f'<div class="n">{esc(f["note"])}</div>' if f.get("note") else "") + "</div>" for f in spec["facts"]) + "</div>")
    if spec.get("summary"):
        out.append('<div class="panel">' + "".join(f"<p>{p}</p>" for p in spec["summary"]) + "</div>")
    if spec.get("breakdown"):
        total = spec.get("breakdown_total_ms") or sum(b["ms"] for b in spec["breakdown"])
        out.append(f'<h2 id="frame">{esc(spec.get("breakdown_title", "Where one frame goes"))}</h2>')
        if spec.get("breakdown_note"):
            out.append(f'<p class="muted">{spec["breakdown_note"]}</p>')
        out.append('<div class="panel"><div class="bar">')
        for b in spec["breakdown"]:
            out.append(f'<span title="{esc(b["name"])}: {b["ms"]:.0f} ms" style="width:{100 * b["ms"] / total:.2f}%;background:{GROUP_COLORS.get(b.get("group"), "var(--c-other)")}"></span>')
        out.append("</div>")
        groups = []
        for b in spec["breakdown"]:
            if b.get("group") not in groups:
                groups.append(b.get("group"))
        out.append('<div class="legend">' + "".join(f'<span><i style="background:{GROUP_COLORS.get(g, "var(--c-other)")}"></i>{esc(spec.get("group_names", {}).get(g, g))}</span>' for g in groups) + "</div>")
        out.append('<div class="rows">')
        peak = max(b["ms"] for b in spec["breakdown"])
        for b in sorted(spec["breakdown"], key=lambda x: -x["ms"]):
            out.append(f'<div class="row"><div>{esc(b["name"])}</div><div class="track"><div class="fill" style="width:{100 * b["ms"] / peak:.1f}%;background:{GROUP_COLORS.get(b.get("group"), "var(--c-other)")}"></div></div><div class="num" style="text-align:right">{b["ms"]:.0f} ms</div></div>')
        out.append("</div></div>")
    for s in sections:
        out.append(f'<h2 id="{esc(s["id"])}">{esc(s["title"])}</h2>')
        if s.get("intro"):
            out.append(f'<p class="muted">{s["intro"]}</p>')
        for p in s.get("paragraphs", []):
            out.append(f"<p>{p}</p>")
        if s.get("cards"):
            out.append('<div class="cards">')
            for c in s["cards"]:
                shots = [x for x in c.get("shots", []) if embed(x.get("src"))]
                out.append('<div class="card">')
                if shots:
                    out.append(f'<div class="shots{" one" if len(shots) == 1 else ""}">' + "".join(f'<figure><img loading="lazy" src="{embed(x["src"])}" alt="{esc(x.get("caption", ""))}"><figcaption>{esc(x.get("caption", ""))}</figcaption></figure>' for x in shots) + "</div>")
                out.append('<div class="body">')
                out.append(f'<h3>{esc(c["name"])}</h3>')
                chips = []
                if c.get("group"):
                    chips.append(f'<span class="chip">{esc(c["group"])}</span>')
                if c.get("cost"):
                    chips.append(f'<span class="chip">{esc(c["cost"])}</span>')
                if c.get("fps_on") is not None and c.get("fps_off") is not None:
                    chips.append(f'<span class="chip">{c["fps_on"]:.2f} → {c["fps_off"]:.2f} fps {gain_html(c.get("gain"))}</span>')
                out.append("<div>" + "".join(chips) + "</div>")
                if c.get("plain"):
                    out.append(f'<p class="plain">{c["plain"]}</p>')
                if c.get("details") or c.get("rule"):
                    out.append("<details><summary>Details</summary><ul class=\"det\">" + "".join(f"<li>{d}</li>" for d in c.get("details", [])) + (f'<li>Skip rule: <code>{esc(c["rule"])}</code></li>' if c.get("rule") else "") + "</ul></details>")
                out.append("</div></div>")
            out.append("</div>")
        if s.get("table"):
            t = s["table"]
            numeric = set(t.get("numeric", []))
            out.append('<div class="panel tablewrap"><table><thead><tr>' + "".join(f'<th class="{"num" if i in numeric else ""}">{esc(col)}</th>' for i, col in enumerate(t["columns"])) + "</tr></thead><tbody>")
            for row in t["rows"]:
                out.append("<tr>" + "".join(f'<td class="{"num" if i in numeric else ""}">{cell if isinstance(cell, str) and cell.startswith("<") else esc(cell)}</td>' for i, cell in enumerate(row)) + "</tr>")
            out.append("</tbody></table></div>")
        for p in s.get("after", []):
            out.append(f"<p>{p}</p>")
    if spec.get("glossary"):
        out.append('<h2 id="glossary">Words used here</h2><div class="panel"><dl class="gl">' + "".join(f"<dt>{esc(g['term'])}</dt><dd>{g['text']}</dd>" for g in spec["glossary"]) + "</dl></div>")
    if spec.get("footer"):
        out.append(f'<p class="muted" style="margin-top:40px">{spec["footer"]}</p>')
    out.append(f'<div id="zoom"><img alt=""></div></div><script>{JS}</script></body></html>')
    return "".join(out)


def main():
    spec = json.load(open(sys.argv[1], encoding="utf-8"))
    base = os.path.dirname(os.path.abspath(sys.argv[1]))
    cwd = os.getcwd()
    os.chdir(base)
    page = render(spec)
    os.chdir(cwd)
    with open(sys.argv[2], "w", encoding="utf-8") as f:
        f.write(page)
    print(f"wrote {sys.argv[2]} ({len(page) / 1e6:.1f} MB)")


if __name__ == "__main__":
    main()
