#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Draws the characters (the three synth tracks', the drummer, the wizard of the FX pages) and writes firmware/src/op_sprites.h: each pose a 112 x 104
picture (the drummer's smaller), 4 bits a pixel (a colour of OP_SPAL in ui_op.c), kept in flash.
  tools/gen_sprites.py [PREVIEW.png]     (the preview: every pose, 4x)
The pictures are made here, by code (ellipses, thick lines, arcs): original drawings, no artwork copied.
Needs Pillow."""
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw

W, H = 112, 104
T, INK, DIM, WHITE, COL, GREEN, ORANGE, BLUE, BLACK, SKIN = range(10)   # 0 = see-through; COL: the track's colour
RGB = {T: (12, 12, 14), INK: (142, 134, 201), DIM: (58, 50, 112), WHITE: (236, 236, 240), COL: (255, 0, 255),
       GREEN: (30, 204, 112), ORANGE: (255, 98, 26), BLUE: (40, 124, 255), BLACK: (0, 0, 0), SKIN: (92, 84, 150)}
OUT = Path(__file__).resolve().parent.parent / "firmware/src/op_sprites.h"


class Pic:
    def __init__(self, w=W, h=H):
        self.im = Image.new("P", (w, h), T)
        self.d = ImageDraw.Draw(self.im)

    def ell(self, cx, cy, rx, ry, fill=BLACK, out=INK, w=2):
        if out is not None:
            self.d.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=out)
            if fill is not None:
                self.d.ellipse((cx - rx + w, cy - ry + w, cx + rx - w, cy + ry - w), fill=fill)
        else:
            self.d.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=fill)

    def blob(self, parts, fill=BLACK, out=INK, w=2):          # ellipses that melt into one outline
        for cx, cy, rx, ry in parts:
            self.d.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=out)
        for cx, cy, rx, ry in parts:
            self.d.ellipse((cx - rx + w, cy - ry + w, cx + rx - w, cy + ry - w), fill=fill)

    def line(self, pts, c=INK, w=1):
        self.d.line(pts, fill=c, width=w)

    def tube(self, pts, w, fill=BLACK, out=INK, e=2):         # a limb: a thick line with an outline
        for c, ww in ((out, w + 2 * e), (fill, w)):
            self.d.line(pts, fill=c, width=ww)
            for x, y in pts:
                r = ww / 2
                self.d.ellipse((x - r, y - r, x + r, y + r), fill=c)

    def arc(self, cx, cy, rx, ry, a0, a1, c=INK, w=1):        # degrees, clockwise from 3 o'clock
        self.d.arc((cx - rx, cy - ry, cx + rx, cy + ry), a0, a1, fill=c, width=w)

    def dot(self, x, y, r, c):
        self.d.ellipse((x - r, y - r, x + r, y + r), fill=c)

    def poly(self, pts, fill=BLACK, out=INK):
        self.d.polygon(pts, fill=fill, outline=out)


def hand(p, x, y, r=5, fingers=None):
    p.ell(x, y, r, r, SKIN, INK, 1)
    for fx, fy in fingers or []:
        p.line([(x, y), (x + fx, y + fy)], INK, 2)


def buddha(lean):
    """The laughing Buddha as a sumo: bald, long ears, a wide grin, an open robe, beads, the belly. lean: he
    leans to the right of the picture, the left foot in the air."""
    p = Pic()
    dx = 7 if lean else 0
    cx = 56 + dx
    # legs first (behind the belly): a wide squat
    if lean:
        p.tube([(cx - 16, 80), (16, 66), (8, 82)], 13, SKIN)                 # the left leg: up, out to the side
        p.ell(6, 87, 9, 5, SKIN)
        p.tube([(cx + 16, 82), (96, 86), (95, 97)], 13, SKIN)
        p.ell(97, 100, 10, 4, SKIN)
    else:
        p.tube([(cx - 16, 82), (19, 85), (20, 97)], 13, SKIN)
        p.ell(18, 100, 10, 4, SKIN)
        p.tube([(cx + 16, 82), (93, 85), (92, 97)], 13, SKIN)
        p.ell(94, 100, 10, 4, SKIN)
    # the robe: open, off the shoulders, behind the body
    p.poly([(cx - 30, 40), (cx - 40, 78), (cx - 26, 90), (cx + 26, 90), (cx + 40, 78), (cx + 30, 40)], COL, INK)
    # the body: chest and belly as one
    p.blob([(cx, 47, 25, 15), (cx, 67, 30, 24)], SKIN)
    p.arc(cx - 11, 46, 11, 8, 20, 160)                                        # the chest
    p.arc(cx + 11, 46, 11, 8, 20, 160)
    p.arc(cx, 66, 22, 19, 25, 155)                                            # the belly's curve
    p.dot(cx, 72, 2, INK)                                                     # the navel
    p.arc(cx, 72, 5, 4, 200, 340)
    p.d.chord((cx - 31, 62, cx + 31, 93), 22, 158, fill=COL, outline=INK)     # the belt, its knot
    p.d.rectangle((cx - 5, 84, cx + 5, 97), fill=COL, outline=INK)
    p.line([(cx, 86), (cx, 96)], INK)
    # the robe's edges over the shoulders
    p.line([(cx - 22, 38), (cx - 30, 60), (cx - 27, 82)], COL, 3)
    p.line([(cx + 22, 38), (cx + 30, 60), (cx + 27, 82)], COL, 3)
    # the arms: fists on the thighs (leaning: one arm out for balance)
    if lean:
        p.tube([(cx - 25, 44), (cx - 40, 54), (cx - 48, 44)], 9, SKIN)
        hand(p, cx - 50, 40, 5)
    else:
        p.tube([(cx - 25, 44), (cx - 38, 60), (cx - 30, 76)], 9, SKIN)
        hand(p, cx - 29, 79, 5)
    p.tube([(cx + 25, 44), (cx + 38, 60), (cx + 30, 76)], 9, SKIN)
    hand(p, cx + 29, 79, 5)
    # the beads
    for i in range(9):
        a = math.radians(20 + i * 17.5)
        p.dot(cx + 17 * math.cos(a), 36 + 13 * math.sin(a), 2, ORANGE)
    # the head: bald and round, the ears' long lobes
    hx = cx + (3 if lean else 0)
    p.ell(hx - 17, 25, 4, 10, SKIN)
    p.ell(hx + 17, 25, 4, 10, SKIN)
    p.ell(hx, 20, 16, 17, SKIN)
    p.arc(hx, 12, 9, 5, 200, 340)                                             # the shine of the bald head
    p.dot(hx, 12, 1, COL)                                                     # the mark on the forehead
    p.arc(hx - 7, 18, 5, 4, 200, 340, WHITE, 2)                               # eyes shut, laughing
    p.arc(hx + 7, 18, 5, 4, 200, 340, WHITE, 2)
    p.arc(hx - 7, 13, 6, 3, 200, 340)                                         # the brows
    p.arc(hx + 7, 13, 6, 3, 200, 340)
    p.dot(hx, 22, 1, INK)
    p.d.chord((hx - 9, 18, hx + 9, 33), 10, 170, fill=WHITE, outline=INK)     # the grin
    p.line([(hx - 6, 28), (hx + 6, 28)], INK)
    p.arc(hx - 11, 25, 3, 3, 90, 270)                                         # the cheeks
    p.arc(hx + 11, 25, 3, 3, 270, 90)
    return p.im


def gorilla(up):
    """The gorilla at its congas (the drums are the scene's): a high skull, a heavy brow, a wide muzzle, long
    thick arms. up: the left hand in the air, open; else both hands down on the drums."""
    p = Pic()
    cx = 56
    p.ell(40, 100, 11, 4, SKIN)                                               # the feet
    p.ell(72, 100, 11, 4, SKIN)
    p.tube([(44, 84), (40, 97)], 11, SKIN)
    p.tube([(68, 84), (72, 97)], 11, SKIN)
    p.blob([(cx, 56, 31, 18), (cx, 72, 25, 22)], SKIN)                        # the shoulders and the body
    p.ell(cx, 70, 15, 16, BLACK, COL, 2)                                      # the chest
    p.arc(cx - 7, 62, 7, 6, 20, 160, COL)
    p.arc(cx + 7, 62, 7, 6, 20, 160, COL)
    p.dot(cx, 80, 1, COL)
    for x, y, ddx, ddy in ((27, 46, -4, -4), (32, 42, -3, -5), (85, 46, 4, -4), (80, 42, 3, -5),
                           (30, 78, -4, 3), (82, 78, 4, 3)):                 # fur
        p.line([(x, y), (x + ddx, y + ddy)], INK, 1)
    # the arms
    if up:
        p.tube([(28, 54), (11, 44), (14, 22)], 13, SKIN)
        hand(p, 14, 15, 7, [(-7, -6), (-3, -9), (2, -9), (7, -5)])
    else:
        p.tube([(28, 54), (9, 60), (18, 70)], 13, SKIN)
        hand(p, 21, 74, 7, [(-2, 8), (3, 8), (7, 6)])
    p.tube([(84, 54), (103, 60), (94, 70)], 13, SKIN)
    hand(p, 91, 74, 7, [(2, 8), (-3, 8), (-7, 6)])
    # the head
    p.ell(cx - 19, 26, 4, 5, SKIN)                                            # ears
    p.ell(cx + 19, 26, 4, 5, SKIN)
    p.blob([(cx, 26, 18, 17), (cx, 15, 12, 13)], SKIN)                        # the skull, its crest
    for i in range(-2, 3):                                                    # the hair on top
        p.line([(cx + i * 4, 4), (cx + i * 5, 0)], INK, 1)
    p.d.rounded_rectangle((cx - 15, 17, cx + 15, 23), 3, fill=INK)            # the brow
    p.ell(cx - 7, 26, 4, 3, WHITE, INK, 1)                                    # the eyes
    p.ell(cx + 7, 26, 4, 3, WHITE, INK, 1)
    p.dot(cx - 6, 26, 1, BLACK)
    p.dot(cx + 6, 26, 1, BLACK)
    p.ell(cx, 37, 13, 9, BLACK, INK, 2)                                       # the muzzle
    p.dot(cx - 3, 33, 1.5, INK)
    p.dot(cx + 3, 33, 1.5, INK)
    p.arc(cx, 35, 9, 7, 20, 160, INK, 2)                                      # a grin
    return p.im


def hula(b):
    """The island girl in a hula: long hair, a flower, a lei, a grass skirt. Hips to the right of the picture, both
    arms out to the left; b: the hands the other way up (they ripple)."""
    p = Pic()
    hip = 61
    # the hair, behind her: long, to the waist
    p.poly([(41, 14), (36, 34), (34, 58), (42, 62), (46, 40), (66, 40), (70, 62), (78, 58), (76, 34), (71, 14), (56, 5)],
           DIM, INK)
    for x0, x1 in ((39, 37), (73, 75)):
        p.line([(x0, 24), (x1, 54)], INK, 1)
    # the legs and feet
    p.tube([(hip - 6, 78), (50, 97)], 6, SKIN, INK, 1)
    p.tube([(hip + 6, 78), (64, 97)], 6, SKIN, INK, 1)
    p.ell(48, 100, 6, 3, SKIN, INK, 1)
    p.ell(66, 100, 6, 3, SKIN, INK, 1)
    p.line([(46, 93), (55, 93)], GREEN, 2)                                    # leaves at the ankles
    p.line([(60, 93), (69, 93)], GREEN, 2)
    # the body: shoulders, a waist, the hips out to one side
    p.poly([(45, 37), (67, 37), (65, 50), (hip + 13, 62), (hip - 13, 62), (48, 50)], SKIN, INK)
    p.d.rounded_rectangle((45, 39, 67, 47), 3, fill=COL, outline=INK)         # the top
    p.dot(hip - 2, 56, 1, INK)
    # the skirt: strands of grass from the hips, swinging out
    for i in range(-9, 10):
        x0 = hip + i * 1.5
        x1 = hip + i * 3.4 + 4
        p.line([(x0, 62), (x1, 84 + (i % 3) * 2 - abs(i) * 0.4)], GREEN, 2 if i % 2 else 1)
    p.d.rounded_rectangle((hip - 15, 60, hip + 15, 65), 2, fill=COL, outline=INK)
    # the arms: both to the left, the wrists bent
    wy = -5 if b else 5
    p.tube([(46, 39), (30, 36), (15, 40)], 4, SKIN, INK, 1)
    p.tube([(15, 40), (7, 40 + wy)], 3, SKIN, INK, 1)
    p.ell(5, 40 + wy * 1.3, 3, 2, SKIN, INK, 1)
    p.ell(27, 50 - wy * 1.3, 3, 2, SKIN, INK, 1)
    p.tube([(64, 40), (56, 52), (38, 50)], 4, SKIN, INK, 1)
    p.tube([(38, 50), (29, 50 - wy)], 3, SKIN, INK, 1)
    # the lei
    for i in range(9):
        a = math.radians(10 + i * 20)
        p.dot(56 + 10 * math.cos(a), 33 + 9 * math.sin(a), 2, ORANGE if i % 2 else WHITE)
    # the head
    p.tube([(56, 30), (56, 36)], 5, SKIN, INK, 1)
    p.ell(56, 20, 12, 13, SKIN)
    p.d.chord((43, 5, 69, 26), 180, 360, fill=DIM, outline=INK)               # the hair over the forehead
    p.ell(51, 21, 2, 2, WHITE, None)                                          # eyes, lashes
    p.ell(61, 21, 2, 2, WHITE, None)
    p.dot(51, 21, 0.6, BLACK)
    p.dot(61, 21, 0.6, BLACK)
    p.line([(48, 18), (53, 17)], INK)
    p.line([(59, 17), (64, 18)], INK)
    p.arc(56, 24, 5, 4, 20, 160, WHITE, 1)                                    # a smile
    for i in range(5):                                                        # the flower in her hair
        a = math.radians(i * 72 - 90)
        p.dot(70 + 4 * math.cos(a), 11 + 4 * math.sin(a), 3, ORANGE)
    p.dot(70, 11, 2, WHITE)
    return p.im


def wizard():
    """The wizard of the FX pages, turned to the right: a robe with stars, a long white beard, a tall hat with its
    tip bent and a star on it (the track's colour), an arm out with the wand: its tip is the picture's (110, 44)."""
    p = Pic()
    p.poly([(30, 52), (14, 100), (20, 103), (76, 103), (82, 100), (64, 52)], DIM, INK)       # the robe
    p.line([(18, 96), (78, 96)], INK)
    for x, y, c in ((28, 84, WHITE), (44, 90, ORANGE), (62, 82, WHITE), (36, 70, ORANGE), (58, 68, WHITE), (50, 78, COL)):
        p.line([(x - 2, y), (x + 2, y)], c)                                                  # its stars
        p.line([(x, y - 2), (x, y + 2)], c)
    p.ell(28, 102, 8, 2, SKIN, INK, 1)                                                       # shoes
    p.ell(66, 102, 8, 2, SKIN, INK, 1)
    p.tube([(34, 56), (24, 70), (28, 80)], 8, DIM)                                           # the near arm, its hand
    hand(p, 29, 83, 4)
    p.tube([(60, 56), (76, 58), (88, 50)], 8, DIM)                                           # the arm with the wand
    hand(p, 90, 49, 4)
    p.line([(88, 50), (110, 44)], WHITE, 2)
    p.ell(48, 42, 13, 13, SKIN)                                                              # the head
    p.poly([(36, 44), (40, 62), (48, 82), (56, 62), (60, 44), (54, 50), (48, 47), (42, 50)], WHITE, INK)   # the beard
    p.line([(44, 56), (47, 72)], INK)
    p.line([(52, 56), (49, 72)], INK)
    p.ell(53, 46, 3, 2, SKIN, INK, 1)                                                        # a nose
    p.dot(44, 41, 1.5, WHITE)
    p.dot(53, 41, 1.5, WHITE)
    p.dot(45, 41, 0.6, BLACK)
    p.dot(54, 41, 0.6, BLACK)
    p.line([(40, 37), (47, 38)], WHITE, 2)                                                   # brows
    p.line([(51, 38), (58, 37)], WHITE, 2)
    p.poly([(33, 34), (44, 6), (52, 2), (60, 1), (54, 6), (63, 34)], DIM, INK)               # the hat, its tip bent
    p.ell(48, 34, 22, 4, DIM, INK, 1)
    p.line([(36, 28), (60, 28)], COL, 2)
    p.arc(46, 18, 4, 4, 60, 300, WHITE, 1)                                                   # a moon on it
    for i in range(5):                                                                       # the star at its tip
        a = math.radians(i * 72 - 90)
        p.line([(62, 2), (62 + 5 * math.cos(a), 2 + 5 * math.sin(a))], COL, 1)
    p.dot(62, 2, 1.5, COL)
    return p.im


def drummer(mouth):
    """The drummer behind the kit (64 x 52): headphones, a cap, a grin; mouth: open (on the kick). The arms and
    the sticks are the scene's, from the shoulders (14, 36) and (50, 36)."""
    p = Pic(64, 52)
    p.poly([(10, 36), (4, 52), (60, 52), (54, 36), (40, 30), (24, 30)], COL, INK)            # the shirt
    p.arc(32, 30, 8, 6, 0, 180, INK)
    p.tube([(32, 26), (32, 31)], 6, SKIN, INK, 1)
    p.ell(32, 16, 13, 13, SKIN)                                                              # the head
    p.d.chord((19, 2, 45, 22), 180, 360, fill=DIM, outline=INK)                              # a cap, its peak
    p.d.rounded_rectangle((36, 10, 50, 13), 1, fill=DIM, outline=INK)
    p.arc(32, 14, 16, 15, 190, 350, COL, 2)                                                  # the headphones
    p.d.rounded_rectangle((13, 11, 19, 23), 2, fill=COL, outline=INK)
    p.d.rounded_rectangle((45, 11, 51, 23), 2, fill=COL, outline=INK)
    p.ell(27, 17, 2, 2, WHITE, None)
    p.ell(37, 17, 2, 2, WHITE, None)
    p.dot(27, 17, 0.6, BLACK)
    p.dot(37, 17, 0.6, BLACK)
    if mouth:
        p.ell(32, 23, 3, 3, BLACK, INK, 1)
    else:
        p.arc(32, 20, 5, 4, 20, 160, WHITE, 1)
    return p.im


POSES = [("WIZARD", wizard()), ("DRUMMER", drummer(False)), ("DRUMMER_HIT", drummer(True)), ("BUDDHA", buddha(False)), ("BUDDHA_LEAN", buddha(True)), ("GORILLA", gorilla(False)),
         ("GORILLA_UP", gorilla(True)), ("HULA", hula(False)), ("HULA_B", hula(True))]


def main():
    L = ["/* generated by tools/gen_sprites.py: the synth tracks' characters, 4 bits a pixel (OP_SPAL in ui_op.c),",
         " * the first pixel of a byte in its low half. Drawn by that script: original pictures. */", "#pragma once",
         f"#define OP_SPR_W {W}", f"#define OP_SPR_H {H}"]
    for name, im in POSES:
        px = list(im.getdata())
        if im.size != (W, H):
            L += [f"#define OP_SPR_{name}_W {im.size[0]}", f"#define OP_SPR_{name}_H {im.size[1]}"]
        by = [px[i] | (px[i + 1] << 4) for i in range(0, len(px), 2)]
        L.append(f"static const uint8_t OP_SPR_{name}[{len(by)}] = {{")
        for i in range(0, len(by), 28):
            L.append("    " + ",".join(str(b) for b in by[i:i + 28]) + ",")
        L.append("};")
    OUT.write_text("\n".join(L) + "\n")
    print(OUT, sum(im.size[0] * im.size[1] // 2 for _, im in POSES), "B")
    if len(sys.argv) > 1:
        s = 4
        s = 3
        sheet = Image.new("RGB", (5 * (W * s + 12) + 12, 2 * (H * s + 12) + 12), (30, 30, 34))
        for i, (name, im) in enumerate(POSES):
            rgb = Image.new("RGB", im.size)
            col = (255, 98, 26) if i < 3 else {0: (40, 124, 255), 1: (30, 204, 112), 2: (236, 236, 240)}[(i - 3) // 2]
            rgb.putdata([col if v == COL else RGB[v] for v in im.getdata()])
            k = i + 1 if i else 0                    # (the wizard alone in its column)
            sheet.paste(rgb.resize((im.size[0] * s, im.size[1] * s), Image.NEAREST),
                        (12 + (k // 2) * (W * s + 12), 12 + (k % 2) * (H * s + 12)))
        sheet.save(sys.argv[1])


if __name__ == "__main__":
    main()
