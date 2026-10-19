"""
MUMU keycaps — 5 Cherry MX keycaps, one per key, each showing the animation that key triggers.

  key 1  laugh   ^ ^ eyes + big grin
  key 2  gasp    round eyes + O mouth
  key 3  silly   wink + tongue out
  key 4  love    heart
  key 5  dizzy   spiral

Soft rounded-square caps with a flat top so they print TOP-DOWN on the bed with no supports.
Each cap comes as two bodies that fit together:
  keycapN_body.stl  — the cap with the icon recessed ICON_DEPTH into the top
  keycapN_icon.stl  — the icon inlay that fills the recess flush
Load both into Bambu Studio / OrcaSlicer as ONE object with two parts and give the
icon a second colour (AMS), or print just the body in one colour — the recessed icon
still reads well. Every parameter is at the top; units are mm.
"""
import math
import cadquery as cq
from shapely.geometry import Point, Polygon, LineString
from shapely.ops import unary_union
from shapely import affinity

# ---------------- Parameters ----------------
BASE      = 18.0     # cap footprint at the skirt (standard 1u is ~18)
TOP       = 14.6     # top face size
H         = 9.0      # total height, skirt bottom to top face
R_BASE    = 3.0      # corner radius at the skirt
R_TOP     = 2.4      # corner radius at the top
WALL      = 1.2      # skirt wall (inner cavity 15.6 mm so it clears the switch housing)
TOP_T     = 2.0      # top thickness (must exceed ICON_DEPTH)
ICON_DEPTH = 0.8     # matches the 0.8 mm engravings on the case
ICON_CLR  = 0.05     # inlay shrink per side so it drops in

STEM_D    = 6.0      # stem cylinder
CROSS_L   = 4.35     # MX cross arm length (nominal 4.0; printed holes shrink) — see fit_tester.stl
CROSS_W   = 1.45     # MX cross arm width  (nominal 1.17)
STEM_GAP  = 2.4      # socket recessed this far up from the skirt rim -> the skirt drops
                     # 2.4 mm down around the switch, so the cap sits closer to the case
CROSS_DEPTH = 4.0

N_PTS     = 72


def rrect_pts(size, r, n_arc=10):
    """Rounded square, always the same number of points so lofts map corner to corner."""
    a = size / 2 - r
    pts = []
    for k, (cx, cy) in enumerate([(a, a), (-a, a), (-a, -a), (a, -a)]):
        for i in range(n_arc + 1):
            t = math.radians(90 * k + 90 * i / n_arc)
            pts.append((cx + r * math.cos(t), cy + r * math.sin(t)))
    return pts


def rrect_loft(b, rb, t, rt, z0, z1):
    w0 = cq.Wire.makePolygon([cq.Vector(x, y, z0) for x, y in rrect_pts(b, rb)], close=True)
    w1 = cq.Wire.makePolygon([cq.Vector(x, y, z1) for x, y in rrect_pts(t, rt)], close=True)
    return cq.Workplane("XY").add(cq.Solid.makeLoft([w0, w1], True))


# ---------------- Icons (2D, shapely, ~11 mm wide) ----------------
def arc(cx, cy, r, a0, a1, width, n=24):
    pts = [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
            cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]
    return LineString(pts).buffer(width / 2, cap_style=1, join_style=1)


def caret(cx, cy, w, h, width):          # ^ shaped happy eye
    return LineString([(cx - w / 2, cy - h / 2), (cx, cy + h / 2), (cx + w / 2, cy - h / 2)]) \
        .buffer(width / 2, cap_style=1, join_style=1)


def heart(cx, cy, s):
    c1 = Point(cx - s * 0.5, cy + s * 0.25).buffer(s * 0.55, 48)
    c2 = Point(cx + s * 0.5, cy + s * 0.25).buffer(s * 0.55, 48)
    tri = Polygon([(cx - s * 1.03, cy + s * 0.05), (cx + s * 1.03, cy + s * 0.05), (cx, cy - s * 1.15)])
    return unary_union([c1, c2, tri])


def icon_laugh():
    eyes = [caret(-2.7, 2.4, 3.0, 1.8, 1.0), caret(2.7, 2.4, 3.0, 1.8, 1.0)]
    grin = Point(0, -0.6).buffer(4.0, 64).intersection(Polygon([(-5, -0.6), (5, -0.6), (5, -6), (-5, -6)]))
    return unary_union(eyes + [grin])


def icon_gasp():
    eyes = [Point(-2.7, 2.3).buffer(1.35, 32), Point(2.7, 2.3).buffer(1.35, 32)]
    mouth = affinity.scale(Point(0, -2.4).buffer(1.0, 48), 1.7, 2.1)
    return unary_union(eyes + [mouth])


def icon_silly():
    wink = arc(-2.7, 2.9, 1.6, 200, 340, 1.0)            # closed eye: smile-curve
    eye = Point(2.7, 2.3).buffer(1.35, 32)
    smile = arc(0, 1.2, 4.0, 215, 325, 1.0)
    tongue = affinity.scale(Point(1.0, -3.2).buffer(1.0, 48), 1.25, 1.5)
    return unary_union([wink, eye, smile, tongue])


