#!/usr/bin/env python3
"""CHOPPAGE "S950" MPC skin generator.

Writes the hand-written MPC Plugin Skins folder (TUI.json, Q-Links.json and every PNG it uses) into
choppage-mpc/skins/. The SDK's build.py copies that folder verbatim to "Plugin Skins/".

Design: the Akai S950 front panel. Warm grey panel, dark text; on every page the S950 LCD across the top
(BLUE by default, GREEN / AMBER / WHITE from SET > LCD) with the break name, tempo, status, the slice playing
and a pattern playhead bar; under it up to four rows of four controls, one row per bank of four Q-Links.
Knobs are film strips, switches and triggers are S950 keycaps with a red LED, choices are rows of keys.
Text: Roboto, names 24 px, values 32 px, dark on light (the LCD keeps its own colours).

The SDK has no drawing canvas, so the LCD shows readouts and a playhead bar, not the waveform (the desktop
edition draws that). Hardware-proven widget rules from Twisted-Helix are kept: no MenuOverlay, the focus frame
underneath button groups, Toggle Switch on Mouse Down for two-state keys, captions as Name labels.

    python3 choppage-mpc/tools/make_skin.py [--params choppage-mpc/tools/params.json] [--out choppage-mpc/skins]
                                            [--preview <dir>]

params.json comes from `test_mpc_app --dump` (ids, kinds, choices, flags). Needs Pillow.
"""
import argparse, json, math, os, sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
MPC = os.path.dirname(HERE)

# ---------------------------------------------------------------- palette (desktop Skin.h)
PANEL = (228, 225, 218)
PANEL_LO = (211, 207, 198)
LINE = (162, 158, 149)
INK = (26, 26, 26)
LABEL = "ff2a2a2a"
VALUE = "ff1a1a1a"
RED = (192, 20, 28)
LED_ON = (255, 42, 42)
LED_OFF = (74, 46, 46)
BEZEL = (20, 20, 20)
KEY_TOP = (244, 242, 236)
KEY_BOT = (214, 210, 200)
KEY_EDGE = (142, 138, 128)
KEY_ON_TOP = (255, 214, 120)     # a picked choice: amber key, like the desktop's lit pads
KEY_ON_BOT = (242, 179, 61)
TRACK = (186, 182, 172)
KNOB_CAP = (62, 62, 66)
BOX = (240, 238, 232)            # value window

# LCD themes: bg, ink, dim, faint (same as the desktop's lcdc())
LCD = [((0x3d, 0x4c, 0xf2), (0xf4, 0xf6, 0xff), (0xc9, 0xd0, 0xff), (0x77, 0x82, 0xfa)),
       ((0xc7, 0xd6, 0xa2), (0x14, 0x20, 0x08), (0x3f, 0x4d, 0x26), (0x8f, 0xa0, 0x6a)),
       ((0xf2, 0xa9, 0x3b), (0x24, 0x12, 0x00), (0x5e, 0x3a, 0x0c), (0xb0, 0x7a, 0x2a)),
       ((0xee, 0xf0, 0xea), (0x15, 0x17, 0x1a), (0x4a, 0x4e, 0x55), (0xa3, 0xa7, 0xad))]

def argb(rgb, a=255):
    return "%02x%02x%02x%02x" % (a, rgb[0], rgb[1], rgb[2])

# ---------------------------------------------------------------- geometry (tab coordinates, 1280 x 628)
W, H = 1280, 628
M = 10                       # outer margin
LCD_X, LCD_Y, LCD_W, LCD_H = M, 6, W - 2 * M, 134
ROW_Y0, ROW_H, ROW_GAP = LCD_Y + LCD_H + 10, 112, 6
COL_W = (W - 2 * M) // 4     # 315
CELL_PAD = 6
KNOB = 84
FRAMES = 128
FM = 6                       # focus margin round button groups

def row_y(r):
    return ROW_Y0 + r * (ROW_H + ROW_GAP)

# ---------------------------------------------------------------- JSON with ordered (and duplicate) keys
class O(list):
    """An ordered JSON object: a list of (key, value) pairs, so duplicate keys survive."""

def o(*pairs):
    return O(pairs)

def dump(v, ind=0):
    sp = " " * ind
    if isinstance(v, O):
        if not v:
            return "{}"
        return "{\n" + ",\n".join("%s %s: %s" % (sp, json.dumps(k), dump(x, ind + 1)) for k, x in v) + "\n" + sp + "}"
    if isinstance(v, list):
        if not v:
            return "[]"
        if all(not isinstance(x, (list, O)) for x in v):
            return "[" + ", ".join(json.dumps(x) for x in v) + "]"
        return "[\n" + ",\n".join(sp + " " + dump(x, ind + 1) for x in v) + "\n" + sp + "]"
    if isinstance(v, float):
        return repr(round(v, 3))
    return json.dumps(v)

def img(colour="0", image=""):
    return o(("version", 1), ("colour", colour), ("image", image))

def bg(colour="0", image=""):
    return o(("version", 1), ("focussed", img(colour, image)), ("unfocussed", img(colour, image)))

def act(on, handler, handle, extra=""):
    return o(("version", 1), ("onAction", on), ("handler", handler), ("handleName", handle), ("additionalData", extra))

