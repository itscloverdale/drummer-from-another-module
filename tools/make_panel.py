#!/usr/bin/env python3
"""Generates the panel SVG, the component SVGs and src/layout.hpp.

The layout follows the Moog DFAM hardware panel: positions were measured from a
straight-on product photo. Rack's SVG renderer can't draw <text>, so every label
is converted to paths with fontTools. Run with the plugin's venv:

    .venv/bin/python tools/make_panel.py            # res/ and src/layout.hpp
    .venv/bin/python tools/make_panel.py --preview  # also build/panel-preview.svg

The preview places the real component artwork on the panel, so the look can be
checked without opening Rack.
"""
import math
import os
import random
import sys

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTS = os.path.join(ROOT, "tools", "fonts")

# The bold block at the bottom left, where the hardware prints its model name.
WORDMARK = "CE"

WOOD = 2 * 5.08            # walnut cheek on each side
PANEL = 60 * 5.08          # the DFAM's 60HP panel
W = PANEL + 2 * WOOD       # 64HP in Rack
H = 128.5
OX = WOOD                  # x offset of the black panel

BG = "#1c1d21"
FG = "#f1f1f1"
OUT_FG = "#1c1d21"

# Rack knobs sweep +-0.83 pi; scales use the same span.
# Rack's SVG renderer uses only the first and last stop of a gradient and draws
# radial gradients with a fixed radius, so all shading here is two-stop linear
# gradients plus translucent shapes.
SWEEP = math.degrees(0.83 * math.pi)


class Font:
    def __init__(self, path):
        self.font = TTFont(path)
        self.glyphs = self.font.getGlyphSet()
        self.cmap = self.font.getBestCmap()
        self.upm = self.font["head"].unitsPerEm

    def _glyph(self, ch):
        return self.cmap.get(ord(ch), ".notdef")

    def width(self, text, size):
        return sum(self.glyphs[self._glyph(c)].width for c in text) * size / self.upm

    def path(self, text, x, y, size, anchor="middle"):
        w = self.width(text, size)
        x0 = {"start": x, "middle": x - w / 2, "end": x - w}[anchor]
        s = size / self.upm
        pen = SVGPathPen(self.glyphs)
        cx = x0
        for c in text:
            g = self.glyphs[self._glyph(c)]
            g.draw(TransformPen(pen, (s, 0, 0, -s, cx, y)))
            cx += g.width * s
        return pen.getCommands()


LABEL = Font(os.path.join(FONTS, "BarlowCondensed-SemiBold.ttf"))
BOLD = Font(os.path.join(FONTS, "BarlowCondensed-Bold.ttf"))

svg = []
preview = []
consts = []


def f3(v):
    return f"{v:.3f}"


def text(s, x, y, size=2.5, anchor="middle", fill=FG, font=LABEL):
    d = font.path(s, x, y, size, anchor)
    if d:
        svg.append(f'<path d="{d}" fill="{fill}"/>')


def const(name, x, y):
    consts.append(f"constexpr float {name}[2] = {{{x:.3f}f, {y:.3f}f}};")


def polar(cx, cy, r, deg):
    """Point at angle deg, clockwise from 12 o'clock."""
    a = math.radians(deg)
    return cx + r * math.sin(a), cy - r * math.cos(a)


# ================================================================ components
# Each component is (size_mm, {layer: svg body}). Layers are drawn bottom to top.
COMPONENTS = {}


def silver_defs(gid, c):
    return (
        f'<defs><linearGradient id="{gid}" gradientUnits="userSpaceOnUse" x1="{c - 2.5}" y1="{c - 5}" x2="{c + 2.5}" y2="{c + 5}">'
        '<stop offset="0" stop-color="#ffffff"/><stop offset="1" stop-color="#a2a2a2"/></linearGradient></defs>'
    )


def wavy_circle(c, r, amp, lobes, step=2):
    pts = []
    for i in range(0, 360, step):
        a = math.radians(i)
        rr = r + amp * math.cos(lobes * a)
        pts.append(f"{f3(c + rr * math.sin(a))},{f3(c - rr * math.cos(a))}")
    return "M" + " L".join(pts) + " Z"


