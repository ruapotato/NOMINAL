# lift.gd — the lift, which is how the tower grows.
#
# The generator puts a shaft at the same x,y on every floor it passes through
# (RM_LIFT), and until now the renderer filled it in: a sealed column of slabs
# with a door onto it that opened into brickwork. The stairs were the only way
# up, and "an elevator should be the thing that takes you to new floors" was
# the owner's whole point about how a building grows.
#
# So the shaft is hollowed out -- tower.gd punches every slab inside it, the
# same subtraction the stairwell already uses -- and this puts a car in it.
#
# WHY THE LANDING DOORS ARE REAL BODIES. A hollow shaft with an open hole on
# every floor is a hole a walking body falls down, from every floor, forever.
# The panels are solid when they are shut and they are only ever open on the
# floor the car is actually standing at, which is also exactly what a real one
# does and for the same reason.
#
# WHY THE PLAYER IS CARRIED EXPLICITLY. Godot will carry a CharacterBody3D on
# an AnimatableBody3D, sometimes, and the failure mode is that the floor slides
# out from under you halfway up a shaft. A lift that drops you is worse than no
# lift, and this has to be assertable from a headless test, so the car moves
# the body it is carrying by the same delta it moved itself.

extends Node3D

const CAR_H := 2.35        # inside height of the car
const SPEED := 2.0         # metres a second, which is a slow goods lift
const DOOR_W := 0.5        # each of two panels
const DOOR_H := 2.05
const DOOR_SPEED := 2.4    # open or shut in a bit under half a second

const CAR_COL := Color("#5b6068")
const CAR_FLOOR := Color("#3e434a")
const DOOR_COL := Color("#8e949c")
const PANEL_COL := Color("#2a2e34")
const LIT := Color("#ffd27a")
const UNLIT := Color("#3a3f47")

var tower: Node3D = null
var rect := Rect2()             # the shaft footprint, in metres
var floors: Array = []          # the floors this shaft passes through, sorted
var face := 0                   # 0 = +x, 1 = -x, 2 = +z, 3 = -z
var door_c := 0.0               # centre of the doorway, along the other axis

var at := 0                     # the floor the car is standing at
var target := 0
var car_y := 0.0                # the top of the car floor, in metres
var open := 0.0                 # 0 shut, 1 wide open
var moving := false

var _car: Node3D = null
var _panels: Array = []         # per floor: {left, right, y}
var _inside_panel: Node3D = null
var _readout: Label3D = null
var _fh := 3.0


# rooms: the K_LIFT room ids that share this footprint, any order.
func setup(t: Node3D, room_ids: Array) -> bool:
	tower = t
	_fh = t.fheight
	var first: Dictionary = t.rooms[room_ids[0]]
	rect = Rect2(first.x0, first.y0, first.x1 - first.x0, first.y1 - first.y0)
	floors.clear()
	for i in room_ids:
		floors.append(int(t.rooms[i].floor))
	floors.sort()
	if not _find_door(t, room_ids):
		return false          # a shaft nobody can get into is not a lift
	at = floors[0]
	target = at
	car_y = float(at) * _fh
	_build_car()
	_build_landing_doors()
	_build_readout()
	rebuild_panels()
	return true


# The doorway is a door EDGE the generator already wrote, so the car opens onto
# a hole that is really there rather than one this file decided to cut.
func _find_door(t: Node3D, room_ids: Array) -> bool:
	for d in t.doors:
		for i in room_ids:
			if d.a != i and d.b != i:
				continue
			var lift_is_a: bool = (d.a == i)
			if d.dir == 0:
				face = 0 if lift_is_a else 1
				door_c = float(d.y) + 0.5
			else:
				face = 2 if lift_is_a else 3
				door_c = float(d.x) + 0.5
			return true
	return false


# The plane of the wall the doors sit in, and the outward direction through it.
func _wall_pos() -> float:
	match face:
		0: return rect.end.x
		1: return rect.position.x
		2: return rect.end.y
		_: return rect.position.y


