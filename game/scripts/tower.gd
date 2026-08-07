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
var phone: Node3D = null
var bag: Control = null          # inventory.gd: what is in your hands
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
var _power_node: MeshInstance3D = null
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
	build(_wanted_seed())


# WHICH BUILDING. The window always built seed 200 and nothing could change
# it, so `--towersh 7008` and the window were two different towers -- and a
# playtester who had spent an hour building one could not look at it:
#
#   "The 3D can't be pointed at a seed. --towersh 7008 and the Godot window
#    are two different buildings, so I could not look at the tower I'd spent
#    an hour building."
#
# That is a testability hole as much as a convenience one. D23 says the 3D is
# a view of the sim and screenshots are the CHECK on the text; a view that
# cannot be pointed at the building under test checks nothing. The spelling is
# the one `wire.gd` already uses for its port, so there is one convention.
func _wanted_seed() -> int:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--seed="):
			return int(a.split("=")[1])
	var env := OS.get_environment("NOMINAL_SEED")
	if env != "":
		return int(env)
	return seed_no


# ---------------------------------------------------------------- the data

func build(s: int) -> bool:
	seed_no = s
	# THE SESSION FIRST. It owns the building and the site -- the same Session
	# core/session.c runs for a socket client -- so it has to exist before
	# anything reads a room out of the extension, or the view would be looking
	# at one building and the game would be charging for another.
	_ses_start()
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
		_spawn_phone()
		_hud()
		_bag()
	_light()
	_dev_sig = _site_sig()
	if with_player:
		_snapshot()
		_wire()
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
# A SWITCHBACK, WHICH IS WHAT THIS COMMENT ALREADY SAID AND THE CODE DID NOT.
#
# The old plan was one straight flight per floor, alternating which side of
# the shaft it used. It read as a switchback in the comment above it and was
# never one, and the difference is the thing the owner walked into twice:
#
#   "Stairwell still has stairs that go right up against a wall so there's no
#    landing for you to walk onto the staircase without jumping onto the
#    staircase."
#
# He is describing `a = lo`. The foot of the flight sat exactly on the room's
# own wall, so there was no floor in front of the bottom step to stand on and
# turn from -- you had to come at the flight side-on and climb onto it. The
# test in game/tests/tower.gd even had to work around it, in as many words:
# "the foot of an even-numbered run is against the stairwell wall, and a
# teleport that overshoots by half a metre puts the test inside brickwork".
# A test that has to know about a defect to pass is a defect with a witness.
#
# So it is a real switchback now, and every stairwell in every seed is the
# same shape a person expects:
#
#     lo ---------------- foot landing (LAND deep, floor you walk in onto)
#        [ flight A up ] [ flight B down ]     two strips, side by side
#     y1 ---------------- half landing (MID deep, at half the floor height)
#        ... whatever is left of the room is plain floor
#
# You enter onto the landing, climb A to the half landing, turn, and climb B
# back over your own entry point to arrive on the floor above at the same end
# you came in. Both flights rise half a storey, so both are the SAME PITCH,
# and the pitch is a constant rather than whatever the room's length happened
# to give: a 5 x 6 m shaft used to be 38 degrees and a 6 x 10 m one 23, which
# is two different staircases in one building.
#
# The floor above is open over both flights and the half landing -- that is
# what a stairwell IS -- so nobody walks into the underside of anything, and
# the only floor kept up there is the landing you arrive on.

const STAIR_LAND := 1.2      # foot landing, and the head landing above it
const STAIR_MID := 1.4       # the half landing you turn on
const STAIR_PITCH := 30.0    # degrees, and it is the same in every stairwell

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
		# Two flights side by side, as wide as the shaft will take up to a
		# comfortable 1.7 m each.
		var rw: float = min(1.7, (cross_hi - cross_lo) * 0.5)
		# THE PITCH DECIDES THE LENGTH, not the other way round. Half a storey
		# at STAIR_PITCH is what a flight needs; if the shaft is shorter than
		# that it gets what there is, and the assertion in the game test that
		# a body can actually climb it is what says whether that was enough.
		var want: float = (fheight * 0.5) / tan(deg_to_rad(STAIR_PITCH))
		var room_for: float = (hi - lo) - STAIR_LAND - STAIR_MID
		var run: float = min(want, room_for)
		if run < 1.0:
			continue          # a shaft this small has no stair in it at all
		var y0: float = lo + STAIR_LAND       # bottom of both flights
		var y1: float = y0 + run              # top of both flights
		var a0: float = cross_lo              # flight A: up, away from the door
		var b0: float = cross_lo + rw         # flight B: back, arriving above
		stairs.append({
			"floor": r.floor, "room": r.i, "axis": axis,
			"y0": y0, "y1": y1, "run": run, "mid_hi": y1 + STAIR_MID,
			"a0": a0, "a1": a0 + rw, "b0": b0, "b1": b0 + rw,
			# The footprint the floor above is open over, and the keys the
			# rest of the file already reads. `a`/`b` are flight A, which is
			# the one you climb first and the one a test stands at the foot of.
			"a": y0, "b": y1, "up": true,
			"c0": a0, "c1": b0 + rw,
			"lo": y0, "hi": y1 + STAIR_MID,
		})


# WHERE YOU STAND IN A STAIRWELL, which is not the middle of it. The middle
# of a stairwell is the flights; the floor you can actually stand on is the
# landing at the near end -- the one you walk in onto and the one you arrive
# on from below. Anything aiming a body at a stair room wants this and not
# room_centre(), and the walking test found that out the hard way when its
# last waypoint landed inside a flight.
func stair_landing(room_i: int) -> Vector2:
	for s in stairs:
		if int(s.room) != room_i:
			continue
		var r = rooms[room_i]
		var lo: float = float(r.y0 if int(s.axis) == 1 else r.x0)
		var along: float = (lo + float(s.y0)) * 0.5
		var across: float = (float(s.a0) + float(s.b1)) * 0.5
		return Vector2(across, along) if int(s.axis) == 1 else Vector2(along, across)
	var c: Vector3 = room_centre(room_i)
	return Vector2(c.x, c.z)


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
	# AND SO IS A RISER. It is the shaft every cable between two floors goes up,
	# and it was a sealed slab: the tubes climbed straight through 160 mm of
	# concrete, which is what a playtester photographed in the floor 3 comms
	# cupboard. core/sessioncheck.c already refuses to walk you into one -- "a
	# riser is a shaft" -- so opening it costs nobody a floor to stand on.
	# The penetration is the part the shafts have in COMMON, so a riser that
	# does not line up with the one below it does not open a hole into a room.
	if f > 0:
		var here := find_room(f, K_RISER)
		var below := find_room(f - 1, K_RISER)
		if here >= 0 and below >= 0:
			var ra: Rect2 = Rect2(rooms[here].x0, rooms[here].y0,
				rooms[here].x1 - rooms[here].x0, rooms[here].y1 - rooms[here].y0)
			var rb: Rect2 = Rect2(rooms[below].x0, rooms[below].y0,
				rooms[below].x1 - rooms[below].x0, rooms[below].y1 - rooms[below].y0)
			var cut := ra.intersection(rb)
			if cut.size.x > 0.4 and cut.size.y > 0.4:
				out.append(cut)
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
		_troffers(f)
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
				# A CORRIDOR RUNS INTO A CORRIDOR. The generator cuts the ring
				# into rectangles -- four of them on this plate -- and it does
				# NOT hang doors between them, because as far as the building is
				# concerned they are one space: core/building.c\'s step_ok()
				# says so in a line of its own, "corridor to corridor, no door
				# needed", and every walking distance in the game is measured
				# through those openings.
				#
				# This pass did not know that rule, so it bricked the ring into
				# four dead ends. From the MDF you could reach the corridor
				# outside it and nothing else: not the lifts, not goods in, not
				# the stairwell. The owner reported the symptom exactly -- "there
				# were multiple floors, but there\'s no staircase, at least not
				# one that\'s accessible" -- and he was right; the flights were
				# fine and the way to them was a wall.
				#
				# bld_walk() and the 3D now agree about what a wall is, which is
				# the only way a metre charged for a walk can mean anything.
				if a != NOROOM and b != NOROOM \
						and int(rooms[a].kind) == K_CORRIDOR \
						and int(rooms[b].kind) == K_CORRIDOR:
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
	var half: float = fheight * 0.5
	# Flight A climbs away from the landing you walked in on; flight B climbs
	# back over it. Same length, same rise, so the same pitch on both.
	_stair_flight(s, base, half, float(s.y0), float(s.y1), float(s.a0), float(s.a1))
	_stair_flight(s, base + half, half, float(s.y1), float(s.y0), float(s.b0), float(s.b1))
	# THE HALF LANDING, which is the whole point of a switchback: somewhere
	# flat to stand while you turn round. It spans both strips, because you
	# arrive on one and leave on the other.
	var col := Color("#7e838a")
	var mlo: float = float(s.y1)
	var mln: float = float(s.mid_hi) - mlo
	var c0: float = float(s.a0)
	var cw: float = float(s.b1) - c0
	# AND IT COLLIDES. The treads either side of it are visual only -- what a
	# capsule really walks on is the invisible incline under them -- so a
	# landing drawn the same way was a platform you fell through, which is
	# exactly what the physics walk caught: onto the landing at 1.68 m,
	# across it, and down to y = 0.0002.
	if s.axis == 1:
		_box(Vector3(c0, base + half - 0.16, mlo), Vector3(cw, 0.16, mln), col, true)
	else:
		_box(Vector3(mlo, base + half - 0.16, c0), Vector3(mln, 0.16, cw), col, true)


# One flight: the treads you see, and the incline under them that a capsule
# can actually walk up. `from` and `to` are along the run's axis and may go
# either way; `y` is the height it starts at and `rise` how far it climbs.
func _stair_flight(s: Dictionary, y: float, rise: float,
		from: float, to: float, c0: float, c1: float) -> void:
	var length: float = absf(to - from)
	if length < 0.5:
		return
	var n: int = int(max(6.0, min(20.0, floor(rise / 0.17))))
	var step: float = rise / float(n)
	var going: float = length / float(n)
	var dirsign: float = 1.0 if to > from else -1.0
	# The steps are what you SEE. The collider is the incline under them: a
	# capsule cannot climb a 0.18 m box without step handling, and a player who
	# cannot get upstairs has a building of disconnected slabs.
	for i in range(n):
		var t0: float = from + dirsign * going * i
		var t1: float = t0 + dirsign * going
		var lo: float = min(t0, t1)
		var col := Color("#8b8f94")
		if s.axis == 1:
			_box(Vector3(c0, y + step * i, lo), Vector3(c1 - c0, step + 0.02, going), col, false)
		else:
			_box(Vector3(lo, y + step * i, c0), Vector3(going, step + 0.02, c1 - c0), col, false)
	# the incline, as two triangles with the normal upward
	var ya := y
	var yb := y + rise
	if s.axis == 1:
		_ramp(Vector3(c0, ya, from), Vector3(c1, ya, from),
			Vector3(c1, yb, to), Vector3(c0, yb, to))
	else:
		_ramp(Vector3(from, ya, c0), Vector3(from, ya, c1),
			Vector3(to, yb, c1), Vector3(to, yb, c0))


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

# THE DOORWAY DECIDES. A row of frames placed on a fixed grid put six racks
# across the only door of the MDF, and the room the day starts in was a room
# you could not leave. So nothing here picks a wall by "the long axis": it asks
# the building where the doors are, throws away every wall a door is in, keeps
# 1.5 m of floor clear in front of each opening, and leaves a working aisle in
# front of the frames and room to get behind them.
# BEHIND A FRAME, WHERE A PERSON HAS TO STAND TO CABLE IT.
#
# This was 0.60 m. The player capsule is 0.28 m in RADIUS -- 0.56 m across --
# so there were two centimetres of clearance on each side and the owner
# reported, correctly, that he could not fit behind the servers. A gap that is
# passable in arithmetic and impassable in play is not a gap.
#
# A real rear aisle in a plant room is 900-1200 mm, for exactly this reason:
# somebody has to stand there with both hands on a cable.
const RACK_BACK := 1.00
const RACK_AISLE := 1.20     # in front: a person and a 42U box on a trolley
const DOOR_CLEAR := 1.50     # floor kept clear inside the room, at every door
const ROW_MARGIN := 0.30     # a shoulder at each end of a row


# Every doorway of this room: which wall of the room it is in (0 = the y0 wall,
# 1 = y1, 2 = x0, 3 = x1) and the rectangle of floor inside the room that has
# to stay walkable for the door to be usable.
func room_doors(ri: int) -> Array:
	var r: Dictionary = rooms[ri]
	var out: Array = []
	for d in doors:
		if d.floor != r.floor:
			continue
		if d.a != ri and d.b != ri:
			continue
		if d.dir == 0:
			var px := float(d.x + 1)
			if absf(px - float(r.x0)) < 0.01:
				out.append({"wall": 2, "clear": Rect2(px, d.y, DOOR_CLEAR, 1.0),
					"gate": Vector2(px, float(d.y) + 0.5), "out": Vector2(-1, 0)})
			elif absf(px - float(r.x1)) < 0.01:
				out.append({"wall": 3, "clear": Rect2(px - DOOR_CLEAR, d.y, DOOR_CLEAR, 1.0),
					"gate": Vector2(px, float(d.y) + 0.5), "out": Vector2(1, 0)})
		else:
			var py := float(d.y + 1)
			if absf(py - float(r.y0)) < 0.01:
				out.append({"wall": 0, "clear": Rect2(d.x, py, 1.0, DOOR_CLEAR),
					"gate": Vector2(float(d.x) + 0.5, py), "out": Vector2(0, -1)})
			elif absf(py - float(r.y1)) < 0.01:
				out.append({"wall": 1, "clear": Rect2(d.x, py - DOOR_CLEAR, 1.0, DOOR_CLEAR),
					"gate": Vector2(float(d.x) + 0.5, py), "out": Vector2(0, 1)})
	return out


static func _rect_gap(a: Rect2, b: Rect2) -> float:
	var dx: float = max(0.0, max(a.position.x - b.end.x, b.position.x - a.end.x))
	var dy: float = max(0.0, max(a.position.y - b.end.y, b.position.y - a.end.y))
	return sqrt(dx * dx + dy * dy)


# A strip of floor `depth` deep, set `back` off a wall of this room, with
# `aisle` of clear floor in front of it, that no doorway needs. Returns the
# strip and the free run along it, or {} if the room cannot hold one.
#
# `want_len` is how much run the caller would like: a wall that gives it all is
# not improved by giving more, so past that the tie is broken by how far the
# strip keeps out of the way of the doors.
func _wall_band(ri: int, depth: float, back: float, aisle: float,
		want_len: float, avoid: Array = []) -> Dictionary:
	var r: Dictionary = rooms[ri]
	var dl := room_doors(ri)
	var need := back + depth + aisle
	var best := {}
	var best_score := -1.0
	for side in range(4):
		if avoid.has(side):
			continue
		var in_wall := false
		for d in dl:
			if int(d.wall) == side:
				in_wall = true
		if in_wall:
			continue                      # you do not park a rack over a door
		var band: Rect2
		var face: Vector3
		match side:
			0:
				if float(r.y1 - r.y0) < need: continue
				band = Rect2(r.x0, float(r.y0) + back, r.x1 - r.x0, depth)
				face = Vector3(0, 0, 1)
			1:
				if float(r.y1 - r.y0) < need: continue
				band = Rect2(r.x0, float(r.y1) - back - depth, r.x1 - r.x0, depth)
				face = Vector3(0, 0, -1)
			2:
				if float(r.x1 - r.x0) < need: continue
				band = Rect2(float(r.x0) + back, r.y0, depth, r.y1 - r.y0)
				face = Vector3(1, 0, 0)
			_:
				if float(r.x1 - r.x0) < need: continue
				band = Rect2(float(r.x1) - back - depth, r.y0, depth, r.y1 - r.y0)
				face = Vector3(-1, 0, 0)
		var along_x: bool = (side == 0 or side == 1)
		var segs: Array = [[
			(band.position.x if along_x else band.position.y) + ROW_MARGIN,
			(band.end.x if along_x else band.end.y) - ROW_MARGIN]]
		for d in dl:
			var c: Rect2 = d.clear
			if not c.intersects(band):
				continue
			var a: float = (c.position.x if along_x else c.position.y) - ROW_MARGIN
			var b: float = (c.end.x if along_x else c.end.y) + ROW_MARGIN
			var nxt: Array = []
			for s in segs:
				if b <= s[0] or a >= s[1]:
					nxt.append(s)
					continue
				if a - s[0] > 0.1: nxt.append([s[0], a])
				if s[1] - b > 0.1: nxt.append([b, s[1]])
			segs = nxt
		var run_lo := 0.0
		var run_hi := 0.0
		for s in segs:
			if s[1] - s[0] > run_hi - run_lo:
				run_lo = s[0]
				run_hi = s[1]
		if run_hi - run_lo < 0.5:
			continue
		var clearance := 99.0
		for d in dl:
			clearance = min(clearance, _rect_gap(band, d.clear))
		var score: float = min(run_hi - run_lo, want_len) * 2.0 + min(clearance, 6.0)
		if score > best_score:
			best_score = score
			best = {"side": side, "band": band, "along_x": along_x, "face": face,
				"lo": run_lo, "hi": run_hi}
	return best


const RACK_PITCH := 0.90
# CLEARANCE AT THE ENDS OF THE ROW, NOT JUST IN FRONT OF IT.
#
# The row was centred in the whole length of the wall, so a six-rack run in a
# 6 m room left 0.45 m at each end. There is a working aisle in front and 0.6 m
# behind, and none of that helps: 0.45 m is not a gap a person walks through,
# so the row reads as a wall and the room reads as sealed. The owner played it
# and reported exactly that -- twice, because the first fix only moved the racks
# out of the doorway and this is a different problem in the same room.
#
# 0.9 m is a doorway. Anything narrower is decoration.
const RACK_END := 0.90

func _plan_racks() -> void:
	racks.clear()
	for r in rooms:
		var n := 0
		match r.kind:
			K_MDF: n = 6
			K_COMMS: n = 1
			K_SERVER: n = 3
			_: continue
		var want: float = float(n - 1) * RACK_PITCH + RACK_W
		var b := _wall_band(r.i, RACK_D, RACK_BACK, RACK_AISLE, want)
		if b.is_empty():
			continue                      # no wall in this room can hold a row
		# The usable run is the wall minus a way past each end of the row.
		var L: float = (b.hi - b.lo) - 2.0 * RACK_END
		if L < RACK_W:
			continue                      # this wall cannot hold a row you can get past
		n = min(n, int(floor((L - RACK_W) / RACK_PITCH)) + 1)
		if n < 1:
			continue
		var run: float = float(n - 1) * RACK_PITCH + RACK_W
		var start: float = b.lo + RACK_END + (L - run) * 0.5
		for i in range(n):
			var d := {"room": r.i, "floor": r.floor, "along_x": bool(b.along_x),
					"face": b.face, "next_u": 36, "x": 0.0, "z": 0.0}
			if b.along_x:
				d.x = start + i * RACK_PITCH
				d.z = b.band.position.y
			else:
				d.x = b.band.position.x
				d.z = start + i * RACK_PITCH
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
	#
	# WHICH SIDE THE FRONT IS ON is now the row's business, not this one's:
	# a row against the far wall faces back into the room, so the rails and the
	# gear go on the -z / -x side of the frame instead.
	var f: Vector3 = k.get("face", Vector3(0, 0, 1))
	var pos: bool = (f.z > 0.0) if k.along_x else (f.x > 0.0)
	var deep: float = d if k.along_x else w
	var front: float = (deep - 0.06) if pos else 0.0
	var hole: float = 0.055 if pos else -0.007
	for s in [0.10, w - 0.13] if k.along_x else [0.10, d - 0.13]:
		var rm := o + (Vector3(s, RACK_BASE, front) if k.along_x else Vector3(front, RACK_BASE, s))
		var rs := Vector3(0.03, RACK_U * U, 0.06) if k.along_x else Vector3(0.06, RACK_U * U, 0.03)
		_box(rm, rs, RACK_RAIL, false)
		for uu in range(0, RACK_U, 3):
			var hm := rm + Vector3(0, uu * U + U * 0.35, 0)
			if k.along_x: hm.z += hole
			else: hm.x += hole
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
	# 6 mm SHY OF THE RAIL PLANE. A box whose front face landed on exactly the
	# plane of the punched rail behind it is the same coplanar-surface flicker
	# as the faceplate, one layer further back.
	var f: Vector3 = k.get("face", Vector3(0, 0, 1))
	if k.along_x:
		var z: float = (k.z + RACK_D - 0.74) if f.z > 0.0 else (k.z + 0.066)
		return {"mn": Vector3(k.x + 0.06, y, z),
				"size": Vector3(RACK_W - 0.12, h, 0.674), "face": f}
	var x: float = (k.x + RACK_D - 0.74) if f.x > 0.0 else (k.x + 0.066)
	return {"mn": Vector3(x, y, k.z + 0.06),
			"size": Vector3(0.674, h, RACK_W - 0.12), "face": f}


# A box standing on the floor of a room, in a row along the low wall, front
# out. This is where a delivery sits until somebody carries it: the height is
# a box on the floor and not a slot at eye level, so it reads as kit that has
# not been racked yet rather than as kit that has.
func _floor_slot(room: int, k: int, nu: int) -> Dictionary:
	var r: Dictionary = rooms[room]
	var h: float = max(0.12, float(nu) * U)
	var y: float = r.floor * fheight + 0.02
	# The same rule as a rack row, at delivery scale: against a wall with no
	# door in it, out of the clear floor every doorway needs. A pallet dumped
	# across the roller door is the same bug as a rack across the MDF door.
	var b := _wall_band(room, 0.62, 0.20, 0.90, 0.62 + float(k) * 0.85)
	var step := 0.85
	if b.is_empty():
		var along_x: bool = (r.x1 - r.x0) >= (r.y1 - r.y0)
		if along_x:
			var xf: float = min(float(r.x0) + 0.9 + float(k) * step, float(r.x1) - 1.0)
			return {"mn": Vector3(xf, y, float(r.y0) + 0.7),
					"size": Vector3(0.62, h, 0.62), "face": Vector3(0, 0, 1)}
		var zf: float = min(float(r.y0) + 0.9 + float(k) * step, float(r.y1) - 1.0)
		return {"mn": Vector3(float(r.x0) + 0.7, y, zf),
				"size": Vector3(0.62, h, 0.62), "face": Vector3(1, 0, 0)}
	var t: float = min(b.lo + float(k) * step, b.hi - 0.62)
	if b.along_x:
		return {"mn": Vector3(t, y, b.band.position.y),
				"size": Vector3(0.62, h, 0.62), "face": b.face}
	return {"mn": Vector3(b.band.position.x, y, t),
			"size": Vector3(0.62, h, 0.62), "face": b.face}


# A TENANT'S DESK, on the floor of the room they rent.
#
# One per square metre on a chequerboard, in reading order, skipping the clear
# floor every doorway needs -- so twenty desks read as a room somebody works
# in and there is a way between them. `k` is the desk's number in that room,
# which is the order site_devs() lists them and therefore the order they were
# installed: t3d0 is always in the same place.
#
# What comes back is where the tenant's COMPUTER stands -- under the desk, on
# the left, its back to the desk's back so the patch lead rises behind the
# furniture rather than through the top of it. The desk itself and the person
# at it are drawn by people.gd from the same cell and the same yaw, which is
# what makes "walk over to their desk" and `go t7d3` the same act: there is one
# place, and the computer, the desk and the person all read it.
func _tenant_desk_slot(room: int, k: int) -> Dictionary:
	var r: Dictionary = rooms[room]
	var clear: Array = []
	for dd in room_doors(room):
		clear.append(dd.clear)
	var spots: Array = []
	for z in range(int(r.y0), int(r.y1)):
		for x in range(int(r.x0), int(r.x1)):
			if (x + z) % 2 != 0:
				continue
			if room_of(int(r.floor), x, z) != room:
				continue
			var cell := Rect2(x, z, 1, 1)
			var blocked := false
			for c in clear:
				if cell.intersects(c):
					blocked = true
			if not blocked:
				spots.append(Vector2(x, z))
	if spots.is_empty():
		return {}
	var p: Vector2 = spots[k % spots.size()]
	var yaw := _desk_yaw(room, int(p.x), int(p.y))
	var centre := Vector3(p.x + 0.5, float(r.floor) * fheight, p.y + 0.5)
	var P = preload("res://scripts/people.gd")
	var a := centre + _rot_xz(Vector3(P.BOX_X0, 0.02, P.BOX_Z0), yaw)
	var b := centre + _rot_xz(Vector3(P.BOX_X1, 0.02 + P.BOX_H, P.BOX_Z1), yaw)
	var mn := Vector3(min(a.x, b.x), a.y, min(a.z, b.z))
	var mx := Vector3(max(a.x, b.x), b.y, max(a.z, b.z))
	return {"mn": mn, "size": mx - mn, "face": _rot_xz(Vector3(0, 0, -1), yaw),
		"centre": centre, "yaw": yaw}


# WHICH WAY THE DESK FACES, decided by the room rather than chosen. The chair
# goes in a square metre that is inside the room and not the clear floor a
# doorway needs -- otherwise a person is sitting in the door -- and of the ways
# that are left, the one with a wall BEHIND the monitor wins, because that is
# where anybody puts a desk. The order is fixed, so the same room always lays
# out the same way.
func _desk_yaw(room: int, x: int, z: int) -> float:
	var r: Dictionary = rooms[room]
	var clear: Array = []
	for dd in room_doors(room):
		clear.append(dd.clear)
	var best := 0.0
	var best_score := -1
	var i := 0
	for d in [Vector2(0, 1), Vector2(0, -1), Vector2(1, 0), Vector2(-1, 0)]:
		var yaw: float = [0.0, PI, PI * 0.5, -PI * 0.5][i]
		i += 1
		var seat := Vector2(x + d.x, z + d.y)
		var score := 0
		if room_of(int(r.floor), int(seat.x), int(seat.y)) != room:
			continue                       # the chair would be in the wall
		var blocked := false
		for c in clear:
			if Rect2(seat.x, seat.y, 1, 1).intersects(c):
				blocked = true
		if blocked:
			continue                       # or in the doorway
		score += 1
		if room_of(int(r.floor), x - int(d.x), z - int(d.y)) != room:
			score += 2                     # a wall behind the screen
		if score > best_score:
			best_score = score
			best = yaw
	return best


# A point in a desk's own frame, put into the building's. The yaws are all
# multiples of a right angle, so this is snapped back to millimetres: a box
# that comes out of a sine 1e-16 off the axis is a box that z-fights the floor.
func _rot_xz(v: Vector3, yaw: float) -> Vector3:
	var s := sin(yaw)
	var c := cos(yaw)
	return Vector3(snappedf(v.x * c + v.z * s, 0.001), v.y,
		snappedf(-v.x * s + v.z * c, 0.001))


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