def bounds(rect, focus="No", invalid="Show", when="Always", prop=False, inval=None):
    r = " ".join(("%g" % v) for v in rect)
    pairs = [("version", 2 if inval else 1), ("acceptsHWFocus", focus), ("showWhenDataModelInvalid", invalid),
             ("whenVisible", when), ("boundsType", "Proportional" if prop else "Absolute"), ("bounds", r)]
    if inval:
        pairs.append(("additionalInvalidatingHandles", inval))
    return o(*pairs)

def entry(name, typ, data, b, remap=()):
    return o(("version", 2),
             ("componentData", o(("version", 1), ("name", name), ("type", typ), ("data", data))),
             ("handle remapping", o(("version", 1), ("map", [o(("key", k), ("value", v)) for k, v in remap]))),
             ("bounds", b))

def component(children, actions=(), colour="0", image="", ignore=False):
    return o(("version", 2), ("actions", list(actions)), ("backgroundData", bg(colour, image)),
             ("ignoreMousePresses", ignore), ("disableCoarseDataWheel", False), ("componentsData", children))

def font(h, style="Bold"):
    return o(("version", 1), ("name", "Roboto"), ("style", style), ("height", float(h)))

def tstyle(h, style, colour, just):
    return o(("version", 1), ("font", font(h, style)), ("colour", colour), ("justification", just), ("case", "Original"))

def label(kind, h, style="Bold", colour=VALUE, just="left verticallyCentred", handle="Data"):
    return o(("version", 1), ("textStyle", tstyle(h, style, colour, just)), ("type", kind), ("handleName", handle))

FOCUS = o(("version", 1), ("backgroundColour", "24c0141c"), ("outlineColour", argb(RED)),
          ("backgroundInset", 0.0), ("outlineThickness", 4.0))

def focus_entry():
    return entry("focus", "Focus", FOCUS, bounds((0, 0, 1, 1), when="WhenFocussed", prop=True))

FOCUS_LOST = [act("Focus Lost", "Focus Lost", "Focus Lost")]

def knob_overlay(bipolar):
    return o(("version", 2), ("version", 1), ("actions", FOCUS_LOST), ("overlayType", "Knob With Header"),
             ("scaleType", "Linear"), ("polarityType", "Bipolar" if bipolar else "Unipolar"),
             ("showMinusSign", True), ("handleName", "Data"))

def overlay_component(data):
    return component([entry("overlay", "KnobOverlay", data, bounds((0, 0, 1280, 800)))], colour=argb(PANEL), ignore=True)

OVERLAY = {False: "cp knob overlay", True: "cp knob overlay bi"}

def knob_actions(bi):
    return [act("Mouse Down", "Q-Link", "Data"), act("Double Click", "Show Overlay", "Data", OVERLAY[bi]),
            act("Enter Pressed", "Show Overlay", "Data", OVERLAY[bi])]

# ---------------------------------------------------------------- artwork
def find_font(cands):
    for c in cands:
        if os.path.exists(c):
            return c
    return None

ROBOTO = find_font(["/usr/share/fonts/truetype/google-fonts/ofl/roboto/Roboto[wdth,wght].ttf",
                    "/usr/share/fonts/truetype/roboto/Roboto-Bold.ttf",
                    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"])
MONO = find_font(["/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
                  "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"]) or ROBOTO

def pil_font(size, weight=700, path=None):
    path = path or ROBOTO
    if not path:
        return ImageFont.load_default()
    f = ImageFont.truetype(path, size)
    try:
        f.set_variation_by_axes([weight, 100])
    except Exception:
        pass
    return f

SS = 4   # supersampling

def aa(w, h, fill=(0, 0, 0, 0)):
    im = Image.new("RGBA", (w * SS, h * SS), fill)
    return im, ImageDraw.Draw(im)

def done(im, w, h):
    return im.resize((w, h), Image.LANCZOS)

def vgrad(d, box, top, bot, radius=0):
    x0, y0, x1, y1 = box
    n = max(1, int(y1 - y0))
    for i in range(n):
        t = i / max(1, n - 1)
        c = tuple(int(top[k] + (bot[k] - top[k]) * t) for k in range(3))
        d.line([(x0, y0 + i), (x1, y0 + i)], fill=c + (255,))

def text_centre(im, text, cx, cy, size, colour=INK, weight=700, path=None):
    d = ImageDraw.Draw(im)
    f = pil_font(size, weight, path)
    d.text((cx, cy), text, font=f, fill=colour + (255,), anchor="mm")

def fit_size(text, w, size, weight=700):
    while size > 14 and pil_font(size, weight).getlength(text) > w:
        size -= 1
    return size

def arc_pts(cx, cy, r, a0, a1, n=64):
    return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n - 90)),
             cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n - 90))) for i in range(n + 1)]

def thick(d, pts, width, colour):
    d.line(pts, fill=colour, width=int(width), joint="curve")
    r = width / 2
    for x, y in (pts[0], pts[-1]):
        d.ellipse((x - r, y - r, x + r, y + r), fill=colour)

