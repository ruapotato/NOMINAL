# people.gd — the desks a tenancy rents, and the person sitting at each one.
#
# The owner: "let's also add in the virtual people to actually be in their
# office at a computer desk similar to the server room, where if you felt like
# it you could go over to their desk and see what issues they're complaining
# about... basically let's make the world feel alive."
#
# NOTHING IN THIS FILE KNOWS ANYTHING. It is handed a list of seats -- a point,
# a yaw and a mood -- and it draws them. Where a desk is comes out of the same
# `_tenant_desk_slot()` that puts the tenant's computer there, which comes out
# of site_devs(); whether somebody is unhappy comes out of `service`'s own
# columns. A person cannot exist here that the model does not have a desk for,
# because there is no list of people anywhere: rebuild() is given one and
# throws the last one away.
#
# AND IT IS ONE MULTIMESH PER POSE PER FLOOR. A full tower is 176 desks; 176
# nodes with a mesh and a collider each is what the racks already cost and it
# is not what a crowd should cost. Five meshes are built once -- the desk and
# four poses -- and every person in the building is an entry in a transform
# buffer that shares them.
#
# PER FLOOR, and that part is measured rather than tidy. One buffer for the
# whole tower has one AABB the size of the tower, and a MultiMesh is culled by
# its buffer: standing in the MDF with eighty desks four floors above cost
# 12.8 ms a frame of geometry nobody could see. Split by floor, the floors you
# are not on fall out of the frustum and the same view costs 0.4 ms. It is the
# same five draw calls per floor either way.
#
# No collision on any of it, deliberately. A person is not a wall: the
# doorways, the aisles and the rack fronts have to stay walkable, and the
# cheapest way to guarantee that is geometry that has no body in it at all.
# game/tests/tower.gd checks the seats against the doorways anyway, because
# "you cannot walk into it" is not the same as "it is not standing in the way".

extends Node3D

# The moods, which are not invented here either -- see tower.gd's _seats().
const M_WORKING := 0        # this desk has an address: somebody is doing a day's work
const M_WAITING := 1        # no address on it, and the tenancy is not striking yet
const M_WAITING_BAD := 2    # no address, and it has cost them days
const M_SLUMPED := 3        # addressed, and the tenancy is striking anyway
const NMOOD := 4

const SKIN := Color("#c69a76")
const HAIR := Color("#4a3b30")
const TROUSER := Color("#3a4048")
const SHOE := Color("#23262b")
const CHAIR := Color("#3a4048")
const CHAIR_DK := Color("#2b2f35")
# Blue is a day's work. Amber is the colour the door beacon already uses for a
# tenancy waiting on you, and red the one it uses when they have gone without.
const SHIRT := [Color("#5f7f9e"), Color("#d9a23c"), Color("#c2543c"), Color("#c2543c")]

const DESK_TOP := Color("#9c8f79")
const DESK_LEG := Color("#5b6068")
const MON_SHELL := Color("#1b1e22")
const MON_GLASS := Color("#12333f")

const H := 0.72             # the desk top

# Where the tenant's computer goes, in the desk's own frame: under the top, on
# the left, its back to the wall side so the patch lead rises BEHIND the desk
# instead of through it. tower.gd puts the device box here; the numbers live in
# this file because they are part of the same piece of furniture.
const BOX_X0 := -0.38
const BOX_X1 := 0.02
const BOX_Z0 := -0.42
const BOX_Z1 := -0.02
const BOX_H := 0.42

const SEAT_X := 0.12        # the chair, and everything above it, sits right of the box

var _mesh: Array = []       # the five meshes, built once and shared
var _mm := {}               # floor -> [MultiMeshInstance3D, one per mesh]


func _ready() -> void:
	_make()


func _make() -> void:
	if not _mesh.is_empty():
		return
	_mesh.append(_desk_mesh())
	for m in range(NMOOD):
		_mesh.append(_person_mesh(m))


func _floor_row(f: int) -> Array:
	if _mm.has(f):
		return _mm[f]
	var row: Array = []
	for i in range(_mesh.size()):
		var mmi := MultiMeshInstance3D.new()
		mmi.name = "f%d_%s" % [f, "desks" if i == 0 else "mood%d" % (i - 1)]
		var mm := MultiMesh.new()
		mm.transform_format = MultiMesh.TRANSFORM_3D
		mm.mesh = _mesh[i]
		mm.instance_count = 0
		mmi.multimesh = mm
		add_child(mmi)
		row.append(mmi)
	_mm[f] = row
	return row