# ------------------------------------------------------------ the tube lights
#
# "I'd like to add in some of those tube lights that you find in office
# buildings." Recessed twin-tube troffers in the ceiling grid: a 1200 x 300 mm
# steel tray sunk into the slab with two tubes and a slotted reflector between
# them, on the 3 m pitch a real ceiling grid puts them on.
#
# NOT REAL LIGHTS. tower.gd bakes its shading into vertex colours -- see
# _shade() -- so that a headless test and a window render exactly the same
# geometry and a test can assert what a player sees. Adding a DirectionalLight
# or an OmniLight per fitting would make the two disagree and would put several
# hundred lights in a building that has no shadow budget for them. So a tube is
# geometry with a colour brighter than white: _shade() multiplies a downward
# face by 0.62, and a tube given 1/0.62 comes out at full white on the face you
# see it from, which is what a lit tube looks like against a grey slab.
#
# The lighting MODEL is unchanged: still one flat ambient, still no shadow, and
# every headless test that passed before this passes after it.

const TROFFER_KINDS := [K_CORRIDOR, K_LIFTLOBBY, K_LOBBY, K_MDF, K_COMMS,
	K_GOODS, K_SERVER, K_PLANT, K_OFFICE, K_RETAIL]
const TROF_L := 1.20        # a 4 ft fitting, because that is the size they are
const TROF_W := 0.30
const TROF_PITCH := 3.0
const TUBE_COL := Color(1.61, 1.60, 1.52)   # white, through _shade()'s 0.62
const TROF_BODY := Color("#e9e6df")


func _troffers(f: int) -> void:
	# In the slab, not hanging off it: the tray top sits flush with the
	# underside of the ceiling and the tubes hang 40 mm below it.
	var y: float = f * fheight + fheight - SLAB_T - 0.06
	var g := 0
	for r in rooms:
		if r.floor != f or not TROFFER_KINDS.has(int(r.kind)):
			continue
		var w: int = r.x1 - r.x0
		var h: int = r.y1 - r.y0
		# the fittings run along the long axis of the room, as they do in a
		# real ceiling, and are laid out from the middle so a narrow corridor
		# gets a line down the centre of it rather than one down one edge
		var along_x: bool = w >= h
		var nx: int = max(1, int(round(float(w) / TROF_PITCH)))
		var ny: int = max(1, int(round(float(h) / TROF_PITCH)))
		for ix in range(nx):
			for iy in range(ny):
				var cx: float = float(r.x0) + float(w) * (float(ix) + 0.5) / float(nx)
				var cz: float = float(r.y0) + float(h) * (float(iy) + 0.5) / float(ny)
				var lx: float = TROF_L if along_x else TROF_W
				var lz: float = TROF_W if along_x else TROF_L
				if lx > float(w) - 0.3 or lz > float(h) - 0.3:
					continue
				_troffer(Vector3(cx - lx * 0.5, y, cz - lz * 0.5), lx, lz, along_x)
				g += 1


func _troffer(mn: Vector3, lx: float, lz: float, along_x: bool) -> void:
	# the steel tray, recessed: a shallow open box with its mouth downwards
	_box(mn, Vector3(lx, 0.06, lz), TROF_BODY, false)
	# two tubes, in from the long edges, hanging under the tray
	var tw := 0.05
	for k in [0.26, 0.74]:
		var tm := mn
		if along_x:
			tm.z += lz * k - tw * 0.5
			tm.x += 0.06
			_box(Vector3(tm.x, mn.y - 0.045, tm.z), Vector3(lx - 0.12, 0.045, tw),
				TUBE_COL, false)
		else:
			tm.x += lx * k - tw * 0.5
			tm.z += 0.06
			_box(Vector3(tm.x, mn.y - 0.045, tm.z), Vector3(tw, 0.045, lz - 0.12),
				TUBE_COL, false)
	# the reflector between them, which is what makes it read as a fitting
	# rather than as two glowing sticks stuck to the ceiling
	if along_x:
		_box(Vector3(mn.x + 0.04, mn.y - 0.022, mn.z + lz * 0.44),
			Vector3(lx - 0.08, 0.022, lz * 0.12), Color("#f6f3ea"), false)
	else:
		_box(Vector3(mn.x + lx * 0.44, mn.y - 0.022, mn.z + 0.04),
			Vector3(lx * 0.12, 0.022, lz - 0.08), Color("#f6f3ea"), false)


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
	if not site_up:
		floors_in_service += 1
		for l in lifts:
			l.rebuild_panels()
		_signage()
		return "floor %d is in service." % f
	# `open` IS THE SESSION'S VERB, and so are its refusals. It costs the
	# landlord's fit-out and it wants somebody standing on the floor to sign it
	# off -- the lift will not take you to a floor that is not in service, so
	# that is the stairs. None of those rules is written here, and the words
	# below are core's words, not a second set that could drift from them.
	var said: String = site("open").strip_edges()
	var now := int(ses_state().get("floors", floors_in_service))
	if now == floors_in_service:
		return said                      # refused: it says why, in its own words
	floors_in_service = now
	for l in lifts:
		l.rebuild_panels()
	_signage()
	return said


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
	_wayfinding()


# ---------------------------------------------------------------- wayfinding
#
# "I would also suggest that there were multiple floors, but there's no
# staircase, at least not one that's accessible."
#
# The staircase is there and a walking body climbs every flight of it. What was
# not there was any reason to walk towards it: on seed 200 the stairwell is
# diagonally across the plate from the MDF, past the lifts and the toilets, and
# nothing anywhere said so. A building nobody can navigate has no stairs,
# whatever the geometry says.
#
# So every corridor and lift lobby gets a sign over the door you should go
# through, and WHICH DOOR THAT IS is not this file's opinion: bld_walk() gives
# the real walking distance from the destination to every room, and the door
# hung with the sign is the one into the neighbour that is nearer. It is the
# building's own metric, so a sign cannot point at a route that is not there.
const WAY_KINDS := [K_CORRIDOR, K_LIFTLOBBY, K_LOBBY]
const WAY_TO := [[K_STAIR, "STAIRS"], [K_GOODS, "GOODS IN"], [K_MDF, "MDF"]]


func _wayfinding() -> void:
	for f in range(nfloors):
		for spec in WAY_TO:
			var dest := find_room(f, int(spec[0]))
			if dest < 0:
				continue
			var d := walk_from(dest)
			if d.size() < rooms.size():
				continue
			for r in rooms:
				if r.floor != f or not WAY_KINDS.has(int(r.kind)):
					continue
				if d[r.i] < 0.0 or d[r.i] < 0.5:
					continue                  # you are in it
				var best := -1
				var bestd: float = d[r.i]
				var through: Dictionary = {}
				for door in doors:
					if door.floor != f or (door.a != r.i and door.b != r.i):
						continue
					var other: int = door.b if door.a == r.i else door.a
					if other >= rooms.size() or d[other] < 0.0:
						continue
					if d[other] < bestd - 0.5:
						bestd = d[other]
						best = other
						through = door
				if best < 0 or best == dest:
					continue        # the door itself is already signed as that room
				var p := Vector3(through.x + (1.0 if through.dir == 0 else 0.5),
					f * fheight + DOOR_H + 0.40,
					through.y + (0.5 if through.dir == 0 else 1.0))
				var toward := room_centre(r.i) - room_centre(best)
				toward.y = 0
				if toward.length() < 0.01:
					continue
				toward = toward.normalized()
				p += toward * (WALL_T * 0.5 + 0.05)
				# "TO STAIRS" rather than "STAIRS": the door under a plain
				# STAIRS sign is the stairwell, and this one is the way to it.
				_sign(p, "TO " + str(spec[1]), atan2(toward.x, toward.z), 18,
					Color("#c9d8a8"))


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
	pass                              # the session does this now: _ses_start()


# EVERY VERB THE 3D PERFORMS IS THE SOCKET'S VERB.
#
# The owner: "Perhaps the commands it's using should tie to 3D space... Claude
# playing a video game in 3D space by taking screenshots of the actual user
# interface is not a fantastic way for it to iterate. It should operate with
# commands over the port. A 3D interface should keep up with what's happening.
# So if you cable from one place to another place, a physical cord gets
# rendered out as if the player had cabled that."
#
# So this file no longer calls site_cable(), site_move() or site_power(). It
# says `plug core:6`, `carry core`, `drop` -- through session_line(), which is
# byte for byte what a socket client runs. One implementation, one set of
# refusals, one place the metres are charged. And it runs the other way too:
# command() below is the whole 3D driven by text, which is what lets a blind
# agent play this and a human watch it play.
func _ses_start() -> void:
	site_up = false
	if machine == null or not machine.has_method("ses_start"):
		return
	if str(machine.ses_start(seed_no, int(machine.site_opening_money()))).strip_edges() == "":
		return
	site_up = true
	# WHAT IS IN THE VAN ON DAY ONE, and it is deliberately not much.
	#
	# It was a router, a switch24 and a server: 2,400 of the best kit in the
	# shop, free, standing in goods in before the player had made a single
	# decision. The owner: "I suspect the default gear given to the player is
	# too much. You should start with basic server, basic uplink, and a switch
	# with a few ports. Just enough to get off the ground, not enough to keep
	# the whole system running until day thirty."
	#
	# So it is the bottom of each range -- a switch4 and a minitower, 505 the
	# pair -- and no router at all, because routing between two subnets is a
	# thing you decide you need and then buy. Measured on seed 7008: this kit
	# carries three desks and cannot address a second segment, and the first
	# twenty-desk tenancy pays nothing until a real server is under it. That
	# is the decision the grades were built for, arriving on day one.
	for line in START_KIT:
		machine.ses_cmd(line)
	# HOW MUCH OF THE TOWER IS OPEN IS THE SESSION'S NUMBER. It used to be a 2
	# written in this file, which was right on day one and a second source of
	# truth for every day after it -- a socket client that typed `open` moved
	# core's count and left the lift's lit buttons behind.
	floors_in_service = int(ses_state().get("floors", floors_in_service))


# What the session says about the player: where they are, what is in their
# hands, what a lead is in. The view reads this; it does not keep a copy.
var _st_cache := {}
var _st_frame := -1

# WHAT TO PRINT IN FRONT OF THE CURSOR, and it is core's answer rather than
# this file's. session_prompt() knows whether the words a client types are
# going to the body, to an appliance's management line or to a real shell on a
# real machine; the wire prints it after every answer, so a socket player can
# always see which machine they are talking to. See the note in wire.gd.
func ses_prompt() -> String:
	if not site_up or not machine.has_method("ses_prompt"):
		return ""
	return str(machine.ses_prompt())


func ses_state() -> Dictionary:
	# CACHED FOR THE FRAME. Every read of it parses a dump out of the
	# extension, and `carrying` is read several times a frame by the HUD and
	# the crosshair; a text protocol is the right shape and re-parsing it
	# sixty times a second is not.
	var fr := Engine.get_process_frames()
	if fr == _st_frame:
		return _st_cache
	var out := {}
	if not site_up:
		_st_cache = out
		_st_frame = fr
		return out
	for line in str(machine.ses_state()).split("\n", false):
		var f: PackedStringArray = line.split(" ", false)
		if f.size() < 2:
			continue
		var vals: Array = []
		for i in range(1, f.size()):
			vals.append(int(f[i]))
		out[f[0]] = vals[0] if vals.size() == 1 else vals
	_st_cache = out
	_st_frame = fr
	return out


# One line, one thing, and the answer the socket would have got.
func site(line: String) -> String:
	if not site_up:
		return "there is no site yet\n"
	_st_frame = -1
	var out := str(machine.ses_cmd(line))
	_st_frame = -1
	# SOMETHING HAPPENED, so the clipboard is stale. Every line that reaches
	# here can move money, a box or a tenancy, and the day/service/load panels
	# are a READ of those verbs rather than a copy -- so the only thing kept
	# here is "go and ask again", once, on the next frame.
	if not _snapping:
		_snap_dirty = true
	return out


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
			"kindname": f[6], "name": f[7],
			# The button and the plug, both facts and both the model's. A
			# monitor on a box with no power in it shows nothing.
			"powered": f.size() > 8 and int(f[8]) != 0,
			"mains": f.size() > 9 and int(f[9]) != 0})
	return out


# ------------------------------------------------------------------- power
#
# "None of the rooms seemed to have power outlets." -- the owner, walking his
# own building. And: "it seems to not have a power cable, I don't see power
# going to any of the networking gear, or the server... The goods room has a
# few items that are not plugged into power, but seem to be working."
#
# Every one of those is true of the picture and false of the model. Core has
# had a full power system since D37: sockets counted per room off the room's
# kind and area, a plug that is a separate act from the button, another socket
# buyable for money on a circuit that eventually fills up, and a five percent
# chance of a filesystem to repair if you pull a live one. `outlets` prints
# all of it. The 3D drew none of it, so the one system the player was told to
# care about was the one system the world would not show -- and a box standing
# on nothing, running, is the world flatly contradicting the model.
#
# So the wall gets its sockets, and every box that is really in one gets its
# lead. Both come from the model on every reconcile: buy a socket with
# `outlet`, pull a plug with `mains`, and the wall and the flex follow.

func site_outlets() -> Dictionary:
	var out: Dictionary = {}
	if not site_up:
		return out
	for line in str(machine.site_outlets()).split("\n", false):
		var f: PackedStringArray = line.split(" ", false)
		if f.size() < 4:
			continue
		out[int(f[0])] = {"built": int(f[1]), "used": int(f[2]), "free": int(f[3])}
	return out


# WHERE THE SOCKETS ON A ROOM'S WALL ARE. Along the longest wall, at the height
# a socket is really at, inset from the corners so a faceplate is never in one.
# Computed rather than stored, for the same reason the lift's buttons are: a
# thing drawn in one place and reached for in another is one fact with two
# answers, and this one has a lead plugged into it.
const OUTLET_Y := 0.32          # centre height, metres off the slab
const OUTLET_W := 0.09
const OUTLET_H := 0.13

func outlet_points(room_i: int) -> Array:
	var out: Array = []
	var have: int = int(site_outlets().get(room_i, {}).get("built", 0))
	if have <= 0 or room_i < 0 or room_i >= rooms.size():
		return out
	var r = rooms[room_i]
	var y: float = float(r.floor) * fheight + OUTLET_Y
	var wx: float = float(r.x1 - r.x0)
	var wy: float = float(r.y1 - r.y0)
	# the longest wall, and the inward normal of it
	var along_x: bool = wx >= wy
	var span: float = (wx if along_x else wy) - 0.8
	if span < 0.3:
		return out
	for k in range(have):
		var t: float = 0.4 + (span * (float(k) + 0.5) / float(have))
		if along_x:
			out.append({"pos": Vector3(float(r.x0) + t, y, float(r.y0) + 0.03),
				"n": Vector3(0, 0, 1)})
		else:
			out.append({"pos": Vector3(float(r.x0) + 0.03, y, float(r.y0) + t),
				"n": Vector3(1, 0, 0)})
	return out


# The plug end on a box: low on its back, which is where a kettle lead goes.
func _inlet_point(d: Dictionary) -> Vector3:
	var back: Vector3 = -Vector3(d.face)
	var c: Vector3 = d.mn + Vector3(d.size) * 0.5
	var half: float = absf(Vector3(d.size).dot(back)) * 0.5
	return Vector3(c.x, float(d.mn.y) + 0.04, c.z) + back * (half - 0.01)


func _draw_power() -> void:
	if _power_node:
		_power_node.name = "PowerGone"
		_power_node.queue_free()
		_power_node = null
	if not site_up:
		return
	var g = preload("res://scripts/vgeo.gd").new()
	var plate := Color("#d9d6cf")
	var slot := Color("#2b2f34")
	var flex := Color("#23262a")
	# every socket on every wall the model says has one
	var seats: Dictionary = {}
	for room_i in site_outlets().keys():
		var pts: Array = outlet_points(int(room_i))
		seats[int(room_i)] = pts
		for p in pts:
			var c: Vector3 = p.pos
			var n: Vector3 = p.n
			var w := Vector3(n.z, 0, n.x).abs() * OUTLET_W
			var mn := Vector3(c.x - max(w.x, 0.006), c.y - OUTLET_H * 0.5, c.z - max(w.z, 0.006))
			var sz := Vector3(max(w.x * 2.0, 0.012), OUTLET_H, max(w.z * 2.0, 0.012))
			g.box(mn, sz, plate, false)
			# two holes in it, so it reads as a socket and not a light switch
			for s in [-0.025, 0.025]:
				var o: Vector3 = Vector3(n.z, 0, n.x).normalized() * float(s)
				g.box(Vector3(c.x + o.x - 0.008, c.y - 0.012, c.z + o.z - 0.008),
					Vector3(0.016, 0.024, 0.016), slot, false)
	# and a lead from every box the model says is plugged in
	var used: Dictionary = {}
	for d in devices:
		var si: int = int(d.get("site", -1))
		if si < 0:
			continue
		var sd := _site_dev(si)
		if sd.is_empty() or not bool(sd.get("mains", false)):
			continue
		var room_i: int = int(sd.get("room", -1))
		var pts: Array = seats.get(room_i, [])
		if pts.is_empty():
			continue
		var k: int = int(used.get(room_i, 0)) % pts.size()
		used[room_i] = k + 1
		var a: Vector3 = _inlet_point(d)
		var b: Vector3 = Vector3(pts[k].pos) + Vector3(pts[k].n) * 0.02
		_run_cable(g, [a, a + (b - a) * 0.5 + Vector3(0, -0.10, 0), b], flex, 9000 + si)
	_power_node = MeshInstance3D.new()
	_power_node.name = "Power"
	_power_node.mesh = g.mesh()
	add_child(_power_node)


# HOW MUCH FLEX AND FACEPLATE IS REALLY DRAWN, in vertices, so a headless test
# can tell "the wall has sockets on it" from "the wall has nothing on it".
func power_drawn() -> int:
	if _power_node == null or not is_instance_valid(_power_node):
		return 0
	var m: Mesh = _power_node.mesh
	if m == null or m.get_surface_count() == 0:
		return 0
	return m.surface_get_array_len(0)


func _site_dev(site_i: int) -> Dictionary:
	for sd in site_devs():
		if int(sd.i) == site_i:
			return sd
	return {}


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
# HOW MUCH COPPER IS ACTUALLY DRAWN, in vertices. A link the site model holds
# and the world does not show is the exact failure the reconcile exists to
# stop, and counting the node by name cannot see it: a queue_free()d node keeps
# its name until the end of the frame, so the replacement is renamed and a
# lookup finds the corpse.
func cables_drawn() -> int:
	if _cable_node == null or not is_instance_valid(_cable_node):
		return 0
	var m: Mesh = _cable_node.mesh
	if m == null or m.get_surface_count() == 0:
		return 0
	return m.surface_get_array_len(0)


func _draw_cables() -> void:
	if _cable_node:
		_cable_node.name = "CablesGone"     # see cables_drawn()
		_cable_node.queue_free()
		_cable_node = null
	var links := site_links()
	if links.is_empty():
		_port_lights()
		_draw_power()
		return
	var g = preload("res://scripts/vgeo.gd").new()
	for l in links:
		if l.state < 0:
			continue                     # pulled out
		var a := _dev_point(l.a, l.aport)
		var b := _dev_point(l.b, l.bport)
		if a == Vector3.INF or b == Vector3.INF:
			continue          # a device the view has not drawn has no end to draw to
		var col: Color = _cable_colour(l)
		var pts := _cable_route(l.a, l.aport, l.b, l.bport, int(l.i))
		_run_cable(g, pts, col, int(l.i))
	_cable_node = MeshInstance3D.new()
	_cable_node.name = "Cables"
	_cable_node.mesh = g.mesh()
	add_child(_cable_node)
	_port_lights()
	_draw_power()


# ------------------------------------------------------------ string mechanics
#
# "Proper string mechanics... once run it should stay in the world as a real
# slack cable -- a catenary sag between its endpoints, following the tray where
# it goes through one, not a straight line floating through walls. Cables
# should look like copper somebody actually ran: colour by kind, a bend radius,
# a little disorder."
#
# Three things make copper look like copper and none of them is the colour:
#
#   THE ROUTE. It comes out of the socket along the socket's own axis, goes up
#   to the containment, travels along the tray through rooms that have one, and
#   comes down at the other end. _tray_route() is a breadth-first search over
#   the same metre grid the walls are built from, so a cable never crosses a
#   wall that a person could not walk through either.
#
#   THE CORNERS. Copper has a bend radius -- four times its diameter is the
#   number on the box -- so every corner is an arc rather than a right angle.
#
#   THE SLACK. A span between two supports hangs. Between the tray and a port
#   the drop is nearly straight; along an unsupported span it sags, by an
#   amount that grows with the span, and every run gets a couple of centimetres
#   of its own disorder off its link number so that two cables between the same
#   two frames are not one cable drawn twice.

# 9 mm across, which is what a patch lead with a moulded boot on it measures
# -- and it is drawn at the size the thing in your hand is rather than at the
# 6.4 mm of the cable inside its jacket, because the owner's note on a floor of
# twenty cabled desks was "I don't see cabling for any of the boxes" and 6.4 mm
# at four metres is one pixel of hairline. The METRES and the PRICE are the site
# model's and are untouched: this is the diameter of the picture, not of the
# copper the game charges you for.
const CABLE_R := 0.0045
const BEND_R := 0.09           # the radius copper will actually take
const SKIRT_Y := 0.06          # where a lead lies when it is lying on a floor


# The colour of a kind of cable, as core/netstack.h numbers them. Grey for
# fibre because fibre patch leads are, and the two coppers are the two colours
# every cabinet in the world is full of.
const CABLE_KIND_COL := [Color("#3fae6a"), Color("#2f6fd0"), Color("#c9c6bd"),
	Color("#c8a33a")]


func _cable_colour(l: Dictionary) -> Color:
	if int(l.state) == 2:              # PORT_TOOLONG: it was laid and it is dead
		return Color("#8a3232")
	var c: Color = CABLE_KIND_COL[int(l.kind) % CABLE_KIND_COL.size()]
	# a little disorder: two runs of the same kind are not the same colour to
	# the millimetre, because two reels are not
	var j: float = float(int(l.i) % 5) * 0.035
	return c.lightened(j) if (int(l.i) % 2) == 0 else c.darkened(j)


# The waypoints a run passes through, port to port.
# Set by _tray_route when the route crosses floors: the index in the returned
# cells at which the leg on the far floor begins. -1 when it does not.
var _riser_split := -1
# The rooms whose leg of a run lies on the floor: see _route_between().
var _skirt_rooms: Array = []


func _cable_route(sa: int, pa: int, sb: int, pb: int, salt: int) -> Array:
	var a := _dev_point(sa, pa)
	var b := _dev_point(sb, pb)
	if a == Vector3.INF or b == Vector3.INF:
		return []
	var fa := _dev_face(sa)
	var fb := _dev_face(sb)
	# out of the socket, along the socket's own axis: a lead does not leave a
	# port sideways
	var a1 := a + fa * 0.05
	var b1 := b + fb * 0.05
	if a.distance_to(b) < 2.5:
		# A PATCH LEAD, between two boxes in the same frame. It does not go up
		# into the tray to travel 400 mm: it comes out of the front, hangs down
		# past whatever is between them, and goes back in. That loop off the
		# front of a rack is what a rack somebody has worked on looks like.
		var lo: float = min(a.y, b.y) - 0.10 - float(salt % 4) * 0.03
		var outd: float = 0.09 + float(salt % 4) * 0.025
		var a2 := a + fa * outd
		var b2 := b + fb * outd
		return [a, a1, a2, Vector3(a2.x, lo, a2.z), Vector3(b2.x, lo, b2.z), b2, b1, b]
	var pts: Array = [a, a1]
	pts.append_array(_route_between(a1, b1, _dev_skirting(sa), _dev_skirting(sb)))
	pts.append(b1)
	pts.append(b)
	return pts


# A DESK'S LEAD DOES NOT FLY ACROSS THE OFFICE AT CEILING HEIGHT.
#
# The owner, looking at a floor of twenty cabled desks: "I don't see cabling
# for any of the boxes." The cable was there and the model had every metre of
# it: it climbed out of the machine under the desk straight up to tray height
# and crossed the room two and a half metres in the air, where it is a hairline
# against a ceiling with nothing else near it to give it a scale.
#
# A tenancy's office has no containment in it -- see TRAY_KINDS, an office is
# not one -- and what really happens in a floor somebody has patched off a
# spool is that the leads run along the skirting to the door and go up into the
# tray in the corridor. So that is what the leg INSIDE their room does now:
# floor level, past the feet of the desks, out of the door. It reads as cable
# somebody ran because it is where cable somebody ran would be, and it is the
# same length of copper the site model already charged for.
#
# Which room, or -1 for a device whose lead behaves as it always did.
func _dev_skirting(site_i: int) -> int:
	for d in devices:
		if int(d.get("site", -1)) != site_i:
			continue
		if not bool(d.get("tenant_desk", false)):
			return -1
		return int(d.get("room_i", -1))
	return -1


