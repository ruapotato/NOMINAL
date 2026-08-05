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
	K_COMMS: Color("#3c6072"),
	K_MDF: Color("#3d6b5c"),
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
const SKIRT_COL := Color("#8a8378")

const SLAB_T := 0.16       # slab thickness, metres
const WALL_T := 0.14
const DOOR_H := 2.05       # head height of a doorway
const EYE := 1.62

# The furniture of the job. A room is not a coloured volume: it is a rack with
# things bolted into it, a tray over the corridor with cable in it, and a sign
# telling you which floor you are on.
const RACK_W := 0.60       # a 19" rack is 600 mm wide, because a floor tile is
const RACK_D := 1.00
const RACK_H := 2.00
const U := 0.04445         # one rack unit, 1.75", which is where 1U comes from
const RACK_U := 42
const RACK_BASE := 0.10    # the plinth: U 1 starts here
const RACK_COL := Color("#25282d")
const RACK_RAIL := Color("#3a3f46")
const TRAY_COL := Color("#6b7078")
const CABLE_COL := [Color("#2f6fd0"), Color("#cf5a3a"), Color("#3fae6a"),
	Color("#d9c04a"), Color("#a05fd0")]

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

# THE TOWER GROWS. The generator makes the whole building up front and that is
# right -- it is the SPACE, and the space is there whether or not anybody is
# paying for it. What is NOT there on day one is the tower in service: the
# lift offers the ground floor and the one above it, and open_next_floor()
# brings the next one up. The owner: "an elevator should be the thing that
# takes you to new floors, those get added during gameplay."
#
# The stairs still go everywhere, deliberately. A lift that has stopped is a
# fault this game should be able to have one day, and a building where that
# fault strands you is a building nobody can play.
var floors_in_service := 2

var lifts: Array = []          # lift.gd instances, one per shaft
var racks: Array = []          # {room, floor, i, x, z, face, used}
var site_up := false
var _cable_node: MeshInstance3D = null
var _cable_from := -1          # the device whose port the spool is on

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
	_plan_racks()
	_site_start()
	_build_mesh()
	_build_lifts()
	_signage()
	_place_devices()
	_draw_cables()
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
# A LIFT SHAFT IS A HOLE ALL THE WAY DOWN. It used to be filled in on every
# floor, which is why the shaft read as sealed geometry and the door onto it
# opened into a slab. The landing doors are what keeps you out of it, not the
# floor, exactly as in a real one.
func _hole_on(f: int) -> Array:
	var out: Array = []
	for r in rooms:
		if r.kind == K_LIFT and r.floor == f:
			out.append(Rect2(r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0))
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
	# REVERSED WINDING. Godot's front faces run the other way round from the
	# order these quads are written in, so every box was showing its far side:
	# you stood on a floor and saw the underside of the slab, with the ceiling
	# painted in the colour of the room above it.
	for p in [a, c, b, a, d, c]:
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
		_trays(f)
	for s in stairs:
		_stair_run(s)
	for i in range(racks.size()):
		_rack_geom(i)
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
	# FROM -1, not 0. Each cell only ever looks at its +x and +y neighbour, so
	# starting at zero never examines the edge between "outside" and the first
	# column -- and the west and north faces of the building were missing
	# entirely. You stood in an office looking out at the void.
	for y in range(-1, bh):
		for x in range(-1, bw):
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
				# A SKIRTING BOARD. Two blank surfaces meeting at a line read as
				# two boxes; the same two with a rail along the bottom read as a
				# room, because it gives the eye something the right size to
				# measure the space against. It is 120 mm, like a real one.
				var skirt_mn := mn
				var skirt_sz := size
				skirt_sz.y = 0.12
				if dir == 0:
					skirt_mn.x -= 0.025
					skirt_sz.x += 0.05
				else:
					skirt_mn.z -= 0.025
					skirt_sz.z += 0.05
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
				_box(skirt_mn, skirt_sz, SKIRT_COL, false)


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


# ------------------------------------------------------------- the racks
#
# The comparable game is Tower Networking Inc., and racks, patch panels and
# cable runs are its entire visual language. Ours is first person, so the same
# furniture has to be there at the size a person meets it: a 600 mm frame, 42
# U of it, and the gear bolted in at the height you would actually reach.
#
# A rack is not decoration. It is where a device GOES: _place_devices() asks
# for U positions out of these frames, so a box in the game is a box in a rack
# and its height off the floor is the U it was mounted at.

func _plan_racks() -> void:
	racks.clear()
	for r in rooms:
		var n := 0
		match r.kind:
			K_MDF: n = 6
			K_COMMS: n = 1
			K_SERVER: n = 3
			_: continue
		var wx: int = r.x1 - r.x0
		var wy: int = r.y1 - r.y0
		var along_x: bool = wx >= wy
		var span: int = wx if along_x else wy
		# A rack needs 600 mm and a person needs to get past it.
		n = min(n, int((span - 1.4) / 0.90))
		# CENTRED ON THE ROOM, with a gap between frames. A row shoved into one
		# corner is a row you only ever see end-on, which is what the first
		# screenshot of this was: two black cages edge-on across an empty floor.
		var pitch := 0.90
		var run: float = float(n - 1) * pitch + RACK_W
		var mid: float = (r.x0 + r.x1) * 0.5 if along_x else (r.y0 + r.y1) * 0.5
		for i in range(n):
			var d := {"room": r.i, "floor": r.floor, "along_x": along_x,
					"next_u": 36, "x": 0.0, "z": 0.0}
			if along_x:
				d.x = mid - run * 0.5 + i * pitch
				d.z = r.y0 + 0.35
			else:
				d.x = r.x0 + 0.35
				d.z = mid - run * 0.5 + i * pitch
			racks.append(d)


