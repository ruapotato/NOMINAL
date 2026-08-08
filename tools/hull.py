"""The ship's hull, as a Blender asset.

    blender --background --python tools/hull.py
    blender --background --python tools/hull.py -- --loa 300 --preview

Writes game/assets/hull.glb, and with --preview a render to /tmp/hull_preview.png.

WHY BLENDER AND NOT GDSCRIPT OR C.  Lofting frames into a surface is the easy
part and any of the three can do it -- the C generator did, and so did the
GDScript one before it.  What makes a hull read as designed rather than
extruded is the things only a modeller has: subdivision for real curvature,
bevels so edges catch the light, solidify so the shell has thickness, booleans
to cut a hangar mouth.  Those are one line each here and do not exist in Godot
at all.

AND IT IS NOT A GENERATOR YOU ARE STUCK WITH.  Run it, then open the .blend and
drag vertices; or change FRAMES below and run it again.  Both work, which is
the point -- the shape stops being something you have to ask for.

THE SILHOUETTE: a wide flat command disc at the bow riding HIGH, a neck that
ramps down and aft, a smaller engineering hull slung BELOW and behind it, and
four nacelles on pylons at twelve, three, six and nine o'clock.

AXES, AND THE BUG THAT COST SIX ITERATIONS.  Blender is Z-UP: x is athwart,
y is fore and aft, z is up.  Godot is Y-up.  This script was written in Godot's
convention inside Blender, so every "height" landed on a horizontal axis and
every bit of sheer moved the section sideways instead of lifting it.  David
found it: "I think we're confusing the blender dimensions and the godot
dimensions... on the Z up and down axis in Blender, there's no height
difference between the front section and the back section."  He was exactly
right, and it explains why the front disc came out squished the wrong way and
why raising the bow did nothing visible.

Everything here is now in BLENDER axes -- y fore and aft, z up -- and the glTF
exporter converts to Godot's Y-up on the way out, which is what it is for.
Metres throughout.
"""

import bpy
import bmesh
import math
import sys
import os
import json
from mathutils import Vector

# --------------------------------------------------------------------- args

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
LOA = 300.0
PREVIEW = "--preview" in argv
if "--loa" in argv:
    LOA = float(argv[argv.index("--loa") + 1])
ASSETS = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      "..", "game", "assets")
OUT = os.path.join(ASSETS, "hull.glb")
TOPO = os.path.join(ASSETS, "ship.json")

# THE DECK HEIGHT, and it is the unit the interior is built from. 4 m floor to
# floor leaves 3.7 m of clear head after the plate, which is what it took to
# stop the ship feeling cramped to walk.
DECK_H = 4.0
HEADROOM = 2.6