# THE ONE ENTRY POINT. `seats` is [{pos, yaw, mood, floor}, ...], and after
# this call the world holds exactly those people and no others.
func rebuild(seats: Array) -> void:
	_make()
	var want := {}
	for s in seats:
		var f: int = int(s.get("floor", 0))
		var m: int = clampi(int(s.get("mood", 0)), 0, NMOOD - 1)
		var t := Transform3D(Basis(Vector3.UP, float(s.get("yaw", 0.0))),
			s.get("pos", Vector3.ZERO))
		if not want.has(f):
			var lists: Array = []
			for i in range(_mesh.size()):
				lists.append([])
			want[f] = lists
		want[f][0].append(t)
		want[f][1 + m].append(t)
	for f in _mm.keys():
		if not want.has(f):
			for mmi in _mm[f]:
				_fill(mmi, [])
	for f in want.keys():
		var row := _floor_row(f)
		for i in range(_mesh.size()):
			_fill(row[i], want[f][i])


func _fill(mmi: MultiMeshInstance3D, xs: Array) -> void:
	var mm: MultiMesh = mmi.multimesh
	mm.instance_count = xs.size()
	for i in range(xs.size()):
		mm.set_instance_transform(i, xs[i])
	mmi.visible = xs.size() > 0


# How many people are drawn, by mood -- what a test reads instead of counting
# nodes, because there are no nodes to count.
func counts() -> Array:
	var out: Array = []
	for m in range(NMOOD):
		var n := 0
		for f in _mm.keys():
			n += int((_mm[f][1 + m] as MultiMeshInstance3D).multimesh.instance_count)
		out.append(n)
	return out


func total() -> int:
	var n := 0
	for f in _mm.keys():
		n += int((_mm[f][0] as MultiMeshInstance3D).multimesh.instance_count)
	return n


# How many instance buffers are live, which is the draw-call count the crowd
# adds when every floor of the tower is in the frustum at once.
func buffers() -> int:
	var n := 0
	for f in _mm.keys():
		for mmi in _mm[f]:
			if (mmi as MultiMeshInstance3D).visible:
				n += 1
	return n


# ---------------------------------------------------------------- the desk
#
# A metre of desk in the middle of a square metre of floor, the same silhouette
# as the workstation in the MDF at the scale a hot desk really is: a top, two
# gable ends, a monitor with the screen towards the chair, a keyboard and a
# mouse. The tenant's computer stands under it, which is why the left half is
# clear.
func _desk_mesh() -> ArrayMesh:
	var g = preload("res://scripts/vgeo.gd").new()
	_box(g, -0.47, 0.47, -0.40, 0.14, H - 0.04, H, DESK_TOP)
	for x0 in [-0.47, 0.41]:
		_box(g, x0, x0 + 0.06, -0.36, 0.10, 0.0, H - 0.04, DESK_LEG)
	# monitor: base, stem, shell, and the glass a shade proud of it
	var mx := SEAT_X
	_box(g, mx - 0.11, mx + 0.11, -0.36, -0.22, H, H + 0.02, MON_SHELL)
	_box(g, mx - 0.03, mx + 0.03, -0.32, -0.27, H + 0.02, H + 0.18, MON_SHELL)
	_box(g, mx - 0.23, mx + 0.23, -0.33, -0.28, H + 0.18, H + 0.50, MON_SHELL)
	_box(g, mx - 0.21, mx + 0.21, -0.279, -0.273, H + 0.20, H + 0.48, MON_GLASS)
	_box(g, mx + 0.18, mx + 0.20, -0.277, -0.271, H + 0.185, H + 0.195, Color("#7fe08a"))
	_box(g, mx - 0.17, mx + 0.17, -0.10, 0.04, H, H + 0.022, Color("#d5d2c8"))
	_box(g, mx + 0.23, mx + 0.30, -0.08, 0.03, H, H + 0.03, Color("#c8c5bc"))
	return g.mesh()