func _rack_geom(i: int) -> void:
	var k: Dictionary = racks[i]
	var base: float = k.floor * fheight
	var w: float = RACK_W if k.along_x else RACK_D
	var d: float = RACK_D if k.along_x else RACK_W
	var o := Vector3(k.x, base, k.z)
	# plinth and top
	_box(o, Vector3(w, 0.08, d), RACK_COL)
	_box(o + Vector3(0, RACK_H - 0.05, 0), Vector3(w, 0.05, d), RACK_COL)
	# four uprights
	for ax in [0.0, w - 0.06]:
		for az in [0.0, d - 0.06]:
			_box(o + Vector3(ax, 0.08, az), Vector3(0.06, RACK_H - 0.13, 0.06), RACK_COL)
	# The two punched rails at the front, and the U holes in them. The holes
	# are what makes it read as a rack rather than a wardrobe: they give the
	# eye a repeat at 1.75 inches, which is the size everything else is in.
	var front := 0.0 if k.along_x else d - 0.06
	if k.along_x: front = d - 0.06
	for s in [0.10, w - 0.13]:
		var rm := o + (Vector3(s, RACK_BASE, front) if k.along_x else Vector3(front, RACK_BASE, s))
		var rs := Vector3(0.03, RACK_U * U, 0.06) if k.along_x else Vector3(0.06, RACK_U * U, 0.03)
		_box(rm, rs, RACK_RAIL, false)
		for uu in range(0, RACK_U, 3):
			var hm := rm + Vector3(0, uu * U + U * 0.35, 0)
			if k.along_x: hm.z += 0.055
			else: hm.x += 0.055
			var hs := Vector3(0.03, 0.012, 0.012) if k.along_x else Vector3(0.012, 0.012, 0.03)
			_box(hm, hs, Color("#8d949c"), false)


# Take `nu` rack units out of frame `i`, filling downwards from eye height,
# and say where the box goes and which way its front faces.
func _rack_slot(i: int, nu: int) -> Dictionary:
	var k: Dictionary = racks[i]
	var top: int = k.next_u
	if top - nu < 1:
		return {}
	k.next_u = top - nu - 1        # a gap, so two boxes are two boxes
	racks[i] = k
	var y: float = k.floor * fheight + RACK_BASE + float(top - nu) * U
	var h: float = float(nu) * U
	if k.along_x:
		return {"mn": Vector3(k.x + 0.06, y, k.z + RACK_D - 0.74),
				"size": Vector3(RACK_W - 0.12, h, 0.68), "face": Vector3(0, 0, 1)}
	return {"mn": Vector3(k.x + RACK_D - 0.74, y, k.z + 0.06),
			"size": Vector3(0.68, h, RACK_W - 0.12), "face": Vector3(1, 0, 0)}


# A box standing on the floor of a room, in a row along the low wall, front
# out. This is where a delivery sits until somebody carries it: the height is
# a box on the floor and not a slot at eye level, so it reads as kit that has
# not been racked yet rather than as kit that has.
func _floor_slot(room: int, k: int, nu: int) -> Dictionary:
	var r: Dictionary = rooms[room]
	var h: float = max(0.12, float(nu) * U)
	var y: float = r.floor * fheight + 0.02
	var along_x: bool = (r.x1 - r.x0) >= (r.y1 - r.y0)
	var step: float = 0.85
	if along_x:
		var x: float = float(r.x0) + 0.9 + float(k) * step
		x = min(x, float(r.x1) - 1.0)
		return {"mn": Vector3(x, y, float(r.y0) + 0.7),
				"size": Vector3(0.62, h, 0.62), "face": Vector3(0, 0, 1)}
	var z: float = float(r.y0) + 0.9 + float(k) * step
	z = min(z, float(r.y1) - 1.0)
	return {"mn": Vector3(float(r.x0) + 0.7, y, z),
			"size": Vector3(0.62, h, 0.62), "face": Vector3(1, 0, 0)}


func racks_in(room: int) -> Array:
	var out: Array = []
	for i in range(racks.size()):
		if racks[i].room == room:
			out.append(i)
	return out


# The same frames, in the order somebody would actually fill them: from the
# middle of the row outwards. A row of six with everything in the end one is a
# row you are always standing at the wrong end of.
func racks_in_fill_order(room: int) -> Array:
	var row := racks_in(room)
	var out: Array = []
	var mid := row.size() / 2
	for d in range(row.size()):
		if mid + d < row.size(): out.append(row[mid + d])
		if d > 0 and mid - d >= 0: out.append(row[mid - d])
	return out


# ------------------------------------------------------------- cable tray
#
# Containment on the corridor ceiling, because that is where cable goes and
# because a corridor with a tray in it tells you at a glance where a run could
# possibly have gone. The cables themselves are drawn from the SITE's links --
# see _draw_cables() -- so an empty tray means you have not run anything yet.

const TRAY_KINDS := [K_CORRIDOR, K_LIFTLOBBY, K_LOBBY, K_MDF, K_COMMS, K_GOODS, K_SERVER]

func tray_y(f: int) -> float:
	return f * fheight + fheight - SLAB_T - 0.32


