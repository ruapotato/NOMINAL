extends Node3D
# THE SHIP, BUILT FROM scenes/ship.tscn AND NOTHING ELSE.
#
# David: "Just write the topology manually into a TSCN file... The C layer will
# only be used for the computer core."
#
# So there is no generator. The scene IS the ship: open scenes/ship.tscn, drag
# a box, and that room changes. This file only turns room volumes into
# geometry you can walk.
#
# THE RULE FOR AUTHORING, and it is the whole interface:
#
#   * every CSGBox3D under a Deck node is a ROOM VOLUME. Its floor is the
#     bottom of the box, its ceiling the top, so the box height IS the clear
#     head. Four metres of box is four metres to stand up in.
#   * where two boxes OVERLAP, there is a doorway. That is how rooms join, and
#     how a long thin "Spine" box becomes a corridor serving everything it
#     touches.
#   * a Deck node's Y is the deck's floor height.
#   * Marker3D under Shafts named Lift*_a_b or Stair*_a_b is a shaft serving
#     decks a..b.
#
# Nothing here reads the C model. The previous version generated a 600 m hull
# with 290,952 triangles in a single concave collision shape, which is what was
# stopping the game from loading at all.

const WALL_T := 0.3
const FLOOR_T := 0.3
const SHAFT_R := 3.0          # half-width of a shaft's footprint

const COL_FLOOR := Color("#39424a")
const COL_WALL  := Color("#2f363c")
const COL_CEIL  := Color("#242a2f")
const COL_LIFT  := Color("#8a6a34")
const COL_STAIR := Color("#4e6a52")

var rooms: Array = []         # {name, deck, aabb}
var shafts: Array = []        # {name, kind, x, z, d0, d1}
var deck_y: Array = []        # deck index -> floor height
var player: CharacterBody3D = null

var _v := PackedVector3Array()
var _c := PackedColorArray()
var _faces := PackedVector3Array()
var _tris := 0


func _ready() -> void:
	_read_scene()
	_build()
	_spawn()


# ------------------------------------------------------------------ scene

func _read_scene() -> void:
	for child in get_children():
		var n := String(child.name)
		if n == "Shafts":
			for m in child.get_children():
				_read_shaft(m)
			continue
		if not n.begins_with("Deck"):
			continue
		var deck := int(n.substr(4))
		while deck_y.size() <= deck:
			deck_y.append(0.0)
		deck_y[deck] = child.position.y
		for box in child.get_children():
			if not (box is CSGBox3D):
				continue
			var b: CSGBox3D = box
			var c: Vector3 = child.position + b.position
			var s: Vector3 = b.size
			rooms.append({
				"name": String(b.name), "deck": deck,
				"x0": c.x - s.x * 0.5, "x1": c.x + s.x * 0.5,
				"y0": c.y - s.y * 0.5, "y1": c.y + s.y * 0.5,
				"z0": c.z - s.z * 0.5, "z1": c.z + s.z * 0.5,
			})
			# THE BOX IS AUTHORING, NOT GEOMETRY. It is hidden at run time --
			# what you see is the floor, walls and ceiling built from it.
			b.visible = false


func _read_shaft(m: Node) -> void:
	var parts: PackedStringArray = String(m.name).split("_")
	if parts.size() < 3:
		push_warning("shaft %s needs a name like LiftMid_0_3" % m.name)
		return
	shafts.append({
		"name": parts[0],
		"kind": 0 if String(m.name).begins_with("Lift") else 1,
		"x": (m as Node3D).position.x, "z": (m as Node3D).position.z,
		"d0": int(parts[1]), "d1": int(parts[2]),
	})


# ------------------------------------------------------------------- mesh

func _quad(a: Vector3, b: Vector3, c: Vector3, d: Vector3, col: Color,
		collide := true) -> void:
	for t in [[a, b, c], [a, c, d]]:
		for p in t:
			_v.append(p)
			_c.append(col)
		if collide:
			for p in t:
				_faces.append(p)
		_tris += 1