func _outward() -> float:
	return 1.0 if (face == 0 or face == 2) else -1.0


func _is_x() -> bool:
	return face <= 1


# ---------------------------------------------------------------- the car

func _build_car() -> void:
	var g = preload("res://scripts/vgeo.gd").new()
	var w := 0.07                              # wall thickness
	# The car fills the shaft but stops short of the walls, and runs right up
	# to the door plane so that stepping through the landing door is stepping
	# into the car rather than into a 100 mm gap over a drop.
	var mn := Vector3(rect.position.x + 0.06, -0.10, rect.position.y + 0.06)
	var mx := Vector3(rect.end.x - 0.06, CAR_H, rect.end.y - 0.06)
	if face == 0: mx.x = rect.end.x
	elif face == 1: mn.x = rect.position.x
	elif face == 2: mx.z = rect.end.y
	else: mn.z = rect.position.y
	var sz := mx - mn
	g.box(Vector3(mn.x, -0.10, mn.z), Vector3(sz.x, 0.10, sz.z), CAR_FLOOR)
	g.box(Vector3(mn.x, CAR_H - 0.06, mn.z), Vector3(sz.x, 0.06, sz.z), CAR_COL)
	# three walls and an opening. The opening is the face the doors are on.
	if face != 1: g.box(Vector3(mn.x, 0, mn.z), Vector3(w, CAR_H, sz.z), CAR_COL)
	if face != 0: g.box(Vector3(mx.x - w, 0, mn.z), Vector3(w, CAR_H, sz.z), CAR_COL)
	if face != 3: g.box(Vector3(mn.x, 0, mn.z), Vector3(sz.x, CAR_H, w), CAR_COL)
	if face != 2: g.box(Vector3(mn.x, 0, mx.z - w), Vector3(sz.x, CAR_H, w), CAR_COL)
	# a light in the ceiling, which is the only reason you can tell it apart
	# from the inside of the shaft
	var lm := Vector3(mn.x + sz.x * 0.25, CAR_H - 0.09, mn.z + sz.z * 0.25)
	g.box(lm, Vector3(sz.x * 0.5, 0.03, sz.z * 0.5), Color("#fff4d8"), false)
	# a handrail down each wall, at the height a handrail is at
	for s in [0, 1]:
		var hm: Vector3
		var hs: Vector3
		if _is_x():
			hm = Vector3(mn.x + 0.05, 0.90, mn.z + (0.02 if s == 0 else sz.z - 0.06))
			hs = Vector3(sz.x - 0.10, 0.05, 0.04)
		else:
			hm = Vector3(mn.x + (0.02 if s == 0 else sz.x - 0.06), 0.90, mn.z + 0.05)
			hs = Vector3(0.04, 0.05, sz.z - 0.10)
		g.box(hm, hs, Color("#9aa1a9"), false)
	_car = g.node("Car")
	_car.position = Vector3(0, car_y, 0)
	add_child(_car)


func car_centre() -> Vector3:
	return Vector3(rect.position.x + rect.size.x * 0.5, car_y + 0.15,
		rect.position.y + rect.size.y * 0.5)


func inside(p: Vector3) -> bool:
	if p.x < rect.position.x - 0.1 or p.x > rect.end.x + 0.1: return false
	if p.z < rect.position.y - 0.1 or p.z > rect.end.y + 0.1: return false
	return p.y > car_y - 0.6 and p.y < car_y + CAR_H


# ------------------------------------------------------- the landing doors

func _door_panel(nm: String, y: float, sign: float) -> Node3D:
	var g = preload("res://scripts/vgeo.gd").new()
	var t := 0.10
	var wall := _wall_pos() - t * 0.5
	var lo := door_c if sign > 0 else door_c - DOOR_W
	if _is_x():
		g.box(Vector3(wall, 0, lo), Vector3(t, DOOR_H, DOOR_W), DOOR_COL)
	else:
		g.box(Vector3(lo, 0, wall), Vector3(DOOR_W, DOOR_H, t), DOOR_COL)
	var n := g.node(nm)
	n.position = Vector3(0, y, 0)
	return n