func _trays(f: int) -> void:
	var y := tray_y(f)
	for y0 in range(bh):
		var x := 0
		while x < bw:
			var r := room_of(f, x, y0)
			if r == NOROOM or not TRAY_KINDS.has(int(rooms[r].kind)):
				x += 1
				continue
			var x2 := x
			while x2 < bw:
				var r2 := room_of(f, x2, y0)
				if r2 == NOROOM or not TRAY_KINDS.has(int(rooms[r2].kind)):
					break
				x2 += 1
			var n := x2 - x
			_box(Vector3(x, y, y0 + 0.25), Vector3(n, 0.05, 0.03), TRAY_COL, false)
			_box(Vector3(x, y, y0 + 0.72), Vector3(n, 0.05, 0.03), TRAY_COL, false)
			for i in range(n):
				_box(Vector3(x + i + 0.35, y + 0.005, y0 + 0.25),
					Vector3(0.07, 0.02, 0.50), TRAY_COL, false)
			x = x2


# -------------------------------------------------------------- the lifts
#
# One car per shaft. The shafts are already vertically aligned on every floor
# they pass through -- the generator guarantees it -- so grouping the RM_LIFT
# rooms by footprint gives each lift the list of landings it serves.

func _build_lifts() -> void:
	for l in lifts:
		l.queue_free()
	lifts.clear()
	var by_rect := {}
	for r in rooms:
		if r.kind != K_LIFT:
			continue
		var key := "%d,%d,%d,%d" % [r.x0, r.y0, r.x1, r.y1]
		if not by_rect.has(key):
			by_rect[key] = []
		by_rect[key].append(r.i)
	for key in by_rect.keys():
		var l = preload("res://scripts/lift.gd").new()
		l.name = "Lift_%d" % lifts.size()
		add_child(l)
		if l.setup(self, by_rect[key]):
			lifts.append(l)
		else:
			l.queue_free()


# ------------------------------------------------------ the tower in service
#
# The building generator makes the whole tower at once, which is right: it is
# the space. What grows is how much of it is OPEN. A floor that is not in
# service has no lit button in the lift and the car will not stop there.

func in_service(f: int) -> bool:
	return f >= 0 and f < floors_in_service


func open_next_floor() -> String:
	if floors_in_service >= nfloors:
		return "every floor in this tower is already in service."
	var f := floors_in_service
	floors_in_service += 1
	for l in lifts:
		l.rebuild_panels()
	_signage()
	var who := ""
	if site_up:
		var n := 0
		for r in rooms:
			if r.floor == f and r.tenant != 0:
				n += 1
		who = "  %d let spaces on it" % n
	return "floor %d is in service.%s" % [f, who]


func lift_for(f: int) -> Object:
	for l in lifts:
		if l.floors.has(f):
			return l
	return lifts[0] if lifts.size() else null


# ------------------------------------------------------------- the signage
#
# A corridor that does not say which floor it is on is four identical
# corridors. These are Label3D, which is an engine node drawing the project's
# own font -- there is still no imported art anywhere in this project.

var _signs: Node3D = null

const SIGNED := {K_MDF: "MDF", K_COMMS: "COMMS", K_GOODS: "GOODS IN",
	K_PLANT: "PLANT", K_STAIR: "STAIRS", K_SERVER: "SERVER ROOM",
	K_TOILET: "WC", K_RISER: "RISER"}

func _signage() -> void:
	if _signs:
		_signs.queue_free()
	_signs = Node3D.new()
	_signs.name = "Signage"
	add_child(_signs)
	# the floor, in the lift lobby, big enough to read across the lobby
	for r in rooms:
		if r.kind != K_LIFTLOBBY:
			continue
		var t := "FLOOR %d" % r.floor
		if not in_service(r.floor):
			t += "  NOT IN SERVICE"
		# ON THE WALLS, not floating in the middle of the room. A sign at the
		# room centre is a sign hanging in mid-air with its ends buried in the
		# brickwork at either side, which is what the first pass of this did.
		var col: Color = Color("#e6ecf2") if in_service(r.floor) else Color("#c08a6a")
		var y: float = r.floor * fheight + 2.30
		var mid := room_centre(r.i)
		for w in [[0, 0.0], [1, PI], [2, PI * 0.5], [3, -PI * 0.5]]:
			var p := mid
			p.y = y
			match int(w[0]):
				0: p.z = float(r.y0) + 0.10
				1: p.z = float(r.y1) - 0.10
				2: p.x = float(r.x0) + 0.10
				3: p.x = float(r.x1) - 0.10
			_sign(p, t, float(w[1]), 34, col)
	# and what is on the other side of a door worth naming
	for d in doors:
		for pair in [[d.a, d.b], [d.b, d.a]]:
			var inside: int = pair[0]
			if inside >= rooms.size():
				continue
			var kind: int = rooms[inside].kind
			# A CORRIDOR HAS TO SAY WHICH FLOOR IT IS ON, or four floors are
			# four identical corridors. The way in from the lifts is where you
			# read it, so that is where it goes.
			var what: String = ""
			if SIGNED.has(kind):
				what = SIGNED[kind]
			elif kind == K_LIFTLOBBY and rooms[pair[1]].kind == K_CORRIDOR:
				what = "LIFTS  FLOOR %d" % d.floor
			else:
				continue
			var p := Vector3(d.x + (1.0 if d.dir == 0 else 0.5),
				d.floor * fheight + DOOR_H + 0.22,
				d.y + (0.5 if d.dir == 0 else 1.0))
			var toward := (room_centre(pair[1]) - room_centre(inside))
			toward.y = 0
			toward = toward.normalized()
			# PROUD OF THE WALL. Sitting it in the wall plane buries half of it
			# in the brickwork, and half a sign reads as a rendering fault.
			p += toward * (WALL_T * 0.5 + 0.05)
			_sign(p, what, atan2(toward.x, toward.z), 22, Color("#d8e2ea"))