def knob_frame(size, frac, bi):
    """S950-panel knob: dark cap, white pointer, red value arc round a light track."""
    im, d = aa(size, size)
    s = SS
    st = 7 * s
    c = size * s / 2
    r = c - st / 2 - s
    thick(d, arc_pts(c, c, r, -135, 135), st, TRACK + (255,))
    ang = -135 + 270 * frac
    a0, a1 = (0, ang) if bi else (-135, ang)
    if a0 > a1:
        a0, a1 = a1, a0
    if a1 - a0 > 0.5:
        thick(d, arc_pts(c, c, r, a0, a1), st, RED + (255,))
    ir = r - st - 3 * s
    d.ellipse((c - ir + 2 * s, c - ir + 3 * s, c + ir + 2 * s, c + ir + 3 * s), fill=(0, 0, 0, 60))   # shadow
    d.ellipse((c - ir, c - ir, c + ir, c + ir), fill=KNOB_CAP + (255,))
    d.ellipse((c - ir * 0.8, c - ir * 0.8, c + ir * 0.8, c + ir * 0.8), fill=(78, 78, 84, 255))
    t = math.radians(ang - 90)
    thick(d, [(c + ir * 0.15 * math.cos(t), c + ir * 0.15 * math.sin(t)),
              (c + ir * 0.85 * math.cos(t), c + ir * 0.85 * math.sin(t))], 5 * s, (248, 248, 248, 255))
    return done(im, size, size)

def film(frames):
    w, h = frames[0].size
    out = Image.new("RGBA", (w, h * len(frames)), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        out.paste(f, (0, i * h))
    return out

def keycap(w, h, text, on, led=True, picked=False):
    """An S950 key: light keycap, caption, red LED top-left (lit = on). picked: amber key (choice rows)."""
    im, d = aa(w, h)
    s = SS
    d.rounded_rectangle((0, 0, w * s - 1, h * s - 1), radius=8 * s, fill=KEY_EDGE + (255,))
    inner = Image.new("RGBA", (w * s - 6 * s, h * s - 8 * s))
    di = ImageDraw.Draw(inner)
    top, bot = (KEY_ON_TOP, KEY_ON_BOT) if picked else (KEY_TOP, KEY_BOT)
    vgrad(di, (0, 0, inner.width, inner.height), top, bot)
    mask = Image.new("L", inner.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, inner.width - 1, inner.height - 1), radius=6 * s, fill=255)
    im.paste(inner, (3 * s, 3 * s), mask)
    if led:
        lr = 7 * s
        lx, ly = 18 * s, 18 * s
        d.ellipse((lx - lr - s, ly - lr - s, lx + lr + s, ly + lr + s), fill=(40, 30, 30, 255))
        d.ellipse((lx - lr, ly - lr, lx + lr, ly + lr), fill=(LED_ON if on else LED_OFF) + (255,))
        if on:
            d.ellipse((lx - lr * 0.4, ly - lr * 0.6, lx + lr * 0.1, ly - lr * 0.1), fill=(255, 200, 200, 255))
    out = done(im, w, h)
    lines = text.split("\n")
    size = min(fit_size(l, w - (52 if led else 16), 30) for l in lines)
    cx = w / 2 + (12 if led else 0)
    for i, l in enumerate(lines):
        text_centre(out, l, cx, h / 2 + (i - (len(lines) - 1) / 2) * (size + 2), size, INK)
    return out

def lcd_frame(theme, w, h):
    bgc, ink, dim, faint = LCD[theme]
    im, d = aa(w, h)
    s = SS
    d.rounded_rectangle((0, 0, w * s - 1, h * s - 1), radius=10 * s, fill=BEZEL + (255,))
    d.rounded_rectangle((8 * s, 8 * s, (w - 8) * s - 1, (h - 8) * s - 1), radius=4 * s, fill=bgc + (255,))
    # faint dot-matrix rows, like the S950's character LCD
    for y in range(12, h - 12, 4):
        d.line([(10 * s, y * s), ((w - 10) * s, y * s)], fill=faint + (14,), width=s)
    # divider between the info lines and the playhead bar
    d.line([(20 * s, (h - 34) * s), ((w - 20) * s, (h - 34) * s)], fill=dim + (110,), width=s)
    return done(im, w, h)

def playhead_frames(theme, w, h, n=65):
    """Progress through the pattern: frame k fills k/64 of the bar, with a tick every bar (16 steps of 64)."""
    bgc, ink, dim, faint = LCD[theme]
    frames = []
    for k in range(n):
        im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        d.rectangle((0, 0, w - 1, h - 1), outline=dim + (255,), width=2)
        fill = int((w - 6) * k / (n - 1))
        if fill > 0:
            d.rectangle((3, 3, 3 + fill, h - 4), fill=ink + (255,))
        for b in range(1, 4):
            x = 3 + (w - 6) * b // 4
            d.line([(x, 0), (x, h - 1)], fill=(bgc if 3 + fill > x else dim) + (255,), width=2)
        frames.append(im)
    return frames

def box_image(w, h):
    im, d = aa(w, h)
    s = SS
    d.rounded_rectangle((0, 0, w * s - 1, h * s - 1), radius=6 * s, fill=LINE + (255,))
    d.rounded_rectangle((2 * s, 2 * s, (w - 2) * s - 1, (h - 2) * s - 1), radius=5 * s, fill=BOX + (255,))
    return done(im, w, h)