# THE PART OF THE ROUTE THAT IS IN THE BUILDING: up into the tray at one end,
# along it, up or down the riser if the two ends are on different floors, and
# along the far floor's tray to over the other end. It is its own function
# because game/tests/tower.gd checks it against the slabs -- a check that
# re-derived the leg heights itself would be checking its own copy of the bug.
func _route_between(a1: Vector3, b1: Vector3, skirt_a := -1, skirt_b := -1) -> Array:
	var fa_i := int(floor((a1.y + 0.3) / fheight))
	var fb_i := int(floor((b1.y + 0.3) / fheight))
	var ya := tray_y(fa_i)
	var yb := tray_y(fb_i)
	# Where a lead lies on the floor of the room it starts in: 60 mm up, which
	# is the middle of a skirting and clear of the slab it is drawn on.
	var la := float(fa_i) * fheight + SKIRT_Y
	var lb := float(fb_i) * fheight + SKIRT_Y
	var pts: Array = [Vector3(a1.x, la if skirt_a >= 0 else ya, a1.z)]
	_riser_split = -1
	# WHICH ROOMS THE ROUTE IS NOT ALLOWED TO SIMPLIFY THROUGH. _tray_route
	# returns the CORNERS of the path, and a lead that runs straight out of an
	# office has no corner in it -- so the whole in-room leg vanished and the
	# only thing left to draw was the diagonal from the door to the desk. The
	# cell each side of a doorway is kept when one of these rooms is involved,
	# which is where the run stops lying on the floor and climbs into the tray.
	_skirt_rooms = []
	if skirt_a >= 0: _skirt_rooms.append(skirt_a)
	if skirt_b >= 0: _skirt_rooms.append(skirt_b)
	var cells := _tray_route(fa_i, Vector2(a1.x, a1.z), fb_i, Vector2(b1.x, b1.z))
	_skirt_rooms = []
	var split := _riser_split
	# WHERE THE CLIMB HAPPENS. The route across two floors is the leg on floor
	# A, the riser, and the leg on floor B -- and every cell of it used to be
	# stamped at floor A's tray height, with the single vertical put at the LAST
	# cell, which is the one beside the box you are plugging into. So the second
	# leg ran at the wrong storey's height and the cable climbed through the
	# ceiling slab over the destination rack. It climbs in the riser now, which
	# is where the hole is, and each leg is drawn at its own floor's tray.
	for i in range(cells.size()):
		if split > 0 and i == split:
			var r: Vector2 = cells[split - 1]
			pts.append(Vector3(r.x, yb, r.y))
		var y: float = ya if (split < 0 or i < split) else yb
		# STILL ON THE FLOOR WHILE IT IS STILL IN THE ROOM. A cell that belongs
		# to the room the lead came out of is a cell the lead is lying on the
		# floor of; the first cell that is not is the doorway, and the climb up
		# to the tray happens there rather than over the desk.
		# A ROOM INDEX IS UNIQUE IN THE BUILDING, not per floor, so which leg a
		# cell belongs to does not come into it: a cell that is in the room the
		# lead came out of is a cell the lead is lying on the floor of. The
		# earlier version asked which LEG it was and got a same-floor run wrong,
		# because a run that never leaves the floor is all one leg and the far
		# end's room never matched.
		var on_a: bool = split < 0 or i < split
		var room_here: int = room_of(fa_i if on_a else fb_i,
			int(floor(cells[i].x)), int(floor(cells[i].y)))
		if skirt_a >= 0 and room_here == skirt_a:
			y = la
		elif skirt_b >= 0 and room_here == skirt_b:
			y = lb
		# AND THE CLIMB HAPPENS AT THE DOOR, NOT OVER THE DESK. It is a vertical
		# at the cell the run is leaving -- the corridor side of the doorway,
		# because the two cells either side of it are both kept -- and then a
		# horizontal into the room at the new height. Climbing at the cell it is
		# ARRIVING at put the vertical just inside the office and left the
		# horizontal leg crossing their room at tray height, which is the
		# picture this was written to get rid of; game/tests/tower.gd measures
		# it now.
		var prev: Vector3 = pts[pts.size() - 1]
		if absf(prev.y - y) > 0.01:
			pts.append(Vector3(prev.x, y, prev.z))
		pts.append(Vector3(cells[i].x, y, cells[i].y))
	if split < 0 and absf(ya - yb) > 0.01 and not cells.is_empty():
		# No riser on this floor at all: the site model has already priced the
		# real run, and the shortest honest thing to draw is a straight climb.
		var last: Vector2 = cells[cells.size() - 1]
		pts.append(Vector3(last.x, yb, last.y))
	# OVER THE FAR END, at whatever height that end's leg runs at. This was
	# always the same point as the last cell -- _tray_route ends ON the target
	# -- and it stays that way for a lead that comes down out of the tray; for
	# one lying on the floor of the room it is going into, a point at tray
	# height here would send it back up to the ceiling above the desk it has
	# just reached.
	var endy: float = yb
	if skirt_b >= 0 and room_of(fb_i, int(floor(b1.x)), int(floor(b1.z))) == skirt_b:
		endy = lb
	var tail := Vector3(b1.x, endy, b1.z)
	if pts.is_empty() or pts[pts.size() - 1].distance_to(tail) > 0.01:
		pts.append(tail)
	return pts


# THE TRAY, AS A ROUTE. Breadth-first over the metre grid of the floor, through
# cells whose room has containment in it -- and through the two endpoint rooms
# whether they have any or not, because a cable has to be able to get out of
# the room it starts in. Returns the corners of the route in metres, or an
# empty list when there is no way through, in which case the caller draws the
# shortest thing it can and the site model has already priced the real one.
func _tray_route(fa: int, from: Vector2, fb: int, to: Vector2) -> Array:
	if fa != fb:
		# ACROSS FLOORS IT GOES UP THE RISER. Horizontally to the riser on this
		# floor, vertically, and horizontally again -- which is the answer to
		# "why is the cable run 33 m when the walk was 78".
		var r := find_room(fa, K_RISER)
		if r < 0: r = find_room(fa, K_STAIR)
		if r < 0:
			return [to]
		var c := room_centre(r)
		var up := _tray_route(fa, from, fa, Vector2(c.x, c.z))
		var down := _tray_route(fb, Vector2(c.x, c.z), fb, to)
		# WHICH CELL IS THE RISER, told to the caller. The two legs are on
		# different floors and are drawn at different heights, and a flat list
		# of corners cannot say where one stops being the other.
		_riser_split = up.size()
		return up + down
	var sx := int(floor(from.x))
	var sy := int(floor(from.y))
	var tx := int(floor(to.x))
	var ty := int(floor(to.y))
	if sx == tx and sy == ty:
		return [to]
	var ra := room_of(fa, sx, sy)
	var rb := room_of(fa, tx, ty)
	var prev := PackedInt32Array()
	prev.resize(bw * bh)
	prev.fill(-2)
	var q := PackedInt32Array()
	q.append(sy * bw + sx)
	prev[sy * bw + sx] = -1
	var head := 0
	var found := false
	while head < q.size():
		var cur: int = q[head]
		head += 1
		if cur == ty * bw + tx:
			found = true
			break
		var cx: int = cur % bw
		var cy: int = cur / bw
		for d in [[1, 0], [-1, 0], [0, 1], [0, -1]]:
			var nx: int = cx + d[0]
			var ny: int = cy + d[1]
			if nx < 0 or ny < 0 or nx >= bw or ny >= bh:
				continue
			if prev[ny * bw + nx] != -2:
				continue
			var rr := room_of(fa, nx, ny)
			if rr == NOROOM:
				continue
			if rr != ra and rr != rb and not TRAY_KINDS.has(int(rooms[rr].kind)):
				continue
			# a doorway or an open edge: a cable goes where a wall is not, the
			# same test the wall pass itself makes
			if not _open_edge(fa, cx, cy, nx, ny):
				continue
			prev[ny * bw + nx] = cur
			q.append(ny * bw + nx)
	if not found:
		return [to]
	var path: Array = []
	var at: int = ty * bw + tx
	while at >= 0:
		path.push_front(Vector2(float(at % bw) + 0.5, float(at / bw) + 0.5))
		at = prev[at]
	# only the corners: a hundred metre-long segments is the same line with a
	# hundred times the triangles in it -- and the two cells either side of the
	# door of a room whose leg runs along the floor, because that is where the
	# run leaves the floor and there is no corner there to keep.
	var out: Array = []
	for i in range(1, path.size() - 1):
		var p0: Vector2 = path[i - 1]
		var p1: Vector2 = path[i]
		var p2: Vector2 = path[i + 1]
		var keep := absf((p1 - p0).angle_to(p2 - p1)) > 0.01
		if not keep and not _skirt_rooms.is_empty():
			var r1 := room_of(fa, int(floor(p1.x)), int(floor(p1.y)))
			var r0 := room_of(fa, int(floor(p0.x)), int(floor(p0.y)))
			var r2 := room_of(fa, int(floor(p2.x)), int(floor(p2.y)))
			keep = (r1 != r0 or r1 != r2) \
				and (_skirt_rooms.has(r0) or _skirt_rooms.has(r1) or _skirt_rooms.has(r2))
		if keep:
			out.append(p1)
	out.append(to)
	return out


# Is there a way through between two neighbouring cells: same room, or a door
# on the edge between them. The same doorset the walls were built from.
func _open_edge(f: int, x0: int, y0: int, x1: int, y1: int) -> bool:
	var ra := room_of(f, x0, y0)
	var rb := room_of(f, x1, y1)
	if ra == rb:
		return true
	# the same corridor-to-corridor rule the walls keep and step_ok() states
	if ra != NOROOM and rb != NOROOM and int(rooms[ra].kind) == K_CORRIDOR \
			and int(rooms[rb].kind) == K_CORRIDOR:
		return true
	if x1 > x0: return doorset.has("%d,%d,%d,%d" % [f, x0, y0, 0])
	if x1 < x0: return doorset.has("%d,%d,%d,%d" % [f, x1, y1, 0])
	if y1 > y0: return doorset.has("%d,%d,%d,%d" % [f, x0, y0, 1])
	return doorset.has("%d,%d,%d,%d" % [f, x1, y1, 1])


# Waypoints in, copper out: corners rounded to a bend radius, spans given the
# slack they would really have, and the whole thing swept as one length.
func _run_cable(g, pts: Array, col: Color, salt := 0) -> void:
	if pts.size() < 2:
		return
	var line := _round_corners(pts, BEND_R)
	line = _sag(line, salt)
	for i in range(line.size() - 1):
		g.tube(line[i], line[i + 1], CABLE_R, col)


# A corner is an arc, not a right angle. Four segments of one, which at 90 mm
# radius is smooth at the distance you look at a cable from.
func _round_corners(pts: Array, r: float) -> Array:
	if pts.size() < 3:
		return pts
	var out: Array = [pts[0]]
	for i in range(1, pts.size() - 1):
		var p0: Vector3 = pts[i - 1]
		var p1: Vector3 = pts[i]
		var p2: Vector3 = pts[i + 1]
		var d0: Vector3 = p1 - p0
		var d1: Vector3 = p2 - p1
		var l0 := d0.length()
		var l1 := d1.length()
		if l0 < 1e-5 or l1 < 1e-5:
			continue
		var rr: float = min(r, min(l0, l1) * 0.45)
		var a := p1 - d0.normalized() * rr
		var b := p1 + d1.normalized() * rr
		out.append(a)
		for k in range(1, 4):
			var t := float(k) / 4.0
			# quadratic through the corner: the arc a length of copper takes
			out.append(a.lerp(p1, t).lerp(p1.lerp(b, t), t))
		out.append(b)
	out.append(pts[pts.size() - 1])
	return out


# THE SLACK. A span hangs between its ends by an amount that grows with the
# span, and a length of copper in a tray hangs a great deal less than one
# crossing a rack, because the tray is holding it up. So the droop is applied
# to spans that are level and not at tray height, and every run gets its own
# small offset so two cables between the same two frames are not one cable.
func _sag(line: Array, salt: int) -> Array:
	if line.size() < 2:
		return line
	var out: Array = []
	for i in range(line.size() - 1):
		var a: Vector3 = line[i]
		var b: Vector3 = line[i + 1]
		out.append(a)
		var span := Vector2(b.x - a.x, b.z - a.z).length()
		if span < 0.25 or absf(b.y - a.y) > 0.3:
			continue                       # a drop does not sag: it hangs straight
		var lying := absf(a.y - tray_y(int(floor((a.y + 0.3) / fheight)))) < 0.06
		# AND A CABLE ON THE FLOOR DOES NOT SAG AT ALL, because the floor is
		# holding it up. Without this the skirting run of a desk lead dips
		# through the slab it is lying on and half of it is inside the concrete.
		var down: float = a.y - float(int(floor((a.y + 0.3) / fheight))) * fheight
		if down < SKIRT_Y + 0.06:
			continue
		var drop: float = span * (0.02 if lying else 0.09)
		drop += float(salt % 3) * 0.004
		var n := int(clamp(span * 3.0, 3.0, 12.0))
		for k in range(1, n):
			var t := float(k) / float(n)
			var p := a.lerp(b, t)
			p.y -= drop * 4.0 * t * (1.0 - t)
			out.append(p)
	out.append(line[line.size() - 1])
	return out


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
	# IN THE AISLE, looking at the row. Which wall the row is against is the
	# doors' decision now, so this reads it off the frames rather than assuming
	# the low edge of the short axis.
	var mine := racks_in(i)
	if not mine.is_empty():
		var mid := Vector3.ZERO
		var face: Vector3 = racks[mine[0]].get("face", Vector3(0, 0, 1))
		for m in mine:
			mid += _rack_front(m)
		mid /= float(mine.size())
		p = mid + face * 3.0
		p.x = clampf(p.x, float(r.x0) + 0.6, float(r.x1) - 0.6)
		p.z = clampf(p.z, float(r.y0) + 0.6, float(r.y1) - 0.6)
	p.y = 0.1
	return p


# The middle of the front face of frame `i`, at chest height, in metres.
func _rack_front(i: int) -> Vector3:
	var k: Dictionary = racks[i]
	var f: Vector3 = k.get("face", Vector3(0, 0, 1))
	var w: float = RACK_W if k.along_x else RACK_D
	var d: float = RACK_D if k.along_x else RACK_W
	var p := Vector3(k.x + w * 0.5, k.floor * fheight + 1.0, k.z + d * 0.5)
	if k.along_x: p.z = k.z + (d if f.z > 0.0 else 0.0)
	else: p.x = k.x + (w if f.x > 0.0 else 0.0)
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
		mid += _rack_front(m)
	mid /= float(mine.size())
	var to := mid - spawn_point()
	to.y = 0.0
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
# HOW TALL EACH KIND IS IN A RACK, AND WHAT COLOUR IT IS. Both tables have to
# name every kind the catalogue sells: a kind missing from them is drawn as an
# anonymous grey 1U box, which is not a crash and is a lie -- a tower unit
# standing in a rack slot, or a 3400 machine that looks like the 45 one. The
# grades landed in core/site.c (D43) and the game test now walks the shop's
# own list against these, so a fourth grade cannot arrive unpainted.
# WHAT IS IN THE VAN ON DAY ONE. Named here rather than typed into the line
# that sends it, because game/tests/tower.gd used to assert "at least three
# boxes are in goods in" -- a second copy of this list, and it broke the day
# the list got shorter. The test reads this now.
const START_KIT := ["order switch4 core", "order minitower files"]

const DEV_U := {"uplink": 1, "switch4": 1, "switch8": 1, "switch24": 1,
	"router": 1, "pc": 4, "minitower": 4, "server": 2, "rackserver": 2,
	"workstation": 4}
const DEV_COL := {"uplink": Color("#9a7b3a"),
	# the three switch grades read as one family, cheapest palest
	"switch4": Color("#5b7f9c"), "switch8": Color("#3f6f96"),
	"switch24": Color("#2d5b80"), "router": Color("#8a5a3e"),
	"pc": Color("#6a707a"), "minitower": Color("#5d636b"),
	"server": Color("#7c828c"), "rackserver": Color("#969ca6"),
	"workstation": Color("#6a707a")}

func _place_devices() -> void:
	devices.clear()
	_desks.clear()
	for k in racks:
		k.next_u = 34
	var mdf := find_room(0, K_MDF)
	if mdf < 0: mdf = find_room(0, K_COMMS)

	# ---- what the SITE says is installed, in the racks of the room it says.
	# This is a read of site_devs(). There is no list of devices in this file.
	_on_floor = {}
	var on_floor := _on_floor
	# THE PLAYER'S OWN WORKSTATION IS NOT A BOX IN A RACK, and since D41 it is
	# a device in the site like everything else -- so it would be drawn twice:
	# once as a grey 1U brick in the frames, and once as the desk it actually
	# is. It is drawn below, by _workstation(), from the room the SITE says it
	# is standing in. Carry it into a cupboard on floor six and that is where
	# the desk, the monitor and the desktop on it are.
	var ws := {}
	for d in site_devs():
		if str(d.kindname) == "workstation":
			ws = d
			continue
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
			# A TENANT'S DESKS ARE NOT A DELIVERY. Twenty of them arrive in one
			# room the day a tenancy moves in, and the delivery row puts a row
			# of boxes along one wall and piles the rest at the end of it.
			# These are people's computers: they stand where desks stand,
			# spread over the floor of the room the tenancy rents.
			slot = {}
			if str(d.kindname) == "desk":
				slot = _tenant_desk_slot(room, k)
				# AND SOMEBODY IS SITTING AT IT. The seat is this slot's own
				# cell and yaw, so the person, the desk and the computer are
				# one place: `go t7d3` walks you to the desk you can see them
				# at. `k` is their number in the tenancy, which is the only
				# thing _seats() needs to know to say which of them can work.
				if not slot.is_empty():
					_desks.append({"tenant": int(d.tenant), "k": k,
						"pos": slot.centre, "yaw": float(slot.yaw),
						"floor": int(d.floor), "dev": str(d.name)})
			if slot.is_empty():
				slot = _floor_slot(room, k, nu)
		# A managed box has a management line and no picture on the back of it.
		_add_device(d.name, -2, false, true, slot.mn, slot.size,
			DEV_COL.get(d.kindname, Color("#2f343a")), slot.face, d.nports, d.i)
		# AND A TENANT'S DESK IS SOMETHING YOU SIT AT. The box under the desk is
		# the device; what a person walks up to and uses is the monitor above
		# it, so the reach point is the screen and the place to stand is the
		# far side of the chair -- the same two facts the workstation in the MDF
		# states about itself, for the same reason.
		# WHICH ROOM IT STANDS IN, kept because a cable has to know: a lead out
		# of a desk runs along the floor of the room it is in and climbs in the
		# corridor, and the route is drawn cell by cell without any other way of
		# asking which of those cells are still theirs.
		devices[devices.size() - 1]["room_i"] = room
		if str(d.kindname) == "desk" and slot.has("centre"):
			var td: Dictionary = devices[devices.size() - 1]
			td.tenant_desk = true
			var gl: Dictionary = _glass_at()
			td.pos = Vector3(slot.centre) \
				+ _rot_xz(Vector3(gl.get("mid", Vector3(0.12, 1.06, -0.27))), float(slot.yaw))
			td.use_from = Vector3(slot.centre) \
				+ _rot_xz(Vector3(0.12, 0.1, 1.15), float(slot.yaw))
			devices[devices.size() - 1] = td

	# WHERE THE DESK IS is where the site says the machine is. It starts in the
	# MDF; a player who picks it up and puts it down on floor six has moved
	# their desk, and the fallback is only for a run with no session in it.
	var ws_room: int = mdf
	if not ws.is_empty() and int(ws.room) >= 0 and int(ws.room) < rooms.size():
		ws_room = int(ws.room)
	if ws_room >= 0 and not (not ws.is_empty() and int(ws.i) == carrying):
		_workstation(ws_room, ws)
	# AND WHETHER THERE IS A PICTURE ON IT. The site says whether that box has
	# power in it; a monitor on a machine that has not got any shows nothing,
	# and the nothing is the diagnosis.
	var lit: bool = ws.is_empty() or bool(ws.get("powered", true))
	_desk_lit(lit)
	# AND IF YOU WERE SITTING AT IT WHEN THE POWER WENT, YOU ARE NOT NOW.
	# Leaving a live desktop full-screen on a machine the site says is off
	# would be the one lie this project cannot afford: the window would be
	# showing a shell on a box with no power in it.
	if not lit and desk_open():
		stand_up()
	if mdf >= 0:
		# the customer's machine, racked: 4U of it, and the phone's whole
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

	_people()


# ================================================== THE PEOPLE AT THE DESKS
#
# "let's also add in the virtual people to actually be in their office at a
# computer desk similar to the server room, where if you felt like it you could
# go over to their desk and see what issues they're complaining about...
# basically let's make the world feel alive."
#
# One person per desk device the site installed, in the room the tenancy rents,
# at the cell _tenant_desk_slot() put their computer in. There is no list of
# people: `_desks` is filled by the same loop that draws the computers, and
# every one of them disappears the moment the model stops saying that desk is
# there.
#
# WHAT IS WRONG WITH THEM IS `service`'s COLUMNS, NOT A MOOD KEPT HERE.
#   addr    how many of the tenancy's desks have a live port AND an address.
#           Only an addressed desk does any work, so the ones past that count
#           have their hand up: they are the job, and they are visible from
#           the doorway without opening a panel.
#   strikes days in a row the tenancy did not get four fifths of its work
#           done. One or more and the room has had a bad week: the ones who
#           CAN work are bent over their desks, and the ones who cannot go
#           from amber to red -- the same two colours the door beacon uses.
#           A served day resets it in core, so the room comes back up the
#           same day the player fixes it.
#
# core says HOW MANY are addressed, not WHICH, so the view spends them in
# install order: t7d0 first. That is a presentation of a number, and it is the
# same order the room was cabled in, which is the order they really would be.
var _desks: Array = []          # {tenant, k, pos, yaw, dev} -- one per desk device
var minimap: Control = null
var _people_node: Node3D = null
var _screens_node: Node3D = null    # what is on their monitors -- see screens.gd

# AND WHO SITS AT EACH ONE, WHICH IS CORE'S TO SAY AND NOT THIS FILE'S.
# `desks <tenant>` prints a name against every desk -- "t1d4  Ola Jelinek" --
# and prints the same one every time, because it is a hash of the seed and the
# device. people.gd spends that name on what they look like, so the view has
# to read it rather than invent one: an invented name would be a second
# opinion about a person the game already has an opinion about.
#
# ASKED ONCE PER TENANCY, EVER. A name cannot change, so this is a cache with
# no invalidation in it, and a tenancy that has not been asked about yet draws
# off the desk's own name until the answer arrives.
var _who := {}                  # device name -> the person at it
var _who_asked := {}            # tenant -> true

func _desk_names(tenants: Dictionary) -> void:
	# NOT WITH A LEAD IN A CONSOLE. `desks` typed while the crash cart is
	# plugged into a box is a line typed at that box's shell, not a question
	# for the landlord -- the same rule _snapshot() follows, for the same
	# reason. It will be asked the next time the body is on its feet.
	if not site_up or ses_where() != 1:
		return
	for t in tenants.keys():
		if _who_asked.has(t):
			continue
		var got := 0
		# READING A NAME IS NOT SOMETHING HAPPENING. site() marks the clipboard
		# stale after every line, because almost every line can move money or a
		# box; this one cannot, and leaving the flag alone stops a new tenancy
		# costing an extra `status`, `service` and `load` on the next frame.
		var was := _snap_dirty
		var said := site("desks %d" % int(t))
		_snap_dirty = was
		for line in said.split("\n", false):
			var f: PackedStringArray = line.split(" ", false)
			# `    t1d4     Ola Jelinek       f1 office #36   10.0.1.14 ...`
			# INDENTED, which is what tells a desk from the header above it:
			# "tenancy 1, f1 office #36: 20 desks" also begins with a t, and
			# read as a desk it seats somebody called "1, f1" at a desk called
			# `tenancy`. Nothing is ever drawn for that, because no device is
			# called that -- but it is a row of nonsense in the cache and the
			# next thing to read it would not know.
			if not line.begins_with("    ") or f.size() < 3 \
					or not str(f[0]).begins_with("t"):
				continue
			_who[str(f[0])] = "%s %s" % [str(f[1]), str(f[2])]
			got += 1
		# Asked and answered. A tenancy that answered nothing is one that has
		# not moved in yet, and it gets asked again when it has.
		if got > 0:
			_who_asked[t] = true


func _seats() -> Array:
	var P = preload("res://scripts/people.gd")
	var addr := {}
	var bad := {}
	# AND WHAT IS ON THE SCREEN IN FRONT OF THEM, which is the same three
	# columns read one more time: `up` is a desk with a lead in it, `addr` is
	# one that also got an address, and the trade is what its software is.
	# See screens.gd for what is real on that glass and what is a depiction.
	var up := {}
	var trade := {}
	var done := {}
	for row in service_rows():
		addr[int(row.tenant)] = int(row.addr)
		up[int(row.tenant)] = int(row.up)
		trade[int(row.tenant)] = _trade_no(str(row.get("trade", "")))
		done[int(row.tenant)] = _done_fraction(str(row.get("done", "")))
		# THE STRIKE COUNT AND NOT THE STAR. A filed complaint never un-files,
		# so a room read off `complained` would be red for the rest of the run
		# however well it was served afterwards -- and the day a player finally
		# fixes a tenancy is the day the building has to show it.
		bad[int(row.tenant)] = int(row.strikes) > 0
	var mine := {}
	for d in _desks:
		mine[int(d.tenant)] = true
	_desk_names(mine)
	var out: Array = []
	for d in _desks:
		var t := int(d.tenant)
		var works: bool = int(d.k) < int(addr.get(t, 0))
		var sad: bool = bool(bad.get(t, false))
		var mood: int = P.M_WORKING
		if works and sad:
			mood = P.M_SLUMPED
		elif not works:
			mood = P.M_WAITING_BAD if sad else P.M_WAITING
		var dev := str(d.get("dev", ""))
		var S = preload("res://scripts/screens.gd")
		# THE SCREEN'S STATE IS THE SAME SPENT COUNT THE POSTURE IS. core says
		# how many desks have a link and how many of those have an address, not
		# WHICH, so both are spent in install order -- t7d0 first -- and the
		# screen on a desk always agrees with the person sitting at it.
		var st: int = S.S_NOLINK
		if works:
			st = S.S_WORKING
		elif int(d.k) < int(up.get(t, 0)):
			st = S.S_NOADDR
		out.append({"pos": d.pos, "yaw": d.yaw, "mood": mood,
			"floor": int(d.get("floor", 0)), "dev": dev,
			"trade": int(trade.get(t, S.T_OFFICE)),
			"state": st, "done": float(done.get(t, 0.0)),
			"who": str(_who.get(dev, dev))})
	return out


# `service`'s trade column, as screens.gd numbers the trades. The words are
# core's -- "office", "voice", "web host", "studio" -- and anything else lands
# on office, which is the baseline trade rather than a guess.
func _trade_no(s: String) -> int:
	var S = preload("res://scripts/screens.gd")
	match s.strip_edges():
		"voice": return S.T_VOICE
		"web host": return S.T_WEBHOST
		"studio": return S.T_STUDIO
	return S.T_OFFICE


# `done` is printed as "12/20" -- what happened out of what was promised -- and
# this is that fraction and nothing else. A tenancy that has not had a day yet
# prints 0/0 and gets nothing on the screen, which is true: no work has been
# asked of them.
func _done_fraction(s: String) -> float:
	var f: PackedStringArray = s.split("/", false)
	if f.size() < 2 or not str(f[0]).is_valid_int() or not str(f[1]).is_valid_int():
		return 0.0
	var b := int(f[1])
	if b <= 0:
		return 0.0
	return clampf(float(int(f[0])) / float(b), 0.0, 1.0)


func _people() -> void:
	if _people_node == null or not is_instance_valid(_people_node):
		_people_node = preload("res://scripts/people.gd").new()
		_people_node.name = "People"
		add_child(_people_node)
	if _screens_node == null or not is_instance_valid(_screens_node):
		_screens_node = preload("res://scripts/screens.gd").new()
		_screens_node.name = "Screens"
		add_child(_screens_node)
	var seats := _seats()
	_people_node.rebuild(seats)
	# THE DESK YOU ARE SITTING AT DOES NOT GET A PICTURE OF A DESK. There is a
	# real machine behind that one glass for as long as you are in the chair,
	# and a depiction painted over the top of the real thing would be the one
	# lie this whole file exists to avoid.
	_screens_node.rebuild(seats, seat_desk)


# What is drawn, by mood, for a test that cannot see -- there are no nodes to
# count, because a floor of people is four instance buffers.
func people_counts() -> Array:
	if _people_node == null or not is_instance_valid(_people_node):
		return [0, 0, 0, 0]
	return _people_node.counts()


func _people_buffers() -> int:
	if _people_node == null or not is_instance_valid(_people_node):
		return 0
	return int(_people_node.buffers())