func _build_landing_doors() -> void:
	_panels.clear()
	for f in floors:
		var y := float(f) * _fh
		var l := _door_panel("door%d_l" % f, y, -1.0)
		var r := _door_panel("door%d_r" % f, y, 1.0)
		add_child(l)
		add_child(r)
		_panels.append({"f": f, "l": l, "r": r})
	_apply_doors()


func _apply_doors() -> void:
	for p in _panels:
		var slide: float = (DOOR_W + 0.02) * (open if p.f == at else 0.0)
		var y: float = float(p.f) * _fh
		if _is_x():
			p.l.position = Vector3(0, y, -slide)
			p.r.position = Vector3(0, y, slide)
		else:
			p.l.position = Vector3(-slide, y, 0)
			p.r.position = Vector3(slide, y, 0)
		# A panel that is out of the way must not still be a wall you walk into.
		var shut: bool = (p.f != at) or open < 0.85
		_set_collide(p.l, shut)
		_set_collide(p.r, shut)


func _set_collide(n: Node3D, on: bool) -> void:
	var b := n.get_node_or_null("body")
	if b:
		(b as StaticBody3D).process_mode = Node.PROCESS_MODE_INHERIT
		(b as CollisionObject3D).collision_layer = 1 if on else 0
		for c in b.get_children():
			if c is CollisionShape3D:
				c.disabled = not on


# ----------------------------------------------------------- the two panels
#
# A call plate on the wall outside, and inside the car a column of buttons --
# one per floor the shaft passes, lit for the floors that are IN SERVICE. An
# unlit button is the honest way to say a floor exists and is not open yet.

func rebuild_panels() -> void:
	if _inside_panel:
		_inside_panel.queue_free()
		_inside_panel = null
	var g = preload("res://scripts/vgeo.gd").new()
	var out := _outward()
	var wall := _wall_pos()
	# --- the plate inside the car, beside the opening
	var d := 0.03
	var side := door_c + 0.75
	if side > (rect.end.y if _is_x() else rect.end.x) - 0.25:
		side = door_c - 0.75
	var px: float
	var pz: float
	if _is_x():
		px = wall - out * 0.09
		pz = side
	else:
		px = side
		pz = wall - out * 0.09
	g.box(Vector3(px - (d if _is_x() else 0.13), 0.85, pz - (0.13 if _is_x() else d)),
		Vector3((d * 2 if _is_x() else 0.26), 0.90, (0.26 if _is_x() else d * 2)),
		PANEL_COL, false)
	var n := floors.size()
	for i in range(n):
		var f: int = floors[n - 1 - i]      # top floor at the top of the panel
		var col: Color = LIT if tower.in_service(f) else UNLIT
		var by := 1.62 - float(i) * 0.115
		var bs := 0.075
		if _is_x():
			g.box(Vector3(px - out * 0.035, by, pz - bs * 0.5), Vector3(0.02, bs, bs), col, false)
		else:
			g.box(Vector3(px - bs * 0.5, by, pz - out * 0.035), Vector3(bs, bs, 0.02), col, false)
	_inside_panel = g.node("InsidePanel")
	_inside_panel.position = Vector3(0, car_y, 0)
	add_child(_inside_panel)
	# --- the call plates, one per landing, on the lobby side of the wall
	if not has_node("CallPlates"):
		var cg = preload("res://scripts/vgeo.gd").new()
		for f in floors:
			var y := float(f) * _fh
			var cx: float
			var cz: float
			if _is_x():
				cx = wall + out * 0.06
				cz = door_c + 0.80
			else:
				cx = door_c + 0.80
				cz = wall + out * 0.06
			cg.box(Vector3(cx - (0.02 if _is_x() else 0.08), y + 0.95, cz - (0.08 if _is_x() else 0.02)),
				Vector3((0.04 if _is_x() else 0.16), 0.30, (0.16 if _is_x() else 0.04)),
				PANEL_COL, false)
			cg.box(Vector3(cx + out * (0.02 if _is_x() else 0.0) - (0.0 if _is_x() else 0.035), y + 1.14, cz + out * (0.0 if _is_x() else 0.02) - (0.035 if _is_x() else 0.0)),
				Vector3((0.015 if _is_x() else 0.07), 0.07, (0.07 if _is_x() else 0.015)), LIT, false)
		var cn := cg.node("CallPlates")
		add_child(cn)