def slug(t):
    out = "".join(c.lower() if c.isalnum() else "_" for c in t)
    while "__" in out:
        out = out.replace("__", "_")
    return out.strip("_") or "x"

# ---------------------------------------------------------------- page description
# Cell kinds: knob, toggle (two-state key), trig (momentary key), pick (row of keys), read (readout box), step knob.
def K(pid, name, bi=False, span=1):
    return {"type": "knob", "param": pid, "name": name, "bi": bi, "span": span}

def T(pid, name, span=1):
    return {"type": "toggle", "param": pid, "name": name, "span": span}

def G(pid, name, span=1):
    return {"type": "trig", "param": pid, "name": name, "span": span}

def P(pid, name, labels, span=1):
    return {"type": "pick", "param": pid, "name": name, "labels": labels, "span": span}

def R(pid, name, span=1):
    return {"type": "read", "param": pid, "name": name, "span": span}

def steps(first):
    return [[K("step%d" % (first + r * 4 + c), "STEP %d" % (first + r * 4 + c)) for c in range(4)] for r in range(4)]

def pages():
    return [
        {"fn": 0, "sub": 0, "name": "SAMPLE", "rows": [
            [K("break", "BREAK"), K("browse", "BROWSE CONTENT/BREAKS", span=2), G("load", "LOAD")],
            [G("dicePattern", "PATTERN\nDICE"), K("patAmount", "PATTERN AMOUNT"), G("diceFx", "FX\nDICE"), K("fxAmount", "FX AMOUNT")],
            [G("undo", "UNDO"), G("redo", "REDO"), T("original", "ORIGINAL"), T("play", "PLAY\n(MPC STOPPED)")],
            [G("akaize", "AKAIZE TO\nMPC BPM"), G("demo", "DEMO\nBREAK"), R("infoMsg", "MESSAGE", span=2)]]},
        {"fn": 1, "sub": 0, "name": "SLICE", "rows": [
            [K("slice", "SLICE"), K("slPitch", "PITCH", bi=True), K("slGate", "GATE"), K("slStretch", "STRETCH")],
            [T("slReverse", "REVERSE"), K("slStutter", "STUTTER"), K("slTrans", "TRANSIENT"), K("slXfade", "XFADE")],
            [T("editAll", "EDIT\nALL SLICES"), T("slMute", "MUTE"), P("slBadge", "HIT", ["K", "S", "G", "H", "O", "X"], span=2)],
            [P("slicing", "SLICING", ["TRANS", "GRID"]), K("sens", "SENSITIVITY"), P("grid", "GRID", ["1/8", "1/16", "1/32"]),
             G("reslice", "RESLICE")]]},
        {"fn": 2, "sub": 0, "name": "SEQ 1-16", "rows": steps(1)},
        {"fn": 2, "sub": 1, "name": "SEQ 17-32", "rows": steps(17)},
        {"fn": 2, "sub": 2, "name": "SEQ 33-48", "rows": steps(33)},
        {"fn": 2, "sub": 3, "name": "SEQ 49-64", "rows": steps(49)},
        {"fn": 3, "sub": 0, "name": "PATTERN", "rows": [
            [P("length", "LENGTH", ["1 BAR", "2 BARS", "4 BARS"], span=2), K("style", "STYLE"), T("layers", "LAYERS")],
            [G("dicePattern", "PATTERN\nDICE"), K("patAmount", "PATTERN AMOUNT"), K("swing", "SWING"), G("clear", "CLEAR")],
            [K("memory", "MEMORY"), G("memSave", "SAVE"), G("memRecall", "RECALL"), G("undo", "UNDO")],
            [G("diceFx", "FX\nDICE"), K("fxAmount", "FX AMOUNT"), T("original", "ORIGINAL"), G("redo", "REDO")]]},
        {"fn": 4, "sub": 0, "name": "FX PADS", "rows": [
            [K("stutRate", "STUTTER RATE"), K("rollRate", "ROLL RATE"), K("tapeMs", "TAPE STOP"), K("akCycle", "AKAIZE CYCLE")],
            [K("akRatio", "AKAIZE STRETCH"), K("diveRes", "FILTER DIVE RES"), K("dropRate", "S950 DROP RATE"),
             K("stretchCycle", "SLICE STRETCH CYCLE")],
            [P("padQuant", "PAD QUANTISE", ["OFF", "1/16", "1/8"], span=2), K("fxAmount", "FX AMOUNT"), G("diceFx", "FX\nDICE")]]},
        {"fn": 5, "sub": 0, "name": "SOUND", "rows": [
            [K("character", "CHARACTER"), K("srate", "SAMPLE RATE"), K("volume", "VOLUME"), K("filter", "FILTER")],
            [P("filterType", "FILTER TYPE", ["LP", "HP"]), K("filterRes", "RESONANCE"), K("dist", "DISTORTION"), K("distTone", "DIST TONE", bi=True)],
            [K("crush", "CRUSH"), K("reverb", "REVERB"), K("echoMix", "ECHO"), K("echoFb", "ECHO FEEDBACK")]]},
        {"fn": 5, "sub": 1, "name": "SPACE", "rows": [
            [K("reverb", "REVERB"), K("revDecay", "DECAY"), K("revDamp", "DAMP"), K("revWidth", "WIDTH")],
            [K("revPre", "PRE-DELAY"), K("echoMix", "ECHO"), K("echoFb", "ECHO FEEDBACK"), K("echoTime", "ECHO TIME")],
            [T("echoMono", "ECHO MONO")]]},
        {"fn": 6, "sub": 0, "name": "SET", "rows": [
            [P("tempoSrc", "TEMPO SOURCE", ["AUTO", "/2", "X2"], span=2), P("speed", "SPEED", ["0.5X", "1X", "2X"], span=2)],
            [T("fit", "FIT TO\nMPC TEMPO"), P("fitMode", "FIT MODE", ["REPITCH", "KEEP"], span=2), P("trigger", "TRIGGER", ["SYNC", "MIDI"])],
            [P("breakMode", "BREAK SWITCH", ["RESTART", "BAR SYNC", "QUANT"], span=2), P("gateMode", "GATE", ["LATCH", "HOLD"]),
             T("velSlice", "VELOCITY\n> SLICE")],
            [P("padNotes", "PAD NOTES (PAD 1)", ["36 C1", "37 MPC", "48 C2"], span=2),
             P("lcdColour", "LCD", ["BLUE", "GREEN", "AMBER", "WHITE"], span=2)]]},
    ]