def make_big_knob():
    s, c = 16.0, 8.0
    bg = (
        '<defs><linearGradient id="kb" gradientUnits="userSpaceOnUse" x1="0" y1="0" x2="0" y2="16">'
        '<stop offset="0" stop-color="#2e2e31"/><stop offset="1" stop-color="#040404"/></linearGradient></defs>'
        f'<circle cx="{c}" cy="{c}" r="7.95" fill="url(#kb)"/>'
    )
    fg = (
        f'<path d="{wavy_circle(c, 7.45, 0.38, 12)}" fill="#111112" stroke="#46464a" stroke-width="0.15"/>'
        f'<line x1="{c}" y1="{c - 5.7}" x2="{c}" y2="{c - 7.55}" stroke="#ffffff" stroke-width="0.6" stroke-linecap="round"/>'
    )
    rings = "".join(
        f'<circle cx="{c}" cy="{c}" r="{f3(r)}" fill="none" stroke="{"#ffffff" if i % 2 else "#000000"}" '
        f'stroke-opacity="0.08" stroke-width="0.14"/>'
        for i, r in enumerate([0.7 + 0.42 * k for k in range(11)])
    )
    cap = (
        silver_defs("kc", c)
        + f'<circle cx="{c}" cy="{c + 0.25}" r="5.75" fill="#000000" fill-opacity="0.55"/>'
        + f'<circle cx="{c}" cy="{c}" r="5.3" fill="url(#kc)" stroke="#6e6e6e" stroke-width="0.15"/>'
        + rings
        + f'<ellipse cx="{c - 1.5}" cy="{c - 1.9}" rx="2.6" ry="1.7" fill="#ffffff" fill-opacity="0.35" '
        f'transform="rotate(-35 {c - 1.5} {c - 1.9})"/>'
        + f'<path d="M{c - 4.6},{c + 2.6} A5.3,5.3 0 0 0 {c + 4.6},{c + 2.6} A6.2,6.2 0 0 1 {c - 4.6},{c + 2.6} Z" '
        'fill="#000000" fill-opacity="0.18"/>'
    )
    COMPONENTS["KnobBig"] = (s, {"bg": bg, "fg": fg, "cap": cap})


def make_small_knob():
    s, c = 8.6, 4.3
    bg = (
        f'<circle cx="{c}" cy="{c}" r="4.15" fill="none" stroke="#7d7d80" stroke-width="0.18"/>'
        f'<circle cx="{c}" cy="{c}" r="3.8" fill="#060606"/>'
    )
    fg = (
        f'<path d="{wavy_circle(c, 3.45, 0.14, 18)}" fill="#141415" stroke="#45454a" stroke-width="0.12"/>'
        f'<line x1="{c}" y1="{c - 0.8}" x2="{c}" y2="{c - 3.25}" stroke="#ffffff" stroke-width="0.5" stroke-linecap="round"/>'
    )
    COMPONENTS["KnobSmall"] = (s, {"bg": bg, "fg": fg})


def toggle_frame(pos):
    """Bat toggle seen from above: hex nut, bushing and a silver bat."""
    c, cy = 3.5, 6.0
    hexagon = " ".join(f"{f3(c + 3.0 * math.cos(math.radians(a)))},{f3(cy + 3.0 * math.sin(math.radians(a)))}" for a in range(0, 360, 60))
    body = (
        '<defs><linearGradient id="tn" gradientUnits="userSpaceOnUse" x1="0.5" y1="3" x2="6.5" y2="9">'
        '<stop offset="0" stop-color="#eeeeee"/><stop offset="1" stop-color="#7c7c7c"/></linearGradient>'
        '<linearGradient id="tb" gradientUnits="userSpaceOnUse" x1="2.7" y1="0" x2="4.3" y2="0">'
        '<stop offset="0" stop-color="#ffffff"/><stop offset="1" stop-color="#8a8a8a"/></linearGradient></defs>'
        f'<polygon points="{hexagon}" fill="url(#tn)" stroke="#4a4a4a" stroke-width="0.15"/>'
        f'<circle cx="{c}" cy="{cy}" r="1.75" fill="#b9b9b9" stroke="#5a5a5a" stroke-width="0.15"/>'
        f'<circle cx="{c}" cy="{cy}" r="1.1" fill="#3a3a3a"/>'
    )
    if pos == "mid":
        body += f'<circle cx="{c}" cy="{cy}" r="0.95" fill="url(#tb)" stroke="#555" stroke-width="0.12"/>'
    else:
        d = -1 if pos == "up" else 1
        tip = cy + d * 5.0
        body += (
            f'<polygon points="{f3(c - 0.75)},{f3(cy)} {f3(c + 0.75)},{f3(cy)} {f3(c + 0.55)},{f3(tip)} {f3(c - 0.55)},{f3(tip)}" '
            'fill="url(#tb)" stroke="#5a5a5a" stroke-width="0.12"/>'
            f'<circle cx="{c}" cy="{f3(tip)}" r="0.8" fill="url(#tb)" stroke="#5a5a5a" stroke-width="0.12"/>'
        )
    return body