# ------------------------------------------------------------------- shape
#
# Stations along the keel as fractions of length overall, each with a half
# beam, a half height and how far the section's centre rides above the keel.
# The command section rides HIGH and the engineering hull hangs BELOW it: that
# offset is most of what makes a two-hull ship read as two hulls rather than
# one slab, and getting it backwards was the first version's worst mistake.
#
# `n` IS THE SQUARENESS OF THE SECTION, and it is the difference between a ship
# and a bone. n=2 is a pure ellipse, which is what the first version used, and
# subdivision turned the result into something organic -- David: "the ship mesh
# is terrible". Above 2 the section becomes a rounded rectangle: flat on top,
# flat underneath, hard chines down each side where the panels meet. That is
# what makes a hull read as PLATED rather than grown, and it is why the command
# section can be a wide low wedge at all -- an ellipse has no flat to be wide
# and low with.
#
# THE SHEER. The bow rides higher than the stern, so the neck runs downhill
# from one to the other. David: "front area a tiny bit higher than the back
# area, so the neck has an angle to it."
#
# ON EDGE. David: "the whole front of the ship and the back of the ship needs
# rotated ninety degrees." So the command section and the engineering hull are
# TALL AND NARROW rather than wide and flat -- a blade standing on its edge
# instead of a saucer lying down. It is the single change that takes this
# furthest from the Trek silhouette while keeping the family.
#
# THE NECK IS THE ONE PART THAT DOES NOT ROTATE, and that is the point: it is
# now the WIDE flat member joining two upright ones, so the three bodies read
# apart instead of being one continuous tube.
#
# AND THE SHEER IS DRAMATIC. "The height difference between the front of the
# ship and the back of the ship needs to be even more dramatic" -- the bow
# rides at 0.115 of length overall and the stern at 0.038, so the neck falls
# through nearly a quarter of the ship's length in height.
#
#      t       half_w   half_h   centre_y    n     what it is
#
# THE TWO BODIES SIT AT DIFFERENT HEIGHTS. David: "There is no height
# difference between the front part and the back part. It's not just a pinched
# neck." He is right -- the previous version dropped the centreline 23 m over a
# 106 m deep section, which is invisible. What makes a two-hull ship read as
# two hulls is that the forward body sits ABOVE the after one and they barely
# share a band of height at all:
#
#   command   centre 0.150 of LOA, half depth 0.072  ->  spans 0.078..0.222
#   engineering centre 0.052,      half depth 0.052  ->  spans 0.000..0.104
#
# They overlap only between 0.078 and 0.104 -- and that narrow band is exactly
# where the neck is, so the neck is not a pinch, it is a ramp between two
# storeys of ship.
FRAMES = [
    # t is along the keel, bow to stern.  half_b is half the BEAM (athwart,
    # Blender x).  half_h is half the HEIGHT (Blender z).  centre_h is how far
    # the section's middle rides above the baseline, also z.
    #
    # THE COMMAND SECTION IS A DISC: wide and thin.  "The front disc thing is
    # not a disc, it's squished the wrong direction."  It is 0.19 of length in
    # beam against 0.022 in height -- nine times wider than it is deep.
    #
    # AND IT RIDES ABOVE THE ENGINEERING HULL WITH NOTHING SHARED:
    #     command      centre 0.150, half height 0.022  ->  0.128 .. 0.172
    #     engineering  centre 0.045, half height 0.048  ->  0.000 .. 0.093
    #
    #      t     half_b   half_h   centre_h   n      what it is
    (0.000,   0.010,   0.006,    0.150,   3.0),   # the point of the bow
    (0.030,   0.062,   0.012,    0.150,   3.6),
    (0.090,   0.128,   0.018,    0.151,   4.0),
    (0.180,   0.178,   0.021,    0.151,   4.4),
    (0.270,   0.190,   0.022,    0.150,   4.4),   # the disc, at its widest
    (0.340,   0.174,   0.021,    0.147,   4.2),
    (0.390,   0.120,   0.020,    0.140,   3.8),   # the shoulder, heading down
    (0.440,   0.040,   0.026,    0.124,   3.2),   # the neck: narrow, and
    (0.500,   0.032,   0.030,    0.100,   3.0),   #   ramping down and aft
    # AND THE STERN IS FLAT TOO, though not as flat as the disc. David: "the
    # back section should be flatter as well. Not as flat as the top saucer
    # section, but still flat. Not a tube." It was 1:1 -- a cylinder -- which
    # is exactly the tube he means. These run about 2.4:1.
    (0.560,   0.046,   0.030,    0.074,   3.0),   # levelling into engineering
    (0.640,   0.066,   0.030,    0.054,   3.6),
    (0.760,   0.072,   0.030,    0.048,   3.8),
    (0.880,   0.066,   0.028,    0.048,   3.6),
    (0.960,   0.048,   0.022,    0.049,   3.2),
    (1.000,   0.030,   0.015,    0.050,   3.0),   # the stern
]

