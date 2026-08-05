# The building, checked without a window.
#
# A blind playtester cannot navigate 3D from screenshots, so everything the
# 3D shell can do has to be checkable here: the geometry is non-empty, the
# player spawns standing on a floor and not inside a wall, the stairs actually
# carry a walking body from one floor to the next, and the crash cart's two
# leads behave the way the hardware they model behaves.
#
# The stair check is a PHYSICS check, not an arithmetic one. Steps that a
# capsule cannot climb look perfectly correct in the data and leave the
# building as a stack of disconnected slabs, which is the exact failure this
# file exists to catch.
extends SceneTree

var bad := 0

func fail(s: String) -> void:
	print("  FAIL: " + s)
	bad += 1

func ok(s: String) -> void:
	print("  ok   " + s)


func _init() -> void:
	var t: Node3D = preload("res://scripts/tower.gd").new()
	t.with_desktop = false            # the desktop is smoke.gd's job
	t.seed_no = 200
	root.add_child(t)
	await process_frame
	await process_frame

	print("tower: seed %d, %d floors, %d x %d m plate, %d rooms, %d doors"
		% [t.seed_no, t.nfloors, t.bw, t.bh, t.rooms.size(), t.doors.size()])

	# ---- geometry exists at all
	if t.nfloors < 2: fail("a one-floor tower has nothing to climb")
	if t.rooms.size() < 10: fail("only %d rooms" % t.rooms.size())
	if t.triangle_count() < 1000:
		fail("the mesh has %d triangles -- nothing was built" % t.triangle_count())
	else:
		ok("%d triangles of geometry, %d floors" % [t.triangle_count(), t.nfloors])

	# ---- every floor has a slab under it and walls on it
	for f in range(t.nfloors):
		var n := 0
		for r in t.rooms:
			if r.floor == f: n += 1
		if n == 0: fail("floor %d has no rooms" % f)

	# ---- the spawn is inside a room, on the ground floor, not in a wall
	var sp: Vector3 = t.spawn_point()
	var rid: int = t.room_of(0, int(floor(sp.x)), int(floor(sp.z)))
	if rid == t.NOROOM:
		fail("the spawn point is outside the building plate")
	else:
		ok("spawn in the %s at (%.0f, %.0f)" % [t.rooms[rid].name, sp.x, sp.z])
	await process_frame
	if t.player == null:
		fail("no player was spawned")
		quit(1)
		return
	# stand still for a moment: a body inside a wall or over a hole moves.
	t.teleport(sp + Vector3(0, 0.3, 0))
	for i in range(30):
		await process_frame
	var rest: Vector3 = t.player.global_position
	if absf(rest.y) > 0.5:
		fail("the player did not come to rest on the lobby slab (y = %.2f)" % rest.y)
	elif rest.distance_to(sp) > 1.2:
		fail("the player was pushed %.1f m from the spawn -- it is inside something"
			% rest.distance_to(sp))
	else:
		ok("stands on the ground floor, y = %.2f" % rest.y)

	# ---- the stairs carry a body up, every floor, under real physics
	for s in t.stairs:
		var f: int = s.floor
		var start := _foot_of(t, s)
		t.teleport(start)
		t.player.drive_active = true
		t.player.drive = Vector2(0, 1)
		t.player.look_at_yaw(_yaw_of(s))
		var climbed := false
		for i in range(800):
			await process_frame
			if t.player.global_position.y > (f + 1) * t.fheight - 0.35:
				climbed = true
				break
		t.player.drive_active = false
		t.player.drive = Vector2.ZERO
		if climbed:
			ok("walked up the stairs from floor %d to %d" % [f, f + 1])
		else:
			fail("could not climb from floor %d: stopped at y = %.2f (want %.2f)"
				% [f, t.player.global_position.y, (f + 1) * t.fheight])

	# ---- a route exists from the lobby to a comms cupboard upstairs, as the
	# building itself computes it, not as the renderer guesses
	var lobby: int = t.find_room(0, t.K_LOBBY)
	if lobby < 0: lobby = t.find_room(0, t.K_GOODS)
	var dist: PackedFloat32Array = t.walk_from(lobby)
	var reached := 0
	for f in range(1, t.nfloors):
		var c: int = t.find_room(f, t.K_COMMS)
		if c < 0: continue
		if dist[c] < 0.0:
			fail("no walking route from the lobby to the comms cupboard on floor %d" % f)
		else:
			reached += 1
			if reached == 1:
				ok("lobby -> comms on floor %d is %.0f m on foot" % [f, dist[c]])
	if reached == 0: fail("there is no comms cupboard above the ground floor")

	# ---- doors are gaps you can get through: every door edge must be clear
	var blocked := 0
	for d in t.doors:
		var key := "%d,%d,%d,%d" % [d.floor, d.x, d.y, d.dir]
		if not t.doorset.has(key): blocked += 1
	if blocked: fail("%d doors did not make it into the wall pass" % blocked)
	else: ok("%d doorways, all of them openings" % t.doors.size())

	# ---- THE CRASH CART. The leads are the lesson: serial talks to a machine
	# that never booted, HDMI does not, and a patch panel has no ports.
	var cart: Node3D = t.cart
	if cart == null:
		fail("no crash cart")
	else:
		var ws := _device(t, "workstation")
		var srv := _device(t, "rack server")
		var pp := _device(t, "patch panel")
		if ws < 0 or srv < 0 or pp < 0:
			fail("expected a workstation, a rack server and a patch panel; got %d devices"
				% t.devices.size())
		else:
			var m: String = cart.plug(pp, "serial")
			if m.find("passive") < 0: fail("the patch panel pretended to have a console: " + m)
			else: ok("patch panel refuses a serial lead: " + m)

			m = cart.plug(srv, "hdmi")
			if m.find("no display output") < 0:
				fail("a rack server produced a picture: " + m)
			else: ok("rack server has no display output: " + m)

			m = cart.plug(srv, "serial")
			var screen: String = cart.screen_text()
			if screen.strip_edges() == "":
				fail("the serial lead said nothing at all")
			elif t.machine.booted():
				if screen.find("nominal") < 0 and screen.find("boot") < 0:
					fail("serial console on a running machine printed no boot log")
				else: ok("serial: the real boot log, %d lines" % screen.split("\n").size())
			else:
				if screen.find("boot stopped at") < 0:
					fail("a machine that did not boot did not say where it stopped")
				else: ok("serial: the boot stopped where it really stopped")
			# and a command on the end of it goes through the same sh_on() the
			# desktop terminal uses
			var out: String = cart.type_line("uname -a")
			if out.strip_edges() == "":
				fail("`uname -a` down the serial lead said nothing")
			else:
				ok("serial `uname -a`: " + out.split("\n")[0].substr(0, 60))
			m = cart.plug(ws, "hdmi")
			if m.find("display on") < 0: fail("the workstation would not drive a screen: " + m)
			else: ok("HDMI on the workstation: " + m)

	print("tower: %d failures" % bad)
	quit(1 if bad else 0)


func _device(t: Node3D, want: String) -> int:
	for i in range(t.devices.size()):
		if t.devices[i].name == want: return i
	return -1


# Standing on the bottom step, facing up the run. Not BEHIND it: the foot of
# an even-numbered run is against the stairwell wall, and a teleport that
# overshoots by half a metre puts the test inside brickwork and blames the
# stairs for it.
func _foot_of(t: Node3D, s: Dictionary) -> Vector3:
	var c: float = (s.c0 + s.c1) * 0.5
	var back: float = s.a + (0.35 if s.b > s.a else -0.35)
	var y: float = s.floor * t.fheight + 0.45
	if s.axis == 1:
		return Vector3(c, y, back)
	return Vector3(back, y, c)


# Facing up the run. -Z is forward for a Godot camera, so a run heading +Z
# needs a yaw of PI.
func _yaw_of(s: Dictionary) -> float:
	if s.axis == 1:
		return PI if s.b > s.a else 0.0
	return -PI * 0.5 if s.b > s.a else PI * 0.5