def make_toggles():
    for pos in ("down", "mid", "up"):
        COMPONENTS[f"Toggle_{pos}"] = ((7.0, 12.0), {"main": toggle_frame(pos)})


def make_buttons():
    colours = {
        "grey": (("#cfcfcf", "#8f8f8f"), ("#a6a6a6", "#7a7a7a"), "#5a5a5a"),
        "red": (("#ff6b6b", "#d4232b"), ("#d4232b", "#9e161c"), "#7a1015"),
        "black": (("#333336", "#0d0d0e"), ("#1c1c1e", "#060606"), "#4a4a4e"),
    }
    for name, (up, down, rim) in colours.items():
        for state, (top, bottom), inset in (("0", up, 0.0), ("1", down, 0.25)):
            body = (
                f'<defs><linearGradient id="b{name}{state}" gradientUnits="userSpaceOnUse" x1="0" y1="0.4" x2="0" y2="7.6">'
                f'<stop offset="0" stop-color="{top}"/><stop offset="1" stop-color="{bottom}"/></linearGradient></defs>'
                f'<rect x="{0.4 + inset}" y="{0.4 + inset}" width="{7.2 - 2 * inset}" height="{7.2 - 2 * inset}" rx="1.9" '
                f'fill="url(#b{name}{state})" stroke="{rim}" stroke-width="0.25"/>'
            )
            COMPONENTS[f"Button_{name}_{state}"] = (8.0, {"main": body})


def make_jack():
    s, c = 8.6, 4.3
    hexagon = " ".join(f"{f3(c + 4.15 * math.cos(math.radians(a + 30)))},{f3(c + 4.15 * math.sin(math.radians(a + 30)))}" for a in range(0, 360, 60))
    body = (
        '<defs><linearGradient id="jn" gradientUnits="userSpaceOnUse" x1="1" y1="1" x2="7.6" y2="7.6">'
        '<stop offset="0" stop-color="#f4f4f4"/><stop offset="1" stop-color="#7e7e7e"/></linearGradient></defs>'
        f'<polygon points="{hexagon}" fill="url(#jn)" stroke="#5a5a5a" stroke-width="0.18"/>'
        f'<circle cx="{c}" cy="{c}" r="2.85" fill="#d9d9d9" stroke="#6a6a6a" stroke-width="0.2"/>'
        f'<circle cx="{c}" cy="{c}" r="2.2" fill="#6e6e6e"/>'
        f'<circle cx="{c}" cy="{c}" r="1.75" fill="#0a0a0a"/>'
    )
    COMPONENTS["Jack"] = (s, {"main": body})


make_big_knob()
make_small_knob()
make_toggles()
make_buttons()
make_jack()


def component_files():
    """One SVG file per layer, named Component[_layer].svg."""
    files = {}
    for name, (size, layers) in COMPONENTS.items():
        w, h = size if isinstance(size, tuple) else (size, size)
        for layer, body in layers.items():
            fname = name if layer == "main" else f"{name}_{layer}"
            files[fname] = (
                '<?xml version="1.0" encoding="UTF-8" standalone="no"?>\n'
                f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}mm" height="{h}mm" viewBox="0 0 {w} {h}">\n{body}\n</svg>\n'
            )
    return files


def place(name, x, y, angle=0.0, layers=None):
    """Inline a component's artwork into the preview, centred at (x, y)."""
    size, all_layers = COMPONENTS[name]
    w, h = size if isinstance(size, tuple) else (size, size)
    for layer, body in all_layers.items():
        if layers and layer not in layers:
            continue
        rot = f" rotate({f3(angle)} {w / 2} {h / 2})" if layer == "fg" else ""
        preview.append(f'<g transform="translate({f3(x - w / 2)} {f3(y - h / 2)}){rot}">{body}</g>')