func _box(mn: Vector3, size: Vector3, col: Color, collide := true) -> void:
	var mx := mn + size
	_quad(Vector3(mn.x, mx.y, mn.z), Vector3(mx.x, mx.y, mn.z),
		Vector3(mx.x, mx.y, mx.z), Vector3(mn.x, mx.y, mx.z), col, collide)
	_quad(Vector3(mn.x, mn.y, mx.z), Vector3(mx.x, mn.y, mx.z),
		Vector3(mx.x, mn.y, mn.z), Vector3(mn.x, mn.y, mn.z), col, collide)
	_quad(Vector3(mn.x, mn.y, mn.z), Vector3(mx.x, mn.y, mn.z),
		Vector3(mx.x, mx.y, mn.z), Vector3(mn.x, mx.y, mn.z), col, collide)
	_quad(Vector3(mx.x, mn.y, mx.z), Vector3(mn.x, mn.y, mx.z),
		Vector3(mn.x, mx.y, mx.z), Vector3(mx.x, mx.y, mx.z), col, collide)
	_quad(Vector3(mn.x, mn.y, mx.z), Vector3(mn.x, mn.y, mn.z),
		Vector3(mn.x, mx.y, mn.z), Vector3(mn.x, mx.y, mx.z), col, collide)
	_quad(Vector3(mx.x, mn.y, mn.z), Vector3(mx.x, mn.y, mx.z),
		Vector3(mx.x, mx.y, mx.z), Vector3(mx.x, mx.y, mn.z), col, collide)


# Do these two rooms overlap enough to be a doorway rather than a touch?
func _joined(a: Dictionary, b: Dictionary) -> bool:
	if int(a.deck) != int(b.deck):
		return false
	var ox: float = min(a.x1, b.x1) - max(a.x0, b.x0)
	var oz: float = min(a.z1, b.z1) - max(a.z0, b.z0)
	return ox > 0.5 and oz > 0.5


# A wall runs along one side of a room, minus the stretches where another room
# overlaps it -- which is what turns an overlap into a doorway without anybody
# placing a door.
func _wall(r: Dictionary, along_x: bool, at: float, other_lo: float,
		other_hi: float, col: Color) -> void:
	var cuts: Array = []
	for o in rooms:
		if o == r or not _joined(r, o):
			continue
		if along_x:
			if o.z0 - 0.01 <= at and at <= o.z1 + 0.01:
				cuts.append([max(other_lo, o.x0), min(other_hi, o.x1)])
		else:
			if o.x0 - 0.01 <= at and at <= o.x1 + 0.01:
				cuts.append([max(other_lo, o.z0), min(other_hi, o.z1)])
	cuts.sort_custom(func(p, q): return p[0] < q[0])
	var pos := other_lo
	var h: float = r.y1 - r.y0
	for cut in cuts:
		if cut[0] > pos:
			_emit_wall(r, along_x, at, pos, cut[0], h, col)
		pos = max(pos, cut[1])
	if pos < other_hi:
		_emit_wall(r, along_x, at, pos, other_hi, h, col)


func _emit_wall(r: Dictionary, along_x: bool, at: float, lo: float, hi: float,
		h: float, col: Color) -> void:
	if hi - lo <= 0.05:
		return
	if along_x:
		_box(Vector3(lo, r.y0, at - WALL_T * 0.5), Vector3(hi - lo, h, WALL_T), col)
	else:
		_box(Vector3(at - WALL_T * 0.5, r.y0, lo), Vector3(WALL_T, h, hi - lo), col)


func _shaft_hole(r: Dictionary, x: float, z: float) -> bool:
	for sh in shafts:
		if int(r.deck) < int(sh.d0) or int(r.deck) > int(sh.d1):
			continue
		if absf(x - float(sh.x)) <= SHAFT_R and absf(z - float(sh.z)) <= SHAFT_R:
			return true
	return false