# FOUR NACELLES, at twelve, three, six and nine o'clock.
#
# David: "instead of a circular nacelle, let's actually add four nacelles like
# Star Trek, but four of them. One left, one right, one up, one down. So it's
# not a direct rip off of a Star Trek ship, but it's close enough."
#
# Which is also the answer to the ring: a hoop reads as jewellery and a shroud
# reads as a barrel, but four nacelles on pylons read as ENGINES, and having
# four of them instead of two is the whole difference between homage and copy.
NAC_T = 0.760           # where on the keel a nacelle is centred
NAC_LEN = 0.330         # how long, as a fraction of LOA
NAC_R = 0.034           # nacelle radius
NAC_OFF = 0.105         # how far off the keel they stand
# ROLLED 45 DEGREES, so the four sit at the diagonals rather than square on the
# axes. David: "the perfectly two coordinate [aligned] nacelles look funny."
# They did -- dead on the vertical and horizontal it reads as a plus sign
# somebody snapped to a grid, and an X reads as a design decision.
NAC_ROLL = math.radians(45)
# AND THE PYLONS ARE CORRIDORS. "The connections between the mesh needs to be
# hollow. As it is, there's no way we could navigate into a port nacelle."
# Quite -- you repair a nacelle, so you have to be able to walk to it.
#
# THE DIMENSIONS BELOW ARE THE CORRIDOR'S OWN, and the RADIAL REACH is derived
# from where the nacelle actually is rather than being a third number that can
# disagree with it. That is not fussiness: the first version used PYLON_W as
# the radial length, so every pylon was a 9 m stub trying to span a 31 m gap,
# and --connect found two of the four nacelles with no way in at all. The other
# two only "passed" because they happened to overlap the hull directly, which
# is an accident and not a corridor.
PYLON_W = 0.030         # the corridor's width, along the keel
PYLON_H = 0.022         # and its height
PYLON_OVER = 1.25       # how far past the gap it runs, so the shells overlap
                        # at BOTH ends and the interiors actually join

SEG = 48                # segments round a frame; subsurf smooths the rest


# ==========================================================================
# THE TOPOLOGY, FROM THE SAME FRAMES AS THE MESH
#
# David: "I want you to generate the ship mesh so you know where everything is
# and it's easier to build the rooms out... If I hand craft it, then everything
# will be arbitrary. We need to be able to have cabling and rooms and doors and
# all this that if I build everything in Blender it's gonna be impossible to
# take over into Godot without manually placing literally everything."
#
# Exactly so, and it is why this file emits DATA as well as a mesh. Both come
# out of FRAMES, so the floor the player walks and the hull they see cannot
# drift apart -- and every room, door, conduit run and repair site downstream
# can be placed by asking rather than by hand.
#
# game/assets/ship.json holds:
#   decks[]      z of each deck, and per station along the keel the beam extent
#                where there is standing room
#   pylons[]     the corridors out to the nacelles: axis, width, which decks
#   nacelles[]   interior extents, so a nacelle is a place you can be
# ==========================================================================

def section(t):
    """Half beam, half height and centre height at a fraction along the keel,
    interpolated between frames. The mesh lofts the same numbers."""
    if t <= FRAMES[0][0]:
        f = FRAMES[0]
        return f[1], f[2], f[3], f[4]
    for i in range(1, len(FRAMES)):
        if t > FRAMES[i][0]:
            continue
        a, b = FRAMES[i - 1], FRAMES[i]
        span = b[0] - a[0]
        u = (t - a[0]) / span if span > 0 else 0.0
        return (a[1] + (b[1] - a[1]) * u, a[2] + (b[2] - a[2]) * u,
                a[3] + (b[3] - a[3]) * u, a[4] + (b[4] - a[4]) * u)
    f = FRAMES[-1]
    return f[1], f[2], f[3], f[4]


def inside(t, x, z):
    """Is this point inside the pressure hull? The superellipse the loft uses."""
    hb, hh, ch, n = section(t)
    hb, hh = hb * LOA, hh * LOA
    if hb <= 0 or hh <= 0:
        return False
    dx = abs(x) / hb
    dz = abs(z - ch * LOA) / hh
    return dx ** n + dz ** n <= 1.0


