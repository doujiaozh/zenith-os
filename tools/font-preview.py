#!/usr/bin/env python3
"""ZenithOS 5x7 font preview / proof chart.

Parses the true glyph bytes out of src/ui/font.asm (the label `font_5x7:` plus
its `db` payload) and renders screenshots/font-preview.png:

  * every printable ASCII glyph (32..126) at scale 6 with its decimal code
  * a "critical pairs" strip for glyphs that are easy to confuse
  * the HUD sample banner lines rendered with the same font

The bitmap font itself is never duplicated here: this tool is a *reader* of
the kernel artifact. Pixels are drawn as solid rectangles (no antialiasing).

Usage:
    python tools/font-preview.py

Requirements: Python 3.8+, Pillow. No other dependencies.
"""

import re
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw
except ImportError:  # pragma: no cover
    sys.exit("Pillow is required: python -m pip install pillow")

# ---------------------------------------------------------------------------
# ABI constants (must match src/ui/font.asm)
# ---------------------------------------------------------------------------
FONT_W = 5
FONT_H = 7
FONT_FIRST = 32
FONT_LAST = 126
GLYPHS = FONT_LAST - FONT_FIRST + 1          # 95
BYTES = GLYPHS * FONT_W                      # 475

ROOT = Path(__file__).resolve().parent.parent
ASM_PATH = ROOT / "src" / "ui" / "font.asm"
OUT_PATH = ROOT / "screenshots" / "font-preview.png"

# ---------------------------------------------------------------------------
# Colors (dark HUD theme, white-on-dark)
# ---------------------------------------------------------------------------
BG = (7, 11, 15)
FG = (236, 243, 248)
ACCENT = (64, 208, 255)
DIM = (110, 138, 156)
LABEL = (150, 178, 196)
GRID = (22, 33, 41)
BASE = (40, 74, 90)


