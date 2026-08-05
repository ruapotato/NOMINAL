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
