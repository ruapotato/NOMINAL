# tower.gd — the building, drawn.
#
# Everything here is a VIEW. The geometry comes out of core/building.c through
# the GDExtension: floors, footprints, rooms in metres, and doors as edges. A
# wall exists exactly where two neighbouring square metres belong to different
# rooms and there is no door on that edge -- so a door cannot open into
# brickwork, and nothing in this file decides where a room is.
#
# One ArrayMesh for the whole tower, one trimesh collider from the same
# triangles. That is what makes a hole in a slab free, which is what makes a
# stairwell possible: the run from floor f to f+1 punches its strip out of the
# slab above, the far end stays as a landing, and the next run comes back the
# other way. A switchback, because a straight flight would need a hole the
# length of the room and there would be nowhere to stand at the top.
#
# Nothing is baked: change the seed and the building changes. There are no
# imported models anywhere in this project and there are not going to be.

extends Node3D

# RoomKind, from building.h. The order is the C enum's order.
const K_CORRIDOR := 0
const K_LOBBY := 1
const K_LIFTLOBBY := 2
const K_LIFT := 3
const K_STAIR := 4
const K_RISER := 5
const K_COMMS := 6
const K_MDF := 7
const K_TOILET := 8
const K_PLANT := 9
const K_GOODS := 10
const K_OFFICE := 11
const K_RESIDENCE := 12
const K_SERVER := 13
const K_RETAIL := 14

const NOROOM := 65535

# Floor colour by room kind. A comms cupboard has to read differently from a
# flat from across a corridor, which is the whole job of this table.
const FLOOR_COL := {
	K_CORRIDOR: Color("#8d8b84"),
	K_LOBBY: Color("#b9ad93"),
	K_LIFTLOBBY: Color("#a39a8c"),
	K_LIFT: Color("#3c3f45"),
	K_STAIR: Color("#6f7378"),
	K_RISER: Color("#4a4139"),
	K_COMMS: Color("#2f6f8f"),
	K_MDF: Color("#2f8f6a"),
	K_TOILET: Color("#9fb3bd"),
	K_PLANT: Color("#7a6a55"),
	K_GOODS: Color("#a08349"),
	K_OFFICE: Color("#9b8f7d"),
	K_RESIDENCE: Color("#a8846b"),
	K_SERVER: Color("#4b6f9b"),
	K_RETAIL: Color("#a37f8c"),
}
const WALL_COL := Color("#cfc9bd")
const OUTER_COL := Color("#8f9ba5")
const CEIL_COL := Color("#d6d3cc")
const SLAB_EDGE := Color("#6d6a64")

const SLAB_T := 0.16       # slab thickness, metres
const WALL_T := 0.14
const DOOR_H := 2.05       # head height of a doorway
const EYE := 1.62

# Set before adding to the tree.
var seed_no := 200
var with_desktop := true
var with_player := true

var machine: Object = null

# The building, as read out of the extension. Nothing else reads it.
var bw := 0
var bh := 0
var nfloors := 0
var fheight := 3.0
var rooms: Array = []          # {i,floor,kind,tenant,x0,y0,x1,y1,name}
var floor_rect: Array = []     # per floor [x0,y0,x1,y1]
var cells: Array = []          # per floor PackedInt32Array, bw*bh
var doorset := {}              # "f,x,y,dir" -> true
var doors: Array = []
var stairs: Array = []         # per stair run: {floor,lo,hi,axis,c0,c1,room}

var player: CharacterBody3D = null
var cart: Node3D = null
var devices: Array = []

var _v := PackedVector3Array()
var _c := PackedColorArray()
var _faces := PackedVector3Array()
var _tri_count := 0


func _ready() -> void:
	if machine == null:
		if not ClassDB.class_exists("NominalStation"):
			push_error("NominalStation is not registered - the GDExtension did not load")
			return
		machine = ClassDB.instantiate("NominalStation")
	build(seed_no)


# ---------------------------------------------------------------- the data