func _sign(p: Vector3, text: String, yaw: float, size: int, col: Color) -> void:
	var l := Label3D.new()
	l.font = preload("res://scripts/uifont.gd").mono()
	l.font_size = size
	l.pixel_size = 0.0035
	l.text = text
	l.modulate = col
	l.outline_size = 0
	l.billboard = BaseMaterial3D.BILLBOARD_DISABLED
	l.double_sided = false
	l.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	l.position = p
	l.rotation = Vector3(0, yaw, 0)
	_signs.add_child(l)


# ---------------------------------------------------------------- the site
#
# core/site.c owns what is installed, what it cost and whether the link came
# up. This calls it; it does not duplicate it. The day-one state is the ISP
# handoff in the MDF and a delivery ON THE FLOOR OF GOODS IN with nothing
# plugged into it, which is what the first morning of a new building actually
# looks like: three boxes by the roller door and a walk ahead of you.
#
# It used to install them in the MDF, which was the same lie the README told
# -- kit that arrives where you happen to be standing makes every room
# equally close to the loading bay, and the floor plan stops being the price
# list and becomes wallpaper. `order` is the only way kit enters the tower.

func _site_start() -> void:
	site_up = false
	if machine == null or not machine.has_method("site_start"):
		return
	if str(machine.site_start(60000)).strip_edges() == "":
		return
	site_up = true
	for line in ["order router edge",
			"order switch24 core",
			"order server files"]:
		machine.site_cmd(line)


func site(line: String) -> String:
	if not site_up:
		return "there is no site yet\n"
	return str(machine.site_cmd(line))


func site_devs() -> Array:
	var out: Array = []
	if not site_up:
		return out
	for line in str(machine.site_devs()).split("\n", false):
		var f: PackedStringArray = line.split(" ", false)
		if f.size() < 8:
			continue
		out.append({"i": int(f[0]), "kind": int(f[1]), "room": int(f[2]),
			"floor": int(f[3]), "tenant": int(f[4]), "nports": int(f[5]),
			"kindname": f[6], "name": f[7]})
	return out


func site_links() -> Array:
	var out: Array = []
	if not site_up:
		return out
	for line in str(machine.site_links()).split("\n", false):
		var f: PackedStringArray = line.split(" ", false)
		if f.size() < 11:
			continue
		out.append({"i": int(f[0]), "a": int(f[1]), "aport": int(f[2]),
			"b": int(f[3]), "bport": int(f[4]), "room_a": int(f[5]),
			"room_b": int(f[6]), "metres": int(f[7]), "cost": int(f[8]),
			"kind": int(f[9]), "state": int(f[10])})
	return out


# The cables that are really there, drawn from the site's own link list, up
# into the tray and along it. The LENGTH and the PRICE are the site model's,
# off the building's cable graph; this line is only how it looks.
func _draw_cables() -> void:
	if _cable_node:
		_cable_node.queue_free()
		_cable_node = null
	var links := site_links()
	if links.is_empty():
		return
	var g = preload("res://scripts/vgeo.gd").new()
	for l in links:
		if l.state < 0:
			continue                     # pulled out
		var a := _dev_point(l.a, l.aport)
		var b := _dev_point(l.b, l.bport)
		if a == Vector3.INF or b == Vector3.INF:
			continue          # a device the view has not drawn has no end to draw to
		var col: Color = CABLE_COL[l.i % CABLE_COL.size()]
		if l.state == 2:                 # PORT_TOOLONG: it was laid and it is dead
			col = Color("#7a3030")
		var pts: Array
		if a.distance_to(b) < 2.5:
			# A PATCH LEAD, between two boxes in the same frame. It does not go
			# up into the tray to travel 400 mm: it comes out of the front, down
			# past whatever is between them, and back in. That loop hanging off
			# the front of a rack is what a rack you have worked on looks like.
			var fa := _dev_face(l.a)
			var fb := _dev_face(l.b)
			var lo: float = min(a.y, b.y) - 0.07 - float(l.i % 4) * 0.022
			var out: float = 0.10 + float(l.i % 4) * 0.022
			var a2 := a + fa * out
			var b2 := b + fb * out
			pts = [a, a2, Vector3(a2.x, lo, a2.z), Vector3(b2.x, lo, b2.z), b2, b]
		else:
			var ya := tray_y(int(a.y / fheight))
			var yb := tray_y(int(b.y / fheight))
			pts = [a, Vector3(a.x, ya, a.z), Vector3(b.x, ya, b.z)]
			if absf(ya - yb) > 0.01:
				pts.append(Vector3(b.x, yb, b.z))
			pts.append(b)
		for i in range(pts.size() - 1):
			_cable_seg(g, pts[i], pts[i + 1], col)
	_cable_node = MeshInstance3D.new()
	_cable_node.name = "Cables"
	_cable_node.mesh = g.mesh()
	add_child(_cable_node)


func _cable_seg(g, a: Vector3, b: Vector3, col: Color) -> void:
	var mn := Vector3(min(a.x, b.x), min(a.y, b.y), min(a.z, b.z))
	var mx := Vector3(max(a.x, b.x), max(a.y, b.y), max(a.z, b.z))
	var t := 0.014
	var size := mx - mn
	size.x = max(size.x, t)
	size.y = max(size.y, t)
	size.z = max(size.z, t)
	g.box(mn, size, col, false)


