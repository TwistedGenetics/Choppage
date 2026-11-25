#!/usr/bin/env python3
"""Renders choppage-mpc/skins/TUI.json the way the MPC lays it out, with real parameter values, so a layout can be
checked without hardware. It reads the generated JSON (not the generator), so it checks what the MPC will load.

    test_mpc_app --state /tmp/state.json [lcd 0-3] [view 0-3]
    python3 choppage-mpc/tools/preview_skin.py /tmp/state.json --out /tmp/preview [--tabs MAIN,SLICE]

Draws: Image (Regular / FilmStrip), Knob (FilmStrip), Label (Value / Name / Text), Button groups, local components
with handle remapping, and conditional visibility (IndexedEnabling / RangedEnabling). Focus frames and overlays
are not drawn. Fonts: Roboto where installed (the MPC's), else DejaVu, so text widths are close, not exact.
The MPC's own header and function-key strip are approximated around the 1280 x 628 tab.
"""
import argparse, json, os, re
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
SKIN = os.path.join(os.path.dirname(HERE), "skins")

FONTS = ["/usr/share/fonts/truetype/google-fonts/ofl/roboto/Roboto[wdth,wght].ttf",
         "/usr/share/fonts/truetype/roboto/Roboto-Bold.ttf",
         "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"]
WEIGHTS = {"Regular": 400, "Medium": 500, "SemiBold": 600, "Bold": 700}


def font(size, style="Bold"):
    for f in FONTS:
        if os.path.exists(f):
            fo = ImageFont.truetype(f, int(size))
            try:
                fo.set_variation_by_axes([WEIGHTS.get(style, 700), 100])
            except Exception:
                pass
            return fo
    return ImageFont.load_default()


def pairs(obj):
    return obj  # json.load keeps the last duplicate key; fine for drawing