def icon_love():
    return heart(0, 0.3, 4.4)


def icon_dizzy():
    pts = []
    for i in range(1, 140):
        a = i * (4.4 * math.pi / 140)
        r = 0.35 + 4.6 * i / 140
        pts.append((r * math.cos(a), r * math.sin(a)))
    return LineString(pts).buffer(0.55, cap_style=1, join_style=1)


ICONS = [("laugh", icon_laugh), ("gasp", icon_gasp), ("silly", icon_silly),
         ("love", icon_love), ("dizzy", icon_dizzy)]


def poly_to_solid(poly, z0, h):
    polys = list(poly.geoms) if poly.geom_type == "MultiPolygon" else [poly]
    out = None
    for p in polys:
        wp = cq.Workplane("XY").workplane(offset=z0).polyline(list(p.exterior.coords)[:-1]).close().extrude(h)
        for hole in p.interiors:
            wp = wp.cut(cq.Workplane("XY").workplane(offset=z0 - 0.01)
                        .polyline(list(hole.coords)[:-1]).close().extrude(h + 0.02))
        out = wp if out is None else out.union(wp)
    return out


# ---------------- Cap ----------------
def make_cap(icon_poly):
    outer = rrect_loft(BASE, R_BASE, TOP, R_TOP, 0, H)
    inner_top = TOP - 2 * WALL - 0.2
    inner = rrect_loft(BASE - 2 * WALL, R_BASE - WALL * 0.6, inner_top, R_TOP - WALL * 0.6, -0.01, H - TOP_T)
    cap = outer.cut(inner)

    stem = (cq.Workplane("XY").workplane(offset=STEM_GAP).circle(STEM_D / 2).extrude(H - TOP_T - STEM_GAP + 0.01))
    cross = (cq.Workplane("XY").workplane(offset=STEM_GAP - 0.01).rect(CROSS_L, CROSS_W).extrude(CROSS_DEPTH)
             .union(cq.Workplane("XY").workplane(offset=STEM_GAP - 0.01).rect(CROSS_W, CROSS_L).extrude(CROSS_DEPTH)))
    cap = cap.union(stem).cut(cross)

    recess = poly_to_solid(icon_poly, H - ICON_DEPTH, ICON_DEPTH + 0.01)
    body = cap.cut(recess)
    inlay = poly_to_solid(icon_poly.buffer(-ICON_CLR, join_style=1), H - ICON_DEPTH, ICON_DEPTH)
    return body, inlay


def fit_tester():
    """Five short sockets with crosses from tight (1) to loose (5) — print this first."""
    sizes = [(4.20, 1.35), (4.30, 1.40), (4.35, 1.45), (4.45, 1.50), (4.55, 1.55)]
    base = cq.Workplane("XY").box(5 * 11 + 4, 14, 2, centered=(True, True, False)).edges("|Z").fillet(2)
    for i, (L, W) in enumerate(sizes):
        x = (i - 2) * 11
        base = base.union(cq.Workplane("XY").workplane(offset=2).center(x, 1.5).circle(STEM_D / 2).extrude(5))
        cross = (cq.Workplane("XY").workplane(offset=-0.1).center(x, 1.5).rect(L, W).extrude(8)
                 .union(cq.Workplane("XY").workplane(offset=-0.1).center(x, 1.5).rect(W, L).extrude(8)))
        base = base.cut(cross)
        base = base.cut(cq.Workplane("XY").workplane(offset=1.4).center(x, -4.6)
                        .text(str(i + 1), 4.5, 1, kind="bold", halign="center", valign="center"))
    return base, sizes


if __name__ == "__main__":
    import os
    os.makedirs("stl", exist_ok=True)
    allb = []
    for i, (name, fn) in enumerate(ICONS, 1):
        poly = fn()
        body, inlay = make_cap(poly)
        assert body.val().isValid() and inlay.val().isValid(), name
        cq.exporters.export(body, f"stl/keycap{i}_{name}_body.stl", tolerance=0.02, angularTolerance=0.1)
        cq.exporters.export(inlay, f"stl/keycap{i}_{name}_icon.stl", tolerance=0.02, angularTolerance=0.1)
        allb.append(body.translate(((i - 3) * 22, 0, 0)).union(inlay.translate(((i - 3) * 22, 0, 0))))
        print(f"key {i} {name}: ok")
    t, sizes = fit_tester()
    cq.exporters.export(t, "stl/fit_tester.stl", tolerance=0.02, angularTolerance=0.1)
    print("fit tester:", ", ".join(f"{i+1}={L}x{W}" for i, (L, W) in enumerate(sizes)))
    a = allb[0]
    for b in allb[1:]:
        a = a.union(b)
    cq.exporters.export(a, "mumu_keycaps_all.step")