func _dev_point(site_i: int, port := 0) -> Vector3:
	for d in devices:
		if int(d.get("site", -1)) == site_i:
			return _port_point(d, port)
	return Vector3.INF


func _dev_face(site_i: int) -> Vector3:
	for d in devices:
		if int(d.get("site", -1)) == site_i:
			return d.face
	return Vector3(0, 0, 1)


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
	# THE MDF. The day starts where the ISP handoff lands and where everything
	# you own is, because that is where an IT person's day starts. The lobby is
	# somewhere you walk TO. It used to spawn you in the entrance hall, which is
	# the front of house of a building you do not work in the front of.
	#
	# Standing clear of the racks: they line one wall, so the spawn is on the
	# far side of the room looking at them.
	var i := find_room(0, K_MDF)
	if i < 0: i = find_room(0, K_COMMS)
	if i < 0: i = find_room(0, K_LOBBY)
	if i < 0:
		for r in rooms:
			if r.floor == 0 and r.kind != K_RISER and r.kind != K_LIFT:
				i = r.i
				break
	var r: Dictionary = rooms[i]
	var p := room_centre(i)
	# The racks line the low edge of the short axis; stand square in front of
	# the middle of the row, far enough back to see the whole frame.
	if (r.x1 - r.x0) >= (r.y1 - r.y0):
		p.z = min(float(r.y1) - 0.9, r.y0 + 0.35 + RACK_D + 2.4)
	else:
		p.x = min(float(r.x1) - 0.9, r.x0 + 0.35 + RACK_D + 2.4)
	p.y = 0.1
	return p


# Facing the rack from the spawn: the middle of the row, not the end of it.
func spawn_yaw() -> float:
	var i := find_room(0, K_MDF)
	if i < 0:
		return 0.0
	var mine := racks_in(i)
	if mine.is_empty():
		return 0.0
	var mid := Vector3.ZERO
	for m in mine:
		mid += Vector3(racks[m].x + 0.3, 0, racks[m].z + 0.5)
	mid /= float(mine.size())
	var to := mid - spawn_point()
	# -Z is forward for a Godot camera, so the yaw that looks along `to` is this.
	return atan2(-to.x, -to.z)


func _spawn_player() -> void:
	player = preload("res://scripts/walker.gd").new()
	player.name = "Player"
	add_child(player)
	player.global_position = spawn_point() + Vector3(0, 0.2, 0)
	player.look_at_yaw(spawn_yaw())


# ------------------------------------------------------------- the devices
#
# What you can plug into. A device is a place in the building plus WHICH real
# machine is behind it, and what sockets it has on the back. The sockets are
# the point: a rack server has no display output, so HDMI gets you nothing
# from it and serial gets you its console -- including the console of a
# machine that never finished booting, which is the interesting one.

# How tall a thing is, in U. A switch is 1U because a switch is 1U.
const DEV_U := {"uplink": 1, "switch8": 1, "switch24": 1, "router": 1,
	"pc": 4, "server": 2}
const DEV_COL := {"uplink": Color("#9a7b3a"), "switch8": Color("#3f6f96"),
	"switch24": Color("#3f6f96"), "router": Color("#8a5a3e"),
	"pc": Color("#6a707a"), "server": Color("#7c828c")}

func _place_devices() -> void:
	devices.clear()
	for k in racks:
		k.next_u = 34
	var mdf := find_room(0, K_MDF)
	if mdf < 0: mdf = find_room(0, K_COMMS)

	# ---- what the SITE says is installed, in the racks of the room it says.
	# This is a read of site_devs(). There is no list of devices in this file.
	_on_floor = {}
	var on_floor := _on_floor
	for d in site_devs():
		if d.i == carrying:
			continue                          # it is in your hands, not in the room
		var room: int = d.room
		if room < 0 or room >= rooms.size():
			room = mdf                       # the handoff is outside; land it in the MDF
		var frames := racks_in_fill_order(room)
		var nu: int = DEV_U.get(d.kindname, 1)
		var slot := {}
		for i in frames:
			slot = _rack_slot(i, nu)
			if not slot.is_empty():
				break
		# NO RACK: IT IS ON THE FLOOR, AND IT IS DRAWN ON THE FLOOR. Goods in
		# has a roller door and no cabinets, and a box that the site says is
		# in a room and the view does not draw is a box nobody can walk up to
		# -- which is how a delivery becomes invisible. Skipping it here was
		# a comment saying "it is still on the floor" and nothing on any
		# floor.
		if slot.is_empty():
			var k: int = int(on_floor.get(room, 0))
			on_floor[room] = k + 1
			slot = _floor_slot(room, k, nu)
		# A managed box has a management line and no picture on the back of it.
		_add_device(d.name, -2, false, true, slot.mn, slot.size,
			DEV_COL.get(d.kindname, Color("#2f343a")), slot.face, d.nports, d.i)

	if mdf >= 0:
		var r: Dictionary = rooms[mdf]
		# your workstation: a desk machine, so it has a screen output. On a desk
		# against the opposite wall from the racks, which is where it goes.
		_add_device("workstation", 0, true, true,
			Vector3(r.x1 - 1.9, 0.72, r.y1 - 1.5), Vector3(0.5, 0.42, 0.5),
			Color("#3a3f46"), Vector3(0, 0, -1), 0, -1)
		_desk(Vector3(r.x1 - 2.4, 0, r.y1 - 1.8), Vector3(1.6, 0.72, 0.8))
		# the customer's machine, racked: 4U of it, and the crash cart's whole
		# lesson lives on the back of it -- serial yes, display no.
		var frames := racks_in_fill_order(mdf)
		if not frames.is_empty():
			var slot := _rack_slot(frames[1] if frames.size() > 1 else frames[0], 4)
			if not slot.is_empty():
				_add_device("rack server", 1, false, true, slot.mn, slot.size,
					Color("#23262b"), slot.face, 2, -1)

	# ---- patch panels. A PATCH PANEL IS PASSIVE: no processor, no console, no
	# ports to plug a lead into, and saying so is true where inventing a console
	# for it would be the exact lie this project does not tell. It is where the
	# floor's copper terminates, so there is one in every comms cupboard and one
	# in the MDF, 2U, with the twenty-four ports drawn on the front of it.
	var pp_rooms: Array = []
	if mdf >= 0: pp_rooms.append(mdf)
	for f in range(nfloors):
		var c := find_room(f, K_COMMS)
		if c >= 0: pp_rooms.append(c)
	for room in pp_rooms:
		var frames := racks_in_fill_order(room)
		if frames.is_empty():
			continue
		var slot := _rack_slot(frames[0], 2)
		if slot.is_empty():
			continue
		_add_device("patch panel", -1, false, false, slot.mn, slot.size,
			Color("#4a4033"), slot.face, 24, -1)