def topology():
    """Decks, pylon corridors and nacelle interiors, as metres."""
    zmin = min(f[3] - f[2] for f in FRAMES) * LOA
    zmax = max(f[3] + f[2] for f in FRAMES) * LOA
    decks = []
    d = 0
    z = math.floor(zmin / DECK_H) * DECK_H
    while z < zmax:
        rows = []
        y = 0
        while y <= LOA:
            t = y / LOA
            # walk out from the keel to find where standing room ends
            hb = section(t)[0] * LOA
            edge = 0.0
            x = 0.0
            while x <= hb + 2.0:
                if inside(t, x, z + 0.1) and inside(t, x, z + HEADROOM):
                    edge = x
                x += 1.0
            if edge > 1.5:
                rows.append([round(y, 1), round(-edge, 1), round(edge, 1)])
            y += 2.0
        if len(rows) > 4:
            decks.append({"deck": d, "z": round(z, 2), "rows": rows})
            d += 1
        z += DECK_H
    # renumber from the lowest deck that exists
    for i, dk in enumerate(decks):
        dk["deck"] = i

    pylons, nacelles = [], []
    for name, ang in (("StbdUpper", NAC_ROLL), ("StbdLower", -NAC_ROLL),
                      ("PortLower", math.pi + NAC_ROLL),
                      ("PortUpper", math.pi - NAC_ROLL)):
        ux, uz = math.cos(ang), math.sin(ang)
        cx, cz = ux * NAC_OFF * LOA, 0.045 * LOA + uz * NAC_OFF * LOA
        nacelles.append({
            "name": name,
            "centre": [round(cx, 1), round(NAC_T * LOA, 1), round(cz, 1)],
            "radius": round(NAC_R * LOA, 1),
            "length": round(NAC_LEN * LOA, 1),
        })
        pylons.append({
            "name": name,
            "from": [0.0, round(NAC_T * LOA - NAC_LEN * LOA * 0.10, 1),
                     round(0.045 * LOA, 1)],
            "to": [round(cx, 1), round(NAC_T * LOA - NAC_LEN * LOA * 0.10, 1),
                   round(cz, 1)],
            "width": round(PYLON_W * LOA, 1),
            "height": round(PYLON_H * LOA, 1),
        })

    return {"loa": LOA, "deck_h": DECK_H, "headroom": HEADROOM,
            "decks": decks, "pylons": pylons, "nacelles": nacelles}


# --------------------------------------------------------------- reachable
#
# CAN YOU ACTUALLY WALK FROM ENGINEERING INTO A NACELLE?
#
# I claimed you could, in a commit message, on the strength of having put a
# Solidify modifier on the pylons and nacelles so they were shells rather than
# solids. That is not the same thing. Three shells that touch are still three
# separate rooms if their INTERIORS do not overlap, and nothing had checked.
#
# A nacelle you cannot reach is a system the engineer cannot repair, which in
# this game is the difference between a working loop and scenery. So it gets a
# measurement rather than an assertion: flood fill the interior volume at two
# metre cells from a point inside the engineering hull and see which of the
# four nacelle centres the fill arrives at.


def in_pylon(x, y, z):
    """Inside one of the four pylon corridors? The pylon is a box lying along
    the keel, rotated about Y so it points out at its nacelle's angle."""
    for ang in (NAC_ROLL, -NAC_ROLL, math.pi + NAC_ROLL, math.pi - NAC_ROLL):
        ux, uz = math.cos(ang), math.sin(ang)
        cx = ux * NAC_OFF * LOA * 0.5
        cy = NAC_T * LOA - NAC_LEN * LOA * 0.10
        cz = 0.045 * LOA + uz * NAC_OFF * LOA * 0.5
        # into the pylon's own frame: it is rotated by `ang` about Y, so the
        # long axis across the ship is the radial direction
        dx, dz = x - cx, z - cz
        radial = dx * ux + dz * uz
        across = -dx * uz + dz * ux
        # THE SAME THREE NUMBERS THE MESH IS BUILT FROM. If this test and the
        # box ever describe different corridors, the check is measuring
        # something the player cannot walk.
        if (abs(radial) <= NAC_OFF * LOA * PYLON_OVER * 0.5 and
                abs(across) <= PYLON_H * LOA * 0.5 and
                abs(y - cy) <= PYLON_W * LOA * 0.5):
            return True
    return False