func call_plate_pos(f: int) -> Vector3:
	var out := _outward()
	var wall := _wall_pos()
	if _is_x():
		return Vector3(wall + out * 0.4, float(f) * _fh + 1.1, door_c + 0.80)
	return Vector3(door_c + 0.80, float(f) * _fh + 1.1, wall + out * 0.4)


func _build_readout() -> void:
	_readout = Label3D.new()
	_readout.font = preload("res://scripts/uifont.gd").mono()
	_readout.font_size = 96
	_readout.pixel_size = 0.0022
	_readout.modulate = Color("#ffd27a")
	_readout.outline_size = 0
	_readout.billboard = BaseMaterial3D.BILLBOARD_DISABLED
	_readout.no_depth_test = false
	_readout.double_sided = true
	add_child(_readout)
	_place_readout()


func _place_readout() -> void:
	if _readout == null:
		return
	var out := _outward()
	var wall := _wall_pos()
	# above the doors on the landing the car is at, because that is the sign a
	# person actually looks at
	if _is_x():
		_readout.position = Vector3(wall + out * 0.08, float(at) * _fh + 2.30, door_c)
		_readout.rotation = Vector3(0, PI * 0.5 * out, 0)
	else:
		_readout.position = Vector3(door_c, float(at) * _fh + 2.30, wall + out * 0.08)
		_readout.rotation = Vector3(0, 0 if out > 0 else PI, 0)
	_readout.text = "%d" % at


# ------------------------------------------------------------- the working

func busy() -> bool:
	return moving or target != at


func serviced() -> Array:
	var out: Array = []
	for f in floors:
		if tower.in_service(f):
			out.append(f)
	return out


# Send it somewhere. Refuses a floor that is not in service, which is the point
# of a floor not being in service.
func go_to(f: int) -> String:
	if not floors.has(f):
		return "this lift does not pass floor %d." % f
	if not tower.in_service(f):
		return "floor %d is not in service. The button is not lit." % f
	target = f
	return "floor %d." % f


func call_to(f: int) -> String:
	return go_to(f)


func _physics_process(dt: float) -> void:
	var want_open := (target == at)
	if want_open:
		if open < 1.0:
			open = min(1.0, open + DOOR_SPEED * dt)
			_apply_doors()
		moving = false
		return
	# shut the doors before going anywhere
	if open > 0.0:
		open = max(0.0, open - DOOR_SPEED * dt)
		_apply_doors()
		return
	moving = true
	var goal := float(target) * _fh
	var dy := clampf(goal - car_y, -SPEED * dt, SPEED * dt)
	var carrying := tower.player != null and inside(tower.player.global_position)
	car_y += dy
	_car.position = Vector3(0, car_y, 0)
	if _inside_panel:
		_inside_panel.position = Vector3(0, car_y, 0)
	if carrying:
		# The body goes exactly where the floor under it went.
		tower.player.global_position += Vector3(0, dy, 0)
		tower.player.velocity.y = 0.0
	if absf(goal - car_y) < 0.001:
		car_y = goal
		_car.position = Vector3(0, car_y, 0)
		if _inside_panel:
			_inside_panel.position = Vector3(0, car_y, 0)
		at = target
		moving = false
		_place_readout()
		_apply_doors()