# A desk to put the workstation on, because a computer floating at 720 mm is
# the sort of thing that makes a room read as a diagram.
func _desk(mn: Vector3, size: Vector3) -> void:
	var g = preload("res://scripts/vgeo.gd").new()
	g.box(mn + Vector3(0, size.y - 0.04, 0), Vector3(size.x, 0.04, size.z), Color("#8a7f6d"))
	for ax in [0.03, size.x - 0.09]:
		g.box(mn + Vector3(ax, 0, 0.05), Vector3(0.06, size.y - 0.04, size.z - 0.1), Color("#5b6068"))
	var n := g.node("Desk")
	add_child(n)


func _add_device(dname: String, which: int, hdmi: bool, serial: bool,
		mn: Vector3, size: Vector3, col: Color,
		face := Vector3(0, 0, 1), nports := 0, site_i := -1) -> void:
	var g = preload("res://scripts/vgeo.gd").new()
	g.box(mn, size, col)
	# The FRONT of a box is what tells you what it is: a lighter faceplate, and
	# a row of ports you can count. A twenty-four port switch that does not
	# visibly have twenty-four ports is a grey brick with a label.
	var fw := 0.012
	var fp := mn
	var fs := size
	if face.z > 0: fp.z = mn.z + size.z - fw
	elif face.z < 0: fp.z = mn.z
	elif face.x > 0: fp.x = mn.x + size.x - fw
	else: fp.x = mn.x
	if absf(face.z) > 0.5: fs.z = fw
	else: fs.x = fw
	g.box(fp, fs, col.lightened(0.30), false)
	# A LIT PANEL. Two lights on the end of a box is how you tell, across a
	# room, that it is powered and that a port is up -- and it is the one thing
	# that makes a rack of boxes read as equipment rather than as shelving.
	var ly: float = mn.y + size.y * 0.5 - 0.008
	for j in range(2):
		var t2: float = 0.028 + float(j) * 0.028
		if absf(face.z) > 0.5:
			g.box(Vector3(mn.x + t2, ly, fp.z + (fw if face.z > 0 else -0.006)),
				Vector3(0.016, 0.016, 0.006), Color("#7fe08a") if j == 0 else Color("#e0b040"), false)
		else:
			g.box(Vector3(fp.x + (fw if face.x > 0 else -0.006), ly, mn.z + t2),
				Vector3(0.006, 0.016, 0.016), Color("#7fe08a") if j == 0 else Color("#e0b040"), false)
	if nports > 0:
		var along: float = size.x if absf(face.z) > 0.5 else size.z
		var run: float = along - 0.10
		var per: float = run / float(nports)
		var pw: float = min(per * 0.62, 0.022)
		var ph: float = min(size.y * 0.45, 0.024)
		var py: float = mn.y + size.y * 0.5 - ph * 0.5
		for i in range(nports):
			var t: float = 0.05 + per * (float(i) + 0.5)
			var pm: Vector3
			var ps: Vector3
			if absf(face.z) > 0.5:
				pm = Vector3(mn.x + t - pw * 0.5, py, fp.z + (fw if face.z > 0 else -0.008))
				ps = Vector3(pw, ph, 0.008)
			else:
				pm = Vector3(fp.x + (fw if face.x > 0 else -0.008), py, mn.z + t - pw * 0.5)
				ps = Vector3(0.008, ph, pw)
			g.box(pm, ps, Color("#0e1114"), false)
	var n := g.node(dname.replace(" ", "_") + "_%d" % devices.size())
	add_child(n)
	# LABELLED, because every rack anybody has ever had to work on is labelled,
	# and because otherwise a row of boxes is a row of boxes.
	var lab := Label3D.new()
	lab.font = preload("res://scripts/uifont.gd").mono()
	lab.font_size = 40
	lab.pixel_size = 0.00042
	lab.text = dname
	lab.modulate = Color("#e9eff5")
	lab.outline_size = 0
	lab.billboard = BaseMaterial3D.BILLBOARD_DISABLED
	lab.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	var lp := mn + size * 0.5
	if absf(face.z) > 0.5:
		lp.x = mn.x + size.x * 0.62
		lp.z = fp.z + (0.02 if face.z > 0 else -0.008)
		lab.rotation = Vector3(0, 0.0 if face.z > 0 else PI, 0)
	else:
		lp.z = mn.z + size.z * 0.62
		lp.x = fp.x + (0.02 if face.x > 0 else -0.008)
		lab.rotation = Vector3(0, PI * 0.5 if face.x > 0 else -PI * 0.5, 0)
	lab.position = lp
	n.add_child(lab)
	devices.append({"name": dname, "which": which, "hdmi": hdmi,
		"serial": serial, "node": n, "pos": mn + size * 0.5, "site": site_i,
		"face": face, "mn": mn, "size": size, "nports": nports, "fw": fw})