# ================================================================ panel art
def wood(x0, seed):
    rnd = random.Random(seed)
    gid = f"wood{seed}"
    svg.append(
        f'<defs><linearGradient id="{gid}" gradientUnits="userSpaceOnUse" x1="{f3(x0)}" y1="0" x2="{f3(x0 + WOOD)}" y2="0">'
        f'<stop offset="0" stop-color="#63402a"/><stop offset="1" stop-color="#442817"/></linearGradient></defs>'
        f'<rect x="{f3(x0)}" y="0" width="{f3(WOOD)}" height="{H}" fill="url(#{gid})"/>'
    )
    for _ in range(22):
        x = x0 + rnd.uniform(0.3, WOOD - 0.3)
        amp, period, phase = rnd.uniform(0.15, 0.7), rnd.uniform(18, 45), rnd.uniform(0, 6.3)
        pts = []
        for i in range(0, 130, 2):
            xx = min(max(x + amp * math.sin(i / period * 6.283 + phase), x0 + 0.1), x0 + WOOD - 0.1)
            pts.append(f"{f3(xx)},{i}")
        dark = rnd.random() < 0.7
        svg.append(
            f'<polyline points="{" ".join(pts)}" fill="none" stroke="{"#2a160b" if dark else "#86573a"}" '
            f'stroke-opacity="{f3(rnd.uniform(0.25, 0.6))}" stroke-width="{f3(rnd.uniform(0.12, 0.45))}"/>'
        )
    edge = x0 + WOOD if x0 == 0 else x0
    svg.append(f'<line x1="{f3(edge)}" y1="0" x2="{f3(edge)}" y2="{H}" stroke="#000" stroke-opacity="0.6" stroke-width="0.4"/>')


def tick_scale(cx, cy, ticks=21):
    for i in range(ticks):
        deg = -SWEEP + 2 * SWEEP * i / (ticks - 1)
        outer = 11.4 if i % 2 == 0 else 10.4
        x1, y1 = polar(cx, cy, 8.9, deg)
        x2, y2 = polar(cx, cy, outer, deg)
        svg.append(f'<line x1="{f3(x1)}" y1="{f3(y1)}" x2="{f3(x2)}" y2="{f3(y2)}" stroke="{FG}" stroke-width="0.32"/>')


def ring_scale(cx, cy, r=10.2):
    x1, y1 = polar(cx, cy, r, -SWEEP)
    x2, y2 = polar(cx, cy, r, SWEEP)
    svg.append(f'<path d="M{f3(x1)},{f3(y1)} A{r},{r} 0 1 1 {f3(x2)},{f3(y2)}" fill="none" stroke="{FG}" stroke-width="0.4"/>')
    for deg in (-SWEEP, SWEEP):
        a, b = polar(cx, cy, r - 0.9, deg)
        c, d = polar(cx, cy, r + 0.9, deg)
        svg.append(f'<line x1="{f3(a)}" y1="{f3(b)}" x2="{f3(c)}" y2="{f3(d)}" stroke="{FG}" stroke-width="0.4"/>')


def scale_text(cx, cy, r, deg, s, size=1.9):
    x, y = polar(cx, cy, r, deg)
    text(s, x, y + size * 0.35, size)


def wave_icon(kind, x, y):
    if kind == "square":
        d = f"M{f3(x - 1.5)},{f3(y + 0.6)} h0.75 v-1.2 h1.5 v1.2 h0.75"
    else:
        d = f"M{f3(x - 1.1)},{f3(y + 0.55)} L{f3(x)},{f3(y - 0.55)} L{f3(x + 1.1)},{f3(y + 0.55)}"
    svg.append(f'<path d="{d}" fill="none" stroke="{FG}" stroke-width="0.32" stroke-linejoin="round"/>')


def tag(s, x, y, size=1.95):
    """White tag with dark text, as the hardware marks its outputs."""
    w = LABEL.width(s, size) + 1.1
    svg.append(f'<rect x="{f3(x - w / 2)}" y="{f3(y - size * 0.86)}" width="{f3(w)}" height="{f3(size * 1.05)}" rx="0.3" fill="{FG}"/>')
    text(s, x, y, size, fill=OUT_FG)


# ---------------------------------------------------------------- background
svg.append(f'<rect x="{f3(OX)}" y="0" width="{f3(PANEL)}" height="{H}" fill="{BG}"/>')
wood(0, 1)
wood(OX + PANEL, 2)

ROW1, ROW2 = 22.0, 53.5
LABEL_DY = 12.6  # big knob label baseline above centre