# WHAT THE WINDOW COSTS RIGHT NOW. Godot's own counters, averaged over the
# last second by the engine, plus the two numbers this file is responsible
# for: how many people are drawn and in how many buffers. A crowd that is
# cheap in theory and dear in the frame is still dear.
func perf_text() -> String:
	var c := people_counts()
	var n: int = c[0] + c[1] + c[2] + c[3]
	return "fps %d, %.2f ms of process, %.2f ms of physics\n" \
			% [int(Performance.get_monitor(Performance.TIME_FPS)),
				Performance.get_monitor(Performance.TIME_PROCESS) * 1000.0,
				Performance.get_monitor(Performance.TIME_PHYSICS_PROCESS) * 1000.0] \
		+ "%d draw calls, %d primitives, %d objects in the frame\n" \
			% [int(Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME)),
				int(Performance.get_monitor(Performance.RENDER_TOTAL_PRIMITIVES_IN_FRAME)),
				int(Performance.get_monitor(Performance.RENDER_TOTAL_OBJECTS_IN_FRAME))] \
		+ "%d people at %d desks, in %d multimesh buffers: %d working, " \
			% [n, n, _people_buffers(), c[0]] \
		+ "%d waiting, %d waiting badly, %d struck\n" % [c[1], c[2], c[3]] \
		+ _screen_perf() \
		+ "%d devices drawn, %d triangles of building\n" \
			% [devices.size(), triangle_count()] \
		+ "%d run quotes asked and kept, last one %.1f ms\n" \
			% [_quote_n, float(_quote_us) / 1000.0]


# WHAT THE SCREENS COST AND WHAT THEY ARE SHOWING, in the same breath. They are
# two triangles and one instance buffer per floor, and the three numbers are
# `service`'s own columns spent over the desks -- so a socket client can read
# what is on the monitors in a room it cannot see, and a screenshot can be
# checked against it.
func _screen_perf() -> String:
	if _screens_node == null or not is_instance_valid(_screens_node):
		return ""
	var c: Array = _screens_node.counts()
	return "%d screens in %d multimesh buffers: %d no link, %d no address, %d working\n" \
		% [int(_screens_node.total()), int(_screens_node.buffers()),
			int(c[0]), int(c[1]), int(c[2])]


# Where everybody is sitting, in world metres: the chair's square metre, which
# is the one thing about a person that could stand in somebody's way.
func people_seats() -> Array:
	var P = preload("res://scripts/people.gd")
	var out: Array = []
	for d in _desks:
		out.append({"tenant": int(d.tenant),
			"pos": d.pos + _rot_xz(Vector3(P.SEAT_X, 0, 0.41), float(d.yaw))})
	return out


# ------------------------------------------------------------ the workstation
#
# "The workstation doesn't really look like the workstation." It was a 500 mm
# cube on a plank: a box among boxes in a room made of boxes, and nothing about
# it said desk. A workstation is a piece of FURNITURE -- a desk against a wall,
# a monitor on it at the height a monitor is at, a keyboard in front of the
# monitor, a chair pushed in behind, and the tower unit on the floor under it
# with the cables going into the back of it. That silhouette is the thing you
# recognise from the doorway, so that is what this builds.
#
# Where it goes is the doors' decision, exactly as the rack rows are: the wall
# the frames are against is taken, no wall with a door in it is available, and
# whatever is left has to hold the desk, the chair, and somebody walking past.

# A box given in the band's own frame: u runs along the wall, v out from it
# into the room, y up. Saves this from being written out four times, once per
# wall a desk can end up against.
func _ubox(g, fr: Dictionary, u0: float, u1: float, v0: float, v1: float,
		y0: float, y1: float, col: Color, collide := true) -> void:
	var a: Vector3 = fr.org + fr.along * u0 + fr.out * v0 + Vector3(0, y0, 0)
	var b: Vector3 = fr.org + fr.along * u1 + fr.out * v1 + Vector3(0, y1, 0)
	var mn := Vector3(min(a.x, b.x), min(a.y, b.y), min(a.z, b.z))
	var mx := Vector3(max(a.x, b.x), max(a.y, b.y), max(a.z, b.z))
	g.box(mn, mx - mn, col, collide)


func _band_frame(b: Dictionary) -> Dictionary:
	var f: Vector3 = b.face
	var org: Vector3
	if b.along_x:
		org = Vector3(0, 0, b.band.position.y if f.z > 0.0 else b.band.end.y)
	else:
		org = Vector3(b.band.position.x if f.x > 0.0 else b.band.end.x, 0, 0)
	return {"along": Vector3(1, 0, 0) if b.along_x else Vector3(0, 0, 1),
		"out": f, "org": org}


const DESK_W := 1.60
const DESK_D := 0.75
const DESK_H := 0.74

var _ws_node: Node3D = null

func _workstation(room: int, ws := {}) -> void:
	var taken: Array = []
	for i in racks_in(room):
		var f: Vector3 = racks[i].get("face", Vector3(0, 0, 1))
		if f.z > 0: taken.append(0)
		elif f.z < 0: taken.append(1)
		elif f.x > 0: taken.append(2)
		else: taken.append(3)
	# 0.05 off the wall, its own depth, and 1.15 in front of it for the chair
	# and for getting past the back of it.
	var b := _wall_band(room, DESK_D, 0.05, 1.15, DESK_W, taken)
	if b.is_empty():
		b = _wall_band(room, DESK_D, 0.05, 1.15, DESK_W)
	if b.is_empty():
		return
	var fr := _band_frame(b)
	var u: float = clampf((b.lo + b.hi) * 0.5 - DESK_W * 0.5, b.lo, b.hi - DESK_W)
	var mid: float = u + DESK_W * 0.5
	var y: float = rooms[room].floor * fheight
	fr.org.y = y
	var g = preload("res://scripts/vgeo.gd").new()

	# --- the desk: a top, two gable ends, a modesty panel at the back
	_ubox(g, fr, u, u + DESK_W, 0.0, DESK_D, DESK_H - 0.04, DESK_H, Color("#9c8f79"))
	for ux in [u + 0.02, u + DESK_W - 0.08]:
		_ubox(g, fr, ux, ux + 0.06, 0.04, DESK_D - 0.03, 0.0, DESK_H - 0.04, Color("#5b6068"))
	_ubox(g, fr, u + 0.09, u + DESK_W - 0.09, 0.05, 0.09, 0.22, DESK_H - 0.05,
		Color("#6b6f76"), false)

	# --- the monitor: base, stem, shell, and a dark panel proud of the shell
	_ubox(g, fr, mid - 0.13, mid + 0.13, 0.12, 0.30, DESK_H, DESK_H + 0.02, Color("#22262b"))
	_ubox(g, fr, mid - 0.03, mid + 0.03, 0.17, 0.24, DESK_H + 0.02, DESK_H + 0.24,
		Color("#2a2f35"), false)
	_ubox(g, fr, mid - 0.28, mid + 0.28, 0.15, 0.21, DESK_H + 0.24, DESK_H + 0.62,
		Color("#1b1e22"))
	# the glass, 6 mm proud so it is not coplanar with the shell it sits in
	_ubox(g, fr, mid - 0.26, mid + 0.26, 0.210, 0.216, DESK_H + 0.26, DESK_H + 0.60,
		Color("#12333f"), false)
	# and the pale line along the bottom bezel that says a monitor is ON
	_ubox(g, fr, mid + 0.22, mid + 0.24, 0.212, 0.218, DESK_H + 0.245, DESK_H + 0.255,
		Color("#7fe08a"), false)

	# --- keyboard and mouse, in front of it, where hands go
	_ubox(g, fr, mid - 0.22, mid + 0.22, 0.40, 0.56, DESK_H, DESK_H + 0.022,
		Color("#d5d2c8"), false)
	_ubox(g, fr, mid + 0.30, mid + 0.38, 0.44, 0.55, DESK_H, DESK_H + 0.03,
		Color("#c8c5bc"), false)

	# --- the chair, pushed in
	_ubox(g, fr, mid - 0.24, mid + 0.24, 0.86, 1.32, 0.44, 0.50, Color("#3a4048"))
	_ubox(g, fr, mid - 0.23, mid + 0.23, 1.24, 1.30, 0.50, 1.00, Color("#3a4048"), false)
	_ubox(g, fr, mid - 0.04, mid + 0.04, 1.05, 1.13, 0.02, 0.44, Color("#2b2f35"), false)
	for a in [0.0, PI * 0.4, PI * 0.8, PI * 1.2, PI * 1.6]:
		_ubox(g, fr, mid + sin(a) * 0.02 - 0.02, mid + sin(a) * 0.24 + 0.02,
			1.09 + cos(a) * 0.02 - 0.02, 1.09 + cos(a) * 0.24 + 0.02,
			0.02, 0.05, Color("#2b2f35"), false)
	# HELD BY REFERENCE, not by name. The furniture is rebuilt whenever the
	# site's device list changes -- a delivery moved, a tenancy moved in -- and
	# a queue_free()d node is still under its old name for the rest of the
	# frame, so looking it up by name the next time finds nothing and the desk
	# gets built twice.
	_ws_node = g.node("Workstation")
	add_child(_ws_node)

	# --- AND THE DESKTOP IS ON IT, from across the room and before you sit.
	#
	# "the monitor does display the 2D desktop that you see when you look at E.
	# It should show you the desktop as you leave it / come to it."
	#
	# So the desktop is not a thing sitting down BOOTS. It is running, on that
	# machine, on that screen, from the moment the building exists -- one de.gd
	# in a SubViewport painted onto the glass. Sitting down moves that same
	# Control onto the window full size and standing up puts it back, so the
	# session you walk away from is the session you come back to, down to the
	# window you left open.
	_desk_screen(fr, mid, y)

	# --- the machine itself, standing on the floor under the desk. This is the
	# DEVICE: it has a display output and a console, and the leads go into the
	# back of it, which is why it is a real box in a real place rather than a
	# picture painted on the desk.
	# BESIDE THE DESK, NOT UNDER IT, AND THIS WAS MEASURED RATHER THAN
	# REDECORATED. The owner: "the default setup has the player's computer too
	# close to a wall to get to the back of it."
	#
	# He was right and it was worse than he could see. The tower stood under
	# the desk, and aim() ends with the same physics ray a walking body uses --
	# so the desk was in the way of its own computer. A probe standing exactly
	# where the game says you use this machine, looking exactly at its only
	# socket, got NOTHING back: not the port, not even the box. There was no
	# angle from which the one port on the player's own machine could be
	# clicked, which is why the spool appeared not to work on it.
	#
	# So it stands at the end of the desk with clear floor in front of it. Its
	# ports already faced into the room; what they lacked was a line of sight.
	var t0: Vector3 = fr.org + fr.along * (u + DESK_W + 0.10) + fr.out * 0.10
	var t1: Vector3 = fr.org + fr.along * (u + DESK_W + 0.32) + fr.out * 0.56
	var mn := Vector3(min(t0.x, t1.x), y + 0.02, min(t0.z, t1.z))
	var size := Vector3(absf(t1.x - t0.x), 0.45, absf(t1.z - t0.z))
	size.x = max(size.x, 0.20)
	size.z = max(size.z, 0.20)
	# THE DEVICE, AND IT IS THE SITE'S DEVICE. Until D41 this was added with
	# `nports 0, site_i -1`: a picture of a computer, on nobody's network, with
	# no socket on the back of it and nothing the model knew about. It is a box
	# now -- one gigabit port you can plug a lead into, an index the session
	# understands, and a name the tower prompt answers to -- so `cable ws core`
	# in the window and at the prompt are the same act on the same object.
	_add_device(str(ws.get("name", "workstation")), 0, true, true, mn, size,
		Color("#3a3f46"), b.face, int(ws.get("nports", 0)), int(ws.get("i", -1)))
	# YOU WALK UP TO THE SCREEN, not to the box under the desk. What counts as
	# "in reach" is where a person stands to use the thing, so the reach point
	# is the monitor and the seat in front of it.
	var d: Dictionary = devices[devices.size() - 1]
	d.pos = fr.org + fr.along * mid + fr.out * 0.30 + Vector3(0, DESK_H + 0.40, 0)
	d.is_desk = true
	# BEHIND THE CHAIR. The chair is a real body you cannot walk through, so
	# the place a person stands to use this is the far side of it, not the seat.
	d.use_from = fr.org + fr.along * mid + fr.out * 1.55 + Vector3(0, 0.1, 0)
	devices[devices.size() - 1] = d


func _add_device(dname: String, which: int, hdmi: bool, serial: bool,
		mn: Vector3, size: Vector3, col: Color,
		face := Vector3(0, 0, 1), nports := 0, site_i := -1) -> void:
	var g = preload("res://scripts/vgeo.gd").new()
	g.box(mn, size, col)
	# The FRONT of a box is what tells you what it is: a lighter faceplate, and
	# a row of ports you can count. A twenty-four port switch that does not
	# visibly have twenty-four ports is a grey brick with a label.
	# PROUD, AND INSET. The faceplate used to be a slab whose outer face sat on
	# exactly the plane of the box's front and whose four edges sat on exactly
	# the box's four sides: five pairs of coplanar surfaces, and the depth
	# buffer picked a different winner every frame. From across the room the
	# fronts of the servers and the switches flickered in and out of the boxes
	# behind them.
	#
	# So the plate stands 2 mm off the front and is set 4 mm in from the edges:
	# no two surfaces share a plane, and 2 mm at arm's length is a bezel rather
	# than a panel that has come loose. Every device kind is drawn by this one
	# function, so this is all of them.
	const FACE_EPS := 0.002
	const FACE_INSET := 0.004
	var fw := 0.012
	var fp := mn
	var fs := size
	if face.z > 0: fp.z = mn.z + size.z - fw + FACE_EPS
	elif face.z < 0: fp.z = mn.z - FACE_EPS
	elif face.x > 0: fp.x = mn.x + size.x - fw + FACE_EPS
	else: fp.x = mn.x - FACE_EPS
	if absf(face.z) > 0.5: fs.z = fw
	else: fs.x = fw
	# in from the edges, in the two directions that are across the face
	fp.y += FACE_INSET
	fs.y = max(0.004, fs.y - FACE_INSET * 2.0)
	if absf(face.z) > 0.5:
		fp.x += FACE_INSET
		fs.x = max(0.004, fs.x - FACE_INSET * 2.0)
	else:
		fp.z += FACE_INSET
		fs.z = max(0.004, fs.z - FACE_INSET * 2.0)
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
	# THE PORTS, AS HOLES. Each one is a real socket in a real place, laid out
	# by port number, and _port_frames() is the ONLY thing that decides where
	# port n is -- the geometry below, the end of a cable, the link light and
	# the crosshair all read the same list, so they cannot disagree about which
	# hole is `core:6`.
	var frames := _port_frames(mn, size, face, nports, fw)
	for pf in frames:
		# a shroud standing proud of the plate, and the hole sunk into it: that
		# little step is what makes an RJ45 read as a socket and not a sticker
		var out: Vector3 = pf.n * 0.004
		_face_box(g, pf.c + out * 0.5, face, pf.w + 0.004, pf.h + 0.004, 0.005,
			col.lightened(0.06))
		_face_box(g, pf.c - pf.n * 0.006, face, pf.w, pf.h * 0.72, 0.012, Color("#090b0e"))
		# the latch slot in the top of it, which is the shape your eye actually
		# uses to tell an RJ45 from a square hole
		_face_box(g, pf.c - pf.n * 0.004 + Vector3(0, pf.h * 0.34, 0), face,
			pf.w * 0.34, pf.h * 0.30, 0.008, Color("#090b0e"))
	# AND THE CONSOLE SOCKET, on the boxes that have one. Drawn wider and
	# shallower than an RJ45 and in a different colour, because the whole
	# point of it is that your eye can tell the two apart at a glance.
	var sf := {}
	if serial:
		sf = _serial_frame(mn, size, face, fw)
		_face_box(g, sf.c + Vector3(sf.n) * 0.002, face,
			float(sf.w) + 0.005, float(sf.h) + 0.005, 0.005, Color("#4a4038"))
		_face_box(g, sf.c - Vector3(sf.n) * 0.005, face,
			float(sf.w), float(sf.h), 0.010, Color("#12100e"))
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
		"face": face, "mn": mn, "size": size, "nports": nports, "fw": fw,
		"ports": frames, "is_desk": false, "use_from": mn + size * 0.5,
		"serial_at": sf})


# ------------------------------------------------------------------ the ports
#
# "This game is largely about running Ethernet cables." So a port is not paint:
# it is a hole, in a place, with a number, and this is the one function that
# says where. HOW MANY there are is not ours to choose -- core/site.h gives
# every device kind its port count and core/site.c refuses a port that does not
# exist ("desk1 has 1 port, numbered 0 to 0"), so `nports` arrives from the
# model and this only arranges them across the face it has.
#
# Two rows past eight of them, because that is what a 24-port 1U switch does:
# forty millimetres of pitch on one row would need a metre and a half of
# faceplate, and the frame is 600 mm wide.
# THE CONSOLE SOCKET, WHICH IS NOT AN ETHERNET PORT.
#
# "The debugger attaches to the same port as the computer on the network
# uplink. Seems like the debugger should connect to a serial-shaped port and be
# the only thing that port does."
#
# He is describing the thing that makes a service processor a service
# processor. A console is out of band: it is a different socket, a different
# shape, wired to a different chip, and it answers when the machine will not
# boot -- which is the entire reason this game has one. Attaching the handset
# to the RJ45 the tenant's frames go through said the opposite: that the
# console is just another thing on the network, and that a dead machine's
# network port would still talk to you.
#
# So it is a socket of its own, in its own place, drawn as the wide trapezoid
# a DE-9 shell actually is. Same rule as the RJ45s above: this function is the
# ONLY thing that says where it is, so the geometry, the crosshair and the lead
# cannot disagree about it.
const SERIAL_W := 0.030
const SERIAL_H := 0.013

func _serial_frame(mn: Vector3, size: Vector3, face: Vector3, fw: float) -> Dictionary:
	var across: bool = absf(face.z) > 0.5
	var along: float = size.x if across else size.z
	var nrm := Vector3(0, 0, 0)
	var plane := 0.0
	if face.z > 0:
		nrm = Vector3(0, 0, 1); plane = mn.z + size.z + fw
	elif face.z < 0:
		nrm = Vector3(0, 0, -1); plane = mn.z - fw
	elif face.x > 0:
		nrm = Vector3(1, 0, 0); plane = mn.x + size.x + fw
	else:
		nrm = Vector3(-1, 0, 0); plane = mn.x - fw
	# hard against the end of the face the ports do not start from, low down,
	# which is where a console port lives on nearly everything that has one
	var t: float = along - min(0.05, along * 0.10) * 0.5 - SERIAL_W * 0.6
	var c := mn + size * 0.5
	c.y = mn.y + min(size.y * 0.28, 0.055)
	if across:
		c.x = mn.x + t
		c.z = plane
	else:
		c.z = mn.z + t
		c.x = plane
	return {"c": c, "n": nrm, "w": min(SERIAL_W, along * 0.35), "h": SERIAL_H}


func _port_frames(mn: Vector3, size: Vector3, face: Vector3, nports: int,
		fw: float) -> Array:
	var out: Array = []
	if nports <= 0:
		return out
	var across: bool = absf(face.z) > 0.5          # the row runs along x
	var along: float = size.x if across else size.z
	var rows: int = 1 if nports <= 8 else 2
	var cols: int = int(ceil(float(nports) / float(rows)))
	var margin: float = min(0.05, along * 0.10)
	var per: float = (along - margin * 2.0) / float(cols)
	var rowh: float = size.y / float(rows)
	var pw: float = min(per * 0.66, 0.0135)        # an RJ45 is 13.5 mm wide
	var ph: float = min(rowh * 0.66, 0.0155)
	# the plane of the outside of the faceplate, and the direction out of it
	var nrm := Vector3(0, 0, 0)
	var plane := 0.0
	if face.z > 0:
		nrm = Vector3(0, 0, 1); plane = mn.z + size.z + fw
	elif face.z < 0:
		nrm = Vector3(0, 0, -1); plane = mn.z - fw
	elif face.x > 0:
		nrm = Vector3(1, 0, 0); plane = mn.x + size.x + fw
	else:
		nrm = Vector3(-1, 0, 0); plane = mn.x - fw
	for i in range(nports):
		var col: int = i % cols
		var row: int = i / cols
		var t: float = margin + per * (float(col) + 0.5)
		var c := mn + size * 0.5
		c.y = mn.y + size.y - rowh * (float(row) + 0.5)
		if across:
			c.x = mn.x + t
			c.z = plane
		else:
			c.z = mn.z + t
			c.x = plane
		out.append({"i": i, "c": c, "n": nrm, "w": pw, "h": ph})
	return out


# THE SCREEN ON THE DESK, live. A SubViewport with the real de.gd in it,
# painted onto a quad sitting a millimetre proud of the glass. It is the same
# machine object the serial lead talks to, which is the rule everywhere in this
# project: two front ends, one Station, no way for them to disagree.
var desk_vp: SubViewport = null
var desk_screen: MeshInstance3D = null

const DESK_SCREEN_W := 0.52
const DESK_SCREEN_H := 0.325


var _desk_at := {}

func _desk_screen(fr: Dictionary, mid: float, y: float) -> void:
	# WHERE the screen is does not depend on whether this run draws a desktop:
	# --headless gates build the tower with with_desktop off and then ask for
	# the desktop later, and a monitor whose position was never worked out is a
	# desk you cannot sit at.
	var out: Vector3 = fr.out
	_desk_at = {
		"pos": fr.org + fr.along * mid + out * 0.2175
			+ Vector3(0, y + DESK_H + 0.43 - fr.org.y, 0),
		"yaw": atan2(out.x, out.z)}
	if with_desktop:
		_desk_build()


# The screen, and the desktop running on it. Called at build time in an
# ordinary run, and on demand from sit_down() for a run that started without
# one, so there is exactly one desktop either way.
func _desk_build() -> void:
	if desk_de != null or _desk_at.is_empty():
		return
	desk_vp = SubViewport.new()
	desk_vp.name = "DeskScreen"
	# 1280 x 800 is the size the desktop was designed at and the size the 2D
	# game runs at, so the layout on the monitor is the layout you sit down to.
	desk_vp.size = Vector2i(1280, 800)
	desk_vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	desk_vp.transparent_bg = false
	desk_vp.disable_3d = true
	desk_vp.handle_input_locally = true
	add_child(desk_vp)
	desk_de = preload("res://scripts/de.gd").new()
	desk_de.machine = machine
	# _new_ticket() increments before it installs, so this lands on the ticket
	# that is really in the rack rather than quietly swapping the machine.
	desk_de.seed_no = seed_no - 1
	desk_de.set_anchors_preset(Control.PRESET_FULL_RECT)
	desk_vp.add_child(desk_de)

	desk_screen = MeshInstance3D.new()
	desk_screen.name = "MonitorGlass"
	var q := QuadMesh.new()
	q.size = Vector2(DESK_SCREEN_W, DESK_SCREEN_H)
	desk_screen.mesh = q
	var mat := StandardMaterial3D.new()
	mat.albedo_texture = desk_vp.get_texture()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR
	desk_screen.material_override = mat
	desk_screen.position = _desk_at.pos
	# A QuadMesh faces +Z; the screen has to face out of the monitor.
	desk_screen.rotation = Vector3(0, float(_desk_at.yaw), 0)
	add_child(desk_screen)


# THE PICTURE ON THE GLASS, or no picture. The desktop keeps running either
# way -- it is a real machine and the site, not this file, decides whether it
# has power -- and what this does is stop drawing it, which is what a monitor
# on a dead box looks like from the doorway.
func _desk_lit(on: bool) -> void:
	if desk_screen != null:
		desk_screen.visible = on


# A box centred on `c`, `w` across the face, `h` up it and `d` through it.
func _face_box(g, c: Vector3, face: Vector3, w: float, h: float, d: float,
		col: Color) -> void:
	var half: Vector3
	if absf(face.z) > 0.5:
		half = Vector3(w * 0.5, h * 0.5, d * 0.5)
	else:
		half = Vector3(d * 0.5, h * 0.5, w * 0.5)
	g.box(c - half, half * 2.0, col, false)


# Where a lead actually goes IN: THE PORT, not the middle of the box. A lead
# into port 7 comes out of the seventh hole, which is the difference between a
# picture of cabling and a picture of a box with a stripe painted down it.
func _port_point(d: Dictionary, port: int) -> Vector3:
	var fr: Array = d.get("ports", [])
	if fr.is_empty():
		return d.mn + d.size * 0.5
	var p: Dictionary = fr[clampi(port, 0, fr.size() - 1)]
	return p.c + p.n * 0.008


# WHAT THE MODEL SAYS ABOUT THIS PORT, and nothing else. PortState out of
# core/netstack.h: 3 is up, 2 is a run past what the copper carries, 1 is
# nothing plugged in. A port with no link in the site's list has nothing in it,
# which is what PORT_NOCABLE means, so that is what this returns.
func port_state(site_i: int, port: int) -> int:
	if site_i < 0:
		return 1
	for l in site_links():
		if l.state < 0:
			continue
		if (l.a == site_i and l.aport == port) or (l.b == site_i and l.bport == port):
			return int(l.state)
	return 1


var _lights: MeshInstance3D = null

# A LINK LIGHT PER PORT, read out of port_state(). Its own mesh, because it is
# the one thing on a device that changes: plug a cable in and the light comes
# on, and nothing else in the room has to be rebuilt for that to be true.
func _port_lights() -> void:
	if _lights:
		_lights.queue_free()
		_lights = null
	var g = preload("res://scripts/vgeo.gd").new()
	for d in devices:
		var s: int = int(d.get("site", -1))
		for pf in d.get("ports", []):
			var st: int = port_state(s, int(pf.i))
			var col := Color("#1a1f24")           # dark: nothing in it
			if st == 3: col = Color("#7fe08a")    # PORT_UP
			elif st == 2: col = Color("#e06a4a")  # PORT_TOOLONG: laid, and dead
			elif st == 0: col = Color("#4a4a52")  # somebody turned it off
			_face_box(g, pf.c + pf.n * 0.001 + Vector3(0, pf.h * 0.62, 0), d.face,
				pf.w * 0.34, pf.h * 0.20, 0.004, col)
	if g.empty():
		return
	_lights = MeshInstance3D.new()
	_lights.name = "PortLights"
	_lights.mesh = g.mesh()
	add_child(_lights)


# ------------------------------------------------------------- the crosshair
#
# "The center of the screen is missing a dot so it's precisely hit anything...
# It should show you that you're interacting with the computer or a particular
# port or a server."
#
# So: one ray, out of the middle of the view, and whatever it lands on is what
# the keys act on. The ray has to be HONEST -- it must hit the thing the player
# thinks it hit -- so it is tested against the real geometry of the real ports
# and the real boxes, and then against the world's own collider, so that a
# server behind a rack upright is not targetable through the steel.
const REACH := 2.2


# Ray against an axis-aligned box: the distance in, or -1.
static func _ray_box(o: Vector3, dir: Vector3, mn: Vector3, mx: Vector3) -> float:
	var t0 := -1.0e9
	var t1 := 1.0e9
	for a in range(3):
		var d: float = dir[a]
		if absf(d) < 1e-9:
			if o[a] < mn[a] or o[a] > mx[a]:
				return -1.0
			continue
		var ta: float = (mn[a] - o[a]) / d
		var tb: float = (mx[a] - o[a]) / d
		if ta > tb:
			var sw := ta; ta = tb; tb = sw
		t0 = max(t0, ta)
		t1 = min(t1, tb)
		if t0 > t1:
			return -1.0
	if t1 < 0.0:
		return -1.0
	return max(t0, 0.0)