# Where a lead actually goes IN: THE PORT, not the middle of the box. A lead
# into port 7 comes out of the seventh hole, which is the difference between a
# picture of cabling and a picture of a box with a stripe painted down it.
func _port_point(d: Dictionary, port: int) -> Vector3:
	var mn: Vector3 = d.mn
	var size: Vector3 = d.size
	var face: Vector3 = d.face
	var n: int = max(1, int(d.nports))
	var along: float = size.x if absf(face.z) > 0.5 else size.z
	var t: float = 0.05 + (along - 0.10) * (float(clampi(port, 0, n - 1)) + 0.5) / float(n)
	var p := mn + size * 0.5
	if absf(face.z) > 0.5: p.x = mn.x + t
	else: p.z = mn.z + t
	var fw: float = d.fw
	if face.z > 0: p.z = mn.z + size.z + fw
	elif face.z < 0: p.z = mn.z - fw
	elif face.x > 0: p.x = mn.x + size.x + fw
	else: p.x = mn.x - fw
	return p


# What you are standing in front of. Distance alone is not enough once things
# are racked 45 mm apart: which one you mean is which one you are LOOKING at.
func nearest_device(from: Vector3, radius := 2.2) -> int:
	var best := -1
	var bestd := 1e9
	var fwd := Vector3.ZERO
	if player and player.cam:
		fwd = -player.cam.global_transform.basis.z
	for i in range(devices.size()):
		var to: Vector3 = devices[i].pos - from
		var d: float = to.length()
		if d > radius:
			continue
		var score := d
		if fwd != Vector3.ZERO and d > 0.05:
			var aim: float = to.normalized().dot(fwd)
			if aim < 0.35:
				continue                    # behind you, or off to one side
			score = d * (2.0 - aim)
		if score < bestd:
			bestd = score
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
	cart.position = Vector3(0.30, -1.30, -0.82)
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


# ---- the lift, without a window.
#
# A blind playtester cannot press a button in a 3D lift lobby, so every one of
# these is what the button does, and the 3D is only the button.

func lift_in() -> Object:
	# The lift the player is standing in, if any.
	if player:
		for l in lifts:
			if l.inside(player.global_position):
				return l
	return null


func lift_call(f: int) -> String:
	# From a landing: bring a car that serves this floor to it.
	var l := lift_for(f)
	if l == null:
		return "there is no lift in this building."
	return l.call_to(f)


func lift_go(f: int) -> String:
	# From inside the car: press a button. A floor not in service has no lit
	# button, and this is the refusal a player reads.
	var l := lift_in()
	if l == null:
		l = lift_for(f)
	if l == null:
		return "there is no lift in this building."
	return l.go_to(f)


func lift_floor() -> int:
	var l := lift_in()
	if l == null:
		l = lifts[0] if lifts.size() else null
	return -1 if l == null else int(l.at)


func lift_busy() -> bool:
	for l in lifts:
		if l.busy():
			return true
	return false


func lift_car_centre(which := 0) -> Vector3:
	if which >= lifts.size():
		return Vector3.ZERO
	return lifts[which].car_centre()


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


# ------------------------------------------------------------- carrying it
#
# THE OTHER HALF OF BUYING. Kit is delivered to goods in on the ground floor
# and it does not get anywhere else on its own: you pick it up, you walk, you
# put it down, and where you put it down is what every metre of copper is
# then measured from. core/site.c refuses to move a box with a cable in it,
# so this cannot be used to teleport a live switch; the refusal is the same
# one the socket session prints, from the same function.
#
# One box, because both hands are on it. That is the same rule session.c
# keeps, and it is the reason there is no inventory here.

var carrying := -1                     # site index in your hands, or -1
var _carry_room := -1
var _on_floor := {}


func player_room() -> int:
	if player == null:
		return NOROOM
	var p := player.global_position
	return room_of(player_floor(), int(floor(p.x)), int(floor(p.z)))


# Pick the box in front of you up, or -- if your hands are full -- put down
# what you are holding, here.
func carry_here(dev: int) -> String:
	if not site_up:
		return ""
	if carrying >= 0:
		return drop_here()
	if dev < 0:
		return "there is nothing in reach to pick up."
	var s: int = int(devices[dev].get("site", -1))
	if s < 0:
		return "%s is not yours to carry." % devices[dev].name
	var room := player_room()
	if room == NOROOM:
		return "you are not standing in a room."
	# site_move is the one that decides. A box with a cable in it does not
	# move, and it says so in the words core/site.c uses.
	var out: String = site("move %d #%d" % [s, room]).strip_edges()
	if out.find("refused") >= 0:
		return out
	carrying = s
	_carry_room = room
	var n: Node = devices[dev].node
	if n: n.queue_free()
	devices.remove_at(dev)
	if _cable_from == s:
		_cable_from = -1
	return "you pick it up. It goes where you go until you put it down."