# ---------------------------------------------------------------- build
class Builder:
    def __init__(self, params, out):
        self.by_id = {p["id"]: p for p in params}
        self.out = out
        self.images = {}
        self.defs = []
        self.keys = set()
        self.replacements = {}

    def p(self, pid):
        if pid not in self.by_id:
            raise SystemExit("make_skin: unknown parameter id %r" % pid)
        return self.by_id[pid]

    def h(self, pid):
        return "Parameter %d" % self.p(pid)["index"]

    def discrete(self, pid):
        p = self.p(pid)
        if p["kind"] in ("bool", "choice"):
            self.replacements[p["index"]] = p["choices"] if p["kind"] == "choice" else ["OFF", "ON"]
        return p

    def named(self, text, pid):
        return "WithName[%s]/%s" % (text.replace("[", "(").replace("]", ")").replace("/", "-"), self.h(pid))

    def save(self, fname, image):
        if fname in self.images:
            old = self.images[fname]
            if old.size != image.size or old.tobytes() != image.convert(old.mode).tobytes():
                raise SystemExit("make_skin: two different images named %s" % fname)
            return fname
        image.save(os.path.join(self.out, fname), optimize=True)
        self.images[fname] = image
        return fname

    def define(self, key, comp):
        if key not in self.keys:
            self.keys.add(key)
            self.defs.append(o(("key", key), ("value", comp)))
        return key

    def caption(self, text, pid, rect, size=22):
        return entry("caption", "Label", label("Name", size, "Bold", LABEL, handle=self.named(text, pid)), bounds(rect))

    # -- the LCD across the top of every page
    def lcd(self):
        strip = "lcd_%dx%d.png" % (LCD_W, LCD_H)
        if strip not in self.images:
            self.save(strip, film([lcd_frame(t, LCD_W, LCD_H) for t in range(4)]))
        kids = [entry("lcd", "Image", o(("version", 2), ("imageType", "FilmStrip"), ("colour", "0"), ("image", strip),
                                         ("numFrames", 4), ("handleName", "Data")),
                      bounds((LCD_X, LCD_Y, LCD_W, LCD_H)), remap=[("Data", self.h("lcdColour"))])]
        lcdh = self.h("lcdColour")
        bar_w, bar_h = LCD_W - 40, 16
        for theme in range(4):
            vis = ["IndexedEnabling/%d/4/%s" % (theme, lcdh)]
            ink, dim = argb(LCD[theme][1]), argb(LCD[theme][2])
            x0, y0 = LCD_X + 22, LCD_Y + 12
            def lab(pid, rect, size, colour, just="left verticallyCentred", style="Bold"):
                return entry("lcd " + pid, "Label", label("Value", size, style, colour, just),
                             bounds(rect, inval=vis), remap=[("Data", self.h(pid))])
            kids += [lab("infoName", (x0, y0, 880, 44), 36, ink),
                     lab("infoBpm", (LCD_X + LCD_W - 330, y0, 308, 44), 36, ink, "right verticallyCentred"),
                     lab("infoStatus", (x0, y0 + 46, 900, 34), 26, ink, style="SemiBold"),
                     lab("infoSlice", (LCD_X + LCD_W - 330, y0 + 46, 308, 34), 26, ink, "right verticallyCentred")]
            ph = "playhead_%d.png" % theme
            if ph not in self.images:
                self.save(ph, film(playhead_frames(theme, bar_w, bar_h)))
            kids.append(entry("playhead", "Image", o(("version", 2), ("imageType", "FilmStrip"), ("colour", "0"), ("image", ph),
                                                     ("numFrames", 65), ("handleName", "Data")),
                              bounds((LCD_X + 20, LCD_Y + LCD_H - 28, bar_w, bar_h), inval=vis),
                              remap=[("Data", self.h("playhead"))]))
        return kids

    # -- background: panel, row plates, value windows
    def background(self, page):
        im = Image.new("RGB", (W, H), PANEL)
        d = ImageDraw.Draw(im)
        for y in range(H):
            t = y / (H - 1)
            d.line([(0, y), (W, y)], fill=tuple(int(PANEL[k] + (PANEL_LO[k] - PANEL[k]) * t) for k in range(3)))
        for r, row in enumerate(page["rows"]):
            y = row_y(r)
            d.rounded_rectangle((M, y, W - M - 1, y + ROW_H - 1), radius=8, outline=LINE, width=2)
            x = M
            for c in row:
                w = COL_W * c["span"]
                if x > M:
                    d.line([(x, y + 12), (x, y + ROW_H - 12)], fill=LINE, width=1)
                if c["type"] in ("knob", "read"):
                    bx, by, bw, bh = self.value_box(c, x, y)
                    im.paste(box_image(bw, bh).convert("RGB"), (bx, by))
                x += w
        return im

    def value_box(self, c, x, y):
        w = COL_W * c["span"]
        if c["type"] == "read":
            return x + CELL_PAD + 4, y + 38, w - 2 * CELL_PAD - 8, ROW_H - 46
        tx = x + CELL_PAD + KNOB + 10
        return tx, y + 44, x + w - CELL_PAD - 4 - tx, 56

    def tab(self, page):
        bgname = "bg_%d_%d.png" % (page["fn"], page["sub"])
        self.save(bgname, self.background(page))
        kids = [entry("background", "Image", o(("version", 2), ("imageType", "Regular"), ("colour", "0"), ("image", bgname)),
                      bounds((0, 0, W, H)))]
        kids += self.lcd()
        for r, row in enumerate(page["rows"]):
            x = M
            for c in row:
                kids += self.cell(c, x, row_y(r))
                x += COL_W * c["span"]
        root = component(kids, colour=argb(PANEL))
        qb = ["%d %d %d %d" % (M, row_y(r), W - 2 * M, ROW_H) for r in range(len(page["rows"]))]
        return o(("version", 1), ("tabName", page["name"]), ("fnKeyIndex", page["fn"]), ("fnKeySubIndex", page["sub"]),
                 ("componentDefinition", root), ("qlinkBoundsData", qb))

    def cell(self, c, x, y):
        t = c["type"]
        w = COL_W * c["span"]
        pid = c["param"]
        out = []
        if t == "knob":
            p = self.p(pid)
            if p["kind"] == "choice":
                self.discrete(pid)
            name = "knob%d%s.png" % (KNOB, "_bi" if c["bi"] else "")
            if name not in self.images:
                self.save(name, film([knob_frame(KNOB, i / (FRAMES - 1), c["bi"]) for i in range(FRAMES)]))
            bx, by, bw, bh = self.value_box(c, x, y)
            key = "cp knob %d%s" % (w, "_bi" if c["bi"] else "")
            kx, ky = CELL_PAD, (ROW_H - KNOB) // 2
            self.define(key, component([
                focus_entry(),
                entry("knob", "Knob", o(("version", 2), ("knobType", "FilmStrip"), ("filmStrip", name), ("numFrames", FRAMES),
                                        ("invert", False), ("handleName", "Data")), bounds((kx, ky, KNOB, KNOB))),
                entry("value", "Label", label("Value", 32, "Bold", VALUE, "horizontallyCentred verticallyCentred"),
                      bounds((bx - x, by - y, bw, bh)))], knob_actions(c["bi"])))
            out.append(self.caption(c["name"], pid, (bx, y + 8, x + w - bx - CELL_PAD, 32)))
            out.append(entry(pid, key, o(("version", 1), ("handleName", "Data")), bounds((x, y, w, ROW_H), focus="Yes", invalid="Hide"),
                             remap=[("Data", self.h(pid))]))
        elif t in ("toggle", "trig"):
            kw, kh = w - 2 * CELL_PAD - 4, ROW_H - 16
            fname = "key_%s_%d.png" % (slug(c["name"]), kw)
            if fname not in self.images:
                self.save(fname, film([keycap(kw, kh, c["name"], False), keycap(kw, kh, c["name"], True)]))
            key = "cp key " + fname
            self.define(key, component([
                entry("state", "Image", o(("version", 2), ("imageType", "FilmStrip"), ("colour", "0"), ("image", fname),
                                          ("numFrames", 2), ("handleName", "Data")), bounds((0, 0, kw, kh))),
                focus_entry()], [act("Mouse Down", "Toggle Switch", "Data"), act("Mouse Down", "Q-Link", "Data")]))
            out.append(entry(pid, key, o(("version", 1), ("handleName", "Data")),
                             bounds((x + CELL_PAD + 2, y + 8, kw, kh), focus="Yes", invalid="Hide"), remap=[("Data", self.h(pid))]))
        elif t == "pick":
            p = self.discrete(pid)
            labels = c["labels"]
            if len(labels) != len(p["choices"]):
                raise SystemExit("make_skin: %s has %d choices, the page lists %d" % (pid, len(p["choices"]), len(labels)))
            n = len(labels)
            gx, gy = x + CELL_PAD, y + 40
            gw, gh = w - 2 * CELL_PAD, ROW_H - 48
            bw = (gw - 2 * FM + 3 * (n - 1)) // n
            out.append(self.caption(c["name"], pid, (x + CELL_PAD + 4, y + 6, w - 2 * CELL_PAD, 30)))
            key = "cp pick " + pid
            kids = [focus_entry()]
            for i, lab in enumerate(labels):
                on = self.save("pick_%d_%s_on.png" % (bw, slug(lab)), keycap(bw, gh - 2 * FM, lab, True, led=False, picked=True))
                off = self.save("pick_%d_%s_off.png" % (bw, slug(lab)), keycap(bw, gh - 2 * FM, lab, False, led=False))
                kids.append(entry("choice %d" % i, "Button",
                                  o(("version", 2), ("onImage", on), ("offImage", off), ("buttonId", i), ("numButtonsInGroup", n),
                                    ("handleName", "Data"), ("gestureBehaviour", "Instant")),
                                  bounds((FM + i * (bw - 3), FM, bw, gh - 2 * FM))))
            self.define(key, component(kids, [act("Mouse Down", "Q-Link", "Data")]))
            out.append(entry(pid, key, o(("version", 1), ("handleName", "Data")),
                             bounds((gx, gy, gw, gh), focus="Yes", invalid="Hide"), remap=[("Data", self.h(pid))]))
        elif t == "read":
            bx, by, bw, bh = self.value_box(c, x, y)
            out.append(self.caption(c["name"], pid, (bx, y + 6, bw, 30)))
            out.append(entry("read " + pid, "Label", label("Value", 30, "Bold", VALUE, "horizontallyCentred verticallyCentred"),
                             bounds((bx + 8, by, bw - 16, bh)), remap=[("Data", self.h(pid))]))
        return out

    def qmap(self, page):
        q = {}
        for r, row in enumerate(page["rows"]):
            col = 0
            for c in row:
                if c["type"] == "read":
                    continue
                if col > 3:
                    raise SystemExit("page %s row %d has more than four Q-Link controls" % (page["name"], r))
                q[13 + col - 4 * r] = self.p(c["param"])["index"]
                col += 1
        return o(("Tab", page["fn"] + 1), ("SubTab", page["sub"] + 1), ("Bank Direction", "RowTopDown"),
                 ("Q-Links", o(*[("Q-Link %d" % i, q.get(i, -1)) for i in range(1, 17)])))

    def build(self):
        os.makedirs(self.out, exist_ok=True)
        for f in os.listdir(self.out):
            if f.endswith(".png") or f.endswith(".json"):
                os.remove(os.path.join(self.out, f))
        for bi in (False, True):
            self.define(OVERLAY[bi], overlay_component(knob_overlay(bi)))
        self.discrete("lcdColour")
        tabs, qmaps = [], []
        for page in pages():
            for row in page["rows"]:
                if sum(c["span"] for c in row) > 4:
                    raise SystemExit("page %s: a row wider than four columns" % page["name"])
            tabs.append(self.tab(page))
            qmaps.append(self.qmap(page))
        repl = [o(("key", "Parameter %d" % i), ("value", ch)) for i, ch in sorted(self.replacements.items())]
        tui = o(("pageData", o(("version", 1),
                               ("componentDefinitions", o(("version", 2), ("importFiles", []), ("localComponentDefinitions", self.defs))),
                               ("replacementStrings", repl),
                               ("info", o(("version", 1), ("type", "CompleteDescription"))),
                               ("tabs", tabs))))
        with open(os.path.join(self.out, "TUI.json"), "w") as f:
            f.write(dump(tui) + "\n")
        # Program mode (no screen page open): the sound and the dice amounts
        prog = [self.p(i)["index"] for i in ("filter", "filterRes", "dist", "crush", "srate", "reverb", "echoMix", "volume",
                                              "patAmount", "fxAmount", "swing", "style", "stutRate", "rollRate", "tapeMs", "akCycle")]
        ql = o(("version", 4), ("info", o(("version", 1), ("type", "CompleteDescription"))),
               ("Screen Mode Q-Links", o(("version", 4), ("map", qmaps))),
               ("Program Mode Q-Links", o(*[("Q-Link %d" % (i + 1), prog[i]) for i in range(16)])))
        with open(os.path.join(self.out, "Q-Links.json"), "w") as f:
            f.write(dump(ql) + "\n")
        return len(tabs)