def big_knob(name, x, y, lab, scale="ticks", extra=None, angle=0.0):
    x += OX
    const(name, x, y)
    if scale == "ticks":
        tick_scale(x, y)
    else:
        ring_scale(x, y)
    if extra == "bipolar":
        scale_text(x, y, 13.4, -SWEEP - 3, "(-)")
        scale_text(x, y, 13.4, SWEEP + 3, "(+)")
    elif extra == "cutoff":
        for deg, s in ((-SWEEP, "20Hz"), (-SWEEP / 3, "200Hz"), (SWEEP / 3, "2kHz"), (SWEEP, "20kHz")):
            scale_text(x, y, 14.2, deg, s, 1.8)
    text(lab, x, y - LABEL_DY)
    place("KnobBig", x, y, angle)


def small_knob(name, x, y, lab=None, angle=0.0):
    x += OX
    const(name, x, y)
    if lab:
        text(lab, x, y - 5.9)
    place("KnobSmall", x, y, angle)


def toggle(name, x, y, lab, top, bottom, frame="up", off_bracket=False):
    x += OX
    const(name, x, y)
    text(lab, x, ROW1 - LABEL_DY if y < 40 else ROW2 - LABEL_DY)
    for leg, ly in ((top, y - 6.6), (bottom, y + 8.3)):
        if leg in ("square", "triangle"):
            wave_icon(leg, x, ly - 0.6)
        else:
            text(leg, x, ly, 1.9)
    if off_bracket:
        svg.append(f'<path d="M{f3(x + 3.6)},{f3(y - 4.2)} h0.9 v8.4 h-0.9" fill="none" stroke="{FG}" stroke-width="0.28"/>')
        text("OFF", x + 5.2, y + 0.7, 1.9, anchor="start")
    place(f"Toggle_{frame}", x, y)


def button(name, x, y, colour, lab=None):
    x += OX
    const(name, x, y)
    if lab:
        text(lab, x, y - 5.4)
    place(f"Button_{colour}_0", x, y)


# ---------------------------------------------------------------- VCOs
big_knob("VCO_DECAY", 18.5, ROW1, "VCO DECAY", angle=-60)
toggle("SEQ_PITCH_MOD", 41.0, ROW1 + 0.5, "SEQ PITCH MOD", "VCO 1&2", "VCO 2", frame="up", off_bracket=True)
big_knob("VCO1_EG_AMT", 61.0, ROW1, "VCO 1 EG AMOUNT", extra="bipolar", angle=40)
big_knob("VCO1_FREQ", 90.2, ROW1, "VCO 1 FREQUENCY", scale="ring", angle=-75)
toggle("VCO1_WAVE", 112.0, ROW1 + 0.5, "VCO 1 WAVE", "square", "triangle", frame="down")

big_knob("FM_AMT", 18.5, ROW2, "1-2 FM AMOUNT", angle=-149)
toggle("HARD_SYNC", 41.0, ROW2, "HARD SYNC", "ON", "OFF", frame="down")
big_knob("VCO2_EG_AMT", 61.0, ROW2, "VCO 2 EG AMOUNT", extra="bipolar")
big_knob("VCO2_FREQ", 90.2, ROW2, "VCO 2 FREQUENCY", scale="ring")
toggle("VCO2_WAVE", 112.0, ROW2, "VCO 2 WAVE", "square", "triangle", frame="up")

# ---------------------------------------------------------------- mixer
small_knob("VCO1_LEVEL", 131.5, 18.0, "VCO 1 LEVEL", angle=90)
small_knob("NOISE_LEVEL", 131.5, 38.5, "NOISE / EXT LEVEL", angle=-149)
small_knob("VCO2_LEVEL", 131.5, 58.5, "VCO 2 LEVEL", angle=-149)

# ---------------------------------------------------------------- VCF and VCA
toggle("VCF_MODE", 151.0, ROW1 + 0.5, "VCF", "HP", "LP", frame="down")
big_knob("CUTOFF", 173.3, ROW1, "CUTOFF", extra="cutoff", angle=15)
big_knob("RESONANCE", 202.0, ROW1, "RESONANCE", angle=-120)
# VCA EG is FAST on top; the module keeps 0 = fast, so its switch frames are flipped.
toggle("VCA_ATTACK", 223.5, ROW1 + 0.5, "VCA EG", "FAST", "SLOW", frame="up")
big_knob("VOLUME", 247.0, ROW1, "VOLUME", angle=60)