# ---------------------------------------------------------------------------
# Parser: read the glyph table straight out of the NASM source
# ---------------------------------------------------------------------------
def parse_font(path):
    """Return a list of 95 column-major glyphs (each a tuple of 5 bytes)."""
    try:
        text = path.read_text(encoding="utf-8")
    except FileNotFoundError:
        sys.exit("missing font source: %s" % path)

    lines = text.splitlines()
    label_line = None
    for i, line in enumerate(lines):
        code_part = line.split(";", 1)[0]
        if re.match(r"^\s*font_5x7\s*:", code_part, re.IGNORECASE):
            label_line = i
            break
    if label_line is None:
        sys.exit("could not find 'font_5x7:' label in %s" % path)

    payload = []
    for line in lines[label_line + 1:]:
        code_part = line.split(";", 1)[0].strip()
        if not code_part:
            continue
        if re.match(r"^%endif\b", code_part, re.IGNORECASE):
            break
        m = re.match(r"^db\b(.*)$", code_part, re.IGNORECASE)
        if not m:
            continue
        for token in m.group(1).split(","):
            token = token.strip()
            if not token:
                continue
            if token.lower().startswith("0x"):
                value = int(token, 16)
            elif token.startswith("$"):
                value = int(token[1:], 16)
            elif re.fullmatch(r"[0-9]+", token):
                value = int(token, 10)
            elif re.fullmatch(r"[0-9A-Fa-f]{1,2}h", token):
                value = int(token[:-1], 16)
            else:
                sys.exit("unparsable db token in font.asm: %r" % token)
            if not 0 <= value <= 0xFF:
                sys.exit("db value out of range: %r" % token)
            payload.append(value)

    if len(payload) != BYTES:
        sys.exit(
            "font.asm payload is %d bytes, expected %d "
            "(%d glyphs x %d columns)" % (len(payload), BYTES, GLYPHS, FONT_W)
        )

    # The ABI uses bit0..bit6 only (7 rows); bit7 must stay clear.
    for i, value in enumerate(payload):
        if value & 0x80:
            sys.exit(
                "font.asm byte %d (glyph %d, code %d) sets bit7; "
                "only bits 0..6 are valid" % (i, i // FONT_W,
                                              FONT_FIRST + i // FONT_W)
            )

    glyphs = []
    for i in range(GLYPHS):
        glyphs.append(tuple(payload[i * FONT_W:(i + 1) * FONT_W]))
    return glyphs


# ---------------------------------------------------------------------------
# Tiny drawing helpers -- every pixel is an integer rectangle (crisp)
# ---------------------------------------------------------------------------
class Canvas:
    def __init__(self, width, height, glyphs):
        self.glyphs = glyphs
        self.img = Image.new("RGB", (width, height), BG)
        self.d = ImageDraw.Draw(self.img)

    def pixels(self, x, y, w, h, color):
        if w <= 0 or h <= 0:
            return
        self.d.rectangle([x, y, x + w - 1, y + h - 1], fill=color)

    def glyph(self, x, y, code, scale, color):
        """Draw one glyph. (x, y) is the top-left of the 5x7 cell."""
        if not (FONT_FIRST <= code <= FONT_LAST):
            return
        cols = self.glyphs[code - FONT_FIRST]
        for cx, column in enumerate(cols):
            for row in range(FONT_H):
                if column & (1 << row):
                    self.pixels(x + cx * scale, y + row * scale, scale, scale, color)

    def text_width(self, text, scale):
        return len(text) * (FONT_W + 1) * scale

    def text(self, x, y, text, scale, color):
        """Draw ASCII text with the bitmap font (advance = 5 + 1 px per cell)."""
        advance = (FONT_W + 1) * scale
        for ch in text:
            self.glyph(x, y, ord(ch), scale, color)
            x += advance
        return x

    def text_centered(self, cx, y, text, scale, color):
        x = cx - self.text_width(text, scale) // 2
        self.text(x, y, text, scale, color)

    def hline(self, x0, x1, y, color):
        if x1 <= x0:
            return
        self.d.rectangle([x0, y, x1 - 1, y], fill=color)


# ---------------------------------------------------------------------------
# Sample content (single source of truth for the rendered proof)
# ---------------------------------------------------------------------------
TITLE = "ZENITHOS 5x7 FONT"
SUBTITLE = ("ASCII 32..126  -  95 GLYPHS  -  475 BYTES  -  "
            "COL-BIT: BIT0=TOP ROW")
BANNER_HEADING = "HUD SAMPLE BANNER (SAME FONT, SCALE 2)"
BANNER_LINES = [
    "ZENITHOS // NEURAL CORE",
    "W/S ZOOM  A/D ROTATE  Q/E PITCH  R RESET",
    "FPS 60  T+00:01:23  SNN 24N 40E SPIKE 128/s  PMM 29.7MB FREE",
    "0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz "
    ".,:;!?[](){}<>/\\-_+=*#%@&|^~'\"`",
]
BANNER_SCALE = 2
BANNER_LINE_H = 24

ZOOM_HEADING = "CRITICAL PAIRS (SCALE 4)"
ZOOM_TEXT = "0O 1Il 2Z 5S 8B ., :; -_ <> [] {} @& %#"


def build_chart(glyphs):
    # ---- geometry ---------------------------------------------------------
    margin = 24
    cols = 16
    rows = (GLYPHS + cols - 1) // cols           # 6 rows
    cell_w = 71                                  # 16 * 71 = 1136
    cell_h = 84
    glyph_scale = 6                              # required: scale 6
    chart_w = cols * cell_w
    chart_top = 96
    chart_bottom = chart_top + rows * cell_h

    banner_scale = BANNER_SCALE
    banner_line_h = BANNER_LINE_H
    banner_lines = BANNER_LINES
    banner_heading = BANNER_HEADING
    line1 = TITLE
    line2 = SUBTITLE
    zoom_heading = ZOOM_HEADING
    zoom_text = ZOOM_TEXT

    # ---- width / height ---------------------------------------------------
    banner_w = max(canvas_width_hint(s, banner_scale) for s in banner_lines)
    header_w = max(canvas_width_hint(line1, 3), canvas_width_hint(line2, 2))
    zoom_scale = 4
    while canvas_width_hint(zoom_text, zoom_scale) > max(chart_w, banner_w) and zoom_scale > 1:
        zoom_scale -= 1
    zoom_w = canvas_width_hint(zoom_text, zoom_scale)
    content_w = max(chart_w, banner_w, header_w, zoom_w)

    banner_top = chart_bottom + 30
    zoom_top = banner_top + 24 + banner_line_h * len(banner_lines) + 34
    footer_top = zoom_top + 22 + FONT_H * zoom_scale + 26
    width = content_w + 2 * margin
    height = footer_top + 7 * 2 + margin

    c = Canvas(width, height, glyphs)
    right = margin + content_w

    # ---- header -----------------------------------------------------------
    c.text(margin, 16, line1, 3, ACCENT)
    x = margin + c.text_width(line1, 3) + 18
    if x + c.text_width("ASCII 32..126", 2) < right:
        c.text(x, 28, "ASCII 32..126", 2, DIM)
    c.text(margin, 44, line2, 2, DIM)
    c.hline(margin, right, 64, GRID)

    # ---- glyph chart ------------------------------------------------------
    c.text(margin, chart_top - 22,
           "GLYPH CHART (SCALE 6) - DECIMAL ASCII CODE BELOW EACH GLYPH",
           2, ACCENT)
    for col in range(cols + 1):
        c.d.rectangle([margin + col * cell_w, chart_top, margin + col * cell_w,
                       chart_bottom - 1], fill=GRID)
    for row in range(rows):
        for col in range(cols):
            idx = row * cols + col
            if idx >= GLYPHS:
                break
            code = FONT_FIRST + idx
            cx = margin + col * cell_w
            cy = chart_top + row * cell_h
            gx = cx + (cell_w - FONT_W * glyph_scale) // 2
            gy = cy + 8
            c.glyph(gx, gy, code, glyph_scale, FG)
            # baseline guide (bottom of row 6) -- shows every glyph shares it
            c.hline(cx + 6, cx + cell_w - 6, gy + FONT_H * glyph_scale + 1, BASE)
            c.text_centered(cx + cell_w // 2, gy + FONT_H * glyph_scale + 6,
                            str(code), 2, LABEL)

    # ---- banner -----------------------------------------------------------
    c.hline(margin, right, banner_top - 12, GRID)
    c.text(margin, banner_top - 2, banner_heading, 2, ACCENT)
    by = banner_top + 24
    for i, text in enumerate(banner_lines):
        color = ACCENT if i == 0 else FG
        c.text(margin, by, text, banner_scale, color)
        by += banner_line_h

    # ---- critical pairs ---------------------------------------------------
    c.hline(margin, right, zoom_top - 12, GRID)
    c.text(margin, zoom_top - 2, zoom_heading, 2, ACCENT)
    c.text(margin, zoom_top + 22, zoom_text, zoom_scale, FG)

    # ---- footer -----------------------------------------------------------
    c.text(margin, footer_top,
           "PARSED FROM src/ui/font.asm - font_5x7: %d GLYPHS / %d BYTES"
           % (GLYPHS, BYTES), 2, DIM)
    c.text(margin, footer_top + 18,
           "WHITE-ON-DARK, NO ANTIALIASING, SCALE 6 GLYPHS", 2, DIM)

    return c.img


def canvas_width_hint(text, scale):
    return len(text) * (FONT_W + 1) * scale


def main():
    if sys.version_info < (3, 8):
        sys.exit("Python 3.8+ required")

    glyphs = parse_font(ASM_PATH)

    # every character in the sample banner must be covered by the table
    for line in BANNER_LINES:
        for ch in line:
            if not (FONT_FIRST <= ord(ch) <= FONT_LAST):
                sys.exit("banner uses character outside font range: %r" % ch)

    img = build_chart(glyphs)
    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    img.save(OUT_PATH, "PNG", optimize=True)

    print("parsed: %s" % ASM_PATH)
    print("font_5x7: %d glyphs x %d bytes = %d data bytes"
          % (GLYPHS, FONT_W, BYTES))
    print("banner:  %d sample lines, all characters in ASCII 32..126"
          % len(BANNER_LINES))
    print("wrote:   %s (%dx%d)" % (OUT_PATH, img.width, img.height))


if __name__ == "__main__":
    main()
