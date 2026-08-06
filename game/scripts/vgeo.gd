# vgeo.gd — vertex-coloured boxes, built the way tower.gd builds them.
#
# The tower is one ArrayMesh with the shading baked into the vertex colours,
# because a flat-lit interior is a pile of boxes you cannot read. Anything that
# MOVES cannot live in that mesh -- a lift car and its doors are geometry that
# changes position every frame -- so they get their own little meshes, and this
# is the same rule applied to them so that a lift does not look like it came
# out of a different game from the corridor outside it.
#
# No imported assets anywhere. There are not going to be any.

extends RefCounted

var v := PackedVector3Array()
var c := PackedColorArray()
var f := PackedVector3Array()

# WHICH PART OF THE THING THIS BOX IS, for the one caller that needs to tell a
# shirt from a head after the mesh is built. Off by default and costing nothing:
# the tower's own mesh is a hundred thousand triangles and does not want eight
# more bytes on every vertex of it. people.gd turns it on, sets `tag` before
# each box, and reads it back in a shader as UV -- which is how one mesh shared
# by twenty people can put a different shirt on each of them without becoming
# twenty meshes. See people.gd for what the two numbers mean.
var tagging := false
var tag := Vector2.ZERO
var t := PackedVector2Array()


# The same table as tower.gd's _shade(): up is full, down is dark, and the two
# horizontal axes differ so a corner reads as a corner.
static func shade(n: Vector3) -> float:
	if n.y > 0.5: return 1.0
	if n.y < -0.5: return 0.62
	if absf(n.x) > 0.5: return 0.80
	return 0.90


# THE SAME FOUR NUMBERS, WITHOUT THE STEPS BETWEEN THEM.
#
# shade() answers one of four values, which is exactly what a box wants: a
# face has one normal, and the step from 0.90 to 0.80 at a corner is what
# makes the corner read. Run a curved surface through it and the curve comes
# back as four flat bands -- a faceted box, which is the thing round geometry
# was bought to stop being.
#
# This is that table interpolated over the sphere of directions. It returns
# shade()'s own number for all six axis normals, so a box shaded through this
# is the same box; everything between them is continuous, and a head lit by
# it has a gradient across it rather than a seam. THAT is what makes eight
# facets read as round -- the silhouette alone would not.
static func smooth_shade(n: Vector3) -> float:
	var hl := Vector2(n.x, n.z).length()
	var side := 0.90
	if hl > 0.0001:
		side = 0.90 - 0.10 * absf(n.x) / hl
	if n.y >= 0.0:
		return lerpf(side, 1.0, n.y)
	return lerpf(side, 0.62, -n.y)


func quad(a: Vector3, b: Vector3, d: Vector3, e: Vector3, col: Color, collide := true) -> void:
	var n := (b - a).cross(d - a).normalized()
	var lit := col * shade(n)
	lit.a = 1.0
	# Reversed winding, for the same reason tower.gd reverses it: Godot's front
	# faces run the other way round from the order these are written in.
	for p in [a, d, b, a, e, d]:
		v.append(p)
		c.append(lit)
		if tagging:
			t.append(tag)
	if collide:
		for p in [a, b, d, a, d, e]:
			f.append(p)


func box(mn: Vector3, size: Vector3, col: Color, collide := true, top: Color = Color(0, 0, 0, 0)) -> void:
	var mx := mn + size
	var tcol := top if top.a > 0.0 else col
	quad(Vector3(mn.x, mx.y, mn.z), Vector3(mn.x, mx.y, mx.z), Vector3(mx.x, mx.y, mx.z), Vector3(mx.x, mx.y, mn.z), tcol, collide)
	quad(Vector3(mn.x, mn.y, mn.z), Vector3(mx.x, mn.y, mn.z), Vector3(mx.x, mn.y, mx.z), Vector3(mn.x, mn.y, mx.z), col, collide)
	quad(Vector3(mn.x, mn.y, mn.z), Vector3(mn.x, mx.y, mn.z), Vector3(mx.x, mx.y, mn.z), Vector3(mx.x, mn.y, mn.z), col, collide)
	quad(Vector3(mx.x, mn.y, mx.z), Vector3(mx.x, mx.y, mx.z), Vector3(mn.x, mx.y, mx.z), Vector3(mn.x, mn.y, mx.z), col, collide)
	quad(Vector3(mn.x, mn.y, mx.z), Vector3(mn.x, mx.y, mx.z), Vector3(mn.x, mx.y, mn.z), Vector3(mn.x, mn.y, mn.z), col, collide)
	quad(Vector3(mx.x, mn.y, mn.z), Vector3(mx.x, mx.y, mn.z), Vector3(mx.x, mx.y, mx.z), Vector3(mx.x, mn.y, mx.z), col, collide)