big_knob("VCF_DECAY", 157.5, ROW2, "VCF DECAY", angle=-30)
big_knob("VCF_EG_AMT", 187.0, ROW2, "VCF EG AMOUNT", extra="bipolar", angle=60)
big_knob("NOISE_VCF_MOD", 216.5, ROW2, "NOISE / VCF MOD", angle=-149)
big_knob("VCA_DECAY", 247.0, ROW2, "VCA DECAY", angle=-30)

# ---------------------------------------------------------------- clock
big_knob("TEMPO", 43.5, 85.0, "TEMPO", scale="ring", angle=-40)
button("TRIGGER_BUTTON", 17.5, 84.5, "grey", "TRIGGER")
const("RUN_LIGHT", OX + 25.5, 98.5)
preview.append(f'<circle cx="{f3(OX + 25.5)}" cy="98.5" r="1.3" fill="#ff3030"/>')
text("RUN / STOP", OX + 28.3, 99.3, anchor="start")
button("RUN", 35.0, 106.5, "red")
button("ADVANCE_BUTTON", 53.8, 106.5, "black", "ADVANCE")

# ---------------------------------------------------------------- sequencer
PITCH_Y, LED_Y, VEL_Y = 82.0, 93.5, 105.5
xs = [OX + 79.4 + 24.07 * i for i in range(8)]
text("PITCH", OX + 73.6, PITCH_Y + 0.9, anchor="end")
text("VELOCITY", OX + 73.6, VEL_Y + 0.9, anchor="end")
pitch_pos, vel_pos, led_pos = [], [], []
for i, x in enumerate(xs):
    text(str(i + 1), x, PITCH_Y - 6.2, 2.8, font=BOLD)
    pitch_pos.append((x, PITCH_Y))
    led_pos.append((x, LED_Y))
    vel_pos.append((x, VEL_Y))
    place("KnobSmall", x, PITCH_Y, -149 + 37 * i)
    place("KnobSmall", x, VEL_Y, 110 - 30 * i)
    preview.append(f'<circle cx="{f3(x)}" cy="{LED_Y}" r="1.3" fill="{"#ff3030" if i == 2 else "#5a1e22"}"/>')

# ---------------------------------------------------------------- bottom strip
# Rack's CPU meter covers the bottom 20 px (6.8 mm) of every module, so nothing
# that matters goes below CLEAR_Y.
CLEAR_Y = H - 6.8
x = OX + 19.6
text(WORDMARK, x, 120.6, 8.6, anchor="start", font=BOLD)
x += BOLD.width(WORDMARK, 8.6) + 2.2
for lines in (("DRUMMER FROM", "ANOTHER MODULE"), ("SEMI-MODULAR ANALOG", "PERCUSSION SYNTHESIZER")):
    svg.append(f'<line x1="{f3(x)}" y1="114.0" x2="{f3(x)}" y2="120.6" stroke="{FG}" stroke-width="0.35"/>')
    x += 1.8
    text(lines[0], x, 117.0, 2.9, anchor="start", font=BOLD)
    text(lines[1], x, 120.4, 2.9, anchor="start", font=BOLD)
    x += max(BOLD.width(s, 2.9) for s in lines) + 2.2

