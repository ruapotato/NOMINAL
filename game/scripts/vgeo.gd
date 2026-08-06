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