func build(s: int) -> bool:
	seed_no = s
	var head: String = str(machine.bld_generate(s))
	if head.strip_edges() == "":
		push_error("tower: the generator refused seed %d" % s)
		return false
	for line in head.split("\n", false):
		var f: PackedStringArray = line.split(" ", false)
		match f[0]:
			"floors": nfloors = int(f[1])
			"plate":
				bw = int(f[1])
				bh = int(f[2])
			"floor_height": fheight = float(f[1])

	floor_rect.clear()
	for line in str(machine.bld_floors()).split("\n", false):
		var f: PackedStringArray = line.split(" ", false)
		floor_rect.append([int(f[2]), int(f[3]), int(f[4]), int(f[5])])

	rooms.clear()
	for line in str(machine.bld_rooms()).split("\n", false):
		var f: PackedStringArray = line.split(" ", false)
		rooms.append({
			"i": int(f[0]), "floor": int(f[1]), "kind": int(f[2]),
			"tenant": int(f[3]), "x0": int(f[4]), "y0": int(f[5]),
			"x1": int(f[6]), "y1": int(f[7]), "name": f[8],
		})

	doors.clear()
	doorset.clear()
	for line in str(machine.bld_doors()).split("\n", false):
		var f: PackedStringArray = line.split(" ", false)
		var d := {"a": int(f[0]), "b": int(f[1]), "floor": int(f[2]),
				"x": int(f[3]), "y": int(f[4]), "dir": int(f[5])}
		doors.append(d)
		doorset["%d,%d,%d,%d" % [d.floor, d.x, d.y, d.dir]] = true

	cells.clear()
	for f in range(nfloors):
		var grid := PackedInt32Array()
		grid.resize(bw * bh)
		var y := 0
		for line in str(machine.bld_cells(f)).split("\n", false):
			var col := 0
			for tok in line.split(" ", false):
				grid[y * bw + col] = int(tok)
				col += 1
			y += 1
		cells.append(grid)

	# The machine in the rack is a REAL ticket, generated from the same seed as
	# the building, so the boot log the serial lead shows is the boot log of a
	# machine that is really in that state.
	machine.take_ticket(s, 1)

	_plan_stairs()
	_build_mesh()
	_place_devices()
	if with_player:
		_spawn_player()
		_spawn_cart()
		_hud()
	_light()
	return true


func room_of(f: int, x: int, y: int) -> int:
	if f < 0 or f >= nfloors or x < 0 or y < 0 or x >= bw or y >= bh:
		return NOROOM
	return cells[f][y * bw + x]


func find_room(f: int, kind: int) -> int:
	for r in rooms:
		if r.floor == f and r.kind == kind:
			return r.i
	return -1


func room_centre(i: int) -> Vector3:
	var r: Dictionary = rooms[i]
	return Vector3((r.x0 + r.x1) * 0.5, r.floor * fheight, (r.y0 + r.y1) * 0.5)


# Metres a PERSON walks from `src` to every room; -1 where there is no route.
func walk_from(src: int) -> PackedFloat32Array:
	var out := PackedFloat32Array()
	for tok in str(machine.bld_walk(src)).split(" ", false):
		out.append(float(tok))
	return out


# ------------------------------------------------------------- the stairs
#
# A switchback per floor: up one side of the stairwell, land, turn, up the
# other way. The run punches its own strip out of the slab above; the landing
# at the far end is what you step onto, and it is the reason the hole is the
# run's length and not the room's.

func _plan_stairs() -> void:
	stairs.clear()
	for r in rooms:
		if r.kind != K_STAIR:
			continue
		if r.floor >= nfloors - 1:
			continue           # nothing above to climb to
		var wx: int = r.x1 - r.x0
		var wy: int = r.y1 - r.y0
		var axis: int = 1 if wy >= wx else 0     # 0 = along x, 1 = along y
		var lo: float = float(r.y0 if axis == 1 else r.x0)
		var hi: float = float(r.y1 if axis == 1 else r.x1)
		var cross_lo: float = float(r.x0 if axis == 1 else r.y0)
		var cross_hi: float = float(r.x1 if axis == 1 else r.y1)
		var rw: float = min(1.7, (cross_hi - cross_lo) * 0.5)
		var landing: float = clamp((hi - lo) * 0.25, 0.9, 1.4)
		# Alternate BOTH ways: even floors climb up one side, odd floors come
		# back up the other. Stacking every flight in the same strip put the
		# next flight directly over this one and a walking body cracked its
		# head on the underside of it two thirds of the way up -- which is why
		# a real stairwell has two flights side by side and not one column.
		var up: bool = (int(r.floor) % 2) == 0
		var a: float = lo if up else hi
		var b: float = (hi - landing) if up else (lo + landing)
		var c0: float = cross_lo if up else cross_hi - rw
		stairs.append({
			"floor": r.floor, "room": r.i, "axis": axis,
			"a": a, "b": b, "up": up,
			"c0": c0, "c1": c0 + rw,
			"lo": min(a, b), "hi": max(a, b),
		})