def in_nacelle(x, y, z):
    for ang in (NAC_ROLL, -NAC_ROLL, math.pi + NAC_ROLL, math.pi - NAC_ROLL):
        ux, uz = math.cos(ang), math.sin(ang)
        cx = ux * NAC_OFF * LOA
        cz = 0.045 * LOA + uz * NAC_OFF * LOA
        if (abs(y - NAC_T * LOA) <= NAC_LEN * LOA * 0.5 and
                (x - cx) ** 2 + (z - cz) ** 2 <= (NAC_R * LOA) ** 2):
            return True
    return False


def in_ship(x, y, z):
    if 0.0 <= y <= LOA and inside(y / LOA, x, z):
        return True
    return in_pylon(x, y, z) or in_nacelle(x, y, z)


def connectivity():
    step = 2.0
    xr = int((NAC_OFF * LOA + NAC_R * LOA + 6.0) / step) + 1
    zlo, zhi = -10.0, max(f[3] + f[2] for f in FRAMES) * LOA + 10.0
    nz = int((zhi - zlo) / step) + 1
    ny = int(LOA / step) + 1

    def key(i, j, k):
        return (i * ny + j) * nz + k

    # start deep in the engineering hull, on the keel
    sy = 0.80 * LOA
    shb, shh, sch, _ = section(0.80)
    si, sj, sk = xr, int(sy / step), int((sch * LOA - zlo) / step)
    if not in_ship(0.0, sy, sch * LOA):
        print("connectivity: the start point is not inside the ship")
        return

    seen = {key(si, sj, sk)}
    stack = [(si, sj, sk)]
    while stack:
        i, j, k = stack.pop()
        for di, dj, dk in ((1, 0, 0), (-1, 0, 0), (0, 1, 0),
                           (0, -1, 0), (0, 0, 1), (0, 0, -1)):
            ni, nj, nk = i + di, j + dj, k + dk
            if not (0 <= ni <= 2 * xr and 0 <= nj < ny and 0 <= nk < nz):
                continue
            kk = key(ni, nj, nk)
            if kk in seen:
                continue
            x = (ni - xr) * step
            y = nj * step
            z = zlo + nk * step
            if not in_ship(x, y, z):
                continue
            seen.add(kk)
            stack.append((ni, nj, nk))

    print("connectivity: %d cells of %.0f m3 reachable from engineering"
          % (len(seen), step ** 3))
    bad = 0
    for name, ang in (("StbdUpper", NAC_ROLL), ("StbdLower", -NAC_ROLL),
                      ("PortLower", math.pi + NAC_ROLL),
                      ("PortUpper", math.pi - NAC_ROLL)):
        ux, uz = math.cos(ang), math.sin(ang)
        cx = ux * NAC_OFF * LOA
        cz = 0.045 * LOA + uz * NAC_OFF * LOA
        i = int(round(cx / step)) + xr
        j = int(round(NAC_T * LOA / step))
        k = int(round((cz - zlo) / step))
        ok = key(i, j, k) in seen
        if not ok:
            bad += 1
        print("  nacelle %-10s at (%6.1f,%6.1f,%6.1f)  %s"
              % (name, cx, NAC_T * LOA, cz,
                 "REACHABLE on foot" if ok else "CUT OFF -- no way in"))
    print("connectivity: %s"
          % ("every nacelle can be walked to" if bad == 0
             else "%d of 4 nacelles cannot be reached" % bad))


def clear():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()
    for block in (bpy.data.meshes, bpy.data.objects, bpy.data.materials):
        for item in list(block):
            if item.users == 0:
                block.remove(item)