# The box a port presents to the crosshair: the hole, and a couple of
# centimetres of air in front of it, so aiming at a socket is aiming at a
# socket and not at the millimetre of faceplate around it.
func _port_box(d: Dictionary, pf: Dictionary) -> Array:
	var c: Vector3 = pf.c
	var n: Vector3 = pf.n
	var half := Vector3(pf.w * 0.55, pf.h * 0.60, pf.w * 0.55)
	if absf(n.z) > 0.5: half.z = 0.0
	else: half.x = 0.0
	var a: Vector3 = c - half - n * 0.004
	var b: Vector3 = c + half + n * 0.022
	return [Vector3(min(a.x, b.x), min(a.y, b.y), min(a.z, b.z)),
		Vector3(max(a.x, b.x), max(a.y, b.y), max(a.z, b.z))]


# What the crosshair is on. {} for nothing.
func aim() -> Dictionary:
	if player == null or player.cam == null:
		return {}
	var o: Vector3 = player.cam.global_position
	var dir: Vector3 = -player.cam.global_transform.basis.z
	var best := {}
	var bt := REACH
	for i in range(devices.size()):
		var d: Dictionary = devices[i]
		# the ports first: they stand in front of the box they are in, so a
		# player aiming at a hole means the hole
		for pf in d.get("ports", []):
			var bb := _port_box(d, pf)
			var t: float = _ray_box(o, dir, bb[0], bb[1])
			if t >= 0.0 and t < bt:
				bt = t
				best = {"kind": "port", "dev": i, "port": int(pf.i),
					"point": o + dir * t}
		# THE CONSOLE SOCKET, ahead of the box for the same reason the RJ45s
		# are: a player aiming at a hole means the hole.
		var sfa: Dictionary = d.get("serial_at", {})
		if not sfa.is_empty():
			var sc: Vector3 = sfa.c
			var sh := Vector3(0.022, 0.022, 0.022)
			var ts: float = _ray_box(o, dir, sc - sh, sc + sh)
			if ts >= 0.0 and ts < bt:
				bt = ts
				best = {"kind": "console", "dev": i, "port": -1, "point": o + dir * ts}
		# then the box itself
		var mn: Vector3 = d.mn
		var mx: Vector3 = mn + d.size
		if bool(d.get("is_desk", false)) or bool(d.get("tenant_desk", false)):
			# YOU AIM AT THE SCREEN, not at the tower unit under the desk: the
			# monitor is the thing a person looks at and reaches for. That is as
			# true of a tenant's desk as of the workstation, and on theirs the
			# box is under the desk behind a person's legs -- so the only thing
			# in this room you could aim at is the glass anyway.
			var c: Vector3 = d.pos
			var half := Vector3(0.30, 0.22, 0.30) if bool(d.get("is_desk", false)) \
				else Vector3(0.26, 0.20, 0.26)
			mn = c - half
			mx = c + half
		var td: float = _ray_box(o, dir, mn - Vector3(0.01, 0.01, 0.01),
			mx + Vector3(0.01, 0.01, 0.01))
		if td >= 0.0 and td < bt:
			bt = td
			best = {"kind": "device", "dev": i, "port": -1, "point": o + dir * td}
	# the lift call plate, which is a thing on a wall you press
	for l in lifts:
		var f := player_floor()
		if not l.floors.has(f):
			continue
		var c: Vector3 = l.call_plate_pos(f)
		var t2: float = _ray_box(o, dir, c - Vector3(0.16, 0.16, 0.16),
			c + Vector3(0.16, 0.16, 0.16))
		if t2 >= 0.0 and t2 < bt:
			bt = t2
			best = {"kind": "lift", "dev": -1, "port": -1, "point": o + dir * t2}
	# AND THE BUTTONS INSIDE THE CAR, which until now were paint.
	#
	# The owner got in, aimed at a lit button, pressed [E] and nothing
	# happened -- *"So the elevator is functionally not working."* He was
	# right. The panel was drawn by lift.gd as vgeo boxes and nothing here
	# knew it existed, so the crosshair never named a button, [E] never had
	# one to press, and the ONLY way to choose a floor was a number key that
	# no button, sign or prompt in the car mentions. A panel you can see and
	# aim at that answers nothing says the lift is broken.
	#
	# Where the buttons are comes from lift.gd's own geometry -- the same
	# three lines that DREW them -- so a button cannot be somewhere other
	# than where it is drawn, and it follows the car up the shaft.
	var incar: Object = lift_in()
	if incar != null:
		for b in incar.buttons():
			var bp: Vector3 = b["pos"]
			var half := Vector3(0.055, 0.055, 0.055)
			var t3: float = _ray_box(o, dir, bp - half, bp + half)
			if t3 >= 0.0 and t3 < bt:
				bt = t3
				best = {"kind": "liftbtn", "dev": -1, "port": -1,
					"floor": int(b["floor"]), "point": o + dir * t3}
	if best.is_empty():
		return {}
	# AND THE WORLD IS IN THE WAY OR IT IS NOT. Everything above is arithmetic
	# on boxes; this is the same collider a walking body hits, so a device on
	# the far side of a wall or behind a rack upright stops being targetable.
	var space := get_world_3d().direct_space_state
	var q := PhysicsRayQueryParameters3D.create(o, o + dir * (bt + 0.02))
	q.collide_with_areas = false
	var hit := space.intersect_ray(q)
	if not hit.is_empty():
		var hd: float = o.distance_to(hit.position)
		if hd < bt - 0.06:
			return {}
	best["dist"] = bt
	return best


# The name of the thing under the dot, and what the keys would do to it. Every
# name here comes from the site model or from the device the view drew; the
# crosshair has no vocabulary of its own.
func aim_text(a: Dictionary) -> Array:
	if a.is_empty():
		return ["", ""]
	if a.kind == "lift":
		return ["lift call plate", "[E] call"]
	# A BUTTON SAYS WHAT PRESSING IT WOULD DO, INCLUDING NOTHING. An unlit
	# button is a floor that exists and is not open yet, which is a true and
	# useful thing to be told while you are standing in the car looking at it
	# -- and much better than the refusal arriving only after you press.
	if a.kind == "liftbtn":
		var bf := int(a.floor)
		if not in_service(bf):
			return ["lift button, floor %d" % bf, "not in service -- the button is not lit"]
		if bf == player_floor():
			return ["lift button, floor %d" % bf, "you are on floor %d" % bf]
		return ["lift button, floor %d" % bf, "[E] go to floor %d" % bf]
	var d: Dictionary = devices[int(a.dev)]
	var s: int = int(d.get("site", -1))
	# THE CONSOLE SOCKET SAYS WHAT IT IS FOR, AND WHAT IT IS NOT FOR. A player
	# who reads "console" over a socket that is not an RJ45 has been told the
	# thing the shape was drawn to tell them.
	if a.kind == "console":
		var dk: Dictionary = devices[int(a.dev)]
		var skey: String = hand_key("serial")
		var what2 := "%s console port" % dk.name
		if phone != null and int(phone.plugged) == int(a.dev):
			return [what2, "the debugger is in it  [U] out"]
		if skey == "":
			return [what2, "serial only. The debugger lead goes in here  [F]"]
		return [what2, "serial only  %s the debugger in  [F]" % skey]
	if a.kind == "port":
		var p: int = int(a.port)
		var st := port_state(s, p)
		var what := "%s port %d" % [d.name, p]
		# A PANEL THE SITE MODEL HAS NEVER HEARD OF. The patch panels and the
		# customer\'s rack server are drawn by the view and are not devices in
		# core/site.c, so there is nothing to run a cable TO. Saying "[LMB] plug
		# in" over a hole that will refuse is the kind of small lie this project
		# does not tell.
		if s < 0:
			return [what, "no line to it: the site does not own this panel"]
		# A HOLE WITH SOMETHING IN IT IS NOT A HOLE YOU CAN RUN A CABLE FROM,
		# and it says that BEFORE the offer rather than after it. With a spool
		# in hand this used to read "[LMB] plug in" over a port that already
		# had a link up in it, and the click was refused by core in words the
		# crosshair had just contradicted.
		if st == 3:
			return [what, "link up"]
		if st == 2:
			return [what, "too long: no link"]
		# THE KEY THAT RUNS THE CABLE, SAID WHERE THE CABLE WOULD GO IN.
		#
		# The owner, playing his own game: "As is, I can't figure out how to
		# actually attach a cable, run a cable from a particular port to
		# another." The machinery was all here -- [C] at a port has run a
		# cable since the tower had ports -- and the only sentence in the game
		# that mentioned cabling at all pointed at [Tab], which went back to
		# the terminal when the bag moved to [I] and has done nothing here
		# since. One dead key was the whole distance between a player and the
		# central verb of this game.
		var key: String = hand_key("spool") if spool_in_hand() else "[C]"
		if _cable_from >= 0:
			# THE END THAT IS ALREADY IN IT. It has no link yet, so the model
			# still calls this port empty; offering to plug the other end into
			# it is offering something core answers with "that is the end you
			# already put in".
			if s == _cable_from and p == _cable_port:
				return [what, "the end of the run you are holding is in here"]
			return [what, "%s this end in%s" % [key, run_cost_at(s)]]
		return [what, "empty  %s run %s from here  [R] grade"
			% [key, drum_grade()]]
	if bool(d.get("is_desk", false)):
		return [str(d.name), "[E] sit down"]
	# SOMEBODY ELSE'S DESK SAYS WHOSE IT IS. `desks <tenant>` names the person
	# at every desk and the view has already read it for the crowd, so the
	# crosshair says "t1d3 -- Ola Jelinek" rather than a device number: the
	# whole point of the verb is that a complaint is a person's, not a row's.
	if bool(d.get("tenant_desk", false)):
		var who := str(_who.get(str(d.name), ""))
		var nm := str(d.name) if who == "" else "%s -- %s" % [str(d.name), who]
		return [nm, "[E] sit down at their machine"]
	var hint := "[F] serial"
	if bool(d.hdmi):
		hint += "  [H] display"
	if s >= 0:
		hint += "  [G] pick up"
	if not bool(d.serial) and not bool(d.hdmi):
		hint = "passive: copper and a label"
	# AND EVERY BOX WITH A HOLE IN THE BACK OF IT SAYS SO. [C] at a box is the
	# next free port -- which is how anybody patches a switch -- so the key is
	# offered from the whole box as well as from one socket on it. The ISP
	# handoff is "passive: copper and a label" and is still the far end of the
	# first cable anybody runs in this building, so it gets the offer too.
	if s >= 0 and not d.get("ports", []).is_empty():
		hint += "  [C] cable" if _cable_from < 0 else "  [C] this end in"
	# AND THE PLUG, WHEN THE LEAD IS IN A HAND. Which of the two things it
	# would do comes off the model rather than off a guess, so the crosshair
	# never offers to plug in a box that is already in the wall.
	var pkey: String = hand_key("power")
	if s >= 0 and pkey != "":
		var sd2 := _site_dev(s)
		if not sd2.is_empty():
			hint += "  %s %s" % [pkey,
				"pull the plug" if bool(sd2.get("mains", false)) else "into the wall"]
	return [str(d.name), hint]


# LOOK AT SOMETHING. A person turns their head; this is that, and it is here
# rather than in the test because it is also what a session command needs when
# it says `plug core:6` and the view has to put the crosshair on that hole --
# the owner: "if you give a command to cable a particular port, the mouse
# automatically aligns to that port."
func aim_at(p: Vector3) -> void:
	if player == null or player.cam == null:
		return
	var eye: Vector3 = player.global_position + Vector3(0, player.EYE, 0)
	var to := p - eye
	if to.length() < 0.01:
		return
	player.look_at_yaw(atan2(-to.x, -to.z))
	var flat := Vector2(to.x, to.z).length()
	player.pitch = clampf(atan2(to.y, flat), -1.45, 1.45)
	player.cam.rotation.x = player.pitch


# WOULD THE NEXT PRESS RUN COPPER? A port the site owns with nothing in it,
# and both hands free of a box -- which is the same test cable_at() makes
# before it types anything at the session.
func would_cable(t: Dictionary) -> bool:
	if t.is_empty() or str(t.get("kind", "")) != "port" or carrying >= 0:
		return false
	var d: Dictionary = devices[int(t.dev)]
	var s: int = int(d.get("site", -1))
	if s < 0:
		return false
	return port_state(s, int(t.port)) == 1


func spool_in_hand() -> bool:
	if bag == null:
		return false
	return str(bag.hand(0)) == "spool" or str(bag.hand(1)) == "spool"


# WHICH BUTTON, NAMED AFTER THE HAND IT IS REALLY IN. The crosshair used to
# say "[LMB]" whenever a spool was anywhere in the hands, which was true while
# the only way to arm one was to drag it into the left. The spool is in the
# RIGHT hand on day one now, and a hint that names the wrong button is the
# same defect as the dead [Tab] this line was written to replace.
func hand_key(item: String) -> String:
	if bag == null:
		return ""
	if str(bag.hand(0)) == item:
		return "[LMB]"
	if str(bag.hand(1)) == item:
		return "[RMB]"
	return ""


# What the keys act on: what the crosshair is on if it is on anything, and
# otherwise the nearest thing within arm's reach. The HUD and the crosshair
# both name THIS, so what you read is always what the key will do.
func target() -> Dictionary:
	var a := aim()
	if not a.is_empty():
		return a
	var n := nearest_device(player.global_position)
	if n < 0:
		return {}
	return {"kind": "device", "dev": n, "port": -1, "dist": -1.0, "far": true}


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


func _spawn_phone() -> void:
	# IN YOUR POCKET, not on a trolley. It hangs off the camera, down at your
	# side, and comes up to your face when a lead goes into something.
	phone = preload("res://scripts/phone.gd").new()
	phone.tower = self
	phone.with_desktop = with_desktop
	player.cam.add_child(phone)


# ---------------------------------------------------- sitting down at the desk
#
# "There's no way to like hit E to use it and or see the 2D interface that we
# built originally." The 2D desktop IS the original game -- the terminal, the
# package manager, the log viewer, the chat with the customer on the other end
# of it -- and until now the only way into it from the building was to find a
# machine with a display output and hold a lead against it.
#
# So: walk up to the workstation, press E, and you are looking at it, full
# screen, at the size it was designed for. It is the same de.gd on the same
# Station object the serial lead talks to, which is the rule this project keeps
# everywhere: two front ends, one machine, no way for them to disagree.

var desk_layer: CanvasLayer = null
var desk_de: Control = null


func desk_open() -> bool:
	return desk_layer != null


# IS THERE A COMPUTER UNDER THIS DESK, AND IS IT ON? Both are the site's
# facts, and neither is this file's to assume. Since D41 the desktop runs on
# the box in the room: carry that box out and the monitor has nothing behind
# it; pull its plug and the screen is dark, which is exactly what a monitor on
# a dead machine shows and exactly what a serial lead into one gives you.
func _ws_dev() -> Dictionary:
	for d in site_devs():
		if str(d.kindname) == "workstation":
			return d
	return {}


func _ws_live() -> bool:
	var d := _ws_dev()
	if d.is_empty():
		return true              # no session: the bench's own workstation
	return bool(d.get("powered", true)) and int(d.i) != carrying


func sit_down() -> String:
	if desk_layer != null:
		return "you are already sitting at it."
	if not with_desktop:
		return "the desktop is not built in this run."
	var w := _ws_dev()
	if not w.is_empty() and int(w.i) == carrying:
		return "the machine that drives this screen is in your hands."
	if not w.is_empty() and not bool(w.get("powered", true)):
		if not bool(w.get("mains", true)):
			return ("the screen is dark. %s is not plugged into anything -- "
				+ "there is no lead\n  from it to a wall socket, so its button "
				+ "does nothing. `outlets` says\n  which rooms have one free.") \
				% str(w.name)
		return ("the screen is dark: %s is switched off. `power %s on`.") \
			% [str(w.name), str(w.name)]
	_desk_build()
	if desk_de == null:
		return "there is no monitor here to sit at."
	# THE SAME DESKTOP, MOVED. Not a new one: the Control comes out of the
	# monitor\'s viewport and onto the window at full size, and goes back when
	# you stand up. That is what makes the session you walk away from the
	# session you come back to -- same windows, same terminal, same scrollback
	# -- and it is what a monitor across a room showing your desktop MEANS.
	desk_layer = CanvasLayer.new()
	desk_layer.name = "Desktop"
	desk_layer.layer = 10
	add_child(desk_layer)
	desk_vp.remove_child(desk_de)
	desk_layer.add_child(desk_de)
	desk_de.set_anchors_preset(Control.PRESET_FULL_RECT)
	if player:
		player.capture(false)
		player.velocity = Vector3.ZERO
		player.set_physics_process(false)
	if hud:
		hud.visible = false
	if reticle:
		reticle.visible = false
	return "you sit down at the workstation.  [Esc] to stand up."


func stand_up() -> String:
	if desk_layer == null:
		return "you are not sitting at anything."
	desk_layer.remove_child(desk_de)
	desk_vp.add_child(desk_de)
	desk_de.set_anchors_preset(Control.PRESET_FULL_RECT)
	desk_layer.queue_free()
	desk_layer = null
	if player:
		player.set_physics_process(true)
		# BACK TO MOUSELOOK. walker.gd releases the pointer on Escape and it
		# gets the key before this does, so standing up has to take it back or
		# [Esc] leaves you standing in the room with a free cursor.
		player.capture(true)
	if hud:
		hud.visible = true
	if reticle:
		reticle.visible = true
	return "you stand up. The desktop is still up on the screen behind you."


# ------------------------------------------- SITTING DOWN AT SOMEBODY ELSE'S
#
# "I'd like those to act a lot like our main one, but with whatever software
# the end user is using."
#
# The screens in a tenancy's office are a DEPICTION -- see screens.gd, which
# says so at the top of itself and says which numbers drive it. This is the
# other half, and it is the half that is real: `sit <desk>` in core/session.c
# boots that machine, and while you are in the chair the window is a terminal
# on it. Every line typed here is session_line(), the same call the socket
# makes, going to a Machine that really exists for as long as you are sat
# there. There is exactly one of them, because a person has one backside and
# because 176 of them would be 3.2 GB (D31).
#
# So the two things a player sees are cleanly separated and neither pretends
# to be the other: across the room, a picture of what the model knows; in the
# chair, the machine.

var seat_layer: CanvasLayer = null
var seat_term: Control = null
var seat_desk := ""              # the desk device the session says you are at
var _seat_intro := ""            # what core said as the chair came out


# Where the glass is on a tenant's desk, in the desk's own frame. Read off the
# desk mesh by screens.gd rather than copied out of people.gd, so a reshaped
# desk moves the reach point with it.
var _glass_cache := {}

func _glass_at() -> Dictionary:
	if _glass_cache.is_empty():
		_glass_cache = preload("res://scripts/screens.gd").new().glass_rect()
	return _glass_cache


# WHICH DESK, IN CORE'S OWN WORDS. session_prompt() prints `desk:t1d3#` while
# you are in the seat and nothing else prints that, so the view reads the desk
# off the prompt instead of keeping a second opinion about where you are
# sitting -- the same rule the room and the money already follow.
func seat_at() -> String:
	if ses_where() != 4:              # SES_SEAT, core/session.h
		return ""
	var p := ses_prompt()
	if not p.begins_with("desk:"):
		return ""
	return p.substr(5).trim_suffix("# ").trim_suffix("#").strip_edges()


# THE VIEW FOLLOWS THE SESSION. `sit t1d3` typed at the socket and [E] pressed
# at the desk are the same line to core, so the chair is not opened by the key:
# it is opened by noticing that the session says you are in one. That is D23's
# rule -- the 3D is a view of the session, never a second copy of it -- and it
# is why a socket client's screenshot of a sitting is a screenshot of the
# terminal rather than of the room they left.
func _reconcile_seat() -> void:
	var at := seat_at()
	if at == seat_desk:
		return
	seat_desk = at
	if at == "":
		_seat_close()
	else:
		_seat_open()
	# the screen on that desk is real while you are in it, and a picture again
	# the moment you are not
	_people()


func _seat_open() -> void:
	if seat_layer != null or not with_desktop:
		return
	seat_layer = CanvasLayer.new()
	seat_layer.name = "Seat"
	seat_layer.layer = 10
	add_child(seat_layer)
	var bg := ColorRect.new()
	bg.color = Color("#05070a")
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	seat_layer.add_child(bg)
	seat_term = preload("res://scripts/terminal.gd").new()
	seat_term.mono = preload("res://scripts/uifont.gd").mono()
	# ONE MACHINE, TWO FRONT ENDS, AGAIN. on_command is site(), which is
	# ses_cmd() -- the socket's own call -- so what this terminal says and what
	# a socket client typing the same word says cannot differ. The prompt is
	# core's `desk:t1d3#`, not a string built here.
	seat_term.on_command = func(s: String) -> String: return site(s)
	seat_term.prompt_fn = func() -> String: return ses_prompt()
	var intro := PackedStringArray()
	for l in _seat_intro.split("\n", false):
		intro.append(l)
	intro.append("")
	intro.append("[Esc] stands up -- and their machine goes with the chair.")
	intro.append("")
	seat_term.banner = intro
	seat_term.set_anchors_preset(Control.PRESET_FULL_RECT)
	seat_layer.add_child(seat_term)
	seat_term.take_focus()
	if player:
		player.capture(false)
		player.velocity = Vector3.ZERO
		player.set_physics_process(false)
	if hud:
		hud.visible = false
	if reticle:
		reticle.visible = false


func _seat_close() -> void:
	if seat_layer == null:
		return
	seat_layer.queue_free()
	seat_layer = null
	seat_term = null
	if player:
		player.set_physics_process(true)
		player.capture(true)
	if hud:
		hud.visible = true
	if reticle:
		reticle.visible = true


func seat_open() -> bool:
	return seat_layer != null


# [Esc] IS `stand`, AND `stand` IS CORE'S. The key does not close the window
# and then tell the session: it types the word, core frees the machine, and the
# window closes because the session stopped saying you were sitting down.
func seat_stand() -> String:
	var said := site("stand")
	_reconcile_seat()
	return said


# What [E] does where you are standing. A workstation is something you USE; a
# lift landing is something you call. One key, and what it does is whatever is
# in front of you.
func use_here(dev: int) -> String:
	if seat_open():
		return seat_stand()
	if desk_open():
		return stand_up()
	if dev >= 0 and bool(devices[dev].get("is_desk", false)):
		return sit_down()
	# THEIR desk, which is core's `sit` and not a second way in. It refuses
	# from the wrong room, in core's words, exactly as it does over the socket.
	if dev >= 0 and bool(devices[dev].get("tenant_desk", false)):
		var said := site("sit %s" % str(devices[dev].name))
		_seat_intro = said
		_reconcile_seat()
		return said
	var landing := _lift_landing()
	if landing != null:
		return landing.call_to(player_floor())
	return ""


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

#
# LEGIBILITY IS NOT A COLOUR CHOICE, IT IS A BACKGROUND. The HUD was light text
# with a one-pixel shadow drawn straight onto the world, and the world in this
# building is a white-lit corridor, a plasterboard wall and a ceiling tile. A
# playtester's screenshot of the floor 3 comms cupboard had eight lines of it
# over a pale wall and a cable tray and about half of them could be read at all.
# The day's report, on the same screen, was perfectly legible -- because it has
# a dark panel behind it. That is the whole lesson: the text needs a surface of
# its own, not a brighter grey.
#
# AND A BLOCK WITH NO STRUCTURE IS A BLOCK NOBODY READS. In that same shot the
# permanent status line, the live `load` telemetry, a four-line hint about a
# floor that is not open, and THE RUN IS OVER were all the same size, the same
# weight and the same colour. So the block is tiered, and the tier says how
# long the line is true for:
#
#   PLACE   where you are standing. Permanent, largest, on its own.
#   NOW     what is in your hands and what the tower is doing this second --
#           a cable end, a box in your arms, the lift, the port that is
#           dropping. Warm, because it is the line that changes under you.
#   STATE   the things that are simply the case: your hands, who is waiting,
#           how many floors are open.
#   HINT    the sentences core prints to explain a refusal. Smaller, dimmer,
#           indented: they are the longest lines and the least urgent.
#   KEY     what the next keystroke would do. Green, because it is an offer.
#   ALERT   the run ending. Its own bar, its own colour, and big enough that
#           it cannot be mistaken for a line about how many metres you walked.
var hud: Control = null             # the whole top-left block; visibility hangs off it
var reticle: Control = null
var _hud_rows: VBoxContainer = null
var _hud_alert: PanelContainer = null
var _hud_alert_lab: Label = null
var _hud_pool: Array = []           # reused row Labels, so this does not churn a frame
var _hud_sig := ""

# tier -> [font size, colour, left inset]
const HUD_TIER := {
	"place": [19, "#ffffff", 0.0],
	"now":   [16, "#ffc46b", 0.0],
	"state": [15, "#ccd9e6", 0.0],
	"hint":  [14, "#93a6b8", 10.0],
	"key":   [15, "#8fe0a8", 0.0],
}


func _hud_style(bg: Color, border: Color, left := 3) -> StyleBoxFlat:
	var sb := StyleBoxFlat.new()
	sb.bg_color = bg
	sb.corner_radius_top_left = 4
	sb.corner_radius_top_right = 4
	sb.corner_radius_bottom_left = 4
	sb.corner_radius_bottom_right = 4
	sb.border_width_left = left
	sb.border_color = border
	sb.content_margin_left = 12.0
	sb.content_margin_right = 16.0
	sb.content_margin_top = 8.0
	sb.content_margin_bottom = 9.0
	return sb


# One row of the block. The shadow stays even though there is a panel behind
# it: the panel is 78% opaque so a bright ceiling still comes through, and a
# dark outline under the glyphs is what carries it the rest of the way.
func _hud_row(tier: String) -> Label:
	var spec: Array = HUD_TIER.get(tier, HUD_TIER["state"])
	var l := Label.new()
	l.add_theme_font_override("font", preload("res://scripts/uifont.gd").mono())
	l.add_theme_font_size_override("font_size", int(spec[0]))
	l.add_theme_color_override("font_color", Color(str(spec[1])))
	l.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.9))
	l.add_theme_constant_override("shadow_offset_x", 1)
	l.add_theme_constant_override("shadow_offset_y", 1)
	return l


# WHICH TIER A LINE IS IN. hud_lines() is unchanged -- it is what `hud` over
# the socket reads back and what game/tests/tower.gd greps -- so the tiering is
# done by reading it, not by making every caller build a structure.
func _hud_tier(line: String, first: bool) -> String:
	if first:
		return "place"
	var t := line.strip_edges()
	if t == "":
		return "state"
	if t.begins_with("THE RUN IS OVER"):
		return "alert"
	# WHAT THE RUN HAS COST SO FAR IS NOT A FOOTNOTE. It is indented because it
	# belongs to the line above it, but it changes with every room you walk
	# through and it is the number the next keypress spends -- which is the
	# definition of the NOW tier, not of the paragraph tier.
	if t.begins_with("from here:") or t.begins_with("drum:"):
		return "now"
	# The continuation lines of core's refusals arrive indented; so does the
	# rest of the `open` paragraph, which is the longest thing on the screen.
	if line.begins_with("  ") or t.begins_with("floor ") and t.find("not in service") > 0:
		return "hint"
	if t.begins_with("load:") or t.begins_with("phone:") \
			or t.begins_with("cable in hand") or t.begins_with("carrying kit") \
			or t.begins_with("in the lift:"):
		return "now"
	if t.begins_with("[") or t.find("   [O] ") > 0:
		return "key"
	return "state"

