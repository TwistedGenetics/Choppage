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
    """A file-name-safe name for a caption; a short hash keeps "NUDGE <" and "NUDGE >" apart."""
    import hashlib
    out = "".join(c.lower() if c.isalnum() else "_" for c in t)
    while "__" in out:
        out = out.replace("__", "_")
    out = out.strip("_") or "x"
    if out.replace("_", "") != "".join(c.lower() for c in t if c.isalnum()) or not t.replace(" ", "").replace("\n", "").isalnum():
        out += "_" + hashlib.md5(t.encode()).hexdigest()[:5]
    return out


# ---------------------------------------------------------------- main-screen art (desktop BR3AK arrangement)
WCOLS, WLEVELS = 84, 12          # MpcParams.h kWaveCols / kWaveLevels
WCOL_W, WAVE_H, TAB_H = 14, 136, 26
WX0 = 640 - WCOLS * WCOL_W // 2  # 52
DRAG_THUMB = 96                  # the drag strip's thumb (its centre is the marker). The strip runs from
                                 # WX0 - DRAG_THUMB / 2 (= 4) to 1276: it must stay on the 1280-wide screen,
                                 # a component outside it leaves the MPC's plugin screen blank (0.6.1 - 0.7.2)
TAB_FRAMES = 15
SEL_YELLOW = (255, 214, 64)
PAD_TOP, PAD_BOT, PAD_EDGE = (124, 146, 178), (88, 108, 138), (52, 64, 84)
PAD_ON_TOP, PAD_ON_BOT = (255, 196, 92), (232, 140, 40)
BADGES = "KSGHOP"

def mix(a, b, t):
    return tuple(int(a[k] + (b[k] - a[k]) * t) for k in range(3))

def mono_font(size):
    return ImageFont.truetype(MONO, size) if MONO else pil_font(size)

def lcd_art(theme, w, h, boxes=(), rules=()):
    """An LCD panel: black bezel, the theme's glass, faint dot rows, outlined boxes and divider rules (tab coords
    relative to the panel)."""
    bgc, ink, dim, faint = LCD[theme]
    im, d = aa(w, h)
    s = SS
    d.rounded_rectangle((0, 0, w * s - 1, h * s - 1), radius=10 * s, fill=BEZEL + (255,))
    d.rounded_rectangle((8 * s, 8 * s, (w - 8) * s - 1, (h - 8) * s - 1), radius=4 * s, fill=bgc + (255,))
    for y in range(12, h - 12, 4):
        d.line([(10 * s, y * s), ((w - 10) * s, y * s)], fill=faint + (14,), width=s)
    for (x, y, bw, bh) in boxes:
        d.rectangle((x * s, y * s, (x + bw) * s - 1, (y + bh) * s - 1), outline=ink + (255,), width=2 * s)
    for (x0, y0, x1) in rules:
        d.line([(x0 * s, y0 * s), (x1 * s, y0 * s)], fill=dim + (160,), width=2 * s)
    return done(im, w, h)

def lcd_key(theme, w, h, text, on, size=28):
    """A key drawn on the LCD, like the desktop's: outlined; lit = filled with ink, text in glass colour."""
    bgc, ink, dim, faint = LCD[theme]
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    if on:
        d.rectangle((0, 0, w - 1, h - 1), fill=ink + (255,))
    else:
        d.rectangle((0, 0, w - 1, h - 1), fill=bgc + (255,), outline=ink + (255,), width=2)
    while size > 14 and mono_font(size).getlength(text) > w - 10:
        size -= 1
    d.text((w / 2, h / 2 + 1), text, font=mono_font(size), fill=(bgc if on else ink) + (255,), anchor="mm")
    return im