# The strip of floor f that the run from f-1 punched out, as a Rect2 in metres.
func _hole_on(f: int) -> Array:
	var out: Array = []
	for s in stairs:
		if s.floor != f - 1:
			continue
		if s.axis == 1:
			out.append(Rect2(s.c0, s.lo, s.c1 - s.c0, s.hi - s.lo))
		else:
			out.append(Rect2(s.lo, s.c0, s.hi - s.lo, s.c1 - s.c0))
	return out


# ------------------------------------------------------------- the geometry

func _shade(n: Vector3) -> float:
	# No lights: the shading is in the vertex colours. A flat-lit interior is
	# a pile of boxes -- you cannot see where a wall meets a floor.
	if n.y > 0.5: return 1.0
	if n.y < -0.5: return 0.62
	if absf(n.x) > 0.5: return 0.80
	return 0.90


func _quad(a: Vector3, b: Vector3, c: Vector3, d: Vector3, col: Color, collide: bool) -> void:
	var n := (b - a).cross(c - a).normalized()
	var lit := col * _shade(n)
	lit.a = 1.0
	# ONE SURFACE, vertex-coloured. Sorting quads into a dictionary of per-
	# colour arrays cost quadratic time: reading a PackedVector3Array out of a
	# nested container copies it, so every wall in the tower re-copied every
	# wall built so far and a five-floor building never finished generating.
	for p in [a, b, c, a, c, d]:
		_v.append(p)
		_c.append(lit)
	if collide:
		for p in [a, b, c, a, c, d]:
			_faces.append(p)
		_tri_count += 2


func _box(mn: Vector3, size: Vector3, col: Color, collide := true, top: Color = Color(0, 0, 0, 0)) -> void:
	var mx := mn + size
	var tcol := top if top.a > 0.0 else col
	# top / bottom
	_quad(Vector3(mn.x, mx.y, mn.z), Vector3(mn.x, mx.y, mx.z), Vector3(mx.x, mx.y, mx.z), Vector3(mx.x, mx.y, mn.z), tcol, collide)
	_quad(Vector3(mn.x, mn.y, mn.z), Vector3(mx.x, mn.y, mn.z), Vector3(mx.x, mn.y, mx.z), Vector3(mn.x, mn.y, mx.z), col, collide)
	# -z / +z
	_quad(Vector3(mn.x, mn.y, mn.z), Vector3(mn.x, mx.y, mn.z), Vector3(mx.x, mx.y, mn.z), Vector3(mx.x, mn.y, mn.z), col, collide)
	_quad(Vector3(mx.x, mn.y, mx.z), Vector3(mx.x, mx.y, mx.z), Vector3(mn.x, mx.y, mx.z), Vector3(mn.x, mn.y, mx.z), col, collide)
	# -x / +x
	_quad(Vector3(mn.x, mn.y, mx.z), Vector3(mn.x, mx.y, mx.z), Vector3(mn.x, mx.y, mn.z), Vector3(mn.x, mn.y, mn.z), col, collide)
	_quad(Vector3(mx.x, mn.y, mn.z), Vector3(mx.x, mx.y, mn.z), Vector3(mx.x, mx.y, mx.z), Vector3(mx.x, mn.y, mx.z), col, collide)


func _build_mesh() -> void:
	_v = PackedVector3Array()
	_c = PackedColorArray()
	_faces = PackedVector3Array()
	_tri_count = 0

	for f in range(nfloors):
		_slabs(f)
		_walls(f)
	for s in stairs:
		_stair_run(s)
	_roof()

	var mesh := ArrayMesh.new()
	var arr := []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = _v
	arr[Mesh.ARRAY_COLOR] = _c
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	var mat := StandardMaterial3D.new()
	mat.vertex_color_use_as_albedo = true
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.cull_mode = BaseMaterial3D.CULL_BACK
	for i in range(mesh.get_surface_count()):
		mesh.surface_set_material(i, mat)

	var mi := MeshInstance3D.new()
	mi.name = "TowerMesh"
	mi.mesh = mesh
	add_child(mi)

	var body := StaticBody3D.new()
	body.name = "TowerBody"
	var shape := ConcavePolygonShape3D.new()
	# BOTH SIDES COLLIDE. A trimesh is one-sided by default, and a stair ramp
	# whose winding came out the wrong way is a ramp you fall straight through
	# -- which is indistinguishable, from inside the game, from having no
	# stairs at all. A building has no face you are meant to pass through.
	shape.backface_collision = true
	shape.set_faces(_faces)
	var cs := CollisionShape3D.new()
	cs.shape = shape
	body.add_child(cs)
	add_child(body)