# What is in your hands, and the two slots the mouse buttons use.
func _bag() -> void:
	var layer := CanvasLayer.new()
	layer.name = "Bag"
	layer.layer = 5
	bag = preload("res://scripts/inventory.gd").new()
	bag.tower = self
	layer.add_child(bag)
	add_child(layer)


func _hud() -> void:
	var layer := CanvasLayer.new()
	layer.layer = 4
	reticle = preload("res://scripts/reticle.gd").new()
	layer.add_child(reticle)
	# The block: a dark panel of rows, and under it the alert bar, which is
	# hidden until there is something to shout about.
	var block := VBoxContainer.new()
	block.name = "Hud"
	block.position = Vector2(12, 10)
	block.add_theme_constant_override("separation", 6)
	block.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var body := PanelContainer.new()
	body.add_theme_stylebox_override("panel",
		_hud_style(Color(0.045, 0.065, 0.095, 0.78), Color("#4f7fa8")))
	_hud_rows = VBoxContainer.new()
	_hud_rows.add_theme_constant_override("separation", 2)
	body.add_child(_hud_rows)
	block.add_child(body)
	# THE RUN ENDING IS NOT A LINE OF BODY TEXT. Its own bar, its own red, and
	# large enough to be the thing you see first.
	_hud_alert = PanelContainer.new()
	_hud_alert.add_theme_stylebox_override("panel",
		_hud_style(Color(0.28, 0.05, 0.03, 0.86), Color("#ff6b4a"), 5))
	_hud_alert_lab = Label.new()
	_hud_alert_lab.add_theme_font_override("font", preload("res://scripts/uifont.gd").mono())
	_hud_alert_lab.add_theme_font_size_override("font_size", 23)
	_hud_alert_lab.add_theme_color_override("font_color", Color("#ff9c7a"))
	_hud_alert_lab.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.9))
	_hud_alert_lab.add_theme_constant_override("shadow_offset_x", 1)
	_hud_alert_lab.add_theme_constant_override("shadow_offset_y", 1)
	_hud_alert.add_child(_hud_alert_lab)
	_hud_alert.visible = false
	block.add_child(_hud_alert)
	hud = block
	layer.add_child(hud)
	# THE PLAN OF THIS FLOOR, under the block, bottom left. It is a Control of
	# its own because it is drawn rather than written, and it is fed from
	# map_rows() every frame -- the same reading a socket client gets from
	# `map`, so the picture and the words cannot disagree about the building.
	minimap = preload("res://scripts/minimap.gd").new()
	minimap.tower = self
	minimap.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)
	minimap.position = Vector2(12, -162)
	layer.add_child(minimap)
	# THE DATE AND THE MONEY, always up and never shouting. Top right, out of
	# the way of the crosshair and of the location line, in `status`'s own
	# sentences: "day 49. 58460 in hand, 1540 spent, 0 taken in rent."
	# ... on a panel of its own, for the same reason: the top right of the view
	# is the ceiling, and a ceiling tile is the brightest surface in the game.
	#
	# AND IN THE SAME TIERS AS THE LEFT-HAND BLOCK, because it was two flat
	# lines of the same 15 px grey: the balance, the burn, the circuit and the
	# billing date all weighed the same, and the balance is the number that
	# ends the run. It gets PLACE; the countdown to the bill gets NOW, because
	# it is the thing that moves under you; and when the bill is close or the
	# balance will not cover it, the block gets its own red bar -- the same one
	# the left side uses for the run ending, which is where this ends up.
	var money := VBoxContainer.new()
	money.name = "Ledger"
	money.add_theme_constant_override("separation", 6)
	money.anchor_left = 1.0
	money.anchor_right = 1.0
	money.offset_right = -12.0
	money.offset_top = 10.0
	money.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_ledger_box = money
	var mbody := PanelContainer.new()
	mbody.add_theme_stylebox_override("panel",
		_hud_style(Color(0.045, 0.065, 0.095, 0.78), Color("#4f7fa8")))
	mbody.size_flags_horizontal = Control.SIZE_SHRINK_END
	_ledger_rows = VBoxContainer.new()
	_ledger_rows.add_theme_constant_override("separation", 2)
	mbody.add_child(_ledger_rows)
	money.add_child(mbody)
	_ledger_alert = PanelContainer.new()
	_ledger_alert.add_theme_stylebox_override("panel",
		_hud_style(Color(0.28, 0.05, 0.03, 0.86), Color("#ff6b4a"), 5))
	_ledger_alert.size_flags_horizontal = Control.SIZE_SHRINK_END
	_ledger_alert_lab = Label.new()
	_ledger_alert_lab.add_theme_font_override("font",
		preload("res://scripts/uifont.gd").mono())
	_ledger_alert_lab.add_theme_font_size_override("font_size", 20)
	_ledger_alert_lab.add_theme_color_override("font_color", Color("#ff9c7a"))
	_ledger_alert_lab.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.9))
	_ledger_alert_lab.add_theme_constant_override("shadow_offset_x", 1)
	_ledger_alert_lab.add_theme_constant_override("shadow_offset_y", 1)
	_ledger_alert_lab.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	_ledger_alert.add_child(_ledger_alert_lab)
	_ledger_alert.visible = false
	money.add_child(_ledger_alert)
	layer.add_child(money)
	add_child(layer)


# PAINT THE BLOCK. Called every frame, but it rebuilds only when the text
# actually changed -- a HUD that reallocates twenty Labels sixty times a second
# is a HUD that costs more than the building behind it.
func _hud_paint() -> void:
	if hud == null or _hud_rows == null:
		return
	var text := where_am_i() + "\n" + hud_lines()
	if text == _hud_sig:
		return
	_hud_sig = text
	var lines := text.split("\n")
	var n := 0
	var alert := ""
	for i in range(lines.size()):
		var line: String = lines[i]
		var tier := _hud_tier(line, i == 0)
		if tier == "alert":
			alert = line.strip_edges()
			continue
		if line.strip_edges() == "":
			continue
		# A row is made once and re-tiered in place. The pool is indexed by
		# position in the block, so a line that changes tier changes font.
		var l: Label
		if n < _hud_pool.size():
			l = _hud_pool[n]
		else:
			l = _hud_row(tier)
			_hud_pool.append(l)
			_hud_rows.add_child(l)
		var spec: Array = HUD_TIER.get(tier, HUD_TIER["state"])
		l.add_theme_font_size_override("font_size", int(spec[0]))
		l.add_theme_color_override("font_color", Color(str(spec[1])))
		# Everything is flush left so the block has one edge rather than four;
		# the hints get one indent back, because they are a paragraph under the
		# line that caused them rather than a fact of their own.
		l.text = ("  " if tier == "hint" else "") + line.strip_edges()
		l.visible = true
		n += 1
	for i in range(n, _hud_pool.size()):
		_hud_pool[i].visible = false
	if alert == "":
		_hud_alert.visible = false
	else:
		_hud_alert_lab.text = alert
		_hud_alert.visible = true
	# Containers do not shrink a Control that is not in one, and Control.size is
	# clamped up to the minimum, so zero means "exactly what fits".
	hud.size = Vector2.ZERO


# Everything under the location line: what is in your hands, what the tower is
# waiting for, what is struggling, and what the next key would need. It is a
# function rather than a block inside _process so that `hud` over the socket
# reads exactly what the window is showing.
const HUD_TENANCIES := 3        # how many of them the block will ever name

func hud_lines() -> String:
	var s := ""
	if phone and str(phone.status) != "unplugged":
		s += "phone: " + str(phone.status) + "\n"
	# THE DRUM, AND WHAT IT HAS PAID OUT SO FAR.
	#
	# A run is the one thing in this game you commit to in two moves with a
	# walk in between, and until now the walk was the part with no numbers on
	# it: the metres and the money arrived together, at the far end, after the
	# decision. Now the drum says what is left on it and where the grade is
	# chosen, and once an end is in a socket the line underneath is `quote`'s
	# own answer for the room you are standing in -- the metres this run has
	# reached, the price, or the reason the copper cannot come with you.
	var dm := drum()
	if _cable_from >= 0:
		s += "cable in hand from %s port %d   walk to the other end\n" \
			% [_cable_name(_cable_from), _cable_port]
		var here_room := int(ses_state().get("room", -1))
		if here_room >= 0:
			var q := run_quote("#%d" % here_room)
			var w := _cost_words(q).strip_edges()
			if w.begins_with("--"):
				w = w.substr(2).strip_edges()
			if w != "":
				s += "  from here: %s\n" % w
		if not dm.is_empty():
			s += "  %d m left on the drum\n" % int(dm.left)
	elif not dm.is_empty():
		s += "drum: %d m of %s in your hands   [R] another grade\n" \
			% [int(dm.left), str(dm.grade)]
	if carrying >= 0:
		s += "carrying kit in both hands   [G] put it down here\n"
	var car: Object = lift_in()
	if car != null:
		s += "in the lift: press a floor number.  in service: %s\n" % str(car.serviced())
	if bag:
		s += "hands: %s / %s   [I] inventory\n" % [_hand_name(0), _hand_name(1)]
	# WHO IS WAITING. A tenancy with the keys and no ports is the job, and it
	# is `service`'s own columns rather than a sentence invented here.
	#
	# AND IT CANNOT GROW WITHOUT BOUND. This was one line per tenancy that was
	# short of ports, which is fine at one tenancy and is nine rows plus the
	# `open` paragraph at seven -- a HUD that fills the left half of the window
	# and hides the building behind it. A full tower has more than seven.
	#
	# So the list is the WORST THREE and a count of the rest. Worst is the
	# strike count first, because that is the clock that ends the run, then how
	# many of their desks are still dark: both are `service`'s columns and
	# neither is a number this file invents. `service` is named in the line
	# that stands in for the others, because the whole list is one verb away.
	#
	# A TENANCY THAT IS CABLED AND STILL STRIKING IS ON IT NOW. The old test
	# was `up < desks`, so a tenancy whose every desk had a port and whose
	# work was not finishing -- the thing `load` exists for -- never appeared
	# here at all. The people at those desks are bent over them in the world;
	# the HUD said nothing.
	var trouble: Array = []
	for row in service_rows():
		if int(row.up) < int(row.desks) or int(row.strikes) > 0:
			trouble.append(row)
	trouble.sort_custom(func(a, b):
		if int(a.strikes) != int(b.strikes):
			return int(a.strikes) > int(b.strikes)
		var da: int = int(a.desks) - int(a.up)
		var db: int = int(b.desks) - int(b.up)
		if da != db:
			return da > db
		return int(a.tenant) < int(b.tenant))
	for i in range(min(trouble.size(), HUD_TENANCIES)):
		var row: Dictionary = trouble[i]
		var tail := ""
		if int(row.strikes) > 0:
			tail = "   %d strike%s" % [int(row.strikes),
				"" if int(row.strikes) == 1 else "s"]
		if bool(row.complained):
			tail += "   *complaint filed"
		s += "tenant %d on floor %d: %d desks, %d up, %d addr%s\n" \
			% [int(row.tenant), int(row.floor), int(row.desks), int(row.up),
				int(row.addr), tail]
	if trouble.size() > HUD_TENANCIES:
		var rest: int = trouble.size() - HUD_TENANCIES
		s += "and %d more %s of the %d in: `service`\n" \
			% [rest, "tenancy" if rest == 1 else "tenancies", service_rows().size()]
	# WHAT IS STRUGGLING, pointed at with the tool that can be pointed. `load`
	# names the port and prints the drops beside it; `show <box>` is where the
	# reason is, and neither is netstat, because a switch has no shell in it.
	if load_drops() > 0:
		var worst := load_worst()
		s += "load: %s   `show %s`\n" % [worst, worst.split(":")[0]]
	# A KEY THAT REFUSES IS WORSE THAN A KEY THAT IS NOT OFFERED. Signing a
	# floor off means standing on it and paying the landlord's fit-out, and
	# both of those sentences are core's, printed by `open` when it refuses.
	s += "%d of %d floors in service" % [floors_in_service, nfloors]
	if floors_in_service < nfloors:
		if ses_floor() == floors_in_service:
			s += "   [O] sign floor %d off and put it into service" % floors_in_service
		else:
			# ONE LINE, NOT CORE'S WHOLE REFUSAL. This printed `open`'s answer
			# verbatim, and the owner read it back to us as what it is:
			#
			#   "It says floor two is not in service and you are on floor zero.
			#    Somebody has to be standing on it to sign off. And the lift
			#    button is not lit. So is the stairs. Go hash 55, F2 stairwell
			#    55, then open. It will cost you 12,096... It doesn't make any
			#    sense at all."
			#
			# Six clauses, two of them room numbers, one of them a price, and
			# the whole paragraph on screen at all times whether or not the
			# player was thinking about opening a floor. It is a fine REPLY --
			# that is what `open` is for, and typing `open` still prints every
			# word of it -- and it is a terrible permanent caption. The HUD
			# says the one thing you would act on and stops.
			s += "\n" + "floor %d is next: the stairs go up to it. Stand on it and [O]" \
				% floors_in_service
	# WHO IS ACTUALLY IN THE BUILDING, which is the fact that was missing.
	#
	# The owner walked the tower and reported "I don't see anybody else". He
	# was right and nothing was broken: people.gd draws a person at every desk
	# a tenancy has, and on DAY ZERO no tenancy has moved in yet -- measured,
	# 0 people on day 0, 20 on day 1, 38 by day 6. The building really is
	# empty, the only thing that fills it is [N], and the HUD never said so.
	# An empty world that gives no reason for being empty reads as a broken
	# one.
	var ppl: Array = people_counts()
	var nppl: int = ppl[0] + ppl[1] + ppl[2] + ppl[3]
	var ntn: int = service_rows().size()
	s += "\nday %d: %d tenanc%s in, %d at their desks" \
		% [int(ses_state().get("day", 0)), ntn, "y" if ntn == 1 else "ies", nppl]
	if ntn == 0:
		s += "   nobody has moved in yet"
	if not run_over:
		s += "\n[N] the next day"
	else:
		s += "\nTHE RUN IS OVER"
	return s


# ------------------------------------------------------------- the mini-map
#
# "It would be better if that window just said the floor you were on and the
# room you were on like it does now, but also included a mini-map instead of
# that confusing blurb." And, when he asked for buyable sockets: "have a way
# to view the mini map for the entire area and request or order additional
# power for a fee."
#
# WHAT IT IS A VIEW OF, and the answer is the same as everything else here:
# the model. Rooms come out of the generator's own room table in metres, where
# you are comes out of the walking body, and how many sockets a room has left
# comes out of site_room_outlets_free() through the extension -- the identical
# number `outlets` prints and the faceplates on the wall are drawn from. There
# is no map data anywhere. This is a reading.
#
# AND IT IS READABLE WITHOUT A WINDOW. map_rows() is the whole picture as
# data, so a socket client can be told what the player can see; the gate in
# game/tests/tower.gd walks it against the room table rather than against a
# screenshot. A map that could only be checked by looking at it is a map that
# would rot the first time a room moved.
func map_rows() -> Array:
	var out: Array = []
	if player == null:
		return out
	var f := player_floor()
	var here := player_room()
	var free := site_outlets()
	for i in range(rooms.size()):
		var r = rooms[i]
		if int(r.floor) != f:
			continue
		out.append({
			"i": i, "name": str(r.name), "kind": int(r.kind),
			"x0": float(r.x0), "y0": float(r.y0),
			"x1": float(r.x1), "y1": float(r.y1),
			"here": i == here,
			"outlets": int(free.get(i, {}).get("built", 0)),
			"free": int(free.get(i, {}).get("free", 0)),
		})
	return out


# The same thing in words, for a client with no window. One line per room on
# this floor, the one you are standing in marked, and the sockets counted.
func map_text() -> String:
	if player == null:
		return ""
	var p: Vector3 = player.global_position
	var s := "floor %d, %d rooms, you at (%.0f, %.0f m)\n" \
		% [player_floor(), map_rows().size(), p.x, p.z]
	for m in map_rows():
		s += "%s %-18s %2.0f,%2.0f to %2.0f,%2.0f  %d socket%s, %d free\n" \
			% ["*" if bool(m.here) else " ", str(m.name),
				float(m.x0), float(m.y0), float(m.x1), float(m.y1),
				int(m.outlets), "" if int(m.outlets) == 1 else "s", int(m.free)]
	return s


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

# WHAT THE SESSION SAYS IS IN YOUR HANDS. Read back out of ses_state() rather
# than kept here, because a socket client can pick a box up too.
var carrying: int:
	get:
		return int(ses_state().get("carrying", -1))
var _here_room := -1
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
	_be_here(room)
	# `carry <box>` -- the session's own verb, so the refusals are the ones a
	# socket client reads: a box with a cable in it does not move, the ISP's
	# handoff is screwed to somebody's wall, and both hands are on whatever you
	# already have. None of those rules is written in this file.
	var out: String = site("carry %s" % _cable_name(s)).strip_edges()
	if int(ses_state().get("carrying", -1)) != s:
		return out
	var n: Node = devices[dev].node
	if n: n.queue_free()
	devices.remove_at(dev)
	_sync_cable()
	return out


# WHERE THE LEGS WENT. The session's `go` verb walks you somewhere and charges
# the metres; a person at the keyboard walks with W, and those metres are just
# as real, so the session is told and the walk is added to its count. This is
# the one thing the view knows first.
func _be_here(room: int) -> void:
	if not site_up or room == NOROOM or room == _here_room:
		return
	_here_room = room
	machine.ses_here(room)
	_st_frame = -1


func drop_here() -> String:
	if carrying < 0:
		return "you are not carrying anything."
	var room := player_room()
	if room == NOROOM:
		return "you are not standing in a room: nowhere to put it down."
	_be_here(room)
	var s := carrying
	var out: String = site("drop").strip_edges()
	if carrying >= 0:
		return out                     # it refused: the box is still in your arms
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
	# BOTH HANDS ARE ON THE BOX. core/session.c has refused this since the
	# socket session existed -- "you are carrying %s. A drum of cable takes
	# both hands too" -- and the 3D shell let you do it anyway, because until
	# the inventory there were no hands in here to be full. There is one rule
	# and this is it, not a second one.
	if carrying >= 0:
		var what := "the box"
		for d in site_devs():
			if int(d.i) == carrying:
				what = str(d.name)
		return "you are carrying %s. A drum of cable takes both hands too: put it down first  [G]." % what
	return cable_at(dev, -1)


# THE SAME MECHANIC, AT A PARTICULAR HOLE. `port` of -1 is "the next free one",
# which is what pressing [C] at a box has always meant; a port number is what
# the crosshair gives you when you click on an actual socket. Either way the
# run itself is core's: site_cable() measures it through the building's own
# cable graph and prices it by the metre, and this only says which two ports
# the player meant.
# THE POWER LEAD, WHICH IS `mains` AND NOTHING ELSE.
#
# "In the inventory we should have a power cable spool, that lets you run
# power cables from wall outlets to power input."
#
# There is no route to work out and no metres to charge: core does not price a
# kettle lead, it asks whether the room has a socket left. So this is the
# session's own `mains <box> on|off`, aimed instead of typed -- the same
# refusals in the same words, including the two that matter most: the room
# whose wall is full, and pulling the plug on something that is running, which
# is a blackout with one machine in it and a one-in-twenty chance of a
# filesystem to repair afterwards.
func mains_at(dev: int) -> String:
	if dev < 0 or not site_up:
		return ""
	var s: int = int(devices[dev].get("site", -1))
	if s < 0:
		return "%s is the view's own scenery: there is no plug on the back of it." \
			% devices[dev].name
	var sd := _site_dev(s)
	if sd.is_empty():
		return ""
	var room := player_room()
	if room != NOROOM:
		_be_here(room)
	var on: bool = not bool(sd.get("mains", false))
	return site("mains %s %s" % [str(sd.name), "on" if on else "off"]).strip_edges()


func cable_at(dev: int, port: int) -> String:
	if dev < 0 or not site_up:
		return ""
	if carrying >= 0:
		var what := "the box"
		for d in site_devs():
			if int(d.i) == carrying:
				what = str(d.name)
		return "you are carrying %s. A drum of cable takes both hands too: put it down first  [G]." % what
	var s: int = int(devices[dev].get("site", -1))
	if s < 0:
		return "%s is not a device the site model owns: there is no line to run a cable to." \
			% devices[dev].name
	var p: int = port if port >= 0 else _free_port(s)
	var room := player_room()
	if room != NOROOM:
		_be_here(room)
	var st := ses_state()
	# A DRUM OFF THE SHELF, then an end in a socket, then the walk, then the
	# other end. Four things a person does, and four verbs core/session.c
	# already has: it is the same sequence a blind playtester types, and it is
	# priced by the metre off the building's own cable graph on the second
	# `plug`. Nothing here works out a length or a cost.
	var said := ""
	if int(st.get("spool", [-1])[0]) < 0:
		said = site("spool cat6").strip_edges() + "\n"
	var out: String = site("plug %s:%d" % [_cable_name(s), p]).strip_edges()
	_sync_cable()
	_draw_cables()
	return (said + out).strip_edges()


# What the session says is in your hands, mirrored into the view so the trail
# of cable is drawn from the port the session says an end is in.
func _sync_cable() -> void:
	var st := ses_state()
	var cab: Array = st.get("cab", [-1, -1])
	if typeof(cab) != TYPE_ARRAY:
		cab = [-1, -1]
	var was := _cable_from
	_cable_from = int(cab[0])
	_cable_port = int(cab[1]) if cab.size() > 1 else 0
	if _cable_from != was:
		# A NEW RUN STARTS WITH NOTHING ON THE FLOOR, and a finished one leaves
		# nothing on it: what is on the floor from here on is the link the
		# model holds, drawn where the model billed it.
		_drop_run()
	if _cable_from < 0 and was >= 0:
		_drop_trail()


# ============================ WHAT THE WALK COSTS, WHILE YOU ARE STILL WALKING
#
# D32 built `quote` because "there is no way to measure a run before paying for
# it", and it fixed that for somebody at a keyboard typing `quote a b`. A
# person WALKING a run is in exactly the position that record describes: the
# drum is paying out behind them and the first number they see is the invoice.
#
# So the crosshair and the HUD carry the quote, and NOTHING HERE COMPUTES IT.
# `quote` is the billing function -- site_run_metres(), site_cable_price() --
# printed, and this reads its lines. There is no metre, no price and no grade
# name in this file: the reason D32 gives is the reason here too, that a second
# copy of the arithmetic is a second thing to be wrong, and a view that quoted
# a price the invoice then disagreed with would be worse than no price at all.
#
# IT IS CACHED BECAUSE IT IS EXPENSIVE. `quote` lays a cable in a Net of its
# own to read the port speed off it (D32), which is about 70 MB allocated and
# freed per call -- fine for a line a person types, not fine sixty times a
# second under a crosshair. The metres between two rooms are a property of the
# building and the building does not change, so a quote is asked once per pair
# per grade and kept. `perf` prints how many and what they cost.
var _quotes := {}                   # "from:port>to:grade" -> {m, price, why}
var _quote_n := 0
var _quote_us := 0


# WHAT IS ON THE DRUM, in core's words. `spool` on its own prints "305 m of
# cat6 on the spool." and that sentence is the only place this file learns
# either number: there is no table of grades in the view and no count of
# metres. Empty when your hands are empty, which is what `spool` says too.
func drum() -> Dictionary:
	var f: PackedStringArray = str(_snap.get("spool", "")).split(" ", false)
	if f.size() >= 4 and str(f[1]) == "m" and str(f[2]) == "of":
		return {"left": int(f[0]), "grade": str(f[3])}
	return {}


# The grade [C] would run: whatever drum is in your hands, and cat6 off the
# shelf if there is none -- which is the word cable_at() types.
func drum_grade() -> String:
	return str(drum().get("grade", "cat6"))


# EVERY GRADE THE GAME SELLS, ASKED OF THE GAME. `spool` refuses a word it
# does not know by naming the ones it does -- "no such cable: ?. cat5, cat5e,
# cat6 or fibre." -- so the key that cycles the drum cannot offer a drum that
# is not on the shelf, and a grade added to core/site.c turns up on it without
# anybody editing this file. A list written out here would be the fourth copy
# of the catalogue in a project that has shipped that bug three times in a day
# (D32). The refusal buys nothing and changes nothing.
var _grades: Array = []

func grades() -> Array:
	if not _grades.is_empty():
		return _grades
	var said := str(site("spool ?"))
	if not said.begins_with("no such cable"):
		return []                  # your hands are full: core said something else
	var i := said.find(". ")
	if i < 0:
		return []
	var list := said.substr(i + 2).strip_edges().replace(" or ", ", ")
	for w in list.trim_suffix(".").split(",", false):
		var g := str(w).strip_edges()
		if g != "":
			_grades.append(g)
	return _grades


# [R]: THE OTHER DRUM. Cat5 and fibre have been in the catalogue since D27 and
# a player at the keyboard could buy neither: cable_at() types `spool cat6` and
# there was no key that said anything else, so the grade -- which is most of
# what `quote` exists to help you decide -- was a decision only a socket client
# could make. This is `spool <grade>`, so the refusals are core's: it will not
# swap a drum with an end of it already in a socket, and says why.
func drum_next() -> String:
	var g := grades()
	if g.is_empty():
		return str(site("spool cat6")).strip_edges()
	var want: String = str(g[0])
	if not drum().is_empty():
		var i: int = g.find(drum_grade())
		want = str(g[(i + 1) % g.size()])
	elif g.has(drum_grade()):
		want = drum_grade()        # the first press takes the drum [C] would
	return str(site("spool %s" % want)).strip_edges()


# One quote, from the end that is already in a socket to somewhere else. `to`
# is spelled the way `quote` spells an end: a box name, or `#41` for a room.
func run_quote(to: String) -> Dictionary:
	if _cable_from < 0 or not site_up:
		return {}
	var grade := drum_grade()
	var key := "%s:%d>%s:%s" % [_cable_name(_cable_from), _cable_port, to, grade]
	if _quotes.has(key):
		return _quotes[key]
	var t0 := Time.get_ticks_usec()
	# A quote buys nothing, books nothing and charges nothing -- it says so
	# itself, in its last line -- so it is not a reason to re-read the
	# clipboard. The `desks` cache does the same thing for the same reason.
	var was_dirty := _snap_dirty
	var said := site("quote %s:%d %s" % [_cable_name(_cable_from), _cable_port, to])
	_snap_dirty = was_dirty
	_quote_us = Time.get_ticks_usec() - t0
	_quote_n += 1
	var q := {"m": -1, "price": -1, "grade": grade, "why": ""}
	for line in said.split("\n", false):
		var t := line.strip_edges()
		# "no quote: there is no cable tray between uplink:0 in f0 MDF #22 and
		# f0 corridor #1." -- core's sentence, kept whole, because the reason
		# a run cannot land somewhere is the interesting half of it.
		if t.begins_with("no quote:"):
			q.why = t.substr(9).strip_edges()
			break
		var m := t.find(" m through the tray")
		if m > 0 and t.begins_with("a run from"):
			var head: String = t.substr(0, m)
			q.m = int(head.substr(head.rfind(" ") + 1))
		var f: PackedStringArray = t.split(" ", false)
		if f.size() >= 2 and str(f[0]) == grade:
			q.price = int(f[1])
	_quotes[key] = q
	return q