func _build() -> void:
	for r in rooms:
		# THE FLOOR, in strips, so a shaft passing through leaves a real hole
		# rather than a mark. Strips are 2 m: fine enough for a 6 m shaft and
		# coarse enough to keep the triangle count sane.
		var z := float(r.z0)
		while z < r.z1 - 0.01:
			var zn: float = min(z + 2.0, r.z1)
			var x := float(r.x0)
			while x < r.x1 - 0.01:
				var xn: float = min(x + 2.0, r.x1)
				if not _shaft_hole(r, (x + xn) * 0.5, (z + zn) * 0.5):
					_box(Vector3(x, r.y0 - FLOOR_T, z),
						Vector3(xn - x, FLOOR_T, zn - z), COL_FLOOR)
				x = xn
			z = zn
		# the ceiling, which is also the deck above's underside
		_box(Vector3(r.x0, r.y1, r.z0),
			Vector3(r.x1 - r.x0, 0.2, r.z1 - r.z0), COL_CEIL)
		# and four walls with the doorways cut out of them
		_wall(r, true, r.z0, r.x0, r.x1, COL_WALL)
		_wall(r, true, r.z1, r.x0, r.x1, COL_WALL)
		_wall(r, false, r.x0, r.z0, r.z1, COL_WALL)
		_wall(r, false, r.x1, r.z0, r.z1, COL_WALL)

	# the shafts, marked so you can find them
	for sh in shafts:
		var col: Color = COL_LIFT if int(sh.kind) == 0 else COL_STAIR
		for d in range(int(sh.d0), int(sh.d1) + 1):
			if d >= deck_y.size():
				continue
			var y: float = deck_y[d]
			for side in [-SHAFT_R, SHAFT_R - 0.4]:
				_box(Vector3(sh.x - SHAFT_R, y - FLOOR_T, sh.z + side),
					Vector3(SHAFT_R * 2.0, 0.25, 0.4), col)
		# a floor at the bottom so it is a shaft and not a pit
		_box(Vector3(sh.x - SHAFT_R, deck_y[int(sh.d0)] - FLOOR_T, sh.z - SHAFT_R),
			Vector3(SHAFT_R * 2.0, FLOOR_T, SHAFT_R * 2.0), col)

	var arr := []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = _v
	arr[Mesh.ARRAY_COLOR] = _c
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	var mat := StandardMaterial3D.new()
	mat.vertex_color_use_as_albedo = true
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mesh.surface_set_material(0, mat)
	var mi := MeshInstance3D.new()
	mi.name = "ShipMesh"
	mi.mesh = mesh
	add_child(mi)

	var body := StaticBody3D.new()
	body.name = "ShipBody"
	var shape := ConcavePolygonShape3D.new()
	shape.backface_collision = true
	shape.set_faces(_faces)
	var cs := CollisionShape3D.new()
	cs.shape = shape
	body.add_child(cs)
	add_child(body)
	print("ship: %d rooms, %d decks, %d shafts, %d triangles"
		% [rooms.size(), deck_y.size(), shafts.size(), _tris])


# ------------------------------------------------------------------ player

func _spawn() -> void:
	var ps := load("res://scenes/player.tscn") as PackedScene
	player = ps.instantiate() if ps != null else preload("res://scripts/walker.gd").new()
	add_child(player)
	for r in rooms:
		if String(r.name) == "MainEngineering":
			player.global_position = Vector3((r.x0 + r.x1) * 0.5, r.y0 + 1.2,
				(r.z0 + r.z1) * 0.5)
			return
	if not rooms.is_empty():
		var r: Dictionary = rooms[0]
		player.global_position = Vector3((r.x0 + r.x1) * 0.5, r.y0 + 1.2,
			(r.z0 + r.z1) * 0.5)


# ------------------------------------------------------------------- lifts

func ride(dir: int) -> bool:
	if player == null:
		return false
	var p: Vector3 = player.global_position
	for sh in shafts:
		if absf(p.x - float(sh.x)) > SHAFT_R or absf(p.z - float(sh.z)) > SHAFT_R:
			continue
		var here := 0
		var best := 1e9
		for d in range(deck_y.size()):
			if absf(float(deck_y[d]) - p.y) < best:
				best = absf(float(deck_y[d]) - p.y)
				here = d
		var want: int = clampi(here + dir, int(sh.d0), int(sh.d1))
		if want == here:
			return false
		player.global_position = Vector3(p.x, float(deck_y[want]) + 1.1, p.z)
		player.velocity = Vector3.ZERO
		print("deck %d" % want)
		return true
	return false


func _unhandled_input(event: InputEvent) -> void:
	if not (event is InputEventKey) or not event.pressed or event.echo:
		return
	match (event as InputEventKey).keycode:
		KEY_E: ride(1)
		KEY_Q: ride(-1)