func triangle_count() -> int:
	return _tri_count


# Slabs, run-length encoded along x so a floor is a few hundred boxes rather
# than a thousand -- and cut where a stairwell comes up through it.
func _slabs(f: int) -> void:
	var holes := _hole_on(f)
	var base := f * fheight
	for y in range(bh):
		var x := 0
		while x < bw:
			var r := room_of(f, x, y)
			if r == NOROOM:
				x += 1
				continue
			var x2 := x
			while x2 < bw and room_of(f, x2, y) == r:
				x2 += 1
			var kind: int = rooms[r].kind
			var col: Color = FLOOR_COL.get(kind, Color("#909090"))
			_emit_slab(Rect2(x, y, x2 - x, 1), base, col, holes)
			x = x2


func _emit_slab(rect: Rect2, base: float, col: Color, holes: Array) -> void:
	# Subtract every hole from the strip and emit what is left. A strip is one
	# metre deep, so the subtraction only ever splits it along x.
	var pieces: Array = [rect]
	for hole in holes:
		var next: Array = []
		for p in pieces:
			if not p.intersects(hole):
				next.append(p)
				continue
			var iy0: float = max(p.position.y, hole.position.y)
			var iy1: float = min(p.end.y, hole.end.y)
			if iy1 - iy0 < 0.01:
				next.append(p)
				continue
			# Depth-wise split first (the strip is 1 m, holes are metre-aligned
			# only along the run, so clip in both directions properly).
			if p.position.y < iy0 - 0.01:
				next.append(Rect2(p.position.x, p.position.y, p.size.x, iy0 - p.position.y))
			if p.end.y > iy1 + 0.01:
				next.append(Rect2(p.position.x, iy1, p.size.x, p.end.y - iy1))
			var mid := Rect2(p.position.x, iy0, p.size.x, iy1 - iy0)
			var ix0: float = max(mid.position.x, hole.position.x)
			var ix1: float = min(mid.end.x, hole.end.x)
			if mid.position.x < ix0 - 0.01:
				next.append(Rect2(mid.position.x, mid.position.y, ix0 - mid.position.x, mid.size.y))
			if mid.end.x > ix1 + 0.01:
				next.append(Rect2(ix1, mid.position.y, mid.end.x - ix1, mid.size.y))
		pieces = next
	for p in pieces:
		if p.size.x < 0.01 or p.size.y < 0.01:
			continue
		_box(Vector3(p.position.x, base - SLAB_T, p.position.y),
			Vector3(p.size.x, SLAB_T, p.size.y), SLAB_EDGE, true, col)


func _walls(f: int) -> void:
	var base := f * fheight
	var top := fheight - SLAB_T
	for y in range(bh):
		for x in range(bw):
			var a := room_of(f, x, y)
			for dir in [0, 1]:
				var b := room_of(f, x + 1, y) if dir == 0 else room_of(f, x, y + 1)
				if a == b:
					continue
				if a == NOROOM and b == NOROOM:
					continue
				var outer := (a == NOROOM or b == NOROOM)
				var col: Color = OUTER_COL if outer else WALL_COL
				var mn: Vector3
				var size: Vector3
				if dir == 0:
					mn = Vector3(x + 1 - WALL_T * 0.5, base, y)
					size = Vector3(WALL_T, top, 1)
				else:
					mn = Vector3(x, base, y + 1 - WALL_T * 0.5)
					size = Vector3(1, top, WALL_T)
				if doorset.has("%d,%d,%d,%d" % [f, x, y, dir]):
					# A DOORWAY IS A GAP, not a decal. The head above it stays,
					# so the opening reads as a door and not as a missing wall.
					if top > DOOR_H:
						var lin := mn
						lin.y = base + DOOR_H
						var ls := size
						ls.y = top - DOOR_H
						_box(lin, ls, col)
					continue
				_box(mn, size, col)


func _roof() -> void:
	var f := nfloors - 1
	var base := (f + 1) * fheight
	for y in range(bh):
		var x := 0
		while x < bw:
			if room_of(f, x, y) == NOROOM:
				x += 1
				continue
			var x2 := x
			while x2 < bw and room_of(f, x2, y) != NOROOM:
				x2 += 1
			_box(Vector3(x, base - SLAB_T, y), Vector3(x2 - x, SLAB_T, 1), CEIL_COL)
			x = x2