# A LENGTH OF CABLE, between two points that are not on an axis. Everything
# else in this project is an axis-aligned box, and copper is the one thing in a
# building that is never square to anything: it comes out of a socket, it
# sags, it turns a corner with a radius on it. So this is a four-sided prism
# swept along the segment, which at 6 mm across reads as round.
#
# The two horizontal faces get the same shading as a box's, so a cable lying in
# a tray is lit like the tray it is lying in.
func tube(a: Vector3, b: Vector3, r: float, col: Color) -> void:
	var ax := b - a
	var len2 := ax.length()
	if len2 < 1.0e-5:
		return
	ax /= len2
	var up := Vector3(0, 1, 0)
	if absf(ax.y) > 0.94:
		up = Vector3(1, 0, 0)
	var u := ax.cross(up).normalized() * r
	var w := ax.cross(u).normalized() * r
	var ring := [u + w, u - w, -u - w, -u + w]
	for i in range(4):
		var p0: Vector3 = ring[i]
		var p1: Vector3 = ring[(i + 1) % 4]
		# BOTH WINDINGS. The ring runs one way round and the mesh is back-face
		# culled, so a sweep along a line that turns a corner shows its inside
		# on half the bends. A cable is 6 mm thick: there is no inside.
		quad(a + p0, a + p1, b + p1, b + p0, col, false)
		quad(a + p0, b + p0, b + p1, a + p1, col, false)


# ---------------------------------------------------------------- a ROUND one
#
# THE ONE THING IN THIS BUILDING THAT IS NOT A BOX. The owner, on the people:
# "I'd like round heads not squares as that makes this look a bit too
# minecrafty." A head is the one piece of a person that a cuboid cannot stand
# in for, because a square head is the whole of that look.
#
# It is a lathe, not a UV sphere library call, and every parameter of it is
# there to be spent carefully: `lon` facets round, `bands` steps up, and `u0`
# to `u1` is a SLICE of it -- 0 the bottom pole, 1 the top -- so a head's skin
# and the hair over its crown are two calls on the same solid rather than a
# ball with a lid balanced on it. A slice that reaches a pole closes with a
# fan and one that does not is left open, because the geometry inside a head
# is geometry nobody sees.
#
# The cost is countable before it is spent: a band of quads is 2*lon
# triangles and a pole fan is lon, so the eight-sided, three-band head below
# is 32 triangles against the 24 the box-and-hair-slab it replaced. Round is
# not automatically expensive; a round head at the resolution a Bezier patch
# would give you is, and none of that would survive being seen from a
# doorway.
#
# EVERY VERTEX CARRIES ITS OWN SHADE, off the true ellipsoid normal and
# through smooth_shade() -- see the note there. That is the half of this that
# does the work: the facets are what the silhouette needs and the gradient is
# what the face needs, and the second one is free.
#
# No collision faces, ever: the only caller is the crowd, and a person is
# deliberately not a wall. See the note at the top of people.gd.
func orb(mid: Vector3, r: Vector3, col: Color, lon := 8, bands := 3,
		u0 := 0.0, u1 := 1.0) -> void:
	lon = maxi(3, lon)
	bands = maxi(1, bands)
	for bi in range(bands):
		var ua := lerpf(u0, u1, float(bi) / float(bands))
		var ub := lerpf(u0, u1, float(bi + 1) / float(bands))
		for k in range(lon):
			var k1: int = (k + 1) % lon
			var pts: Array = []
			if ua <= 0.0005:                        # closed on the bottom pole
				pts = [_orb_p(mid, r, ua, 0, lon), _orb_p(mid, r, ub, k, lon),
					_orb_p(mid, r, ub, k1, lon)]
			elif ub >= 0.9995:                      # closed on the top pole
				pts = [_orb_p(mid, r, ua, k, lon), _orb_p(mid, r, ua, k1, lon),
					_orb_p(mid, r, ub, 0, lon)]
			else:
				pts = [_orb_p(mid, r, ua, k, lon), _orb_p(mid, r, ua, k1, lon),
					_orb_p(mid, r, ub, k1, lon), _orb_p(mid, r, ub, k, lon)]
			_orb_face(pts, mid, r, col)