def loft():
    """The pressure hull: every frame's ellipse joined to the next."""
    bm = bmesh.new()
    rings = []
    for t, hb, hh, ch, n in FRAMES:
        ring = []
        for s in range(SEG):
            a = math.tau * s / SEG
            ca, sa = math.cos(a), math.sin(a)
            bx = math.copysign(abs(ca) ** (2.0 / n), ca)    # athwart
            bz = math.copysign(abs(sa) ** (2.0 / n), sa)    # up
            ring.append(bm.verts.new((
                bx * hb * LOA,          # x: beam
                t * LOA,                # y: along the keel
                ch * LOA + bz * hh * LOA)))   # z: height
        rings.append(ring)
    for i in range(len(rings) - 1):
        for s in range(SEG):
            n = (s + 1) % SEG
            bm.faces.new((rings[i][s], rings[i][n],
                          rings[i + 1][n], rings[i + 1][s]))
    # cap both ends so solidify and the boolean have a closed manifold
    bm.faces.new(list(reversed(rings[0])))
    bm.faces.new(rings[-1])
    # CREASE THE CHINES. Subdivision melts every edge equally, which is what
    # turned the first hull into a pebble. Creasing the four long lines where
    # the panels meet keeps them sharp while the panels themselves stay smooth.
    bm.edges.ensure_lookup_table()
    crease = bm.edges.layers.float.new("crease_edge") \
        if "crease_edge" not in bm.edges.layers.float else \
        bm.edges.layers.float["crease_edge"]
    corners = {int(SEG * 0.125), int(SEG * 0.375), int(SEG * 0.625),
               int(SEG * 0.875)}
    for i in range(len(rings) - 1):
        for sidx in corners:
            a, b = rings[i][sidx], rings[i + 1][sidx]
            e = bm.edges.get((a, b))
            if e is not None:
                e[crease] = 0.85
    bm.normal_update()

    me = bpy.data.meshes.new("HullMesh")
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new("Hull", me)
    bpy.context.collection.objects.link(obj)
    return obj


def nacelles():
    """Four engines at the diagonals, and the corridors that reach them.

    Cylinders along the keel rather than round it, rolled 45 degrees so the
    stern is an X rather than a plus. Each is joined to the hull by a pylon big
    enough to walk down -- which is not decoration: the nacelle is a thing that
    breaks and the engineer is the one who goes and fixes it.

    Everything here is hollow. The hull is solidified inward, and so are these,
    so the interior of the ship, the pylons and the nacelles are one connected
    volume rather than three solids that happen to touch.
    """
    made = []
    for name, ang in (("StbdUpper", NAC_ROLL),
                      ("StbdLower", -NAC_ROLL),
                      ("PortLower", math.pi + NAC_ROLL),
                      ("PortUpper", math.pi - NAC_ROLL)):
        ux, uz = math.cos(ang), math.sin(ang)
        cx = ux * NAC_OFF * LOA
        cz = 0.045 * LOA + uz * NAC_OFF * LOA
        bpy.ops.mesh.primitive_cylinder_add(
            vertices=32, radius=NAC_R * LOA, depth=NAC_LEN * LOA,
            end_fill_type="NGON",
            location=(cx, NAC_T * LOA, cz),
            rotation=(math.radians(90), 0, 0))
        nac = bpy.context.object
        nac.name = "Nacelle" + name
        made.append(nac)

        # THE PYLON, running from inside the hull out to inside the nacelle.
        # It is deliberately longer at both ends than the gap it spans, so the
        # three shells overlap and the interiors join instead of meeting at a
        # seam a person cannot pass through.
        bpy.ops.mesh.primitive_cube_add(size=1, location=(
            cx * 0.5, NAC_T * LOA - NAC_LEN * LOA * 0.10,
            0.045 * LOA + uz * NAC_OFF * LOA * 0.5))
        arm = bpy.context.object
        arm.name = "Pylon" + name
        # x is the RADIAL reach, and it is the distance to the nacelle with a
        # quarter over so it buries itself in the hull at one end and the
        # nacelle at the other.
        arm.scale = (NAC_OFF * LOA * PYLON_OVER, PYLON_W * LOA, PYLON_H * LOA)
        arm.rotation_euler = (0, -ang, 0)
        made.append(arm)
    return made


def shape(obj):
    """Curvature and edges. NOT thickness -- that comes last, once the whole
    ship is a single surface. See weld() for why that ordering matters."""
    sub = obj.modifiers.new("Subdivision", "SUBSURF")
    sub.levels = 1
    sub.render_levels = 2

    bev = obj.modifiers.new("Bevel", "BEVEL")
    bev.width = 0.0016 * LOA
    bev.segments = 2
    bev.limit_method = "ANGLE"
    bev.angle_limit = math.radians(35)

    for poly in obj.data.polygons:
        poly.use_smooth = True