func _stair_run(s: Dictionary) -> void:
	var base: float = s.floor * fheight
	var length: float = absf(s.b - s.a)
	if length < 1.0:
		return
	var n: int = int(max(8.0, min(24.0, floor(fheight / 0.17))))
	var rise: float = fheight / float(n)
	var going: float = length / float(n)
	var dirsign: float = 1.0 if s.b > s.a else -1.0
	# The steps are what you SEE. The collider is the incline under them: a
	# capsule cannot climb a 0.18 m box without step handling, and a player who
	# cannot get upstairs has a building of disconnected slabs.
	for i in range(n):
		var t0: float = s.a + dirsign * going * i
		var t1: float = t0 + dirsign * going
		var lo: float = min(t0, t1)
		var col := Color("#8b8f94")
		if s.axis == 1:
			_box(Vector3(s.c0, base + rise * i, lo), Vector3(s.c1 - s.c0, rise + 0.02, going), col, false)
		else:
			_box(Vector3(lo, base + rise * i, s.c0), Vector3(going, rise + 0.02, s.c1 - s.c0), col, false)
	# the incline, as two triangles with the normal upward
	var y0 := base
	var y1 := base + fheight
	if s.axis == 1:
		_ramp(Vector3(s.c0, y0, s.a), Vector3(s.c1, y0, s.a),
			Vector3(s.c1, y1, s.b), Vector3(s.c0, y1, s.b))
	else:
		_ramp(Vector3(s.a, y0, s.c0), Vector3(s.a, y0, s.c1), Vector3(s.b, y1, s.c1), Vector3(s.b, y1, s.c0))


# An invisible ramp: collision only, no triangles in the visible mesh.
func _ramp(a: Vector3, b: Vector3, c: Vector3, d: Vector3) -> void:
	var n := (b - a).cross(c - a).normalized()
	var quad := [a, b, c, a, c, d]
	if n.y < 0.0:
		quad = [a, d, c, a, c, b]
	for p in quad:
		_faces.append(p)
	_tri_count += 2


func _light() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color("#101418")
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(1, 1, 1)
	env.ambient_light_energy = 1.0
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)


# ------------------------------------------------------------- the player

func spawn_point() -> Vector3:
	# The entrance lobby, or failing that anything on the ground floor you can
	# stand in. Never a riser or a lift shaft: you cannot walk into either.
	var i := find_room(0, K_LOBBY)
	if i < 0: i = find_room(0, K_GOODS)
	if i < 0:
		for r in rooms:
			if r.floor == 0 and r.kind != K_RISER and r.kind != K_LIFT:
				i = r.i
				break
	var p := room_centre(i)
	p.y = 0.1
	return p


func _spawn_player() -> void:
	player = preload("res://scripts/walker.gd").new()
	player.name = "Player"
	add_child(player)
	player.global_position = spawn_point() + Vector3(0, 0.2, 0)


# ------------------------------------------------------------- the devices
#
# What you can plug into. A device is a place in the building plus WHICH real
# machine is behind it, and what sockets it has on the back. The sockets are
# the point: a rack server has no display output, so HDMI gets you nothing
# from it and serial gets you its console -- including the console of a
# machine that never finished booting, which is the interesting one.

func _place_devices() -> void:
	devices.clear()
	var mdf := find_room(0, K_MDF)
	if mdf < 0: mdf = find_room(0, K_COMMS)
	if mdf >= 0:
		var r: Dictionary = rooms[mdf]
		# your workstation: a desk machine, so it has a screen output
		_add_device("workstation", 0, true, true,
			Vector3(r.x0 + 1.2, 0, r.y0 + 1.2), Vector3(0.6, 0.5, 0.6), Color("#3a3f46"))
		# the customer's machine, racked. A rack server has serial and no
		# display output at all, which is why the crash cart carries both leads.
		_add_device("rack server", 1, false, true,
			Vector3(r.x0 + 1.2, 0, r.y0 + 3.0), Vector3(0.7, 1.9, 1.0), Color("#23262b"))
	for f in range(nfloors):
		var c := find_room(f, K_COMMS)
		if c < 0:
			continue
		var r: Dictionary = rooms[c]
		# A PATCH PANEL IS PASSIVE. No processor, no console, no ports to plug
		# a lead into -- and saying so is true, where inventing a console for
		# it would be the exact lie this project does not tell.
		_add_device("patch panel", -1, false, false,
			Vector3(r.x0 + 0.6, f * fheight, r.y0 + 0.6), Vector3(0.5, 1.6, 0.9), Color("#4a4033"))