# What the crosshair says beside "[C] this end in", when there is already an
# end in a socket somewhere behind you. Three things can be true and each of
# them is a different move, so each of them is a different sentence:
#
#   the run fits          the metres and the price, before the money goes
#   the drum is short     core's own two numbers, at the moment you could
#                         still walk back rather than after the refusal
#   there is no tray      the copper cannot follow you here at all
#
# The refusals themselves stay core's: this changes nothing about what the
# next keystroke is allowed to do, it only stops the answer being a surprise.
func run_cost_at(s: int) -> String:
	if s < 0 or _cable_from < 0:
		return ""
	var q := run_quote(_cable_name(s))
	return _cost_words(q)


func _cost_words(q: Dictionary) -> String:
	if q.is_empty():
		return ""
	if str(q.get("why", "")) != "":
		return "  -- " + str(q.why)
	var m := int(q.get("m", -1))
	if m < 0:
		return ""
	var left := int(drum().get("left", -1))
	if left >= 0 and m > left:
		return "  -- %d m of run and %d m left on the drum: it will not reach" \
			% [m, left]
	var p := int(q.get("price", -1))
	if p < 0:
		return "  -- %d m through the tray" % m
	return "  -- %d m of %s, %d" % [m, str(q.grade), p]


# ---------------------------------------------------- the cable in your hands
#
# One end is in a socket and the rest of the drum is in your arms, so the
# copper trails from the port you plugged to your hands as you walk.
#
# AND IT LIES ON THE FLOOR BEHIND YOU, WHERE YOU WALKED. The owner: "it'd be
# fun to literally run cable down corridors... you shouldn't be penalized for
# running cables literally physically wherever you want", and, of the drawing:
# "that should rest on the floor when we're cabling things." A straight line
# from the port to your hands went through walls and through the floor of the
# room next door, which is the one thing a person pulling a drum never does.
# So the walk is remembered: a crumb every 800 mm, at the height of the feet
# that dropped it, and the copper is drawn along them. Round a corner, and the
# cable is round the corner.
#
# WHAT IT IS AND IS NOT. This is the drum paying out -- slack cable on the
# floor, exactly as far as you have walked -- and it is a picture of an act
# rather than a claim about the invoice. The moment the far end goes in, this
# is thrown away and the run is redrawn where core BILLED it: through the tray,
# up the riser, the metres `quote` has been printing in the HUD the whole time
# you were walking. A view that went on drawing 60 m of floor copper for a run
# billed at 21 m through the ceiling would be telling you the price was wrong.
# See docs/decisions-d38.md, which is a record of exactly this reconciliation.
const CRUMB_M := 0.8            # how often the drum leaves one
const CRUMB_MAX := 400          # 320 m of walk, and the drum holds 305
var _cable_port := -1
var _cable_dev := -1
var _trail: MeshInstance3D = null
var _laid: MeshInstance3D = null
var _crumbs: Array = []


func _drop_trail() -> void:
	if _trail:
		_trail.queue_free()
		_trail = null


# The end came out, or the run finished: the floor copper goes with it.
func _drop_run() -> void:
	_crumbs.clear()
	if _laid:
		_laid.queue_free()
		_laid = null


# One step of the walk, if it was far enough from the last one to be a
# different place. Height is the feet, plus the thickness of a lead, because
# that is where a cable somebody is pulling actually is.
func _lay_crumb() -> void:
	if _cable_from < 0 or player == null:
		return
	var p: Vector3 = player.global_position
	p.y += CABLE_R
	if not _crumbs.is_empty():
		var last: Vector3 = _crumbs[_crumbs.size() - 1]
		if Vector2(p.x - last.x, p.z - last.z).length() < CRUMB_M \
				and absf(p.y - last.y) < 0.5:
			return
	_crumbs.append(p)
	# A LONGER WALK THAN THE DRUM HOLDS. Nothing stops a player wandering the
	# tower with an end in their hand, so the oldest half is thinned rather
	# than grown without bound: the shape of where they went survives and the
	# buffer does not.
	if _crumbs.size() > CRUMB_MAX:
		var keep: Array = []
		for i in range(_crumbs.size()):
			if i >= CRUMB_MAX / 2 or i % 2 == 0:
				keep.append(_crumbs[i])
		_crumbs = keep
	_draw_laid()


# The copper already on the floor. Rebuilt when a crumb is added rather than
# every frame: it is up to four hundred segments and it does not move.
func _draw_laid() -> void:
	if _laid:
		_laid.queue_free()
		_laid = null
	if _cable_from < 0 or _crumbs.size() < 1:
		return
	var a := _dev_point(_cable_from, _cable_port)
	if a == Vector3.INF:
		return
	var face := _dev_face(_cable_from)
	var pts: Array = [a, a + face * 0.06]
	# down the front of the rack to the floor, then away along the walk
	var foot: Vector3 = a + face * 0.10
	foot.y = float(_crumbs[0].y)
	pts.append(foot)
	pts.append_array(_crumbs)
	var g = preload("res://scripts/vgeo.gd").new()
	_run_cable(g, pts, Color("#2f6fd0"), 1)
	_laid = MeshInstance3D.new()
	_laid.name = "CableLaid"
	_laid.mesh = g.mesh()
	add_child(_laid)


func _draw_trail() -> void:
	if _cable_from < 0 or player == null or player.cam == null:
		_drop_trail()
		return
	var a := _dev_point(_cable_from, _cable_port)
	if a == Vector3.INF:
		_drop_trail()
		return
	var face := _dev_face(_cable_from)
	var cam: Camera3D = player.cam
	# where your hands are: down and to the left of the eye, out in front
	var hands: Vector3 = cam.global_position \
		+ cam.global_transform.basis * Vector3(-0.22, -0.30, -0.45)
	var g = preload("res://scripts/vgeo.gd").new()
	# THE LAST FEW METRES ONLY. Everything behind the last crumb is already
	# drawn and lying still; this is the piece between the floor and your
	# hands, which is the only part that moves with you.
	var from: Vector3 = a + face * 0.06
	if not _crumbs.is_empty():
		from = _crumbs[_crumbs.size() - 1]
	var mid := (from + hands) * 0.5
	mid.y = min(from.y, hands.y) - from.distance_to(hands) * 0.16
	var pts: Array = [from, mid, hands]
	if _crumbs.is_empty():
		pts = [a, a + face * 0.06, mid, hands]
	_run_cable(g, pts, Color("#2f6fd0"), 1)
	_drop_trail()
	_trail = MeshInstance3D.new()
	_trail.name = "CableInHand"
	_trail.mesh = g.mesh()
	add_child(_trail)


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


# ==================================================== THE LOOP, IN THE WORLD
#
# core/siteday.c has had a clock, tenants who move in on their day, desks that
# generate real traffic, rent for the work that finished, complaints after
# three bad days and a run that ends -- and none of it was reachable from
# inside the building. You could walk a tower for an hour and never learn the
# date, the balance, or that somebody on floor three had moved in and was
# sitting in a room full of computers with no ports.
#
# Everything below is a VIEW OF `status`, `service`, `load` and `day`. There is
# no second copy of the clock, the money or the tenancies here: the panels hold
# the TEXT those verbs printed, parsed only far enough to know which line to
# put where, and every number a player reads is the number core printed.

var _ledger_box: VBoxContainer = null      # the date and the money, top right
var _ledger_rows: VBoxContainer = null
var _ledger_pool: Array = []
var _ledger_alert: PanelContainer = null
var _ledger_alert_lab: Label = null
var _ledger_sig := ""
var _snap := {"status": "", "service": "", "load": "", "open": "", "spool": ""}
var _snap_dirty := true
var _snapping := false
var _waiting: Node3D = null         # the beacons over tenancies with no ports
var _dev_sig := ""
var run_over := false
# The sentence that ended the run, kept for good. See _show_report().
var run_over_why := ""
# What the handset is currently showing, so _reconcile_phone only acts on a
# change rather than re-plugging the lead sixty times a second.
var _phone_dev := -1
var _phone_lead := ""


# WHERE THE SESSION THINKS YOU ARE STANDING, which is not always where the view
# does: a socket client can walk the body across the building. Anything that
# depends on the floor -- and `open` charges money on it -- asks this one.
func ses_floor() -> int:
	var r := int(ses_state().get("room", -1))
	if r < 0 or r >= rooms.size():
		return -1
	return int(rooms[r].floor)


func ses_where() -> int:
	return int(ses_state().get("where", 1))     # SES_BODY == 1, from session.h


# Go and read the clipboard: four verbs, no more often than something happens.
func _snapshot() -> void:
	if not site_up or _snapping:
		return
	_snap_dirty = false
	# A LEAD IS IN A CONSOLE AND THE KEYBOARD BELONGS TO IT. `status` typed at
	# a shell is a line typed at somebody's machine, not a question for the
	# landlord, so the clipboard waits until the lead comes out.
	if ses_where() != 1:
		return
	_snapping = true
	_snap.status = site("status")
	_snap.service = site("service")
	_snap.load = site("load")
	# WHAT IS IN YOUR HANDS, in core's words rather than in a copy of them.
	# `spool` is the drum: how much is left on it and what grade it is, which
	# is what the HUD and the crosshair both say and what [R] changes.
	_snap.spool = site("spool")
	# WHAT THE NEXT FLOOR NEEDS, IN CORE'S WORDS, BEFORE THE KEY IS PRESSED.
	# `open` refuses from anywhere but the floor itself and says what it wants
	# and what it costs while refusing -- so asking it from the wrong floor is
	# free, and it is the only way to print the fit-out without a second copy
	# of the price. The condition is core's own (`here_floor(ses) != f`), read
	# off the session rather than off the body, so this can never be the call
	# that spends the money.
	if floors_in_service < nfloors and ses_floor() != floors_in_service:
		_snap.open = site("open")
	_snapping = false
	_beacons()
	# AND THE ROOM ITSELF SAYS SO. The beacon over the door is `service`'s
	# columns in words; the people at the desks under it are the same columns
	# in postures, re-read here rather than remembered, so a tenancy that got
	# its day's work done stops looking like one that did not the moment the
	# day is counted.
	_people()


# The rows of `service`, as the columns it printed them in. Nothing is computed
# here: the header is "floor tenant desks up addr done worst strikes rent/day"
# and this is that line, split.
func service_rows() -> Array:
	var out: Array = []
	# BY THE HEADER'S OWN NAMES, not by counting from the left. `service` grew
	# a `trade` column between `tenant` and `desks` while this file was being
	# written, and a positional parser read the word "office" as a desk count
	# -- so every tenancy in the tower had nought desks, the HUD said so, and
	# the people at those desks vanished. The columns are core's to change; the
	# names are what it prints them under.
	# COUNTED BACK FROM THE END, because one of the columns holds words. A
	# trade is "office" on most rows and "web host" on some, which is two
	# tokens, so nothing to the right of it can be found by counting forwards
	# either. The header says how far each name is from the last column and the
	# row is read from its own end -- and a `files` cell that ends in the "<-"
	# that means "served off another floor" has that taken off first.
	var back := {}
	var wide := 0
	for line in str(_snap.service).split("\n", false):
		var f: PackedStringArray = line.split(" ", false)
		if f.size() >= 4 and str(f[0]) == "floor" and str(f[1]) == "tenant":
			back.clear()
			wide = f.size()
			for i in range(f.size()):
				back[str(f[i])] = f.size() - 1 - i
			continue
		if back.is_empty() or f.size() < wide:
			continue
		# AND IT REALLY IS A ROW. `service` prints a sentence under a tenancy
		# that is having a bad day -- "20 of 80 transfers did not finish inside
		# the busy period" -- which begins with a number and is longer than the
		# header. Read as a row it said tenant 0 on floor 20 had eighty desks,
		# and the HUD printed that. A row is two numbers first and numbers in
		# every numeric column; a sentence is not.
		if not f[0].is_valid_int() or not f[1].is_valid_int():
			continue
		var toks: Array = Array(f)
		if str(toks[toks.size() - 1]) == "<-":
			toks.remove_at(toks.size() - 1)
		var last: int = toks.size() - 1
		var sane := true
		for nm in ["desks", "up", "addr", "rent/day"]:
			var at: int = last - int(back.get(nm, -1))
			if at < 2 or at > last or not str(toks[at]).is_valid_int():
				sane = false
		if not sane:
			continue
		var strikes := str(toks[last - int(back.get("strikes", 2))])
		# THE TRADE, WHICH IS WORDS AND SO IS READ AS THE GAP. It sits between
		# `tenant` and `desks` and is one token on most rows and two on a "web
		# host", which is why nothing here counts from either end to find it:
		# it is everything between the column before it and the column after.
		var tr: Array = []
		for at in range(2, last - int(back.get("desks", 7))):
			tr.append(str(toks[at]))
		out.append({"floor": int(f[0]), "tenant": int(f[1]),
			"trade": " ".join(tr),
			"desks": int(toks[last - int(back.get("desks", 7))]),
			"up": int(toks[last - int(back.get("up", 6))]),
			"addr": int(toks[last - int(back.get("addr", 5))]),
			"done": str(toks[last - int(back.get("done", 4))]),
			"worst": str(toks[last - int(back.get("worst", 3))]),
			"strikes": int(strikes.replace("*", "")),
			"complained": strikes.find("*") >= 0,
			"rent": int(toks[last - int(back.get("rent/day", 1))])})
	return out


# The busiest port `load` named, and its drops -- as its own row, so what a
# player reads in a corridor is the line the tool prints.
func load_worst() -> String:
	for line in str(_snap.load).split("\n", false):
		var f: PackedStringArray = line.split(" ", false)
		if f.size() < 5 or f[0].find(":") < 0:
			continue
		return line.strip_edges()
	return ""


func load_drops() -> int:
	var f: PackedStringArray = load_worst().split(" ", false)
	if f.size() < 5:
		return 0
	return int(f[4])


# The first two lines of `status`: the date, the balance, and when the circuit
# is next billed. Core's sentences, not a second rendering of the same numbers.
func ledger_text() -> String:
	var out: Array = []
	for line in str(_snap.status).split("\n", false):
		out.append(line.strip_edges())
		if out.size() == 2:
			break
	if out.is_empty():
		var st := ses_state()
		return "day %d.  %d in hand." % [int(st.get("day", 0)), int(st.get("money", 0))]
	return "\n".join(out)


# THE MONEY BLOCK, TIERED. ledger_text() is untouched -- it is what `hud` over
# the socket reads back and what game/tests/tower.gd greps -- so the tiering is
# done by reading it, exactly the way _hud_tier() reads hud_lines().
#
# The two sentences were one weight of grey: the balance, the burn, the circuit
# and the day the bill lands all looked equally like scenery. They are not the
# same thing. The BALANCE is the number the run ends on -- money below zero and
# the lease is done, core/siteday.c -- so it is PLACE, the same size as the room
# you are standing in. The COUNTDOWN is NOW, because it moves under you whether
# or not you touch anything. And the circuit's size and price are STATE: facts
# about the building, true until you change them.
#
# Returns [[tier, text], ...], with an "alert" row last when there is one.
const BILL_SOON_DAYS := 3

func ledger_rows() -> Array:
	var txt := ledger_text()
	var rows: Array = []
	var m1 := RegEx.create_from_string(
		"day (\\d+)\\. (-?\\d+) in hand, (-?\\d+) spent, (-?\\d+) taken in rent"
		).search(txt)
	if m1 == null:
		# Before the first snapshot there is only the fallback sentence. Show
		# it rather than showing nothing: a HUD that is empty until a verb runs
		# is a HUD that looks broken.
		for line in txt.split("\n", false):
			rows.append(["state", line.strip_edges()])
		return rows
	var money := int(m1.get_string(2))
	rows.append(["place", "%d in hand" % money])
	rows.append(["state", "day %s   %s spent, %s taken in rent"
		% [m1.get_string(1), m1.get_string(3), m1.get_string(4)]])
	var m2 := RegEx.create_from_string(
		"the circuit is (\\d+) Mb \\((-?\\d+) a month, next billed in (\\d+) day"
		).search(txt)
	if m2 == null:
		return rows
	var price := int(m2.get_string(2))
	var days := int(m2.get_string(3))
	rows.append(["state", "%s Mb circuit, %d a month" % [m2.get_string(1), price]])
	rows.append(["now", "next billed in %d day%s" % [days, "" if days == 1 else "s"]])
	# AND THE BILL YOU CANNOT PAY, SAID BEFORE IT LANDS. The circuit is the one
	# standing charge in the game and it is taken whatever the network did with
	# it; a run that ends overdrawn ends on the morning that charge went out.
	# A player who reads "1540 a month" in the same grey as everything else has
	# not been told anything. So: red when the balance will not cover the bill,
	# from the day that becomes true rather than on the day it lands, and red
	# when the bill is a few days out whatever the balance is.
	var alert := ""
	var s := "" if days == 1 else "S"
	if money < 0:
		alert = "OVERDRAWN BY %d" % -money
	elif money < price:
		alert = "%d DUE IN %d DAY%s, %d IN HAND" % [price, days, s, money]
	elif days <= BILL_SOON_DAYS:
		alert = "%d DUE IN %d DAY%s" % [price, days, s]
	if alert != "":
		rows.append(["alert", alert])
	return rows


# PAINT IT, on the same terms as _hud_paint(): rebuilt only when the text
# changed, rows pooled and re-tiered in place, and the panel shrunk to the
# sentence rather than reserving a dark bar across the top right of the view.
func _ledger_paint() -> void:
	if _ledger_box == null or _ledger_rows == null:
		return
	var rows := ledger_rows()
	var sig := str(rows)
	if sig == _ledger_sig:
		return
	_ledger_sig = sig
	var n := 0
	var alert := ""
	for row in rows:
		var tier: String = str(row[0])
		var text: String = str(row[1])
		if tier == "alert":
			alert = text
			continue
		var l: Label
		if n < _ledger_pool.size():
			l = _ledger_pool[n]
		else:
			l = _hud_row(tier)
			l.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
			_ledger_pool.append(l)
			_ledger_rows.add_child(l)
		var spec: Array = HUD_TIER.get(tier, HUD_TIER["state"])
		l.add_theme_font_size_override("font_size", int(spec[0]))
		l.add_theme_color_override("font_color", Color(str(spec[1])))
		l.text = text
		l.visible = true
		n += 1
	for i in range(n, _ledger_pool.size()):
		_ledger_pool[i].visible = false
	if alert == "":
		_ledger_alert.visible = false
	else:
		_ledger_alert_lab.text = alert
		_ledger_alert.visible = true
	_ledger_box.size = Vector2.ZERO
	var m := _ledger_box.get_combined_minimum_size()
	_ledger_box.offset_left = -12.0 - m.x
	_ledger_box.offset_bottom = _ledger_box.offset_top + m.y


# The room a tenancy holds, so a beacon can hang over their door.
func tenant_room(t: int) -> int:
	for r in rooms:
		if int(r.tenant) == t:
			return int(r.i)
	return -1


# DOES THIS TENANCY RENT THIS ROOM? tenant_room() above answers "which room",
# singular, and a tenancy holds as many as eleven of them -- it returns the
# first. That was harmless while every desk of a tenancy was installed into
# that first room; since desks are apportioned across every room a tenancy
# leases, "the room they rent" is the wrong question and this is the right
# one.
func room_rented_by(room_i: int, t: int) -> bool:
	if room_i < 0:
		return false
	for r in rooms:
		if int(r.i) == room_i:
			return int(r.tenant) == t
	return false


# WHO IS WAITING, WITHOUT TYPING A VERB.
#
# A tenancy that has moved in and has no ports is the player's whole job, and
# until now the building said nothing about it: the desks appeared in a room
# upstairs and the only way to find out was `service`. So the rooms they are in
# say so over their own doors, in `service`'s own columns -- and the sign goes
# when the ports do, because it is rebuilt from the table every time.
func _beacons() -> void:
	if _waiting:
		_waiting.queue_free()
	_waiting = Node3D.new()
	_waiting.name = "Waiting"
	add_child(_waiting)
	for row in service_rows():
		if int(row.up) >= int(row.desks):
			continue
		var ri := tenant_room(int(row.tenant))
		if ri < 0:
			continue
		var txt := "TENANT %d WAITING\n%d desks, %d up, %d addr" \
			% [int(row.tenant), int(row.desks), int(row.up), int(row.addr)]
		var col := Color("#e0b040") if int(row.up) > 0 else Color("#e07a5a")
		var placed := 0
		for dd in room_doors(ri):
			var g: Vector2 = dd.gate
			var o: Vector2 = dd.out
			var p := Vector3(g.x, float(rooms[ri].floor) * fheight + DOOR_H + 0.30,
				g.y) + Vector3(o.x, 0, o.y) * (WALL_T * 0.5 + 0.06)
			var l := Label3D.new()
			l.font = preload("res://scripts/uifont.gd").mono()
			l.font_size = 20
			l.pixel_size = 0.0035
			l.text = txt
			l.modulate = col
			l.outline_size = 0
			l.billboard = BaseMaterial3D.BILLBOARD_DISABLED
			l.double_sided = false
			l.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
			l.position = p
			l.rotation = Vector3(0, atan2(o.x, o.y), 0)
			_waiting.add_child(l)
			placed += 1
		if placed == 0:
			var c := room_centre(ri)
			c.y += 2.4
			var l2 := Label3D.new()
			l2.font = preload("res://scripts/uifont.gd").mono()
			l2.font_size = 20
			l2.pixel_size = 0.0035
			l2.text = txt
			l2.modulate = col
			l2.billboard = BaseMaterial3D.BILLBOARD_ENABLED
			l2.position = c
			_waiting.add_child(l2)


func waiting_signs() -> int:
	return 0 if _waiting == null else _waiting.get_child_count()


# ---------------------------------------------------------- a day passes
#
# A deliberate act with its consequences shown. `day` is the session's verb and
# the prose it prints is already the right report -- who moved in, what got
# served, what rent arrived, which port was busiest, what the building did to
# your kit overnight -- so this shows THAT, rather than a second summary of the
# same day written in GDScript.

var _report: CanvasLayer = null
var report_text := ""
const REPORT_LINES := 16     # of the day's report, at most, on the screen at once

func advance_day(line := "day") -> String:
	if not site_up:
		return ""
	var said: String = site(line).strip_edges()
	_reconcile()
	_snapshot()
	_show_report(said)
	return said


func _show_report(said: String) -> void:
	report_text = said
	if said.find("THE RUN IS OVER") >= 0 or said.find("the run ended") >= 0:
		# WHY IT ENDED, KEPT. `report_text` is only ever "the last thing the
		# day printed", so the sentence naming the reason was overwritten by
		# the next `day` -- which answers with the terse "the run ended on day
		# 19" rather than repeating it. A player who dismissed the panel and
		# pressed [N] lost the explanation for good, and a socket client
		# reading `hud` afterwards got the same nothing.
		#
		# Set once and never replaced: a run ends for one reason and that
		# reason does not change afterwards.
		if run_over_why == "":
			for ln in said.split("\n"):
				if ln.find("THE RUN IS OVER") >= 0 or ln.find("the run ended") >= 0:
					run_over_why = ln.strip_edges()
					break
		run_over = true
	if not with_player or hud == null:
		return
	_dismiss_report()
	_report = CanvasLayer.new()
	_report.name = "DayReport"
	_report.layer = 8
	var pad := ColorRect.new()
	pad.color = Color(0.04, 0.06, 0.08, 0.92)
	var lab := Label.new()
	lab.add_theme_font_override("font", preload("res://scripts/uifont.gd").mono())
	lab.add_theme_font_size_override("font_size", 17)
	lab.add_theme_color_override("font_color",
		Color("#e07a5a") if run_over else Color("#e8eef4"))
	# A REPORT HAS TO FIT ON THE SCREEN. One day is a line or four; `day 49`
	# over the socket is forty-nine of them, and a panel the height of seven
	# hundred lines covers the HUD, the ledger and the building behind it. The
	# tail is the part that matters -- the days you are about to live in -- and
	# it says how many went past rather than pretending they did not.
	var f: Font = preload("res://scripts/uifont.gd").mono()
	var view := get_viewport().get_visible_rect().size
	# CLEAR OF THE HUD. The location, the tenancies and what the next floor
	# needs are all top left, and a report sitting on top of them is a report
	# that costs you the thing it is telling you about. It used to be a fixed
	# 250 px down, which was clear of a HUD of five lines and straight through
	# the middle of one with seven tenancies and a run-ending bar on it -- so it
	# is measured off the block instead of guessed at.
	var top := 250.0
	if hud and hud.visible:
		top = max(top, hud.position.y + hud.size.y + 18.0)
	top = min(top, view.y - 220.0)
	# ... AND THE PANEL IS AS TALL AS THE TEXT REALLY IS. The height was
	# 22 px a line against a font that draws at 24, so the last two lines of a
	# long report -- which are the ones that say how to get out of it -- hung
	# below the panel, on the building, unreadable. It is the font's own line
	# height now, and how many days fit is worked out from the room there is.
	# (a Label's line pitch is the font's height plus its line_spacing constant,
	# which is 3 by default -- leaving it out is where the 22 came from)
	var lh: float = f.get_height(17) + 3.0
	var room: int = int((view.y - top - 40.0 - 36.0) / lh) - 3
	var most: int = clampi(room, 4, REPORT_LINES)
	var all := said.split("\n")
	var shown := said
	if all.size() > most:
		var keep: Array = []
		for i in range(all.size() - most, all.size()):
			keep.append(all[i])
		shown = "... %d earlier days went past\n" % (all.size() - most) \
			+ "\n".join(keep)
	lab.text = shown + ("\n\n" if shown != "" else "") \
		+ ("the run is over." if run_over else "[Esc] back to the building   [N] the next day")
	pad.add_child(lab)
	# The panel is the size of what core wrote: a fixed box would either crop
	# a mains failure or leave a hole under a quiet day.
	var w := 0.0
	var lines := lab.text.split("\n")
	for line in lines:
		w = max(w, f.get_string_size(line, HORIZONTAL_ALIGNMENT_LEFT, -1, 17).x)
	pad.size = Vector2(min(w + 48.0, view.x - 120.0),
		min(float(lines.size()) * lh + 36.0, view.y - top - 40.0))
	pad.position = Vector2(60, top)
	lab.set_anchors_preset(Control.PRESET_FULL_RECT)
	lab.offset_left = 24.0
	lab.offset_top = 18.0
	lab.offset_right = -24.0
	lab.offset_bottom = -18.0
	_report.add_child(pad)
	add_child(_report)


func report_open() -> bool:
	return _report != null


func _dismiss_report() -> void:
	if _report:
		_report.queue_free()
		_report = null


# ------------------------------------------------------- keeping up with it
#
# The view reconciles off the session: what boxes exist and where they are is
# read back out of site_devs() after anything that could have moved one, and
# the world is rebuilt from that read. A tenancy moving in is twenty new
# devices in a room upstairs and nothing here has to be told about it.