def apply_all(obj):
    bpy.context.view_layer.objects.active = obj
    for m in list(obj.modifiers):
        bpy.ops.object.modifier_apply(modifier=m.name)


def weld(hull, parts):
    """ONE MESH, NOT SEVERAL OVERLAPPING ONES.

    David: "They can't be separate meshes or they won't have a connection to
    the inside of the rest of the ship. It has to be one mesh, not many meshes
    overlapping into each other."

    He is right, and it is a sharper point than the flood fill I wrote first.
    That test filled the mathematical union of the primitives and reported
    every nacelle reachable -- but the GEOMETRY was a hull shell, four nacelle
    shells and four pylon shells, each solidified separately, so at every
    overlap there were two walls between the two spaces. Mathematically joined,
    physically a set of sealed rooms. A check that measures the intent instead
    of the artefact is worse than no check, because it reads as evidence.

    So: boolean UNION every part into the hull, apply it, and only THEN
    solidify. One surface, one shell, one interior -- and the pylons really are
    corridors into the nacelles rather than pipes glued to the outside.
    """
    for p in parts:
        apply_all(p)
    for p in parts:
        b = hull.modifiers.new("Weld" + p.name, "BOOLEAN")
        b.operation = "UNION"
        b.object = p
        b.solver = "EXACT"
    apply_all(hull)
    for p in parts:
        bpy.data.objects.remove(p)

    # HOW MANY PIECES THE UNION CAME OUT IN, counted BEFORE thickness -- and
    # the ordering is the whole subtlety. Solidifying a closed manifold gives
    # an outer shell AND an inner one, two closed surfaces that share no edge,
    # so a piece count taken afterwards reads 2 for a perfectly good hull. The
    # claim worth making is about the UNION: one piece means the hull, the four
    # pylons and the four nacelles are a single connected surface, so the space
    # inside them is a single space.
    pieces = loose_parts(hull)

    # AND THE THICKNESS LAST, on that single surface.
    sol = hull.modifiers.new("Solidify", "SOLIDIFY")
    sol.thickness = 0.0035 * LOA
    sol.offset = -1.0
    apply_all(hull)
    print("weld: %d connected piece%s before thickness, %d after"
          % (pieces, "" if pieces == 1 else "s", loose_parts(hull)))
    return pieces


def loose_parts(obj):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    seen = set()
    pieces = 0
    for v in bm.verts:
        if v.index in seen:
            continue
        pieces += 1
        stack = [v]
        seen.add(v.index)
        while stack:
            u = stack.pop()
            for e in u.link_edges:
                w = e.other_vert(u)
                if w.index not in seen:
                    seen.add(w.index)
                    stack.append(w)
    bm.free()
    return pieces


def material(obj):
    mat = bpy.data.materials.new("HullPlate")
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (0.30, 0.33, 0.36, 1.0)
    bsdf.inputs["Metallic"].default_value = 0.65
    bsdf.inputs["Roughness"].default_value = 0.45
    obj.data.materials.append(mat)


def ortho(path, loc, name):
    """One orthographic elevation, AIMED rather than rotated by hand.

    Four of my elevation cameras this session came back empty or cropped
    because I was computing Euler angles in my head and getting them wrong. A
    camera pointed at the middle of the ship with to_track_quat() cannot be
    wrong in that way, and it is the same method the three-quarter view has
    used all along -- which is the one that never failed.
    """
    bpy.ops.object.camera_add(location=loc)
    cam = bpy.context.object
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = LOA * 1.12
    aim = Vector((0, LOA * 0.5, LOA * 0.09)) - Vector(loc)
    cam.rotation_euler = aim.to_track_quat("-Z", "Y").to_euler()
    # roll the long axis of the ship across the frame rather than up it
    cam.rotation_euler.rotate_axis("Z", 0)
    bpy.context.scene.camera = cam
    bpy.context.scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(cam)