# ---------------------------------------------------------------- approximate preview (layout check, not the MPC)
def preview(builder, outdir, theme=0):
    os.makedirs(outdir, exist_ok=True)
    demo = {"infoName": "BREAK 1/8  AMEN BROTHER 170", "infoBpm": "170.0 BPM", "infoStatus": "27 SLICES  2 BARS  MPC 170.0  PLAYING",
            "infoSlice": "S05 KICK", "infoMsg": "AKAIZED TO 170.0 BPM"}
    vals = {"break": "1 C1", "browse": "170  AMEN BROTHER", "slice": "S05", "memory": "M01"}
    shots = []
    for page in pages():
        im = Image.open(os.path.join(builder.out, "bg_%d_%d.png" % (page["fn"], page["sub"]))).convert("RGBA")
        lcd = Image.open(os.path.join(builder.out, "lcd_%dx%d.png" % (LCD_W, LCD_H)))
        im.alpha_composite(lcd.crop((0, theme * LCD_H, LCD_W, (theme + 1) * LCD_H)), (LCD_X, LCD_Y))
        d = ImageDraw.Draw(im)
        ink, dim = LCD[theme][1], LCD[theme][2]
        d.text((LCD_X + 22, LCD_Y + 34), demo["infoName"], font=pil_font(36), fill=ink, anchor="lm")
        d.text((LCD_X + LCD_W - 22, LCD_Y + 34), demo["infoBpm"], font=pil_font(36), fill=ink, anchor="rm")
        d.text((LCD_X + 22, LCD_Y + 75), demo["infoStatus"], font=pil_font(26, 600), fill=ink, anchor="lm")
        d.text((LCD_X + LCD_W - 22, LCD_Y + 75), demo["infoSlice"], font=pil_font(26), fill=ink, anchor="rm")
        ph = Image.open(os.path.join(builder.out, "playhead_%d.png" % theme))
        fh = 16
        im.alpha_composite(ph.crop((0, 20 * fh, ph.width, 21 * fh)), (LCD_X + 20, LCD_Y + LCD_H - 28))
        for r, row in enumerate(page["rows"]):
            x, y = M, row_y(r)
            for c in row:
                w = COL_W * c["span"]
                p = builder.p(c["param"])
                if c["type"] == "knob":
                    k = Image.open(os.path.join(builder.out, "knob%d%s.png" % (KNOB, "_bi" if c["bi"] else "")))
                    fr = 80
                    im.alpha_composite(k.crop((0, fr * KNOB, KNOB, (fr + 1) * KNOB)), (x + CELL_PAD, y + (ROW_H - KNOB) // 2))
                    bx, by, bw, bh = builder.value_box(c, x, y)
                    d.text((bx, y + 24), c["name"], font=pil_font(22), fill=(42, 42, 42), anchor="lm")
                    v = vals.get(c["param"]) or (p["choices"][int(p["def"])] if p["choices"] else
                                                 ("05 K" if c["param"].startswith("step") else "%g" % p["def"]))
                    d.text((bx + bw / 2, by + bh / 2), v, font=pil_font(32), fill=INK, anchor="mm")
                elif c["type"] in ("toggle", "trig"):
                    k = Image.open(os.path.join(builder.out, "key_%s_%d.png" % (slug(c["name"]), w - 2 * CELL_PAD - 4)))
                    kh = k.height // 2
                    on = c["param"] in ("play", "layers", "fit", "echoMono")
                    im.alpha_composite(k.crop((0, kh if on else 0, k.width, kh * (2 if on else 1))), (x + CELL_PAD + 2, y + 8))
                elif c["type"] == "pick":
                    d.text((x + CELL_PAD + 4, y + 21), c["name"], font=pil_font(22), fill=(42, 42, 42), anchor="lm")
                    n = len(c["labels"])
                    gw, gh = w - 2 * CELL_PAD, ROW_H - 48
                    bw = (gw - 2 * FM + 3 * (n - 1)) // n
                    sel = int(p["def"])
                    for i, lab in enumerate(c["labels"]):
                        f = "pick_%d_%s_%s.png" % (bw, slug(lab), "on" if i == sel else "off")
                        im.alpha_composite(Image.open(os.path.join(builder.out, f)), (x + CELL_PAD + FM + i * (bw - 3), y + 40 + FM))
                elif c["type"] == "read":
                    bx, by, bw, bh = builder.value_box(c, x, y)
                    d.text((bx, y + 21), c["name"], font=pil_font(22), fill=(42, 42, 42), anchor="lm")
                    d.text((bx + bw / 2, by + bh / 2), demo.get(c["param"], ""), font=pil_font(30), fill=INK, anchor="mm")
                x += w
        # the MPC's own function-key strip under the tab (approximate)
        full = Image.new("RGBA", (1280, 800), (24, 24, 26, 255))
        full.alpha_composite(im, (0, 110))
        fd = ImageDraw.Draw(full)
        names = ["SAMPLE", "SLICE", "SEQ", "PATTERN", "FX PADS", "SOUND", "SET"]
        for i, n in enumerate(names):
            fx = 12 + i * 180
            fd.rounded_rectangle((fx, 748, fx + 168, 792), radius=6, fill=(220, 30, 40) if i == page["fn"] else (60, 60, 64))
            fd.text((fx + 84, 770), n, font=pil_font(22), fill=(255, 255, 255), anchor="mm")
        fd.text((24, 55), "CHOPPAGE  ·  " + page["name"], font=pil_font(30), fill=(235, 235, 235), anchor="lm")
        path = os.path.join(outdir, "page_%d_%d.png" % (page["fn"], page["sub"]))
        full.convert("RGB").save(path)
        shots.append(path)
    return shots

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--params", default=os.path.join(HERE, "params.json"))
    ap.add_argument("--out", default=os.path.join(MPC, "skins"))
    ap.add_argument("--preview")
    ap.add_argument("--preview-theme", type=int, default=0)
    a = ap.parse_args()
    params = json.load(open(a.params))
    b = Builder(params, a.out)
    n = b.build()
    print("make_skin: %d tabs, %d images, %d components -> %s" % (n, len(b.images), len(b.defs), a.out))
    if a.preview:
        for p in preview(b, a.preview, a.preview_theme):
            print("  preview", p)

if __name__ == "__main__":
    main()