func _site_sig() -> String:
	var s := ""
	for d in site_devs():
		# AND WHETHER IT IS ON. `power ws off` moves no box and changes no
		# port, so the signature did not notice it -- and the desktop stayed
		# painted on the monitor of a machine the site said was dead, which is
		# exactly the picture this whole record exists to stop being possible.
		s += "%d:%d:%d:%d%d," % [int(d.i), int(d.room), int(d.nports),
			1 if bool(d.get("powered", false)) else 0,
			1 if bool(d.get("mains", false)) else 0]
	return s + "|%d" % carrying


func _reconcile() -> void:
	if not site_up:
		return
	# A FLOOR OPENED IS A FLOOR OPENED WHOEVER TYPED IT. `open` over the socket
	# moves core's count, and the lit buttons in the lift and the NOT IN
	# SERVICE on the lobby wall are a picture of that count.
	var now := int(ses_state().get("floors", floors_in_service))
	if now != floors_in_service:
		floors_in_service = now
		for l in lifts:
			l.rebuild_panels()
		_signage()
	var sig := _site_sig()
	if sig != _dev_sig:
		_dev_sig = sig
		_rebuild_devices()
	_draw_cables()
	# AND THE END OF THE CABLE IS IN THE SOCKET WHOEVER PUT IT THERE. `plug
	# uplink:0` typed at the socket left the session holding a drum with one
	# end in a port and the window showing neither the trail of copper nor the
	# line in the HUD that says a run is open -- so the one state a player can
	# be in that spends money on the NEXT keystroke was invisible to exactly
	# the client that cannot see the window anyway. Same rule as the handset
	# below: the session knows, and this makes the picture agree.
	_sync_cable()
	# AND THE HANDSET FOLLOWS THE LEAD, WHOEVER PUT IT IN.
	#
	# `plug core` over the socket put the SESSION on a management line and left
	# the handset hanging at the player's side saying "no lead plugged in" --
	# so the one verb with its own front end in this window was the one verb
	# the window did not reconcile with. D23's rule is that the view is never
	# the source of truth and everything must be drivable over the socket;
	# this was the exception, and it cost an agent the ability to photograph
	# or verify the handset at all, which is how it was found.
	#
	# The session has always known: `plugged <dev> <hdmi>`. Nothing here
	# decides anything -- it reads that pair and makes the prop agree, the
	# same way the lift buttons above follow `open`.
	_reconcile_phone()
	_snap_dirty = true


func _reconcile_phone() -> void:
	if phone == null:
		return
	# `plugged <dev> <hdmi>` arrives as a two-element array, because ses_state
	# packs a multi-value line that way.
	var st := ses_state()
	var pl: Variant = st.get("plugged", -1)
	var dev := -1
	var hdmi := 0
	if pl is Array and (pl as Array).size() >= 2:
		dev = int((pl as Array)[0])
		hdmi = int((pl as Array)[1])
	else:
		dev = int(pl)
	var want_lead := "display" if hdmi == 1 else "serial"
	if dev < 0:
		if str(phone.status) != "unplugged":
			phone.unplug()
		# AND THE MEMORY GOES OUT WITH THE LEAD.
		#
		# The two variables below are an optimisation -- plugging the same box
		# in twice is not an event, and re-plugging sixty times a second is --
		# and this branch cleared the PROP without clearing them. So `plug
		# core`, `unplug`, `plug core` left the pair still reading `core` on
		# the third line, the early return below fired, and the handset stayed
		# dark while the session was sitting on a management line. Rare until
		# [Esc] started taking the lead out through this same path, and
		# constant afterwards. The cache is only allowed to say what the prop
		# is really showing.
		_phone_dev = -1
		_phone_lead = ""
		return
	if dev == _phone_dev and want_lead == _phone_lead:
		return
	_phone_dev = dev
	_phone_lead = want_lead
	# AND THE TWO NUMBERS ARE NOT THE SAME NUMBER. `plugged` off ses_state is
	# the SITE's device index; phone.plug() takes an index into `devices`,
	# which is this file's own list of things you can walk up to -- it holds
	# props the site has never heard of (the patch panels, the customer's rack
	# server) and, since D41, draws the player's workstation out of order
	# because it is a desk rather than a box in a frame. The two happened to
	# line up until something was placed out of order, and then `plug core`
	# put the handset on the box after it.
	phone.plug(dev_of_site(dev), want_lead)
	# AND IT CALLS THE FAR END WHAT THE SESSION CALLS IT. A switch and a
	# router are appliances: the lead reaches a management line, not a shell,
	# and `plug core` says so while the handset was answering "serial console
	# on core" about the same wire. Two names for one thing is the defect this
	# project has spent the day removing. SES_MGMT is 2 in core/session.h.
	if int(st.get("where", 0)) == 2 and str(phone.status).begins_with("serial console on "):
		phone.status = "management line on " + str(phone.status).substr(18)


# THE SITE'S INDEX FOR A BOX, TRANSLATED INTO THIS FILE'S. -1 if the view is
# not drawing it -- which is a real state: a box in your hands is not standing
# in a room.
func dev_of_site(site_i: int) -> int:
	if site_i < 0:
		return -1
	for i in range(devices.size()):
		if int(devices[i].get("site", -1)) == site_i:
			return i
	return -1


func _rebuild_devices() -> void:
	for d in devices:
		var n: Node = d.get("node", null)
		if n and is_instance_valid(n):
			n.queue_free()
	devices.clear()
	if _ws_node and is_instance_valid(_ws_node):
		_ws_node.queue_free()
		_ws_node = null
	_place_devices()


# ------------------------------------------------------ the line from outside
#
# EVERY VERB THE 3D PERFORMS IS THE SOCKET'S VERB, and this is that sentence
# read the other way: a line arriving from outside drives the window. It is the
# same session_line() the keys call, so there is no command an agent can type
# that a player cannot do and none a player can do that an agent cannot type.
#
# The body follows. `go f3.comms` is core's walk -- it charges the metres and
# refuses a floor that is not in service -- and afterwards the view puts the
# player where the session says they now are, because the session is the
# authority on where the legs went and the view is a picture of it.

func command(line: String) -> String:
	line = line.strip_edges()
	if line == "":
		return ""
	if not site_up:
		return "there is no session yet\n"
	# THE WINDOW'S OWN STATE, for a client that cannot see it. Not a verb of
	# the game: a way of asking what is on the screen, which is the one thing a
	# socket client genuinely cannot read off the session.
	if line == "hud":
		var s := ledger_text() + "\n" + where_am_i() + "\n" + hud_lines()
		if report_open():
			s += "\n--- the day's report is up ---\n" + report_text
		# AND WHY THE RUN ENDED, WHICH OUTLIVES THE PANEL. A client that
		# dismissed the report, or connected after it, could read the whole
		# ledger and never learn the game was over or what ended it.
		if run_over and run_over_why != "":
			s += "\n" + run_over_why
		return s + "\n"
	# THE MAP, IN WORDS. Same rule as `hud`: a client with no window is told
	# exactly what the panel in the corner is drawn from -- the rooms of this
	# floor in metres, the one the body is standing in, and the sockets each
	# has left. The gate walks this against the room table, so the picture is
	# checkable without anybody looking at it.
	if line == "map":
		return map_text()
	# WHAT THE FRAME COSTS, from the engine's own counters rather than from a
	# stopwatch in a comment. A tower fills up with people, racks and copper as
	# it is played and nothing else in this project could say what that did to
	# the frame; `hud` is the same kind of verb -- the window's state, asked for
	# by a client that cannot see the window.
	if line == "perf":
		return perf_text()
	# POINTING THE CAMERA, which a socket player had no way to do at all.
	#
	# A playtester's end-of-run screenshot of the MDF showed a desk and an
	# empty floor rather than the four racks they had spent an hour filling,
	# because the body happened to be facing the other way and there was no
	# `turn`, `face` or `look at` in the game. Screenshots are meant to be a
	# CHECK on the text -- the one thing a socket client cannot verify by
	# reading -- and a check you cannot aim is not a check.
	#
	# The head-turning itself is aim_at(), which already exists because
	# `plug core:6` puts the crosshair on that hole. This is the same motion
	# asked for by name, and it is a view verb: it moves no metres, spends no
	# money and changes nothing core knows about. That is why it is here and
	# not in session.c -- the session has no camera.
	if line == "face" or line == "turn" or line == "look at":
		return "face what? `face core` a box, `face core:6` a socket on it,\n" \
			+ "  `face <room>` the middle of a room you are in.\n"
	if line.begins_with("face ") or line.begins_with("turn ") \
			or line.begins_with("look at "):
		var what := line.substr(8 if line.begins_with("look at ") else 5)
		return _face(what.strip_edges())
	# AND A KEYSTROKE A SOCKET PLAYER HAS NOT GOT. The day's report is a modal
	# that only [Esc] dismissed, on a client with no keyboard in the window --
	# so every line after a `day` went to a session whose report was still up
	# and stayed up, in every screenshot, over everything behind it.
	if line == "dismiss" or line == "ok" or line == "esc":
		if not report_open():
			return "there is no report up.\n"
		if run_over:
			return "the run is over. That panel does not go away.\n"
		_dismiss_report()
		return "the report is dismissed and the building is behind it again.\n"
	var out := ""
	# WHAT THE PROMPT SAID BEFORE THE LINE RAN, because the line may be the one
	# that changes it -- `sit` and `stand` both do.
	var was_prompt := ses_prompt()
	if line == "day" or line.begins_with("day "):
		out = advance_day(line)
	else:
		out = site(line)
	# AND THE WINDOW SHOWS WHAT THE SOCKET TYPED, when the socket is typing at
	# somebody's desk. Two front ends onto one session is this project's rule
	# everywhere else; a terminal in the window that stayed blank while an agent
	# drove the same machine down the wire would be a screenshot that could not
	# be used as evidence of anything.
	if seat_open() and seat_term != null:
		seat_term.write(was_prompt + line + "\n" + out)
	# AND WHAT CORE SAID WHEN THE CHAIR CAME OUT IS THE FIRST THING ON THAT
	# SCREEN. `sit` prints who stood up, what state their machine is in and
	# what they think of their week -- read off the model -- and a terminal
	# that opened on a bare prompt would have thrown all of it away.
	if line == "sit" or line.begins_with("sit "):
		_seat_intro = out
	var st := ses_state()
	var r := int(st.get("room", -1))
	if r >= 0 and r < rooms.size() and r != player_room():
		stand_in(r)
	_reconcile()
	# AND `sit` OVER THE SOCKET PUTS THE WINDOW IN THE CHAIR. The session is
	# the authority on where the body is and what it is typing at, so the
	# terminal on somebody's desk opens because core says you are sitting at
	# it -- not because a key was pressed. A socket client and a player at the
	# keyboard get the same window out of the same word.
	_reconcile_seat()
	_snapshot()
	# AND THE HEAD TURNS TO THE HOLE THE LINE NAMED. The owner: "if you give a
	# command to cable a particular port, the mouse automatically aligns to
	# that port." A command that puts an end in `core:6` and leaves the player
	# looking at a wall is a window that is not keeping up with the game.
	if line.begins_with("plug ") and line.find(":") > 0:
		_face_port(line.substr(5).strip_edges())
	return out


# FACE A BOX, A SOCKET ON ONE, OR A ROOM. The body does not move and nothing
# is charged: turning your head is free in a real building too. It answers
# with what is now in front of the crosshair, because a client that cannot see
# the screen has to be told the aim took -- "ok" would prove nothing.
func _face(what: String) -> String:
	if player == null or player.cam == null:
		return "there is nobody in the building to turn.\n"
	if what == "":
		return "face what?\n"
	var port := -1
	var name := what
	if what.find(":") > 0:
		var f := what.split(":", false)
		name = str(f[0])
		port = int(f[1]) if f.size() > 1 else -1
	var here := int(ses_state().get("room", -1))
	for d in site_devs():
		if str(d.name) != name:
			continue
		# A WALL IS A WALL TO A CAMERA TOO. Every device in the tower has a
		# point in world space, so aiming at one four rooms away succeeds and
		# photographs the plaster in between. The session's rule is the right
		# one here as well: you can only see what is in the room with you.
		if here >= 0 and int(d.room) != here:
			return "refused: the camera did not move -- %s is in f%d %s #%d " \
				% [name, int(rooms[int(d.room)].floor),
					str(rooms[int(d.room)].name), int(d.room)] \
				+ "and you\n  are not, so there is a wall between you and it. " \
				+ "`go %s` first.\n" % name
		var p: Vector3 = _dev_point(int(d.i), max(port, 0))
		if p == Vector3.INF:
			return "refused: the camera did not move -- %s is in the building " \
				% name + "but nothing\n  of it is drawn in this room. `go %s` " \
				% name + "first.\n"
		aim_at(p)
		if port >= 0:
			return "you look at %s port %d.\n" % [name, port]
		return "you look at %s, a %s.\n" % [name, str(d.kindname)]
	# Not a box: a room, spelled the way `go` spells one. The middle of it,
	# which is what somebody standing in a doorway turns to look at.
	var want := -1
	if what.begins_with("#"):
		want = int(what.substr(1))
	elif here >= 0 and here < rooms.size():
		for i in range(rooms.size()):
			if int(rooms[i].floor) == int(rooms[here].floor) \
					and str(rooms[i].name).to_lower() == what.to_lower():
				want = i
	if want < 0 or want >= rooms.size():
		return "there is no box or room called %s to look at. `look` says what " \
			% what + "is in\n  this room, `map` draws the floor.\n"
	aim_at(room_centre(want))
	return "you look towards f%d %s #%d.\n" \
		% [int(rooms[want].floor), str(rooms[want].name), want]


func _face_port(spec: String) -> void:
	var f := spec.split(":", false)
	if f.size() < 2 or player == null:
		return
	var want := -1
	for d in site_devs():
		if str(d.name) == str(f[0]):
			want = int(d.i)
	if want < 0:
		return
	var p := _dev_point(want, int(f[1]))
	if p == Vector3.INF:
		return
	# AND YOU ARE STANDING AT IT. Somebody putting a lead in a socket is at
	# arm's length from that socket, not across the room -- and a step inside
	# the room you are already in is free, because the session prices a walk by
	# the rooms between here and there.
	var here := int(ses_state().get("room", -1))
	if here >= 0 and here < rooms.size():
		var n := _dev_face(want)
		var stand := p + n * 1.1
		var fl := int(rooms[here].floor)
		if room_of(fl, int(floor(stand.x)), int(floor(stand.z))) == here:
			stand.y = float(fl) * fheight + 0.25
			teleport(stand)
	aim_at(p)


# PUT THE BODY WHERE THE SESSION SAYS IT IS. The metres were charged by the
# session's own walk, so this is not a shortcut around the building -- it is
# the view catching up with a walk that has already happened and been paid for.
# Somewhere in the room a person could actually stand: not inside a rack, not
# in a delivery, and not in the doorway.
func stand_in(room: int) -> void:
	if player == null or room < 0 or room >= rooms.size():
		return
	_here_room = room                 # already walked: do not charge it twice
	var p := standing_point(room)
	teleport(p)
	# AND YOU LOOK INTO THE ROOM YOU JUST WALKED INTO, because a person does.
	# Landing in a comms cupboard facing the wall you came through is a
	# screenshot of brickwork and a crosshair on nothing.
	var mid := room_centre(room)
	mid.y = p.y + player.EYE
	if Vector2(mid.x - p.x, mid.z - p.z).length() > 0.6:
		aim_at(mid)


func standing_point(room: int) -> Vector3:
	var r: Dictionary = rooms[room]
	var y: float = float(r.floor) * fheight + 0.25
	var mid := room_centre(room)
	var best := Vector3(mid.x, y, mid.z)
	var bestscore := -1.0e9
	var boxes: Array = []
	for d in devices:
		if absf(float(d.mn.y) - float(r.floor) * fheight) > fheight:
			continue
		boxes.append(Rect2(d.mn.x, d.mn.z, d.size.x, d.size.z))
	for k in racks:
		if int(k.floor) != int(r.floor):
			continue
		var w: float = RACK_W if k.along_x else RACK_D
		var dd: float = RACK_D if k.along_x else RACK_W
		boxes.append(Rect2(k.x, k.z, w, dd))
	for x in range(int(r.x0), int(r.x1)):
		for z in range(int(r.y0), int(r.y1)):
			if room_of(int(r.floor), x, z) != room:
				continue
			var c := Vector2(float(x) + 0.5, float(z) + 0.5)
			var clear := 9.0
			for b in boxes:
				clear = min(clear, _point_gap(b, c))
			var score: float = min(clear, 1.2) * 4.0 - c.distance_to(Vector2(mid.x, mid.z)) * 0.2
			if score > bestscore:
				bestscore = score
				best = Vector3(c.x, y, c.y)
	return best


static func _point_gap(b: Rect2, p: Vector2) -> float:
	var dx: float = max(0.0, max(b.position.x - p.x, p.x - b.end.x))
	var dy: float = max(0.0, max(b.position.y - p.y, p.y - b.end.y))
	return sqrt(dx * dx + dy * dy)


func _wire() -> void:
	var w := preload("res://scripts/wire.gd").new()
	w.name = "Wire"
	w.tower = self
	add_child(w)


func wire_port() -> int:
	var w := get_node_or_null("Wire")
	return 0 if w == null else int(w.port)


# ------------------------------------------------------------------ THE KEYS
#
# "esc should exit the debugger/de use mode, tab may be overload for terminal."
#
# TAB BELONGS TO THE TERMINAL. It is a shell and it completes paths, and
# terminal.gd has had real completion in it since before the building existed;
# a key that opened a bag instead was the view stealing a key from the machine.
# So the inventory is on [I] -- and, since the owner asked for it by name,
# ALSO on Tab, handled inside inventory.gd itself so that it steps aside for
# anything with a shell in it: a desk, somebody else's machine, the handset.
# Tab still goes where Tab goes whenever there is something to complete.
#
# AND CABLING IS ON [C], WHICH IS THE KEY THIS WINDOW EXISTS FOR. See
# aim_text(): the crosshair says it at every port and every box, because the
# one sentence that used to mention cabling named a key that did nothing and
# the owner could not find the verb at all. D38.
#
# ESC LEAVES WHATEVER YOU ARE IN, and it is the same key every time: out of the
# desktop, out of the handset, out of the bag. It is handled in _input rather
# than _unhandled_input because a focused terminal consumes every key it is
# given -- which is correct of a terminal, and would leave a player sitting at
# a desk with no way back out of it.
func _input(event: InputEvent) -> void:
	if player == null:
		return
	if not (event is InputEventKey) or not event.pressed or event.is_echo():
		return
	if (event as InputEventKey).keycode != KEY_ESCAPE:
		return
	if report_open() and not run_over:
		_dismiss_report()
	elif bag and bag.visible:
		bag.toggle()
	elif desk_open():
		print(stand_up())
	elif seat_open():
		print(seat_stand())
	elif phone and phone.focused:
		print(phone.let_go())
	else:
		return
	get_viewport().set_input_as_handled()


# Everything else. It is _unhandled_input rather than a poll of the keyboard
# because a poll samples once a frame and drops a key that was tapped between
# two of them -- which is why pressing [F] at a rack sometimes did nothing at
# all -- and because while the desktop or the handset has focus the keys belong
# to whatever is typing.
func _unhandled_input(event: InputEvent) -> void:
	if player == null:
		return
	if desk_open() or seat_open() or (bag and bag.visible) or (phone and phone.focused):
		return
	# THE HANDS. Left and right click use whatever is in the left and right
	# hand, which is inventory.gd's business; what is under the crosshair is
	# this file's. Only while the mouse is captured, so the first click into
	# the window is still the one that grabs the pointer.
	var t := target()
	if event is InputEventMouseButton and event.pressed and player.look_free:
		var mb: InputEventMouseButton = event
		var side := -1
		if mb.button_index == MOUSE_BUTTON_LEFT: side = 0
		elif mb.button_index == MOUSE_BUTTON_RIGHT: side = 1
		if side >= 0 and bag:
			var said: String = bag.use(side, int(t.get("dev", -1)),
				int(t.get("port", -1)))
			if said != "":
				print(said)
			get_viewport().set_input_as_handled()
		return
	if not (event is InputEventKey) or not event.pressed or event.is_echo():
		return
	var k: InputEventKey = event
	var dev: int = int(t.get("dev", -1))
	var said2 := ""
	match k.keycode:
		KEY_I:
			if bag: bag.toggle()
		KEY_E:
			if t.get("kind", "") == "lift":
				var l := _lift_landing()
				said2 = l.call_to(player_floor()) if l != null else ""
			elif t.get("kind", "") == "liftbtn":
				# The button and the number key are two ways in to one act, so
				# they are literally the same call. A player who presses the
				# button and a player who knows about the digit get the same
				# answer, in the same words, on the same sign in the car.
				var car2: Object = lift_in()
				said2 = car2.button_press(int(t.get("floor", -1))) if car2 != null else ""
			else:
				said2 = use_here(dev)
		# THE LEAD GOES IN THROUGH THE SESSION, NOT INTO THE PROP.
		#
		# These two keys moved the handset directly and left core believing
		# nothing was plugged in anywhere -- the exact fault _reconcile_phone()
		# was written to fix, running the other way. `ses_prompt()` still said
		# `f0 MDF>` while the player was typing at a machine, and a socket
		# client asking `look` was told the cart was on the shelf.
		#
		# `plug <box>` and `plug hdmi <box>` are the session's own words for
		# these, so pressing [F] is now the same act as typing it: the same
		# refusals in the same sentences, and the prop follows the session
		# afterwards through the reconciler like everything else does. A box
		# the site model does not own -- a patch panel the view drew -- has no
		# name to type, so that one is still the prop's own answer.
		KEY_F:
			said2 = _lead_in(dev, false)
		# [H] WAS THE DISPLAY LEAD AND IS GONE. "Debugger: display has no
		# point, if something has a display you just use the display.
		# Debugger: serial is the only thing it should be for." He is right,
		# and it is the same argument as the console socket above: a machine
		# with a monitor is used through its monitor -- sit at it -- and a
		# machine without one is used through the serial lead, which is what a
		# service processor is for. `plug hdmi` still exists in the session
		# and still drives the desk screens; what is gone is the pretence
		# that a picture is a debugging tool.
		KEY_U:
			# and out again through core's `unplug`, which is what detach()
			# does: the prop cannot put a lead down the session still holds.
			if phone: said2 = str(phone.detach())
		KEY_C:
			said2 = cable_at(dev, int(t.get("port", -1)))
		KEY_R:
			said2 = drum_next()
		KEY_G:
			said2 = carry_here(dev)
		KEY_O:
			said2 = open_next_floor()
		KEY_N:
			# THE DAY IS A DELIBERATE ACT. Nothing advances the clock but this,
			# and what it does is shown rather than summarised: `day`'s own
			# report, up on the screen until you have read it.
			if report_open():
				_dismiss_report()
			elif not run_over:
				said2 = advance_day()
		_:
			var car: Object = lift_in()
			if car != null and k.keycode >= KEY_0 and k.keycode <= KEY_9:
				said2 = car.go_to(int(k.keycode) - int(KEY_0))
			else:
				return
	if said2 != "":
		print(said2)
	get_viewport().set_input_as_handled()


# [F] and [H], as the session's own `plug`. The device has to be one core
# knows by name; the panels and the customer's rack server are the view's own
# scenery and there is nothing in the session to plug into, so the prop
# answers for those and says what it is.
func _lead_in(dev: int, hdmi: bool) -> String:
	if dev < 0 or phone == null:
		return ""
	# THE LEAD GOES IN THE CONSOLE SOCKET AND NOWHERE ELSE. Until now it went
	# into the box -- which in practice meant it went into whatever the
	# crosshair was on, including the RJ45 the tenant's frames cross. That is
	# the opposite of what a service processor is: a console is out of band, on
	# its own socket, wired to its own chip, and it answers when the machine
	# will not boot. Refusing here, in words, at the ethernet port, is how the
	# shape on the box gets taught.
	if not hdmi and phone.plugged != dev:
		var dk: Dictionary = devices[dev]
		if not dk.get("serial_at", {}).is_empty():
			var aim_now: Dictionary = target()
			if str(aim_now.get("kind", "")) == "port":
				return "%s: that is an ethernet port. The debugger goes in the console socket -- the wide one, low on the same face." % dk.name
	var s: int = int(devices[dev].get("site", -1))
	if s < 0:
		return str(phone.plug(dev, "hdmi" if hdmi else "serial"))
	var out: String = site("plug %s%s"
		% ["hdmi " if hdmi else "", _cable_name(s)]).strip_edges()
	_reconcile_phone()
	return out


func _hand_name(side: int) -> String:
	var h: String = str(bag.hand(side))
	if h == "": return "empty"
	if h == "box": return "both on the box"
	return str(bag.KIT[h].label)


func _process(_dt: float) -> void:
	if player == null:
		return
	if desk_open() or seat_open():
		return                 # the world waits while you are sitting at it
	if minimap != null and is_instance_valid(minimap):
		minimap.show_floor(player_floor(), map_rows(),
			Vector2(player.global_position.x, player.global_position.z),
			player.rotation.y)
	# THE CLIPBOARD, RE-READ WHEN SOMETHING HAPPENED AND NOT OTHERWISE. Four
	# session verbs is nothing once, and a great deal sixty times a second.
	if _snap_dirty:
		_snapshot()
	# IT IS IN YOUR HAND WHEN IT IS THE EQUIPPED ITEM, and nowhere otherwise.
	# The crash cart appeared out of the air the moment a lead went in; a
	# handset you have dragged into a slot is a handset you are holding, and it
	# comes up to your face when the lead goes in.
	if phone and bag:
		var h0: String = str(bag.hand(0))
		var h1: String = str(bag.hand(1))
		phone.visible = h0 == "serial" or h0 == "display" \
			or h1 == "serial" or h1 == "display"
	# The cable trailing out of the port you plugged into, to your hands --
	# and the metres of it already lying on the floor where you walked.
	_lay_crumb()
	_draw_trail()
	if bag and bag.visible:
		if reticle:
			reticle.show_target("", "", false, false)
		return                 # and while you are looking in the bag
	var t := target()
	var dev: int = int(t.get("dev", -1))
	var car: Object = lift_in()
	if reticle:
		var nm := aim_text(t)
		# THE DOT IS BRIGHT WHEN THE RAY IS REALLY ON IT. What the label says
		# is always what the key will do -- the fallback to whatever is within
		# arm's reach still names itself -- but the dot is the honest report of
		# where the ray landed, which is the thing the owner was missing.
		var on: bool = not t.get("far", false) and not t.is_empty()
		# AND IT IS GREEN WHEN THE NEXT PRESS RUNS COPPER. The accent used to
		# mean "a spool is in your hand", which was the state of the inventory
		# rather than the state of the building -- and [C] runs a cable with
		# both hands empty, so the one press that spends money was the one the
		# dot said nothing about.
		reticle.show_target(nm[0], nm[1], on, would_cable(t))
	_hud_paint()
	_ledger_paint()
	# WHAT YOU ARE CARRYING IS IN THE ROOM YOU ARE IN, at every step of the
	# walk -- not in an inventory that resolves when you put it down. The
	# site is told the moment you cross the threshold, so `show` from a
	# terminal in the middle of a carry says where the box really is.
	var r := player_room()
	if r != NOROOM and r != _here_room:
		_be_here(r)


func _cable_name(s: int) -> String:
	for d in site_devs():
		if int(d.i) == s:
			return str(d.name)
	return "#%d" % s


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