class Renderer:
    def __init__(self, tui, params, state, skin):
        self.defs = {d["key"]: d["value"] for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        self.repl = {r["key"]: r["value"] for r in tui["pageData"].get("replacementStrings", [])}
        self.params = params
        self.state = state
        self.skin = skin
        self.cache = {}

    def img(self, name):
        if name not in self.cache:
            self.cache[name] = Image.open(os.path.join(self.skin, name)).convert("RGBA")
        return self.cache[name]

    def pindex(self, handle):
        m = re.search(r"Parameter (\d+)$", handle or "")   # also through wrappers like ResetOnGestureEnd/
        return int(m.group(1)) if m else None

    def value(self, handle):
        i = self.pindex(handle)
        return None if i is None else float(self.state[str(i)][0])

    def norm(self, handle):
        i = self.pindex(handle)
        if i is None:
            return 0.0
        p = self.params[i]
        lo, hi = p["min"], p["max"]
        return 0.0 if hi == lo else (self.value(handle) - lo) / (hi - lo)

    def text(self, handle):
        i = self.pindex(handle)
        if i is None:
            return ""
        v, t = self.state[str(i)]
        key = "Parameter %d" % i
        if key in self.repl and self.params[i]["kind"] in ("choice", "bool"):
            ch = self.repl[key]
            return ch[max(0, min(len(ch) - 1, int(round(v))))]
        return t

    def visible(self, b, remap):
        for h in b.get("additionalInvalidatingHandles", []) if isinstance(b, dict) else []:
            parts = h.split("/")
            kind = parts[0]
            target = "/".join(parts[3:] if kind == "IndexedEnabling" else parts[4:])
            target = remap.get(target, target)
            v = self.value(target)
            if v is None:
                continue
            if kind == "IndexedEnabling":
                if int(round(v)) != int(parts[1]):
                    return False
            elif kind == "RangedEnabling":
                if not (int(parts[1]) <= int(round(v)) <= int(parts[2])):
                    return False
        return True

    def rect(self, b, pw, ph):
        x, y, w, h = map(float, b["bounds"].split())
        if b.get("boundsType") == "Proportional":
            return x * pw, y * ph, w * pw, h * ph
        return x, y, w, h

    def draw_component(self, canvas, comp, ox, oy, w, h, remap):
        bgc = comp["backgroundData"]["unfocussed"]
        if bgc.get("colour", "0") not in ("0", "") and len(bgc["colour"]) == 8:
            c = bgc["colour"]
            a, r, g, b_ = (int(c[i:i + 2], 16) for i in (0, 2, 4, 6))
            if a:
                ImageDraw.Draw(canvas).rectangle((ox, oy, ox + w - 1, oy + h - 1), fill=(r, g, b_, a))
        for e in comp["componentsData"]:
            self.draw_entry(canvas, e, ox, oy, w, h, remap)

    def draw_entry(self, canvas, e, ox, oy, pw, ph, remap):
        cd = e["componentData"]
        typ, data = cd["type"], cd["data"]
        b = e["bounds"]
        if b.get("whenVisible") == "WhenFocussed":
            return
        local = dict(remap)
        for m in e.get("handle remapping", {}).get("map", []):
            local[m["key"]] = remap.get(m["value"], m["value"])
        if not self.visible(b, local):
            return
        x, y, w, h = self.rect(b, pw, ph)
        x, y = ox + x, oy + y
        handle = local.get(data.get("handleName", ""), data.get("handleName", "")) if isinstance(data, dict) else ""
        if typ == "Image":
            im = self.img(data["image"]) if data.get("image") else None
            if im is None:
                return
            if data.get("imageType") == "FilmStrip":
                n = data["numFrames"]
                fh = im.height // n
                f = max(0, min(n - 1, int(round(self.norm(handle) * (n - 1)))))
                im = im.crop((0, f * fh, im.width, (f + 1) * fh))
            canvas.alpha_composite(im.resize((int(w), int(h))) if im.size != (int(w), int(h)) else im, (int(x), int(y)))
        elif typ == "Knob":
            im = self.img(data["filmStrip"])
            n = data["numFrames"]
            fh = im.height // n
            f = max(0, min(n - 1, int(round(self.norm(handle) * (n - 1)))))
            canvas.alpha_composite(im.crop((0, f * fh, im.width, (f + 1) * fh)), (int(x), int(y)))
        elif typ == "Label":
            st = data["textStyle"]
            kind = data["type"]
            if kind == "Value":
                t = self.text(handle)
            elif kind == "Name":
                m = re.match(r"WithName\[(.*)\]/", handle)
                t = m.group(1) if m else self.params[self.pindex(handle)]["name"]
            else:
                t = data.get("text", "")
            c = st["colour"]
            col = tuple(int(c[i:i + 2], 16) for i in (2, 4, 6)) + (int(c[0:2], 16),)
            fo = font(st["font"]["height"] * 0.86, st["font"]["style"])   # JUCE height ~ ascent+descent
            d = ImageDraw.Draw(canvas)
            just = st["justification"]
            while fo.size > 8 and fo.getlength(t) > w:
                fo = font(fo.size - 1, st["font"]["style"])
            if "horizontallyCentred" in just or just.startswith("centred"):
                d.text((x + w / 2, y + h / 2), t, font=fo, fill=col, anchor="mm")
            elif just.startswith("right"):
                d.text((x + w, y + h / 2), t, font=fo, fill=col, anchor="rm")
            else:
                d.text((x, y + h / 2), t, font=fo, fill=col, anchor="lm")
        elif typ == "Button":
            v = self.value(handle)
            on = v is not None and int(round(v)) == data["buttonId"]
            name = data["onImage"] if on else data["offImage"]
            if name:
                canvas.alpha_composite(self.img(name), (int(x), int(y)))
        elif typ == "Slider" and data.get("sliderType") == "TrackAndThumb":
            th = self.img(data["thumbImage"])
            v = self.norm(handle)
            canvas.alpha_composite(th, (int(x + v * (w - th.width)), int(y + (h - th.height) / 2)))
        elif typ in ("Focus", "KnobOverlay"):
            return
        elif typ in self.defs:
            self.draw_component(canvas, self.defs[typ], x, y, w, h, local)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("state")
    ap.add_argument("--skin", default=SKIN)
    ap.add_argument("--params", default=os.path.join(HERE, "params.json"))
    ap.add_argument("--out", default="/tmp/choppage-preview")
    ap.add_argument("--tabs", default="")
    a = ap.parse_args()
    tui = json.load(open(os.path.join(a.skin, "TUI.json")))
    params = json.load(open(a.params))
    state = json.load(open(a.state))
    r = Renderer(tui, params, state, a.skin)
    os.makedirs(a.out, exist_ok=True)
    want = [t.strip() for t in a.tabs.split(",") if t.strip()]
    tabs = tui["pageData"]["tabs"]
    fnames = {}
    for t in tabs:
        fnames.setdefault(t["fnKeyIndex"], t["tabName"])
    for t in tabs:
        if want and t["tabName"] not in want:
            continue
        tab = Image.new("RGBA", (1280, 628), (0, 0, 0, 255))
        r.draw_component(tab, t["componentDefinition"], 0, 0, 1280, 628, {})
        full = Image.new("RGBA", (1280, 800), (24, 24, 26, 255))
        full.alpha_composite(tab, (0, 110))
        d = ImageDraw.Draw(full)
        d.text((24, 55), "CHOPPAGE  ·  " + t["tabName"], font=font(30), fill=(235, 235, 235), anchor="lm")
        for i, (fn, name) in enumerate(sorted(fnames.items())):
            fx = 12 + i * 180
            d.rounded_rectangle((fx, 748, fx + 168, 792), radius=6, fill=(220, 30, 40) if fn == t["fnKeyIndex"] else (60, 60, 64))
            d.text((fx + 84, 770), name, font=font(22), fill=(255, 255, 255), anchor="mm")
        path = os.path.join(a.out, "%d_%d_%s.png" % (t["fnKeyIndex"], t["fnKeySubIndex"], t["tabName"].replace(" ", "_")))
        full.convert("RGB").save(path)
        print(path)


if __name__ == "__main__":
    main()