# A point on the lathe. `u` runs 0 at the bottom pole to 1 at the top, which
# is the way round a head is built -- from the chin up.
func _orb_p(mid: Vector3, r: Vector3, u: float, k: int, lon: int) -> Vector3:
	var th := PI * u
	var a := TAU * float(k) / float(lon)
	return mid + Vector3(r.x * sin(th) * cos(a), -r.y * cos(th),
		r.z * sin(th) * sin(a))


# One face of it, shaded per vertex and wound to face OUTWARDS -- which is
# checked against the centre rather than trusted to the loop order, because a
# ring that runs the other way round is a head that is inside out and back-face
# culled to nothing, and that is not a mistake worth making twice.
func _orb_face(pts: Array, mid: Vector3, r: Vector3, col: Color) -> void:
	var cols: Array = []
	var ctr := Vector3.ZERO
	for p in pts:
		var d: Vector3 = p - mid
		var n := Vector3(d.x / (r.x * r.x), d.y / (r.y * r.y),
			d.z / (r.z * r.z)).normalized()
		var lit := col * smooth_shade(n)
		lit.a = 1.0
		cols.append(lit)
		ctr += p
	ctr /= float(pts.size())
	var flip: bool = (pts[1] - pts[0]).cross(pts[2] - pts[0]).dot(ctr - mid) < 0.0
	for i in range(1, pts.size() - 1):
		_orb_tri(pts[0], pts[i], pts[i + 1], cols[0], cols[i], cols[i + 1], flip)


func _orb_tri(p0: Vector3, p1: Vector3, p2: Vector3, c0: Color, c1: Color,
		c2: Color, flip: bool) -> void:
	# Reversed winding, the same reversal quad() makes and for the same reason.
	var ps := [p0, p2, p1] if not flip else [p0, p1, p2]
	var cs := [c0, c2, c1] if not flip else [c0, c1, c2]
	for i in range(3):
		v.append(ps[i])
		c.append(cs[i])
		if tagging:
			t.append(tag)


func empty() -> bool:
	return v.size() == 0


func mesh() -> ArrayMesh:
	var m := ArrayMesh.new()
	if v.size() == 0:
		return m
	var arr := []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = v
	arr[Mesh.ARRAY_COLOR] = c
	if tagging and t.size() == v.size():
		arr[Mesh.ARRAY_TEX_UV] = t
	m.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	var mat := StandardMaterial3D.new()
	mat.vertex_color_use_as_albedo = true
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.cull_mode = BaseMaterial3D.CULL_BACK
	for i in range(m.get_surface_count()):
		m.surface_set_material(i, mat)
	return m


# A MeshInstance3D, and a StaticBody3D beside it when anything asked to collide.
func node(nm: String) -> Node3D:
	var root := Node3D.new()
	root.name = nm
	var mi := MeshInstance3D.new()
	mi.name = "mesh"
	mi.mesh = mesh()
	root.add_child(mi)
	if f.size() > 0:
		var body := StaticBody3D.new()
		body.name = "body"
		var shape := ConcavePolygonShape3D.new()
		shape.backface_collision = true
		shape.set_faces(f)
		var cs := CollisionShape3D.new()
		cs.shape = shape
		body.add_child(cs)
		root.add_child(body)
	return root