# --------------------------------------------------------------- the person
#
# Seated, facing the screen, in the same vertex-coloured boxes everything else
# in this building is made of. The three poses are the same body with different
# arms and a different neck, so a room of them reads as one workforce.
func _person_mesh(mood: int) -> ArrayMesh:
	var g = preload("res://scripts/vgeo.gd").new()
	var s := SEAT_X
	var shirt: Color = SHIRT[mood]
	# the chair, which is the same chair whatever kind of day they are having
	_box(g, s - 0.22, s + 0.22, 0.20, 0.62, 0.42, 0.48, CHAIR)
	_box(g, s - 0.21, s + 0.21, 0.56, 0.60, 0.48, 0.98, CHAIR)
	_box(g, s - 0.04, s + 0.04, 0.37, 0.45, 0.06, 0.42, CHAIR_DK)
	_box(g, s - 0.24, s + 0.24, 0.39, 0.43, 0.02, 0.06, CHAIR_DK)
	_box(g, s - 0.02, s + 0.02, 0.17, 0.65, 0.02, 0.06, CHAIR_DK)
	# legs: thighs along the seat, shins down to the floor, shoes on the end
	_box(g, s - 0.16, s + 0.16, 0.12, 0.44, 0.46, 0.56, TROUSER)
	for x0 in [s - 0.15, s + 0.03]:
		_box(g, x0, x0 + 0.12, 0.12, 0.24, 0.06, 0.50, TROUSER)
		_box(g, x0, x0 + 0.12, 0.02, 0.18, 0.0, 0.07, SHOE)

	if mood == M_SLUMPED:
		# HEAD IN HANDS, ELBOWS ON THE DESK. Nothing floats above this office
		# and no icon says so: the room is bent over its desks, and that is
		# what a bad week looks like from the doorway.
		_box(g, s - 0.17, s + 0.17, 0.16, 0.44, 0.46, 0.90, shirt)
		_box(g, s - 0.05, s + 0.05, 0.10, 0.20, 0.86, 0.94, SKIN)
		_box(g, s - 0.10, s + 0.10, -0.02, 0.16, 0.88, 1.08, SKIN)
		_box(g, s - 0.105, s + 0.105, -0.025, 0.165, 1.03, 1.11, HAIR)
		for x0 in [s - 0.26, s + 0.16]:
			# upper arm forward along the desk, forearm up to the head
			_box(g, x0, x0 + 0.10, 0.10, 0.40, H + 0.02, H + 0.10, shirt)
			_box(g, x0 + 0.01, x0 + 0.09, 0.06, 0.16, H + 0.02, 0.98, shirt)
			_box(g, x0 + 0.01, x0 + 0.09, 0.02, 0.14, 0.98, 1.10, SKIN)
		return g.mesh()

	# upright: torso, neck, head, hair
	_box(g, s - 0.17, s + 0.17, 0.24, 0.46, 0.46, 1.02, shirt)
	_box(g, s - 0.05, s + 0.05, 0.32, 0.40, 1.00, 1.09, SKIN)
	_box(g, s - 0.10, s + 0.10, 0.26, 0.45, 1.09, 1.31, SKIN)
	_box(g, s - 0.105, s + 0.105, 0.255, 0.455, 1.25, 1.33, HAIR)
	# left arm on the keyboard in every upright pose
	_box(g, s - 0.25, s + 0.25, 0.30, 0.42, 0.86, 0.98, shirt)      # shoulders
	_box(g, s - 0.25, s - 0.17, 0.30, 0.42, 0.74, 0.90, shirt)
	_box(g, s - 0.24, s - 0.16, -0.04, 0.34, H + 0.02, H + 0.10, shirt)
	_box(g, s - 0.23, s - 0.15, -0.10, -0.02, H + 0.02, H + 0.09, SKIN)
	if mood == M_WORKING:
		_box(g, s + 0.17, s + 0.25, 0.30, 0.42, 0.74, 0.90, shirt)
		_box(g, s + 0.16, s + 0.24, -0.04, 0.34, H + 0.02, H + 0.10, shirt)
		_box(g, s + 0.15, s + 0.23, -0.10, -0.02, H + 0.02, H + 0.09, SKIN)
		return g.mesh()
	# A HAND UP. The one gesture that is legible across a room and in a
	# screenshot, and it means exactly what it means: this desk cannot work
	# and somebody is waiting for the IT department.
	_box(g, s + 0.17, s + 0.25, 0.30, 0.42, 0.78, 1.00, shirt)
	_box(g, s + 0.17, s + 0.25, 0.30, 0.40, 1.00, 1.58, shirt)
	_box(g, s + 0.16, s + 0.26, 0.29, 0.41, 1.58, 1.76, SKIN)
	return g.mesh()


func _box(g, x0: float, x1: float, z0: float, z1: float, y0: float, y1: float,
		col: Color) -> void:
	g.box(Vector3(x0, y0, z0), Vector3(x1 - x0, y1 - y0, z1 - z0), col, false)