func _add_device(dname: String, which: int, hdmi: bool, serial: bool,
		pos: Vector3, size: Vector3, col: Color) -> void:
	var n := Node3D.new()
	n.name = dname.replace(" ", "_") + "_%d" % devices.size()
	n.position = pos + Vector3(size.x * 0.5, size.y * 0.5, size.z * 0.5)
	var mi := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = size
	mi.mesh = bm
	var mat := StandardMaterial3D.new()
	mat.albedo_color = col
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mi.material_override = mat
	n.add_child(mi)
	var body := StaticBody3D.new()
	var cs := CollisionShape3D.new()
	var bs := BoxShape3D.new()
	bs.size = size
	cs.shape = bs
	body.add_child(cs)
	n.add_child(body)
	add_child(n)
	devices.append({"name": dname, "which": which, "hdmi": hdmi,
		"serial": serial, "node": n, "pos": n.position})


func nearest_device(from: Vector3, radius := 2.2) -> int:
	var best := -1
	var bestd := radius
	for i in range(devices.size()):
		var d: float = from.distance_to(devices[i].pos)
		if d < bestd:
			bestd = d
			best = i
	return best


func _spawn_cart() -> void:
	# CARRIED, not pushed. An inventory holds more than a real person does, and
	# a trolley that has to be shoved through every doorway is physics work
	# that teaches nothing about a network. It hangs off the camera and comes
	# up in front of you when a lead goes in.
	cart = preload("res://scripts/crashcart.gd").new()
	cart.tower = self
	cart.with_desktop = with_desktop
	cart.position = Vector3(0.34, -1.34, -0.95)
	player.cam.add_child(cart)


# ------------------------------------------------------------ the headless API
#
# Everything a player can do here has to be doable without a window, because a
# blind playtest cannot navigate 3D from screenshots and blind playtests are
# what have found every bug in this project.

func teleport(p: Vector3) -> void:
	if player:
		player.velocity = Vector3.ZERO
		player.global_position = p


func player_floor() -> int:
	if player == null:
		return -1
	return int(floor((player.global_position.y + 0.3) / fheight))


# ------------------------------------------------------------------- the HUD

var hud: Label = null

func _hud() -> void:
	var layer := CanvasLayer.new()
	hud = Label.new()
	hud.add_theme_font_override("font", preload("res://scripts/uifont.gd").mono())
	hud.add_theme_font_size_override("font_size", 15)
	hud.add_theme_color_override("font_color", Color("#e8eef4"))
	hud.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.85))
	hud.add_theme_constant_override("shadow_offset_x", 1)
	hud.add_theme_constant_override("shadow_offset_y", 1)
	hud.position = Vector2(14, 12)
	layer.add_child(hud)
	add_child(layer)


func where_am_i() -> String:
	if player == null:
		return ""
	var f := player_floor()
	var p := player.global_position
	var r := room_of(f, int(floor(p.x)), int(floor(p.z)))
	var what: String = "outside" if r == NOROOM else str(rooms[r].name)
	return "floor %d  %s  (%.0f, %.0f m)" % [f, what, p.x, p.z]


func _process(_dt: float) -> void:
	if player == null:
		return
	var near := nearest_device(player.global_position)
	if hud:
		var t := where_am_i()
		if cart:
			t += "\ncart: " + str(cart.status)
		if near >= 0:
			t += "\n%s in reach   [F] serial lead   [H] HDMI lead   [U] unplug" % devices[near].name
		hud.text = t
	if Input.is_key_pressed(KEY_F) and not _f_down:
		if near >= 0 and cart:
			print(cart.plug(near, "serial"))
	_f_down = Input.is_key_pressed(KEY_F)
	if Input.is_key_pressed(KEY_H) and not _h_down:
		if near >= 0 and cart:
			print(cart.plug(near, "hdmi"))
	_h_down = Input.is_key_pressed(KEY_H)
	if Input.is_key_pressed(KEY_U) and not _u_down and cart:
		cart.unplug()
	_u_down = Input.is_key_pressed(KEY_U)

var _f_down := false
var _h_down := false
var _u_down := false