func drop_here() -> String:
	if carrying < 0:
		return "you are not carrying anything."
	var room := player_room()
	if room == NOROOM:
		return "you are not standing in a room: nowhere to put it down."
	var out: String = site("move %d #%d" % [carrying, room]).strip_edges()
	var s := carrying
	carrying = -1
	_carry_room = -1
	_place_one(s)
	return out


# One box, into the room the site says it is in: a rack slot if that room has
# a frame with space in it, and the floor if it has not.
func _place_one(s: int) -> void:
	for d in site_devs():
		if d.i != s:
			continue
		var room: int = d.room
		if room < 0 or room >= rooms.size():
			return
		var nu: int = DEV_U.get(d.kindname, 1)
		var slot := {}
		for i in racks_in_fill_order(room):
			slot = _rack_slot(i, nu)
			if not slot.is_empty():
				break
		if slot.is_empty():
			var k: int = int(_on_floor.get(room, 0))
			_on_floor[room] = k + 1
			slot = _floor_slot(room, k, nu)
		_add_device(d.name, -2, false, true, slot.mn, slot.size,
			DEV_COL.get(d.kindname, Color("#2f343a")), slot.face, d.nports, d.i)
		return


# RUN A CABLE, in the building, between two boxes. The price and the length
# come out of site_cable() off the building's own tray graph -- this only says
# which two ports the player meant.
func cable_here(dev: int) -> String:
	if dev < 0 or not site_up:
		return ""
	var s: int = int(devices[dev].get("site", -1))
	if s < 0:
		return "%s is passive. There is nothing to plug into." % devices[dev].name
	if _cable_from < 0:
		_cable_from = s
		return "spool at %s. Walk to the other end and press [C]." % devices[dev].name
	if _cable_from == s:
		_cable_from = -1
		return "spool put back."
	var a := _cable_from
	_cable_from = -1
	var out := site("cable %d:%d %d:%d cat6" % [a, _free_port(a), s, _free_port(s)])
	_draw_cables()
	return out.strip_edges()


func _free_port(s: int) -> int:
	# The lowest port with nothing in it, which is how anybody patches a switch.
	var used := {}
	for l in site_links():
		if l.state < 0:
			continue
		if l.a == s: used[l.aport] = true
		if l.b == s: used[l.bport] = true
	for d in site_devs():
		if d.i != s:
			continue
		for p in range(d.nports):
			if not used.has(p):
				return p
	return 0


func _process(_dt: float) -> void:
	if player == null:
		return
	var near := nearest_device(player.global_position)
	var car: Object = lift_in()
	var landing := _lift_landing()
	if hud:
		var t := where_am_i()
		if cart:
			t += "\ncart: " + str(cart.status)
		if _cable_from >= 0:
			t += "\nspool in hand"
		if carrying >= 0:
			t += "\ncarrying kit in both hands   [G] put it down here"
		elif near >= 0:
			t += "\n%s in reach   [F] serial lead   [H] HDMI lead   [U] unplug" % devices[near].name
			if int(devices[near].get("site", -1)) >= 0:
				t += "   [C] cable   [G] pick up"
		if car != null:
			t += "\nin the lift: press a floor number.  in service: %s" % str(car.serviced())
		elif landing != null:
			t += "\n[E] call the lift"
		t += "\n%d of %d floors in service   [O] open the next one" % [floors_in_service, nfloors]
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
	if Input.is_key_pressed(KEY_C) and not _c_down and near >= 0:
		print(cable_here(near))
	_c_down = Input.is_key_pressed(KEY_C)
	if Input.is_key_pressed(KEY_G) and not _g_down:
		print(carry_here(near))
	_g_down = Input.is_key_pressed(KEY_G)
	# WHAT YOU ARE CARRYING IS IN THE ROOM YOU ARE IN, at every step of the
	# walk -- not in an inventory that resolves when you put it down. The
	# site is told the moment you cross the threshold, so `show` from a
	# terminal in the middle of a carry says where the box really is.
	if carrying >= 0:
		var r := player_room()
		if r != NOROOM and r != _carry_room:
			_carry_room = r
			site("move %d #%d" % [carrying, r])
	if Input.is_key_pressed(KEY_E) and not _e_down and landing != null:
		print(landing.call_to(player_floor()))
	_e_down = Input.is_key_pressed(KEY_E)
	if Input.is_key_pressed(KEY_O) and not _o_down:
		print(open_next_floor())
	_o_down = Input.is_key_pressed(KEY_O)
	# The buttons inside the car, on the number row.
	if car != null:
		for f in range(min(10, nfloors)):
			if Input.is_key_pressed(KEY_0 + f) and not _num_down[f]:
				print(car.go_to(f))
			_num_down[f] = Input.is_key_pressed(KEY_0 + f)


# The call plate you are standing in front of, if you are standing in front of
# one. Two metres, which is arm's reach plus a step.
func _lift_landing() -> Object:
	if player == null:
		return null
	var f := player_floor()
	for l in lifts:
		if not l.floors.has(f):
			continue
		if player.global_position.distance_to(l.call_plate_pos(f)) < 2.0:
			return l
	return null

var _f_down := false
var _h_down := false
var _u_down := false
var _c_down := false
var _g_down := false
var _e_down := false
var _o_down := false
var _num_down := [false, false, false, false, false, false, false, false, false, false]