def wave_frames():
    """((theme * 3 + look) * 2 + mark) * WLEVELS + level: the desktop's white trace on the LCD glass.
    look 1 = the selected slice (yellow edges, like the desktop's yellow box), 2 = the slice playing now."""
    import random
    rnd = random.Random(950)
    w, h = WCOL_W * SS, WAVE_H * SS
    grain = [rnd.uniform(0.35, 1.0) for _ in range(WCOL_W)]
    frames = []
    for theme in range(4):
        bgc, ink, dim, faint = LCD[theme]
        for look in range(3):
            for mark in range(2):
                for level in range(WLEVELS):
                    base = bgc if look == 0 else mix(bgc, SEL_YELLOW, 0.16) if look == 1 else mix(bgc, ink, 0.22)
                    im = Image.new("RGB", (w, h), base)
                    d = ImageDraw.Draw(im)
                    d.line([(0, h // 2), (w, h // 2)], fill=mix(base, ink, 0.35), width=SS)
                    hh = (h / 2 - 5 * SS) * level / (WLEVELS - 1)
                    if level > 0:
                        for x, g in enumerate(grain):
                            a = max(SS, hh * g)
                            d.line([(x * SS + SS // 2, h / 2 - a), (x * SS + SS // 2, h / 2 + a)], fill=ink, width=SS)
                    if look == 1:
                        d.rectangle((0, 0, w, 3 * SS), fill=SEL_YELLOW)
                        d.rectangle((0, h - 3 * SS, w, h), fill=SEL_YELLOW)
                    if mark:
                        d.rectangle((0, 0, 2 * SS - 1, h), fill=SEL_YELLOW if look == 1 else ink)
                    frames.append(im.resize((WCOL_W, WAVE_H), Image.LANCZOS))
    return frames

def tab_frames():
    """theme * 15 + t: 0 none, 1-6 a slice starts here (K S G H O X), 7-12 the same selected, 13 inside a slice,
    14 inside the selected slice. Together they draw the desktop's row of hit boxes under the waveform."""
    frames = []
    for theme in range(4):
        bgc, ink, dim, faint = LCD[theme]
        for t in range(TAB_FRAMES):
            im = Image.new("RGB", (WCOL_W * SS, TAB_H * SS), bgc)
            d = ImageDraw.Draw(im)
            W_, H_ = WCOL_W * SS, TAB_H * SS
            sel = t in range(7, 13) or t == 14
            if t > 0:
                if sel:
                    d.rectangle((0, 0, W_, H_ - 1), fill=ink)
                else:
                    d.line([(0, SS), (W_, SS)], fill=ink, width=2 * SS)
                    d.line([(0, H_ - SS - 1), (W_, H_ - SS - 1)], fill=ink, width=2 * SS)
                if t <= 12:
                    d.rectangle((0, 0, 2 * SS - 1, H_), fill=ink if not sel else bgc)
                    letter = BADGES[(t - 1) % 6]
                    d.text((W_ / 2 + SS, H_ / 2 + SS), letter, font=mono_font(20 * SS), fill=bgc if sel else ink, anchor="mm")
            frames.append(im.resize((WCOL_W, TAB_H), Image.LANCZOS))
    return frames

def clear_png(w, h):
    return Image.new("RGBA", (w, h), (0, 0, 0, 0))

def pad_key(w, h, name, on, win, hold_text=None):
    """An FX pad as on the desktop: slate key, white name, a window for the setting (transparent: the setting label
    under the pad shows through). on = held (amber)."""
    im, d = aa(w, h)
    s = SS
    d.rounded_rectangle((0, 0, w * s - 1, h * s - 1), radius=6 * s, fill=PAD_EDGE + (255,))
    inner = Image.new("RGBA", ((w - 4) * s, (h - 6) * s))
    vgrad(ImageDraw.Draw(inner), (0, 0, inner.width, inner.height), PAD_ON_TOP if on else PAD_TOP, PAD_ON_BOT if on else PAD_BOT)
    mask = Image.new("L", inner.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, inner.width - 1, inner.height - 1), radius=5 * s, fill=255)
    im.paste(inner, (2 * s, 2 * s), mask)
    out = done(im, w, h)
    size = fit_size(name, w - 12, 24)
    text_centre(out, name, w / 2, 22, size, (255, 255, 255) if not on else (40, 24, 6))
    wx, wy, ww, wh = win
    dd = ImageDraw.Draw(out)
    dd.rectangle((wx - 2, wy - 2, wx + ww + 1, wy + wh + 1), fill=(20, 24, 32, 255))
    if hold_text:
        dd.rectangle((wx, wy, wx + ww - 1, wy + wh - 1), fill=LCD[0][0] + (255,))
        dd.text((wx + ww / 2, wy + wh / 2 + 1), hold_text, font=mono_font(22), fill=LCD[0][1] + (255,), anchor="mm")
    else:
        dd.rectangle((wx, wy, wx + ww - 1, wy + wh - 1), fill=(0, 0, 0, 0))
    return out

def dice_icon(d, x, y, size, pip=(255, 255, 255)):
    d.rounded_rectangle((x, y, x + size, y + size), radius=size // 6, fill=(26, 26, 26, 255))
    r = size / 11
    for px, py in ((0.27, 0.27), (0.73, 0.27), (0.5, 0.5), (0.27, 0.73), (0.73, 0.73)):
        cx, cy = x + px * size, y + py * size
        d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=pip + (255,))

def dice_key(w, h, on):
    k = keycap(w, h, "", on, led=False, picked=on)
    d = ImageDraw.Draw(k)
    sz = min(w, h) - 22
    dice_icon(d, (w - sz) / 2, (h - sz) / 2, sz)
    return k

def bar_key(w, h, text, on):
    """The PATTERN / FX bar: tap to open its section under the LCD (amber while open), again to go back."""
    k = keycap(w, h, "", on, led=False, picked=on)
    d = ImageDraw.Draw(k)
    d.text((24, h / 2), text, font=pil_font(34), fill=INK + (255,), anchor="lm")
    hint = "BACK TO MAIN" if on else "OPEN"
    d.text((w - 22, h / 2), hint, font=pil_font(20), fill=(60, 60, 60, 255), anchor="rm")
    # the desktop's LED dash
    lx = w - 22 - pil_font(20).getlength(hint) - 46
    d.rounded_rectangle((lx, h / 2 - 4, lx + 30, h / 2 + 4), radius=2, fill=(LED_ON if on else (60, 60, 60)) + (255,))
    return k


# ---------------------------------------------------------------- the BR3AK SEQ grid
CELL_FRAMES = 66                 # MpcParams.h kCellFrames (strip = 1 spare + 4 themes x 66 + 1 spare)
FXROW_FRAMES = 34                # MpcParams.h kFxRowFrames
LANES = ["KICK", "SNARE", "GHOST", "HAT", "OPEN", "PERC"]
GX0, GPITCH, GCELL = 140, 69, 63 # grid columns: x of column 0, pitch, cell width
FX_LETTER = "SRAT"               # STUTTER REVERSE STRETCH (Akaize) TAPE STOP, as on the desktop

def cell_frames(w, h):
    """1 spare + theme x (0 empty, 1 empty on a beat, 2-65 a hit: slice number) + 1 spare. The spares let the jog
    wheel move a cell's value either way from any frame."""
    frames = []
    for theme in range(4):
        bgc, ink, dim, faint = LCD[theme]
        for c in range(CELL_FRAMES):
            im = Image.new("RGB", (w, h), bgc)
            d = ImageDraw.Draw(im)
            if c <= 1:
                d.rectangle((1, 1, w - 2, h - 2), outline=dim if c == 1 else mix(bgc, dim, 0.55), width=2)
            else:
                d.rectangle((0, 0, w - 1, h - 1), fill=ink)
                t = str(c - 1)
                size = min(int(h * 0.78), 28)
                while size > 10 and mono_font(size).getlength(t) > w - 6:
                    size -= 1
                d.text((w / 2, h / 2 + 1), t, font=mono_font(size), fill=bgc, anchor="mm")
            frames.append(im)
    return [frames[0]] + frames + [frames[-1]]

def fxrow_frames(w, h):
    """theme x (selected ? 17 : 0) + f; f 0 none, 1-16 FX type (S R A T) x rate (8 16 32 64)."""
    frames = []
    for theme in range(4):
        bgc, ink, dim, faint = LCD[theme]
        for sel in range(2):
            for f in range(17):
                im = Image.new("RGB", (w, h), bgc)
                d = ImageDraw.Draw(im)
                if sel:
                    d.rectangle((0, 0, w - 1, h - 1), outline=SEL_YELLOW, width=3)
                if f:
                    k = f - 1
                    t = FX_LETTER[k // 4] + str((8, 16, 32, 64)[k % 4])
                    d.rectangle((3, 3, w - 4, h - 4), fill=mix(bgc, ink, 0.25))
                    size = min(int(h * 0.75), 22)
                    while size > 10 and mono_font(size).getlength(t) > w - 8:
                        size -= 1
                    d.text((w / 2, h / 2 + 1), t, font=mono_font(size), fill=ink, anchor="mm")
                else:
                    d.line([(8, h / 2), (w - 8, h / 2)], fill=mix(bgc, dim, 0.6), width=2)
                frames.append(im)
    return frames

def head_frames(h):
    """The step numbers over the grid, the step playing now inverted: theme * 17 + (0 none, 1-16)."""
    w = 16 * GPITCH
    frames = []
    for theme in range(4):
        bgc, ink, dim, faint = LCD[theme]
        for now in range(17):
            im = Image.new("RGB", (w, h), bgc)
            d = ImageDraw.Draw(im)
            for k in range(16):
                x = k * GPITCH
                lit = now == k + 1
                if lit:
                    d.rectangle((x, 0, x + GCELL - 1, h - 1), fill=ink)
                t = str(k + 1) if k % 4 == 0 or lit else "·"
                d.text((x + GCELL / 2, h / 2 + 1), t, font=mono_font(min(22, h - 4)), fill=bgc if lit else ink, anchor="mm")
            frames.append(im)
    return frames

def grid_cover(theme, w, h, lane_y, lane_h):
    """The LCD under the grid: glass + lane names."""
    bgc, ink, dim, faint = LCD[theme]
    im = Image.new("RGB", (w, h), bgc)
    d = ImageDraw.Draw(im)
    for y in range(0, h, 4):
        d.line([(0, y), (w, y)], fill=mix(bgc, faint, 0.06))
    for i, n in enumerate(LANES + ["FX"]):
        y = lane_y + i * lane_h + (lane_h - 6) / 2
        d.text((8, y), n, font=mono_font(min(24, lane_h - 4)), fill=ink, anchor="lm")
    return im

def dice_bar(w, h, text, on):
    """The SEQ page's dice: the whole bar rolls (the desktop's PATTERN / FX bars)."""
    k = keycap(w, h, "", on, led=False, picked=on)
    d = ImageDraw.Draw(k)
    sz = h - 22
    dice_icon(d, 14, 11, sz)
    d.text((14 + sz + 18, h / 2), text, font=pil_font(34), fill=INK + (255,), anchor="lm")
    return k

# ---------------------------------------------------------------- page description
# Cell kinds: knob, toggle (two-state key), trig (momentary key), pick (row of keys), read (readout box), step knob.
def K(pid, name, bi=False, span=1):
    return {"type": "knob", "param": pid, "name": name, "bi": bi, "span": span}

def T(pid, name, span=1):
    return {"type": "toggle", "param": pid, "name": name, "span": span}

def G(pid, name, span=1):
    return {"type": "trig", "param": pid, "name": name, "span": span}

def P(pid, name, labels, span=1, ids=None):
    """ids: the choice each button sets, when the buttons are not in the parameter's order"""
    return {"type": "pick", "param": pid, "name": name, "labels": labels, "span": span, "ids": ids}

def R(pid, name, span=1):
    return {"type": "read", "param": pid, "name": name, "span": span}

def steps(first):
    return [[K("step%d" % (first + r * 4 + c), "STEP %d" % (first + r * 4 + c)) for c in range(4)] for r in range(4)]

def pages():
    return [
        {"fn": 3, "sub": 0, "name": "PATTERN", "rows": [
            [P("length", "LENGTH (BARS)", ["1", "2", "4"]), K("style", "STYLE"), T("layers", "LAYERS"), K("swing", "SWING")],
            [G("dicePattern", "PATTERN\nDICE"), K("patAmount", "PATTERN AMOUNT"), G("clear", "CLEAR"), G("resetOrig", "RESET TO\nORIGINAL")],
            [K("memory", "MEMORY"), G("memSave", "SAVE\nPATTERN"), G("memSaveOrig", "SAVE\nORIGINAL"), G("memRecall", "RECALL")],
            [G("diceFx", "FX\nDICE"), T("original", "ORIGINAL"), G("undo", "UNDO"), G("redo", "REDO")]]},
        {"fn": 4, "sub": 0, "name": "FX PADS", "rows": [
            [K("stutRate", "STUTTER RATE"), K("rollRate", "ROLL RATE"), K("tapeMs", "TAPE STOP"), K("diveRes", "FILTER DIVE RES")],
            [K("akCycLen", "AKAIZE PAD LENGTH"), K("akCycFreq", "AKAIZE PAD FREQ"), T("akFreqManual", "PAD FREQ\nMANUAL"), K("akRatio", "AKAIZE PAD STRETCH")],
            [K("slCycLen", "SLICE STRETCH LENGTH"), K("slCycFreq", "SLICE STRETCH FREQ"), T("slFreqManual", "SLICE FREQ\nMANUAL"), K("dropRate", "S950 DROP RATE")],
            [P("padQuant", "FX PAD QUANTISE (START AND STOP ON THE GRID)", ["OFF", "1/32", "1/16", "1/8", "1/4", "BAR"], span=2,
               ids=[0, 4, 1, 2, 3, 5]), K("fxAmount", "FX AMOUNT"), G("diceFx", "FX\nDICE")]]},
        {"fn": 4, "sub": 1, "name": "AKAIZER", "rows": [
            [T("akrSync", "SYNC TO\nMPC BPM"), K("akrLoopBpm", "LOOP BPM (0 IS AUTO)"), K("akrTarget", "TARGET (NO MPC TEMPO)"), K("akrStretch", "STRETCH (SYNC OFF)")],
            [K("akrCycLen", "CYCLE LENGTH"), K("akrCycFreq", "CYCLE FREQ"), T("akrFreqManual", "FREQ\nMANUAL"), K("akrPitch", "PITCH", bi=True)],
            [G("akaize", "AKAIZE\nTHE BREAK"), R("infoBpm", "BREAK TEMPO"), R("infoMsg", "MESSAGE", span=2)]]},
        {"fn": 5, "sub": 0, "name": "SOUND", "rows": [
            [K("character", "CHARACTER"), K("srate", "SAMPLE RATE"), K("volume", "VOLUME"), K("filter", "FILTER")],
            [P("filterType", "FILTER TYPE", ["LP", "HP"]), K("filterRes", "RESONANCE"), K("dist", "DISTORTION"), K("distTone", "DIST TONE", bi=True)],
            [K("crush", "CRUSH"), K("reverb", "REVERB"), K("echoMix", "ECHO"), K("echoFb", "ECHO FEEDBACK")]]},
        {"fn": 5, "sub": 1, "name": "SPACE", "rows": [
            [K("reverb", "REVERB"), K("revDecay", "DECAY"), K("revDamp", "DAMP"), K("revWidth", "WIDTH")],
            [K("revPre", "PRE-DELAY"), K("echoMix", "ECHO"), K("echoFb", "ECHO FEEDBACK"), K("echoTime", "ECHO TIME")],
            [T("echoMono", "ECHO MONO")]]},
        {"fn": 6, "sub": 0, "name": "PLAY", "rows": [
            [P("gateMode", "GATE (HOW LONG A BREAK PLAYS)", ["LATCH", "HOLD", "ONE SHOT"], span=4)],
            [P("muteGroup", "MUTE GROUP OF THIS BREAK (SAME GROUP CUTS, OTHERS PLAY TOGETHER)", ["1", "2", "3", "4"], span=4)],
            [P("breakMode", "BREAK SWITCH (IN A GROUP)", ["RESTART", "BAR SYNC", "QUANT"], span=3), T("velSlice", "VELOCITY\n> SLICE")],
            [P("trigger", "TRIGGER", ["SYNC", "MIDI"], span=2), P("padNotes", "PAD NOTES (PAD 1)", ["36 C1", "37 MPC", "48 C2"], span=2)]]},
        {"fn": 6, "sub": 1, "name": "SET", "rows": [
            [P("tempoSrc", "TEMPO SOURCE", ["AUTO", "/2", "X2"], span=2), P("speed", "SPEED", ["0.5X", "1X", "2X"], span=2)],
            [T("fit", "FIT TO\nMPC TEMPO"), P("fitMode", "FIT MODE", ["REPITCH", "KEEP"], span=2)],
            [P("lcdColour", "LCD", ["BLUE", "GREEN", "AMBER", "WHITE"], span=4)],
            [P("slicing", "SLICING", ["TRANS", "GRID"]), K("sens", "SENSITIVITY"), P("grid", "GRID", ["1/8", "1/16", "1/32"]),
             G("reslice", "RESLICE")]]},
    ]

# ---------------------------------------------------------------- build
TAP_PARAMS = ("waveTap", "listTap", "seqTap")   # tap areas: the MPC resets them when the finger lifts

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
            out.append(self.flip_at(pid, (x + CELL_PAD + 2, y + 8, kw, kh), fname))
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
                on = self.save("pick_%dx%d_%s_on.png" % (bw, gh, slug(lab)), keycap(bw, gh - 2 * FM, lab, True, led=False, picked=True))
                off = self.save("pick_%dx%d_%s_off.png" % (bw, gh, slug(lab)), keycap(bw, gh - 2 * FM, lab, False, led=False))
                kids.append(entry("choice %d" % i, "Button",
                                  o(("version", 2), ("onImage", on), ("offImage", off),
                                    ("buttonId", c["ids"][i] if c.get("ids") else i), ("numButtonsInGroup", n),
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


    # ================================================================ free-layout helpers
    def vis_theme(self, t):
        return ["IndexedEnabling/%d/4/%s" % (t, self.h("lcdColour"))]

    def vis_view(self, v):
        return ["IndexedEnabling/%d/4/%s" % (v, self.h("view"))]

    def image_at(self, png, rect, vis=None, name="image"):
        return entry(name, "Image", o(("version", 2), ("imageType", "Regular"), ("colour", "0"), ("image", png)),
                     bounds(rect, inval=vis))

    def strip_at(self, png, frames, rect, pid, vis=None, name=None):
        return entry(name or "strip " + pid, "Image",
                     o(("version", 2), ("imageType", "FilmStrip"), ("colour", "0"), ("image", png), ("numFrames", frames),
                       ("handleName", "Data")), bounds(rect, inval=vis), remap=[("Data", self.h(pid))])

    def label_at(self, pid, rect, size, colour=VALUE, just="left verticallyCentred", vis=None, style="Bold"):
        return entry("label " + pid, "Label", label("Value", size, style, colour, just), bounds(rect, inval=vis),
                     remap=[("Data", self.h(pid))])

    def caption_at(self, text, pid, rect, size=20, vis=None, just="left verticallyCentred", colour=LABEL):
        return entry("caption", "Label", label("Name", size, "Bold", colour, just, handle=self.named(text, pid)),
                     bounds(rect, inval=vis))

    def knob_at(self, pid, rect, ksize, vis=None, bi=False, value_size=28, layout="side"):
        """A knob cell: film-strip knob + its value in a window. The whole rect is the touch area; the caption is
        a separate label added before it. layout side: knob left, value right; under: value under the knob."""
        p = self.p(pid)
        if p["kind"] == "choice":
            self.discrete(pid)
        x, y, w, h = rect
        strip = "knob%d%s.png" % (ksize, "_bi" if bi else "")
        if strip not in self.images:
            self.save(strip, film([knob_frame(ksize, i / (FRAMES - 1), bi) for i in range(FRAMES)]))
        if layout == "side":
            kx, ky = 4, (h - ksize) // 2
            vr = (ksize + 12, h - 44, w - ksize - 16, 40)
        else:
            kx, ky = (w - ksize) // 2, 2
            vr = (6, ksize + 6, w - 12, h - ksize - 8)
        key = "cp knob2 %dx%d k%d %s%s" % (w, h, ksize, layout, "_bi" if bi else "")
        self.define(key, component([
            focus_entry(),
            entry("knob", "Knob", o(("version", 2), ("knobType", "FilmStrip"), ("filmStrip", strip), ("numFrames", FRAMES),
                                    ("invert", False), ("handleName", "Data")), bounds((kx, ky, ksize, ksize))),
            entry("value", "Label", label("Value", value_size, "Bold", VALUE, "horizontallyCentred verticallyCentred"),
                  bounds(vr))], knob_actions(bi)))
        return [entry(pid, key, o(("version", 1), ("handleName", "Data")), bounds(rect, focus="Yes", invalid="Hide", inval=vis),
                      remap=[("Data", self.h(pid))])], (x + vr[0], y + vr[1], vr[2], vr[3])

    def split_strip(self, png):
        """(on, off) single images from a two-frame strip (frame 1 = on)."""
        im = self.images[png]
        h2 = im.height // 2
        base = png[:-4]
        on = self.save(base + "__on.png", im.crop((0, h2, im.width, 2 * h2)))
        off = self.save(base + "__off.png", im.crop((0, 0, im.width, h2)))
        return on, off

    def momentary_at(self, pid, rect, on_png, off_png, vis=None):
        """A pad held for as long as it's touched: a one-button group in Hold mode, its handle wrapped in
        ResetOnGestureEnd so the parameter goes back to its default (off) when the finger lifts."""
        x, y, w, h = rect
        key = "cp hold %s" % on_png
        self.define(key, component([
            focus_entry(),
            entry("pad", "Button", o(("version", 2), ("onImage", on_png), ("offImage", off_png), ("buttonId", 1),
                                     ("numButtonsInGroup", 1), ("handleName", "Data"), ("gestureBehaviour", "Hold")),
                  bounds((0, 0, w, h)))]))
        return entry(pid, key, o(("version", 1), ("handleName", "Data")), bounds(rect, focus="Yes", invalid="Hide", inval=vis),
                     remap=[("Data", "ResetOnGestureEnd/" + self.h(pid))])

    def flip_at(self, pid, rect, png, vis=None, qlink=True):
        """A two-frame picture flipped by a tap (Toggle Switch): switches and the PATTERN / FX bars. A trigger
        (UNDO, DICE, LOAD ...) is momentary instead: lit while touched, back to off when the finger lifts."""
        x, y, w, h = rect
        if self.p(pid)["trigger"]:
            return self.momentary_at(pid, rect, *self.split_strip(png), vis)
        key = "cp flip %s" % png
        acts = [act("Mouse Down", "Toggle Switch", "Data")]
        if qlink:
            acts.append(act("Mouse Down", "Q-Link", "Data"))
        self.define(key, component([
            entry("state", "Image", o(("version", 2), ("imageType", "FilmStrip"), ("colour", "0"), ("image", png),
                                      ("numFrames", 2), ("handleName", "Data")), bounds((0, 0, w, h))),
            focus_entry()], acts))
        return entry(pid, key, o(("version", 1), ("handleName", "Data")), bounds(rect, focus="Yes", invalid="Hide", inval=vis),
                     remap=[("Data", self.h(pid))])

    def radio_at(self, pid, rect, images, vis=None, ids=None, name=None, focus=True, qlink=True):
        """A button group: images = [(on, off, (x, y, w, h) relative)], buttonId = position (or ids)."""
        x, y, w, h = rect
        n = len(images)
        count = max(ids) + 1 if ids else n
        key = name or "cp radio %s %d" % (pid, x * 10000 + y)
        kids = [focus_entry()] if focus else []
        for i, (on, off, r) in enumerate(images):
            kids.append(entry("choice %d" % i, "Button",
                              o(("version", 2), ("onImage", on), ("offImage", off), ("buttonId", ids[i] if ids else i),
                                ("numButtonsInGroup", count), ("handleName", "Data"), ("gestureBehaviour", "Instant")), bounds(r)))
        self.define(key, component(kids, [act("Mouse Down", "Q-Link", "Data")] if qlink else []))
        return entry(pid, key, o(("version", 1), ("handleName", "Data")),
                     bounds(rect, focus="Yes" if focus else "No", invalid="Hide", inval=vis),
                     remap=[("Data", ("ResetOnGestureEnd/" if pid in TAP_PARAMS else "") + self.h(pid))])

    def key_png(self, text, w, h):
        fname = "key_%s_%dx%d.png" % (slug(text), w, h)
        if fname not in self.images:
            self.save(fname, film([keycap(w, h, text, False), keycap(w, h, text, True)]))
        return fname

    def lcd_key_png(self, theme, text, w, h, size=28):
        fname = "lcdkey_%d_%s_%dx%d.png" % (theme, slug(text) if slug(text) != "x" else "sym%d" % (sum(map(ord, text))), w, h)
        if fname not in self.images:
            self.save(fname, film([lcd_key(theme, w, h, text, False, size), lcd_key(theme, w, h, text, True, size)]))
        return fname

    def lcd_radio_pngs(self, theme, text, w, h, size=24):
        base = "lcdbtn_%d_%s_%dx%d" % (theme, slug(text), w, h)
        on = self.save(base + "_on.png", lcd_key(theme, w, h, text, True, size))
        off = self.save(base + "_off.png", lcd_key(theme, w, h, text, False, size))
        return on, off

    def wave_block(self, y_wave, y_tab, tap_vis=None):
        """The 84 waveform columns, the hit-letter row and the tap area over both."""
        kids = []
        if "wave_cols.png" not in self.images:
            self.save("wave_cols.png", film(wave_frames()))
            self.save("wave_tabs.png", film(tab_frames()))
            self.save("clear_col.png", clear_png(WCOL_W, WAVE_H + TAB_H + 4))
        nw, nt = 4 * 3 * 2 * WLEVELS, 4 * TAB_FRAMES
        assert nw == self.p("wave1")["max"] + 1 and nt == self.p("tab1")["max"] + 1, "frame counts differ from MpcParams.h"
        for c in range(WCOLS):
            kids.append(self.strip_at("wave_cols.png", nw, (WX0 + c * WCOL_W, y_wave, WCOL_W, WAVE_H), "wave%d" % (c + 1), name="wave"))
        for c in range(WCOLS):
            kids.append(self.strip_at("wave_tabs.png", nt, (WX0 + c * WCOL_W, y_tab, WCOL_W, TAB_H), "tab%d" % (c + 1), name="tab"))
        imgs = [("clear_col.png", "clear_col.png", (c * WCOL_W, 0, WCOL_W, WAVE_H + TAB_H + 4)) for c in range(WCOLS)]
        kids.append(self.radio_at("waveTap", (WX0, y_wave, WCOLS * WCOL_W, WAVE_H + TAB_H + 4), imgs, tap_vis,
                                  ids=list(range(1, WCOLS + 1)), name="cp wave tap", focus=False, qlink=False))
        return kids

    def drag_strip(self, pid, rect, theme):
        """A horizontal TrackAndThumb slider: the thumb sits on the selected slice's start marker and follows the finger."""
        x, y, w, h = rect
        thumb = "dragthumb_%d.png" % theme
        if thumb not in self.images:
            self.save(thumb, lcd_key(theme, DRAG_THUMB, h, "START", True, 20))
        key = "cp drag %d" % theme
        self.define(key, component([
            focus_entry(),
            entry("drag", "Slider", o(("version", 3), ("sliderType", "TrackAndThumb"), ("direction", "Horizontal"),
                                      ("thumbImage", thumb), ("thumbTrackVisible", False), ("handleName", "Data")),
                  bounds((0, 0, w, h)))]))
        return entry(pid, key, o(("version", 1), ("handleName", "Data")),
                     bounds(rect, focus="Yes", invalid="Hide", inval=self.vis_theme(theme)), remap=[("Data", self.h(pid))])

    def lcd_panel(self, rect, boxes, rules, tag):
        x, y, w, h = rect
        png = "lcdpanel_%s.png" % tag
        rel_boxes = [(bx - x, by - y, bw, bh) for (bx, by, bw, bh) in boxes]
        rel_rules = [(a - x, b - y, c - x) for (a, b, c) in rules]
        self.save(png, film([lcd_art(t, w, h, rel_boxes, rel_rules) for t in range(4)]))
        return self.strip_at(png, 4, rect, "lcdColour", name="lcd panel")

    def lcd_text(self, pid, rect, size, just="left verticallyCentred", style="Bold", dim=False):
        out = []
        for t in range(4):
            out.append(self.label_at(pid, rect, size, argb(LCD[t][2] if dim else LCD[t][1]), just, vis=self.vis_theme(t), style=style))
        return out

    # ================================================================ MAIN: the desktop's main screen
    def main_tab(self):
        kids = []
        # background: panel, value windows for the always-on cells
        bgim = Image.new("RGB", (W, H), PANEL)
        d = ImageDraw.Draw(bgim)
        for y in range(H):
            t = y / (H - 1)
            d.line([(0, y), (W, y)], fill=mix(PANEL, PANEL_LO, t))
        windows = []

        # ---- the LCD (desktop rows: transport + name + LOAD + BPM, break keys, wave, hits, slice line, status)
        LX, LY, LW, LH = 10, 4, 1260, 344
        rowA, rowB = (16, 44), (66, 38)
        name_box = (166, rowA[0], 596, rowA[1])
        bpm_box = (978, rowA[0], 156, rowA[1])
        kids.append(self.lcd_panel((LX, LY, LW, LH), [name_box, bpm_box], [(26, 312, 1254)], "main"))
        kids += self.lcd_text("infoName", (name_box[0] + 10, name_box[1] + 2, name_box[2] - 20, name_box[3] - 4), 30,
                              "horizontallyCentred verticallyCentred")
        kids += self.lcd_text("infoBpm", (bpm_box[0] + 10, bpm_box[1] + 2, bpm_box[2] - 20, bpm_box[3] - 4), 30,
                              "horizontallyCentred verticallyCentred")
        kids += self.lcd_text("infoSlParams", (26, 284, 1228, 28), 22, style="Bold")
        kids += self.lcd_text("infoStatus", (26, 316, 900, 28), 22, style="SemiBold")
        kids += self.lcd_text("infoSlice", (930, 316, 324, 28), 22, "right verticallyCentred")
        # keys on the glass (one set per LCD colour)
        for t in range(4):
            v = self.vis_theme(t)
            kids.append(self.flip_at("play", (26, rowA[0], 62, rowA[1]), self.lcd_key_png(t, "▶", 62, rowA[1]), v))
            kids.append(self.flip_at("brkPrev", (96, rowA[0], 62, rowA[1]), self.lcd_key_png(t, "◀", 62, rowA[1]), v, qlink=False))
            kids.append(self.flip_at("brkNext", (770, rowA[0], 62, rowA[1]), self.lcd_key_png(t, "▶ ", 62, rowA[1]), v, qlink=False))
            kids.append(self.flip_at("showDisk", (840, rowA[0], 130, rowA[1]), self.lcd_key_png(t, "LOAD", 130, rowA[1]), v, qlink=False))
            # the waveform here is the same picture as on SLICE: its zoom can be put back from MAIN too (lit = ZOOM SLICE)
            kids.append(self.flip_at("zoom", (1142, rowA[0], 112, rowA[1]), self.lcd_key_png(t, "ZOOM", 112, rowA[1], 24), v, qlink=False))
            notes = ["1 C1", "2 C#1", "3 D1", "4 D#1", "5 E1", "6 F1", "7 F#1", "8 G1"]
            bw = (1228 - 7 * 8) // 8
            imgs = []
            for i, n in enumerate(notes):
                on, off = self.lcd_radio_pngs(t, n, bw, rowB[1], 24)
                imgs.append((on, off, (i * (bw + 8), 0, bw, rowB[1])))
            kids.append(self.radio_at("break", (26, rowB[0], 1228, rowB[1]), imgs, v, name="cp breaks %d" % t))
        kids += self.wave_block(110, 250, ["RangedEnabling/0/2/4/%s" % self.h("view")])

        # ---- the dice row: PATTERN bar + AMOUNT, FX bar + AMOUNT
        DY, DH = 356, 66
        for (dice, show, amount, text, x) in (("dicePattern", "showPattern", "patAmount", "PATTERN", 10),
                                               ("diceFx", "showFx", "fxAmount", "FX", 650)):
            dk = "dice_%dx%d.png" % (DH, DH)
            if dk not in self.images:
                self.save(dk, film([dice_key(DH, DH, False), dice_key(DH, DH, True)]))
            kids.append(self.flip_at(dice, (x, DY, DH, DH), dk))
            bk = "bar_%s.png" % slug(text)
            self.save(bk, film([bar_key(404 - 6, DH, text, False), bar_key(404 - 6, DH, text, True)]))
            kids.append(self.flip_at(show, (x + DH + 6, DY, 404 - 6, DH), bk, qlink=False))
            kids.append(self.caption_at("AMOUNT", amount, (x + 476 + 72, DY, 74, 22), 18))
            k, win = self.knob_at(amount, (x + 476, DY, 150, DH), 60, value_size=24)
            windows.append(win)
            kids += k

        # ---- sections under the dice row (one shows, by MP_VIEW)
        SY = 430
        pad_w, pad_h, gap = 152, 88, 6
        settings = ["stutRate", "rollRate", None, "tapeMs", None, "akCycLen", "diveRes", "dropRate"]
        names = ["STUTTER", "ROLL", "REVERSE", "TAPE STOP", "HALF-TIME", "AKAIZE", "FILT DIVE", "S950 DROP"]
        win = (10, 44, pad_w - 20, 34)
        for view in (0, 1):     # the 8 FX pads along the bottom (MAIN and FX sections)
            vv = self.vis_view(view)
            for i in range(8):
                x = 11 + i * (pad_w + gap)
                if settings[i]:
                    # the setting shows through the pad's window; drawn first so the pad on top keeps the touch
                    kids.append(self.label_at(settings[i], (x + win[0], SY + win[1], win[2], win[3]), 22, argb(LCD[0][1]),
                                              "horizontallyCentred verticallyCentred", vis=vv))
                hold = None if settings[i] else "HOLD"
                off = self.save("pad_%d_off.png" % i, pad_key(pad_w, pad_h, names[i], False, win, hold))
                on = self.save("pad_%d_on.png" % i, pad_key(pad_w, pad_h, names[i], True, win, hold))
                kids.append(self.momentary_at("pad%d" % (i + 1), (x, SY, pad_w, pad_h), on, off, vv))
        # windows behind the pad settings: shown with the pads only (MAIN and FX sections)
        padwin = Image.new("RGBA", (W, pad_h), (0, 0, 0, 0))
        dw = ImageDraw.Draw(padwin)
        for i in range(8):
            if settings[i]:
                x = 11 + i * (pad_w + gap)
                dw.rectangle((x + win[0], win[1], x + win[0] + win[2] - 1, win[1] + win[3] - 1), fill=LCD[0][0] + (255,))
        self.save("pad_windows.png", padwin)
        for view in (1, 0):
            kids.insert(1, self.image_at("pad_windows.png", (0, SY, W, pad_h), self.vis_view(view), "pad windows"))

        # MAIN: SAMPLE RATE, FILTER, DISTORTION, CRUSH, REVERB, VOLUME (the desktop's bottom knobs + slider + VOL)
        KY, KH = 526, 98
        vv = self.vis_view(0)
        sec_main = Image.new("RGBA", (W, KH), (0, 0, 0, 0))
        for i, (pid, cap) in enumerate((("srate", "SAMPLE RATE"), ("filter", "FILTER"), ("dist", "DISTORTION"),
                                        ("crush", "CRUSH"), ("reverb", "REVERB"), ("volume", "VOLUME"))):
            x = 10 + i * 210
            kids.append(self.caption_at(cap, pid, (x + 96, KY + 6, 110, 26), 20, vis=vv))
            k, wv = self.knob_at(pid, (x, KY, 206, KH), 84, vis=vv, value_size=26)
            sec_main.paste(box_image(wv[2], wv[3]), (wv[0], wv[1] - KY))
            kids += k
        self.save("sec_main.png", sec_main)
        kids.insert(1, self.image_at("sec_main.png", (0, KY, W, KH), vv, "section main"))

        # FX: each pad's setting knob under it
        vv = self.vis_view(1)
        sec_fx = Image.new("RGBA", (W, KH), (0, 0, 0, 0))
        dfx = ImageDraw.Draw(sec_fx)
        for i in range(8):
            x = 11 + i * (pad_w + gap)
            if settings[i]:
                k, wv = self.knob_at(settings[i], (x, KY, pad_w, KH), 60, vis=vv, value_size=22, layout="under")
                kids += k
            else:
                dfx.text((x + pad_w / 2, KH / 2), "HOLD ONLY", font=pil_font(20), fill=(90, 90, 90, 255), anchor="mm")
        self.save("sec_fx.png", sec_fx)
        kids.insert(1, self.image_at("sec_fx.png", (0, KY, W, KH), vv, "section fx"))

        # PATTERN: the grid takes the LCD under the break keys (BR3AK's SEQ tab), the tools come up underneath
        vv = self.vis_view(3)
        gy, gh = 108, 236
        for t in range(4):
            pass
        cover = "gridcover_main.png"
        self.save(cover, film([grid_cover(t, 1236, gh, 24, 30) for t in range(4)]))
        kids.append(entry("grid cover", "Image", o(("version", 2), ("imageType", "FilmStrip"), ("colour", "0"), ("image", cover),
                                                     ("numFrames", 4), ("handleName", "Data")),
                          bounds((22, gy, 1236, gh), inval=vv), remap=[("Data", self.h("lcdColour"))]))
        kids += self.grid_block(gy + 2, 20, gy + 24, 30, 26, 26, vv, "main")
        r0, r1, rh = SY, SY + 98, 92
        sec_pat = Image.new("RGBA", (W, H - SY), (0, 0, 0, 0))
        def pick_row(pid, rect, labels, cap):
            self.discrete(pid)
            x0, y0, w0, h0 = rect
            kids.append(self.caption_at(cap, pid, (x0 + 6, y0 + 2, w0 - 12, 24), 20, vis=vv))
            n = len(labels)
            bw = (w0 - 2 * FM + 2 * (n - 1)) // n
            imgs = []
            for i, l in enumerate(labels):
                on = self.save("pick_%d_%s_on.png" % (bw, slug(l)), keycap(bw, 58, l, True, led=False, picked=True))
                off = self.save("pick_%d_%s_off.png" % (bw, slug(l)), keycap(bw, 58, l, False, led=False))
                imgs.append((on, off, (FM + i * (bw - 2), FM, bw, 58)))
            kids.append(self.radio_at(pid, (x0, y0 + 24, w0, 70), imgs, vv))
        pick_row("length", (10, r0, 330, rh), ["1 BAR", "2 BARS", "4 BARS"], "LENGTH")
        pick_row("seqPage", (346, r0, 420, rh), ["1-16", "17-32", "33-48", "49-64"], "STEPS SHOWN")
        x = 772
        for pid, text in (("seqFollow", "FOLLOW"), ("original", "ORIGINAL"), ("clear", "CLEAR"), ("resetOrig", "RESET")):
            w_ = 140 if pid in ("seqFollow", "original") else (1270 - 772 - 280) // 2
            kids.append(self.flip_at(pid, (x, r0 + 4, w_ - 4, rh - 8), self.key_png(text, w_ - 4, rh - 8), vv, qlink=False))
            x += w_
        kids += self.key_row("stepFx", (10, r1, 640, rh), ["OFF", "STUTTER", "REVERSE", "STRETCH", "TAPE STOP"],
                             "FX ON THE SELECTED STEP (TAP A STEP TO PICK IT)", 58, vv)
        kids += self.key_row("stepRate", (656, r1, 330, rh), ["1/8", "1/16", "1/32", "1/64"], "FX RATE", 58, vv)
        x = 992
        for pid, text in (("undo", "UNDO"), ("redo", "REDO")):
            kids.append(self.flip_at(pid, (x, r1 + 4, 135, rh - 8), self.key_png(text, 135, rh - 8), vv))
            x += 139
        self.save("sec_pattern.png", sec_pat)
        kids.insert(1, self.image_at("sec_pattern.png", (0, SY, W, H - SY), vv, "section pattern"))

        # DISK: the break list. Tap it (or a line), turn the jog wheel: the break under it plays; push the jog
        # wheel to load it into the selected break. DEMO and AKAIZE beside it.
        vv = self.vis_view(2)
        sec_disk = Image.new("RGBA", (W, H - SY), (0, 0, 0, 0))
        LW_, LH_, line_h = 1036, 192, 32
        sec_disk.paste(box_image(LW_, LH_), (10, 0))
        kids.append(self.caption_at("TURN THE JOG WHEEL TO HEAR A BREAK  -  PUSH IT TO LOAD", "browse",
                                    (24, SY + 2, LW_ - 28, 26), 20, vis=vv, colour=argb((150, 24, 30))))
        for k_ in range(5):
            kids.append(self.label_at("list%d" % (k_ + 1), (24, SY + 30 + k_ * line_h, LW_ - 28, line_h), 26, VALUE, vis=vv))
        if "clear_line.png" not in self.images:
            self.save("clear_line.png", clear_png(LW_, line_h))
        lines = [entry("line %d" % k_, "Button", o(("version", 2), ("onImage", "clear_line.png"), ("offImage", "clear_line.png"),
                                                   ("buttonId", k_ + 1), ("numButtonsInGroup", 6), ("handleName", "Tap"),
                                                   ("gestureBehaviour", "Instant")),
                       bounds((0, 30 + k_ * line_h, LW_, line_h))) for k_ in range(5)]
        self.define("cp disk list", component([focus_entry()] + lines,
                                              [act("Enter Pressed", "Toggle Switch", "Load"), act("Mouse Down", "Q-Link", "Data")]))
        kids.append(entry("browse", "cp disk list", o(("version", 1), ("handleName", "Data")),
                          bounds((10, SY, LW_, LH_), focus="Yes", invalid="Hide", inval=vv),
                          remap=[("Data", self.h("browse")), ("Tap", "ResetOnGestureEnd/" + self.h("listTap")), ("Load", self.h("load"))]))
        kids.append(self.flip_at("demo", (1052, r0 + 4, 218, rh - 8), self.key_png("DEMO\nBREAK", 218, rh - 8), vv))
        kids.append(self.flip_at("akaize", (1052, r1 + 4, 218, rh - 8), self.key_png("AKAIZE TO\nMPC BPM", 218, rh - 8), vv))
        self.save("sec_disk.png", sec_disk)
        kids.insert(1, self.image_at("sec_disk.png", (0, SY, W, H - SY), vv, "section disk"))

        for (x, y, w, h) in windows:
            bgim.paste(box_image(w, h).convert("RGB"), (x, y))
        self.save("bg_main.png", bgim)
        kids.insert(0, self.image_at("bg_main.png", (0, 0, W, H), name="background"))
        qb = ["10 526 1260 98", "10 356 1260 66", "10 4 1260 344", "10 430 1260 90"]
        tab = o(("version", 1), ("tabName", "MAIN"), ("fnKeyIndex", 0), ("fnKeySubIndex", 0),
                ("componentDefinition", component(kids, colour=argb(PANEL))), ("qlinkBoundsData", qb))
        q = {13: "filter", 14: "dist", 15: "crush", 16: "reverb",
             9: "srate", 10: "volume", 11: "patAmount", 12: "fxAmount",
             5: "break", 6: "slice", 7: "slMove", 8: "browse",
             1: "stutRate", 2: "rollRate", 3: "tapeMs", 4: "akCycLen"}
        qm = o(("Tab", 1), ("SubTab", 1), ("Bank Direction", "RowTopDown"),
               ("Q-Links", o(*[("Q-Link %d" % i, self.p(q[i])["index"]) for i in range(1, 17)])))
        return tab, qm

    # ================================================================ SLICE / CHOP: the waveform with editing under it
    def slice_tab(self, sub):
        kids = []
        LX, LY, LW, LH = 10, 4, 1260, 302
        name_box = (26, 16, 430, 40)
        bpm_box = (1054, 16, 200, 40)
        kids.append(self.lcd_panel((LX, LY, LW, LH), [name_box, bpm_box], [(26, 268, 1254)], "slice"))
        kids += self.lcd_text("infoName", (name_box[0] + 10, name_box[1], name_box[2] - 20, name_box[3]), 26)
        kids += self.lcd_text("infoBpm", (bpm_box[0] + 8, bpm_box[1], bpm_box[2] - 16, bpm_box[3]), 26,
                              "horizontallyCentred verticallyCentred")
        # the drag strip under the hit row: put a finger on it and slide to move the selected slice's start
        for t in range(4):
            track = "dragtrack_%d.png" % t
            if track not in self.images:
                self.save(track, lcd_key(t, WCOLS * WCOL_W, 34, "<  DRAG TO MOVE THE START  >", False, 20))
            kids.append(self.image_at(track, (WX0, 234, WCOLS * WCOL_W, 34), self.vis_theme(t), name="drag track"))
            kids.append(self.drag_strip("slDrag", (WX0 - DRAG_THUMB // 2, 234, WCOLS * WCOL_W + DRAG_THUMB, 34), t))
            # ADD / DELETE right on the LCD, beside the zoom keys
            v = self.vis_theme(t)
            kids.append(self.flip_at("slAdd", (740, 16, 120, 40), self.lcd_key_png(t, "+ ADD", 120, 40, 22), v, qlink=False))
            kids.append(self.flip_at("slDel", (868, 16, 120, 40), self.lcd_key_png(t, "- DEL", 120, 40, 22), v, qlink=False))
        kids += self.lcd_text("infoStatus", (26, 272, 900, 26), 22, style="SemiBold")
        kids += self.lcd_text("infoSlice", (930, 272, 324, 26), 22, "right verticallyCentred")
        for t in range(4):
            imgs = []
            for i, z in enumerate(["ALL", "SLICE"]):
                on, off = self.lcd_radio_pngs(t, "ZOOM " + z, 128, 40, 22)
                imgs.append((on, off, (i * 134, 0, 128, 40)))
            self.discrete("zoom")
            kids.append(self.radio_at("zoom", (466, 16, 262, 40), imgs, self.vis_theme(t), name="cp zoom %d" % t))
        kids += self.wave_block(62, 204)
        # three rows of four, as on the grid pages
        if sub == 0:
            name = "SLICE"
            rows = [[K("slice", "SLICE"), K("slMove", "START"), K("slEnd", "END"), P("slBadge", "HIT", ["K", "S", "GS", "H", "O", "P"])],
                    [K("slPitch", "PITCH", bi=True), K("slGate", "GATE"), K("slStretch", "STRETCH"), K("slTrans", "TRANSIENT")],
                    [T("slReverse", "REVERSE"), K("slStutter", "STUTTER"), K("slXfade", "XFADE"), T("slMute", "MUTE")]]
        else:
            name = "CHOP"
            rows = [[K("slice", "SLICE"), K("slMove", "START"), G("slAdd", "ADD\n(SPLIT)"), G("slDel", "DELETE\n(JOIN)")],
                    [T("editAll", "EDIT\nALL SLICES"), G("slNudgeL", "NUDGE\nSTART <"), G("slNudgeR", "NUDGE\nSTART >"), G("reslice", "RESLICE")],
                    [P("slicing", "SLICING", ["TRANS", "GRID"]), K("sens", "SENSITIVITY"), P("grid", "GRID", ["1/8", "1/16", "1/32"]),
                     T("slMute", "MUTE")]]
        page = {"fn": 1, "sub": sub, "name": name, "rows": rows}
        global ROW_Y0, ROW_H
        saved = (ROW_Y0, ROW_H)
        ROW_Y0, ROW_H = 312, 100
        try:
            bgim = self.background(page)
            # the LCD sits on top of the background's top part
            png = "bg_1_%d.png" % sub
            self.save(png, bgim)
            kids.insert(0, self.image_at(png, (0, 0, W, H), name="background"))
            for r, row in enumerate(rows):
                x = M
                for c in row:
                    kids += self.cell(c, x, row_y(r))
                    x += COL_W * c["span"]
            qb = ["%d %d %d %d" % (M, row_y(r), W - 2 * M, ROW_H) for r in range(3)]
        finally:
            ROW_Y0, ROW_H = saved
        tab = o(("version", 1), ("tabName", name), ("fnKeyIndex", 1), ("fnKeySubIndex", sub),
                ("componentDefinition", component(kids, colour=argb(PANEL))), ("qlinkBoundsData", qb))
        return tab, self.qmap(page)


    def grid_block(self, y_head, head_h, y_lanes, lane_h, cell_h, fx_h, vis=None, tag=""):
        """The SEQ grid: step row, 6 lanes x 16 cells, the FX row. Each cell is its own focusable control: a tap
        puts a hit there or takes it off (and selects the step), and while it has focus the jog wheel steps through
        that lane's slices. A tap on the FX row selects the step for STEP FX / RATE."""
        kids = []
        cs = "cells_%d.png" % cell_h
        if cs not in self.images:
            self.save(cs, film(cell_frames(GCELL, cell_h)))
            self.save("clear_cell_%d.png" % cell_h, clear_png(GCELL, cell_h))
        fs = "fxrow_%d.png" % fx_h
        if fs not in self.images:
            self.save(fs, film(fxrow_frames(GCELL, fx_h)))
            self.save("clear_fx_%d.png" % fx_h, clear_png(GPITCH, fx_h))
        hs = "seqhead_%d.png" % head_h
        if hs not in self.images:
            self.save(hs, film(head_frames(head_h)))
        assert 4 * CELL_FRAMES + 2 == self.p("cell1")["max"] + 1 and 4 * 17 == self.p("seqHead")["max"] + 1
        assert 4 * FXROW_FRAMES == self.p("fxrow1")["max"] + 1
        kids.append(self.strip_at(hs, 68, (GX0, y_head, 16 * GPITCH, head_h), "seqHead", vis, "step row"))
        clear = "clear_cell_%d.png" % cell_h
        key = "cp seq cell %d" % cell_h
        # buttonId comes from the component definition, so give each cell its own definition id via a wrapper
        for lane in range(6):
            for col in range(16):
                i = lane * 16 + col
                k2 = "%s #%d" % (key, i + 1)
                self.define(k2, component([
                    focus_entry(),
                    entry("cell", "Image", o(("version", 2), ("imageType", "FilmStrip"), ("colour", "0"), ("image", cs),
                                              ("numFrames", 4 * CELL_FRAMES + 2), ("handleName", "Data")), bounds((0, 0, GCELL, cell_h))),
                    entry("tap", "Button", o(("version", 2), ("onImage", clear), ("offImage", clear), ("buttonId", i + 1),
                                             ("numButtonsInGroup", 113), ("handleName", "Tap"), ("gestureBehaviour", "Instant")),
                          bounds((0, 0, GCELL, cell_h)))]))
                kids.append(entry("cell%d" % (i + 1), k2, o(("version", 1), ("handleName", "Data")),
                                  bounds((GX0 + col * GPITCH, y_lanes + lane * lane_h, GCELL, cell_h), focus="Yes", invalid="Hide", inval=vis),
                                  remap=[("Data", self.h("cell%d" % (i + 1))), ("Tap", "ResetOnGestureEnd/" + self.h("seqTap"))]))
        yfx = y_lanes + 6 * lane_h
        for col in range(16):
            kids.append(self.strip_at(fs, 4 * FXROW_FRAMES, (GX0 + col * GPITCH, yfx, GCELL, fx_h), "fxrow%d" % (col + 1), vis, "fx row"))
        cl = "clear_fx_%d.png" % fx_h
        imgs = [(cl, cl, (col * GPITCH, 0, GPITCH, fx_h)) for col in range(16)]
        kids.append(self.radio_at("seqTap", (GX0, yfx, 16 * GPITCH, fx_h), imgs, vis, ids=list(range(97, 113)),
                                  name="cp fx row tap %s" % tag, focus=False, qlink=False))
        return kids

    def key_row(self, pid, rect, labels, caption, key_h, vis=None):
        """A caption over a row of keys for a choice (the picked one amber)."""
        x, y, w, h = rect
        self.discrete(pid)
        out = [self.caption_at(caption, pid, (x + 4, y, w - 8, h - key_h - 2), 18, vis=vis)]
        n = len(labels)
        bw = (w + 2 * (n - 1)) // n
        imgs = []
        for i, l in enumerate(labels):
            on = self.save("pick_%dx%d_%s_on.png" % (bw, key_h, slug(l)), keycap(bw, key_h, l, True, led=False, picked=True))
            off = self.save("pick_%dx%d_%s_off.png" % (bw, key_h, slug(l)), keycap(bw, key_h, l, False, led=False))
            imgs.append((on, off, (i * (bw - 2), 0, bw, key_h)))
        out.append(self.radio_at(pid, (x, y + h - key_h, w, key_h), imgs, vis, name="cp keyrow %s %d %d" % (pid, x, y)))
        return out

    def lcd_radio(self, pid, rect, labels, size=22, gap=6, vis_extra=None):
        """A row of LCD keys for a choice, one set per LCD colour."""
        x, y, w, h = rect
        self.discrete(pid)
        n = len(labels)
        bw = (w - gap * (n - 1)) // n
        out = []
        for t in range(4):
            imgs = []
            for i, l in enumerate(labels):
                on, off = self.lcd_radio_pngs(t, l, bw, h, size)
                imgs.append((on, off, (i * (bw + gap), 0, bw, h)))
            out.append(self.radio_at(pid, rect, imgs, self.vis_theme(t), name="cp lcdradio %s %d %d" % (pid, t, x * 1000 + y)))
        return out

    def lcd_toggle(self, pid, rect, text, size=22, qlink=False):
        x, y, w, h = rect
        return [self.flip_at(pid, rect, self.lcd_key_png(t, text, w, h, size), self.vis_theme(t), qlink=qlink) for t in range(4)]

    def seq_tab(self):
        kids = []
        LX, LY, LW, LH = 10, 4, 1260, 466
        kids.append(self.lcd_panel((LX, LY, LW, LH), [], [], "seq"))
        HY, HH = 16, 42
        self.discrete("original")
        kids += self.lcd_radio("original", (26, HY, 330, HH), ["PATTERN", "ORIGINAL"], 24)
        kids += self.lcd_radio("length", (376, HY, 360, HH), ["1 BAR", "2 BARS", "4 BARS"], 22)
        kids += self.lcd_radio("seqPage", (756, HY, 370, HH), ["1-16", "17-32", "33-48", "49-64"], 22)
        kids += self.lcd_toggle("seqFollow", (1140, HY, 114, HH), "FOLLOW", 20)
        lane_y, lane_h, fx_h = 100, 52, 36
        for t in range(4):
            png = "gridcover_seq_%d.png" % t
            self.save(png, grid_cover(t, 1228, 7 * lane_h + 2, 0, lane_h))
            kids.append(self.image_at(png, (26, lane_y, 1228, 7 * lane_h + 2), self.vis_theme(t), "grid lanes"))
        kids += self.grid_block(66, 28, lane_y, lane_h, lane_h - 6, fx_h, tag="seq")
        kids += self.lcd_text("infoBrk", (30, 64, 104, 32), 26)
        # under the LCD: the two dice bars (the whole bar rolls) and the pattern tools
        DY, DH = 478, 66
        for (dice, amount, text, x) in (("dicePattern", "patAmount", "PATTERN", 10), ("diceFx", "fxAmount", "FX", 650)):
            png = "dicebar_%s.png" % slug(text)
            self.save(png, film([dice_bar(470, DH, text, False), dice_bar(470, DH, text, True)]))
            kids.append(self.flip_at(dice, (x, DY, 470, DH), png))
            kids.append(self.caption_at("AMOUNT", amount, (x + 476 + 72, DY, 74, 22), 18))
            k, win = self.knob_at(amount, (x + 476, DY, 150, DH), 60, value_size=24)
            kids += k
            self._seq_windows.append(win)
        # the selected step's FX (tap a cell or the FX row to pick the step)
        RY, RH = 552, 72
        kids.append(self.caption_at("STEP", "seqSel", (86, RY, 110, 22), 18))
        k, win = self.knob_at("seqSel", (10, RY, 196, RH), 64, value_size=22)
        kids += k
        self._seq_windows.append(win)
        kids += self.key_row("stepFx", (212, RY, 640, RH), ["OFF", "STUTTER", "REVERSE", "STRETCH", "TAPE STOP"], "FX ON THE STEP", 44)
        kids += self.key_row("stepRate", (858, RY, 300, RH), ["1/8", "1/16", "1/32", "1/64"], "FX RATE", 44)
        kids.append(self.flip_at("undo", (1164, RY, 106, RH), self.key_png("UNDO", 106, RH)))
        bgim = Image.new("RGB", (W, H), PANEL)
        d = ImageDraw.Draw(bgim)
        for yy in range(H):
            d.line([(0, yy), (W, yy)], fill=mix(PANEL, PANEL_LO, yy / (H - 1)))
        for (wx, wy, ww, wh) in self._seq_windows:
            bgim.paste(box_image(ww, wh).convert("RGB"), (wx, wy))
        self.save("bg_seq.png", bgim)
        kids.insert(0, self.image_at("bg_seq.png", (0, 0, W, H), name="background"))
        qb = ["%d %d %d %d" % (GX0, lane_y + r * lane_h, 16 * GPITCH, lane_h) for r in range(4)]
        tab = o(("version", 1), ("tabName", "SEQ"), ("fnKeyIndex", 2), ("fnKeySubIndex", 0),
                ("componentDefinition", component(kids, colour=argb(PANEL))), ("qlinkBoundsData", qb))
        q = {13 + c - 4 * r: "vstep%d" % (r * 4 + c + 1) for r in range(4) for c in range(4)}
        qm = o(("Tab", 3), ("SubTab", 1), ("Bank Direction", "RowTopDown"),
               ("Q-Links", o(*[("Q-Link %d" % i, self.p(q[i])["index"]) for i in range(1, 17)])))
        return tab, qm

    def build(self):
        os.makedirs(self.out, exist_ok=True)
        for f in os.listdir(self.out):
            if f.endswith(".png") or f.endswith(".json"):
                os.remove(os.path.join(self.out, f))
        for bi in (False, True):
            self.define(OVERLAY[bi], overlay_component(knob_overlay(bi)))
        self.discrete("lcdColour")
        tabs, qmaps = [], []
        self._seq_windows = []
        for t, q in (self.main_tab(), self.slice_tab(0), self.slice_tab(1), self.seq_tab()):
            tabs.append(t)
            qmaps.append(q)
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
                                              "patAmount", "fxAmount", "swing", "style", "stutRate", "rollRate", "tapeMs", "akCycLen")]
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


# ---------------------------------------------------------------- hardware rules (learned on Twisted-Helix)
def check_skin(path):
    """1. A Label drawn over a touch area takes the touch on the MPC (Helix 0.2.2): no tab-level label may overlap
          an interactive entry painted before it.
       2. The focus frame must be the first child of a pressable component (Helix 0.2.6).
       3. A cell that opens an overlay must contain something reading its Data handle (Helix 0.2.3).
       4. Nothing may sit off the 1280 x 800 screen (CHOPPAGE 0.6.1)."""
    t = json.load(open(path))
    defs = {d["key"]: d["value"] for d in t["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
    def shown_when(b):
        """{handle: index} for IndexedEnabling conditions."""
        out = {}
        for c in b.get("additionalInvalidatingHandles", []):
            parts = c.split("/")
            if parts[0] == "IndexedEnabling":
                out["/".join(parts[3:])] = int(parts[1])
        return out

    def exclusive(a, b):
        return any(h in b and b[h] != v for h, v in a.items())

    bad = []
    for tab in t["pageData"]["tabs"]:
        seen = []
        for e in tab["componentDefinition"]["componentsData"]:
            typ, b = e["componentData"]["type"], e["bounds"]
            x, y, w, h = map(float, b["bounds"].split())
            vis = shown_when(b)
            # 4. everything on the 1280 x 800 screen: one component past an edge leaves the whole plugin screen
            #    blank on the MPC (CHOPPAGE 0.6.1 - 0.7.2: the drag strip started at x = -8)
            if b.get("boundsType", "Absolute") == "Absolute" and (x < 0 or y < 0 or x + w > 1280 or y + h > 800):
                bad.append("%s: '%s' is off the screen (%g %g %g %g)" % (tab["tabName"], e["componentData"]["name"], x, y, w, h))
            if typ == "Label":
                for nm, (X, Y, W_, H_), v2 in seen:
                    if x < X + W_ and X < x + w and y < Y + H_ and Y < y + h and not exclusive(vis, v2):
                        bad.append("%s: label '%s' over '%s'" % (tab["tabName"], e["componentData"]["name"], nm))
            elif typ not in ("Image", "Focus"):
                seen.append((e["componentData"]["name"], (x, y, w, h), vis))
    late, empty = [], []
    for key, v in defs.items():
        types = [k["componentData"]["type"] for k in v["componentsData"]]
        pressable = {"Button", "Knob", "Slider", "Envelope"} & set(types)
        if pressable and "Focus" in types and types.index("Focus") > min(types.index(x) for x in pressable):
            late.append(key)
        if any(a_["handler"] == "Show Overlay" for a_ in v["actions"]):
            if not any(k["componentData"]["type"] != "Focus" and k["componentData"]["data"].get("handleName") == "Data"
                       for k in v["componentsData"]):
                empty.append(key)
    msgs = []
    if bad:
        msgs.append("labels on top of touch areas:\n  " + "\n  ".join(bad[:20]))
    if late:
        msgs.append("focus frame not first in: " + ", ".join(late))
    if empty:
        msgs.append("overlay cells with nothing reading Data: " + ", ".join(empty))
    if msgs:
        raise SystemExit("make_skin: " + "\n".join(msgs))

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
    check_skin(os.path.join(a.out, "TUI.json"))
    print("make_skin: %d tabs, %d images, %d components -> %s" % (n, len(b.images), len(b.defs), a.out))
    if a.preview:
        for p in preview(b, a.preview, a.preview_theme):
            print("  preview", p)

if __name__ == "__main__":
    main()