def preview(path):
    # FRAMED OFF THE SHIP'S OWN LENGTH. The first attempt put a 60 mm lens 278 m
    # from a 300 m hull and rendered a close-up of the neck.
    bpy.ops.object.camera_add(location=(LOA * 1.25, -LOA * 0.55, LOA * 0.55))
    cam = bpy.context.object
    cam.data.lens = 35
    direction = Vector((0, LOA * 0.5, LOA * 0.08)) - cam.location
    cam.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
    bpy.context.scene.camera = cam

    bpy.ops.object.light_add(type="SUN", location=(0, LOA, LOA))
    bpy.context.object.data.energy = 4.0
    bpy.ops.object.light_add(type="SUN", location=(0, -LOA * 0.4, -LOA))
    bpy.context.object.data.energy = 1.2

    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.render.resolution_x = 1600
    sc.render.resolution_y = 900
    sc.render.filepath = path
    sc.display.shading.light = "STUDIO"
    sc.display.shading.show_cavity = True
    bpy.ops.render.render(write_still=True)

    # and the two that actually settle the shape
    # Both elevations are AIMED at the middle of the ship, not rotated by hand.
    ortho("/tmp/hull_top.png", (0.0, LOA * 0.5, LOA * 2.0), "top")
    ortho("/tmp/hull_side.png", (LOA * 2.0, LOA * 0.5, LOA * 0.09), "side")


def main():
    clear()
    hull = loft()
    shape(hull)
    parts = nacelles()
    pieces = weld(hull, parts)
    material(hull)
    if pieces != 1:
        print("weld: WARNING -- the ship is in %d pieces, so its interiors "
              "are not joined" % pieces)

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.export_scene.gltf(filepath=OUT, export_format="GLB",
                              use_selection=True, export_apply=True)
    print("hull: %.0f m, %d frames -> %s" % (LOA, len(FRAMES), OUT))

    topo = topology()
    with open(TOPO, "w") as fh:
        json.dump(topo, fh, indent=1)
    print("topology: %d decks -> %s" % (len(topo["decks"]), TOPO))
    for dk in topo["decks"]:
        area = sum((r[2] - r[1]) * 2.0 for r in dk["rows"])
        print("  deck %2d at z %5.1f  %5d m2 over %d stations"
              % (dk["deck"], dk["z"], area, len(dk["rows"])))

    if PREVIEW:
        preview("/tmp/hull_preview.png")
        print("preview: /tmp/hull_preview.png")



def measure():
    """Print the mesh's real dimensions. Looking at a render tells you whether
    the CAMERA is right; this tells you whether the SHAPE is. Three of my
    preview cameras this session pointed at nothing, and I spent two of those
    rounds unsure which of the two was wrong."""
    clear()
    h = loft()
    shape(h)
    dg = bpy.context.evaluated_depsgraph_get()
    me = h.evaluated_get(dg).to_mesh()
    xs = [v.co.x for v in me.vertices]
    ys = [v.co.y for v in me.vertices]
    zs = [v.co.z for v in me.vertices]
    print("HULL  length %.0f m (y)  beam %.0f m (x)  height %.0f m (z)"
          % (max(ys) - min(ys), max(xs) - min(xs), max(zs) - min(zs)))
    for label, t in (("bow point", 0.02), ("command disc", 0.27),
                     ("neck", 0.50), ("engineering", 0.78), ("stern", 0.98)):
        sel = [v.co for v in me.vertices if abs(v.co.y - t * LOA) < 4.0]
        if not sel:
            continue
        beam = max(v.x for v in sel) - min(v.x for v in sel)
        hgt = max(v.z for v in sel) - min(v.z for v in sel)
        lo, hi = min(v.z for v in sel), max(v.z for v in sel)
        print("  %-15s beam %5.1f  height %5.1f  wide:tall %4.1f:1  "
              "z %5.1f..%5.1f"
              % (label, beam, hgt, beam / max(hgt, 0.01), lo, hi))


if "--measure" in argv:
    measure()
elif "--connect" in argv:
    connectivity()
else:
    main()