# ---------------------------------------------------------------- patch bay
PB_X = [OX + 266.0, OX + 280.0, OX + 294.0]
PB_Y = [15.6 + 13.0 * i for i in range(8)]
in_w = LABEL.width("IN / ", 2.2)
out_w = LABEL.width("OUT", 2.2) + 1.1
hx = OX + 280.0 - (in_w + out_w) / 2
text("IN / ", hx, 6.3, 2.2, anchor="start")
tag("OUT", hx + in_w + out_w / 2, 6.3, 2.2)
patch_bay = [
    [("in", "TRIGGER_INPUT", "TRIGGER"), ("in", "VCA_CV_INPUT", "VCA CV"), ("out", "VCA_OUTPUT", "VCA")],
    [("in", "VELOCITY_INPUT", "VELOCITY"), ("in", "VCA_DECAY_INPUT", "VCA DECAY"), ("out", "VCA_EG_OUTPUT", "VCA EG")],
    [("in", "EXT_AUDIO_INPUT", "EXT AUDIO"), ("in", "VCF_DECAY_INPUT", "VCF DECAY"), ("out", "VCF_EG_OUTPUT", "VCF EG")],
    [("in", "NOISE_LEVEL_INPUT", "NOISE LEVEL"), ("in", "VCO_DECAY_INPUT", "VCO DECAY"), ("out", "VCO_EG_OUTPUT", "VCO EG")],
    [("in", "VCF_MOD_INPUT", "VCF MOD"), ("in", "VCO1_CV_INPUT", "VCO 1 CV"), ("out", "VCO1_OUTPUT", "VCO 1")],
    [("in", "FM_AMT_INPUT", "1-2 FM AMT"), ("in", "VCO2_CV_INPUT", "VCO 2 CV"), ("out", "VCO2_OUTPUT", "VCO 2")],
    [("in", "TEMPO_INPUT", "TEMPO"), ("in", "RUN_STOP_INPUT", "RUN / STOP"), ("in", "ADV_CLOCK_INPUT", "ADV / CLOCK")],
    [("out", "TRIGGER_OUTPUT", "TRIGGER"), ("out", "VELOCITY_OUTPUT", "VELOCITY"), ("out", "PITCH_OUTPUT", "PITCH")],
]
for r, row in enumerate(patch_bay):
    for c, (io, name, lab) in enumerate(row):
        x, y = PB_X[c], PB_Y[r]
        const(name, x, y)
        if io == "out":
            tag(lab, x, y - 5.2)
        else:
            text(lab, x, y - 5.2, 1.95)
        place("Jack", x, y)

# RESET isn't on the hardware; it sits below the patch bay.
RESET_Y = 116.5
assert RESET_Y + 4.3 < CLEAR_Y
const("RESET_INPUT", PB_X[0], RESET_Y)
text("RESET", PB_X[0] + 5.6, RESET_Y + 0.7, 1.95, anchor="start")
text("(RACK EXTRA)", PB_X[0] + 5.6, RESET_Y + 3.2, 1.6, anchor="start")
place("Jack", PB_X[0], RESET_Y)

# Screw centres, from the hardware.
SCREWS = [(OX + 17, 3.2), (OX + 114, 3.2), (OX + 188, 3.2), (OX + 299, 3.2),
          (OX + 8.5, 125.3), (OX + 113, 125.3), (OX + 191, 125.3), (OX + 296, 125.3)]
for x, y in SCREWS:
    preview.append(f'<circle cx="{f3(x)}" cy="{y}" r="2.0" fill="#0c0c0c" stroke="#333" stroke-width="0.2"/>')


# ================================================================ write
def write_svg(path, body, w=W, h=H):
    with open(path, "w") as f:
        f.write(
            '<?xml version="1.0" encoding="UTF-8" standalone="no"?>\n'
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{w:.2f}mm" height="{h}mm" viewBox="0 0 {w:.2f} {h}">\n'
            f"{body}\n</svg>\n"
        )


def arr(name, pts):
    inner = ", ".join(f"{{{x:.3f}f, {y:.3f}f}}" for x, y in pts)
    return f"constexpr float {name}[8][2] = {{{inner}}};"


write_svg(os.path.join(ROOT, "res", "DrummerFromAnotherModule.svg"), "\n".join(svg))
comp_dir = os.path.join(ROOT, "res", "components")
os.makedirs(comp_dir, exist_ok=True)
for fname, content in component_files().items():
    with open(os.path.join(comp_dir, fname + ".svg"), "w") as f:
        f.write(content)

screws = ", ".join(f"{{{x:.3f}f, {y:.3f}f}}" for x, y in SCREWS)
with open(os.path.join(ROOT, "src", "layout.hpp"), "w") as f:
    f.write("#pragma once\n// Generated by tools/make_panel.py. Do not edit; change the script and rerun it.\n")
    f.write("// Positions are widget centres in mm.\n\nnamespace layout {\n\n")
    f.write("\n".join(consts + [arr("STEP_LIGHT", led_pos), arr("PITCH_KNOB", pitch_pos), arr("VEL_KNOB", vel_pos)]))
    f.write(f"\nconstexpr float SCREW[{len(SCREWS)}][2] = {{{screws}}};")
    f.write("\n\n} // namespace layout\n")

if "--preview" in sys.argv:
    os.makedirs(os.path.join(ROOT, "build"), exist_ok=True)
    write_svg(os.path.join(ROOT, "build", "panel-preview.svg"), "\n".join(svg + preview))
print(f"wrote res/DrummerFromAnotherModule.svg ({W / 5.08:.0f}HP), {len(component_files())} component SVGs, src/layout.hpp")
