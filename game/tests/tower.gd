# The building, checked without a window.
#
# A blind playtester cannot navigate 3D from screenshots, so everything the
# 3D shell can do has to be checkable here: the geometry is non-empty, the
# player spawns standing on a floor and not inside a wall, the stairs actually
# carry a walking body from one floor to the next, and the mobile debugger's two
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

	# ---- THE SPAWN IS THE MDF, and there is a rack in it to face.
	if t.rooms[rid].kind != t.K_MDF:
		fail("the day starts in the %s, not the main frame room" % t.rooms[rid].name)
	else:
		ok("the day starts in the MDF")
	var mdf: int = t.find_room(0, t.K_MDF)
	if t.racks_in(mdf).is_empty():
		fail("the MDF has no racks in it")
	else:
		ok("%d racks in the MDF" % t.racks_in(mdf).size())

	# ---- YOU CAN LEAVE THE ROOM YOU START IN. Six racks were placed on a fixed
	# grid across the only door of the MDF and the first playtest of the tower
	# ended in the room it began in: "there's no way to actually leave the
	# server room." Reachability in the room graph said everything was fine,
	# because the room graph does not know a rack is 2 m of steel. So this is a
	# WALK, with the same physics as the stair climb: spawn, cross the room,
	# through the doorway, into the corridor.
	var mdf_doors: Array = t.room_doors(mdf)
	if mdf_doors.is_empty():
		fail("the MDF has no door at all")
	else:
		var g: Dictionary = mdf_doors[0]
		var gate: Vector2 = g.gate
		var outv: Vector2 = g.out
		var inside := gate - outv * 1.3
		var outside := gate + outv * 1.6
		t.teleport(sp + Vector3(0, 0.3, 0))
		for i in range(20):
			await process_frame
		var legs: Array = [inside, gate + outv * 0.1, outside]
		var got := true
		for w in legs:
			if not await _walk_to(self, t, w, 600):
				got = false
				break
		var here: int = t.player_room()
		if not got or here == t.NOROOM:
			fail("walked from the spawn towards the MDF door and got stuck at (%.1f, %.1f)"
				% [t.player.global_position.x, t.player.global_position.z])
		elif here == mdf:
			fail("walked through the MDF doorway and came out still in the MDF at (%.1f, %.1f) -- something is standing in the way"
				% [t.player.global_position.x, t.player.global_position.z])
		else:
			ok("walked out of the MDF into the %s" % t.rooms[here].name)

	# ---- AND NOTHING IS PARKED IN A DOORWAY, on any floor. A rack row is
	# planned off the room's doors; this is that claim checked as data, so a
	# room the physics walk does not visit cannot regress quietly.
	var fouled := 0
	for i in range(t.racks.size()):
		var k: Dictionary = t.racks[i]
		var w: float = t.RACK_W if k.along_x else t.RACK_D
		var d: float = t.RACK_D if k.along_x else t.RACK_W
		var foot := Rect2(k.x, k.z, w, d)
		for dd in t.room_doors(int(k.room)):
			if foot.intersects(dd.clear):
				fouled += 1
				fail("a rack in the %s stands in the clear floor of its door"
					% t.rooms[int(k.room)].name)
	if fouled == 0:
		ok("all %d racks stand clear of every doorway" % t.racks.size())

	# ---- THE LIFT. It has to physically carry a body between floors, and it
	# has to refuse a floor that is not in service -- which is the whole of how
	# this tower grows.
	if t.lifts.is_empty():
		fail("no lift was built, and the shafts are on every floor")
	else:
		var lift = t.lifts[0]
		var closed: int = t.nfloors - 1
		if t.in_service(closed):
			fail("every floor is in service at the start: the tower does not grow")
		else:
			ok("%d of %d floors in service at the start" % [t.floors_in_service, t.nfloors])
		var refused: String = t.lift_go(closed)
		if refused.find("not in service") < 0:
			fail("the lift went to a floor that is not in service: " + refused)
		else:
			ok("the lift refuses floor %d: %s" % [closed, refused])

		# ride it, with a body in it, under the same physics as the stairs
		var start: Vector3 = lift.car_centre()
		t.teleport(start + Vector3(0, 0.4, 0))
		for i in range(20):
			await process_frame
		if not lift.inside(t.player.global_position):
			fail("the car did not hold the player: y = %.2f" % t.player.global_position.y)
		var want := 1
		while want < t.floors_in_service and not lift.floors.has(want):
			want += 1
		var said: String = t.lift_go(want)
		var arrived := false
		for i in range(900):
			await process_frame
			if not t.lift_busy() and lift.at == want:
				arrived = true
				break
		var py: float = t.player.global_position.y
		if not arrived:
			fail("the lift never got to floor %d (%s)" % [want, said])
		elif absf(py - want * t.fheight) > 0.6:
			fail("the lift arrived at floor %d and left the player at y = %.2f"
				% [want, py])
		else:
			ok("the lift carried a walking body from floor 0 to %d, y = %.2f" % [want, py])
		# and back down, so it is not a one-way trip
		t.lift_go(0)
		for i in range(900):
			await process_frame
			if not t.lift_busy() and lift.at == 0:
				break
		if absf(t.player.global_position.y) > 0.6:
			fail("the lift would not come back down: y = %.2f" % t.player.global_position.y)
		else:
			ok("and back down to the ground floor")

		# ---- OPENING A FLOOR MAKES IT REACHABLE. Before, the button is dead.
		var before: int = t.floors_in_service
		if before < t.nfloors:
			var opened: String = t.open_next_floor()
			if t.floors_in_service != before + 1:
				fail("open_next_floor did not open one: " + opened)
			elif t.lift_go(before).find("not in service") >= 0:
				fail("floor %d opened and the lift still refuses it" % before)
			else:
				ok("opening floor %d makes it reachable: %s" % [before, opened.strip_edges()])
			for i in range(900):
				await process_frame
				if not t.lift_busy():
					break
			t.lift_go(0)
			for i in range(900):
				await process_frame
				if not t.lift_busy():
					break

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

	# ---- EVERY DEVICE IS REACHABLE ON FOOT from where the day starts. A box
	# you cannot walk to is a box you cannot plug a lead into, and the site
	# model will happily install one into a room with no door.
	var from_spawn: PackedFloat32Array = t.walk_from(mdf)
	var unreachable := 0
	var buried := 0
	for d in t.devices:
		var df: int = int(floor((d.pos.y + 0.3) / t.fheight))
		var dr: int = t.room_of(df, int(floor(d.pos.x)), int(floor(d.pos.z)))
		if dr == t.NOROOM:
			buried += 1
			fail("%s is outside the building plate at (%.1f, %.1f)" % [d.name, d.pos.x, d.pos.z])
		elif from_spawn[dr] < 0.0:
			unreachable += 1
			fail("you cannot walk from the MDF to the %s in the %s"
				% [d.name, t.rooms[dr].name])
	if unreachable == 0 and buried == 0:
		ok("all %d devices are reachable on foot from the MDF" % t.devices.size())

	# ---- and the site model is the one that says what they are
	var kit: Array = t.site_devs()
	if kit.size() < 4:
		fail("day one holds %d devices; it should hold the handoff and a delivery"
			% kit.size())
	else:
		var names := ""
		for d in kit: names += " " + str(d.name)
		ok("day one:" + names)

	# ---- THE DELIVERY IS IN GOODS IN, and getting it anywhere else is a walk.
	# The README says hardware arrives somewhere rather than in an inventory,
	# and this is the half of that claim the 3D has to honour: the boxes are
	# on the floor of the loading bay, they are things you can walk up to, and
	# picking one up and putting it down is what moves it.
	var goods: int = t.find_room(0, t.K_GOODS)
	if goods < 0:
		fail("the tower has no goods in")
	else:
		var delivered := 0
		var elsewhere := ""
		for d in kit:
			if int(d.kind) == 0:
				continue                      # the ISP handoff, outside
			if int(d.room) == goods: delivered += 1
			else: elsewhere += " " + str(d.name)
		if delivered < 3:
			fail("only %d of the delivery is in goods in;%s is elsewhere"
				% [delivered, elsewhere])
		else:
			ok("%d boxes delivered to goods in, not to the room you start in" % delivered)

		# and every one of them is DRAWN there: a box the site says is in a
		# room and the view does not show is a box nobody can walk up to.
		var seen := 0
		for d in t.devices:
			var df: int = int(floor((d.pos.y + 0.3) / t.fheight))
			if t.room_of(df, int(floor(d.pos.x)), int(floor(d.pos.z))) == goods:
				seen += 1
		if seen < 3:
			fail("goods in holds 3 boxes and the view draws %d of them" % seen)
		else:
			ok("and all %d of them are standing on the floor of it" % seen)

		# ---- CARRY ONE. Pick it up in goods in, walk it to the MDF, put it
		# down, and the site has to agree that is where it now lives.
		t.teleport(t.room_centre(goods) + Vector3(0, 0.4, 0))
		for i in range(10):
			await process_frame
		var box := -1
		for i in range(t.devices.size()):
			if int(t.devices[i].get("site", -1)) >= 0 and t.devices[i].name == "core":
				box = i
		if box < 0:
			fail("the switch that was delivered is not in the room to pick up")
		else:
			var said: String = t.carry_here(box)
			if t.carrying < 0:
				fail("could not pick the delivery up: " + said)
			else:
				ok("picked it up in goods in: " + said)
				t.teleport(t.room_centre(mdf) + Vector3(0, 0.4, 0))
				for i in range(10):
					await process_frame
				var put: String = t.drop_here()
				var where := -1
				for d in t.site_devs():
					if str(d.name) == "core": where = int(d.room)
				if where != mdf:
					fail("put down in the MDF and the site says room %d: %s"
						% [where, put])
				else:
					ok("carried to the MDF and the site agrees: " + put)
	# nothing is cabled on day one: that is the job
	if t.site_links().size() != 0:
		fail("something was already cabled before the player touched it")
	else:
		ok("and not one of them is plugged into anything yet")

	# ---- doors are gaps you can get through: every door edge must be clear
	var blocked := 0
	for d in t.doors:
		var key := "%d,%d,%d,%d" % [d.floor, d.x, d.y, d.dir]
		if not t.doorset.has(key): blocked += 1
	if blocked: fail("%d doors did not make it into the wall pass" % blocked)
	else: ok("%d doorways, all of them openings" % t.doors.size())

	# ---- THE MOBILE DEBUGGER. The leads are the lesson, and they are the same
	# lesson whether they hang off a trolley or a handset: serial talks to a
	# machine that never booted, the display lead does not, and a patch panel
	# has no ports at all.
	var mob: Node3D = t.phone
	if mob == null:
		fail("no mobile debugger")
	else:
		var ws := _device(t, "workstation")
		var srv := _device(t, "rack server")
		var pp := _device(t, "patch panel")
		if ws < 0 or srv < 0 or pp < 0:
			fail("expected a workstation, a rack server and a patch panel; got %d devices"
				% t.devices.size())
		else:
			var m: String = mob.plug(pp, "serial")
			if m.find("passive") < 0: fail("the patch panel pretended to have a console: " + m)
			else: ok("patch panel refuses a serial lead: " + m)

			m = mob.plug(srv, "hdmi")
			if m.find("no display output") < 0:
				fail("a rack server produced a picture: " + m)
			else: ok("rack server has no display output: " + m)

			m = mob.plug(srv, "serial")
			var screen: String = mob.screen_text()
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
			var out: String = mob.type_line("uname -a")
			if out.strip_edges() == "":
				fail("`uname -a` down the serial lead said nothing")
			else:
				ok("serial `uname -a`: " + out.split("\n")[0].substr(0, 60))
			m = mob.plug(ws, "hdmi")
			if m.find("display on") < 0: fail("the workstation would not drive a screen: " + m)
			else: ok("HDMI on the workstation: " + m)

	# ---- THE INVENTORY IS A PICTURE OF THE SIMULATION, not a second one. The
	# hands are the rule core/session.c already keeps: both of them are on a box
	# you are carrying, so the spool cannot be in one at the same time, and the
	# refusal a player reads has to be that rule rather than a new one.
	var bag: Control = t.bag
	if bag == null:
		fail("no inventory")
	else:
		if bag.hand(0) == "" or bag.hand(1) == "":
			fail("nothing is in either hand at the start: %s / %s"
				% [bag.hand(0), bag.hand(1)])
		else:
			ok("hands start as %s / %s" % [bag.hand(0), bag.hand(1)])
		if bag.equip("spool", 0) != "":
			fail("could not put the spool in a free hand")
		elif bag.hand(0) != "spool":
			fail("the spool went into a hand and did not stay there")
		else:
			ok("dragged the spool into the left hand")
		# now fill both hands with a box and try again
		t.teleport(t.room_centre(goods) + Vector3(0, 0.4, 0))
		for i in range(10):
			await process_frame
		var carry := -1
		for i in range(t.devices.size()):
			if int(t.devices[i].get("site", -1)) >= 0 and t.devices[i].name == "edge":
				carry = i
		if carry < 0:
			fail("nothing left in goods in to pick up")
		else:
			t.carry_here(carry)
			if t.carrying < 0:
				fail("could not pick a second box up")
			elif bag.hand(0) != "box" or bag.hand(1) != "box":
				fail("carrying a box and the hands say %s / %s"
					% [bag.hand(0), bag.hand(1)])
			else:
				ok("carrying a box puts it in BOTH hands: %s / %s"
					% [bag.hand(0), bag.hand(1)])
			var no: String = bag.equip("spool", 0)
			if no.find("both hands") < 0:
				fail("the inventory let a spool into a full hand: '%s'" % no)
			else:
				ok("and refuses the spool: " + no)
			# and the refusal is not decoration: the cable verb refuses too
			var no2: String = t.cable_here(carry if carry < t.devices.size() else 0)
			if no2.find("both hands") < 0:
				fail("cable_here ran a cable with both hands full: '%s'" % no2)
			else:
				ok("and so does running a cable, in core's words")
			t.drop_here()

	# ---- [E] AT THE WORKSTATION IS THE 2D DESKTOP. Walk to it -- so a desk
	# nobody can reach fails here rather than looking fine in a screenshot --
	# and open it. This is last because de.gd installs a ticket when it starts.
	var wsi := _device(t, "workstation")
	if wsi < 0:
		fail("there is no workstation in the MDF")
	else:
		var seat: Vector3 = t.devices[wsi].use_from
		t.teleport(t.room_centre(mdf) + Vector3(0, 0.4, 0))
		for i in range(20):
			await process_frame
		var walked: bool = await _walk_to(self, t, Vector2(seat.x, seat.z), 900)
		if not walked:
			fail("could not walk to the workstation in the MDF")
		else:
			ok("walked to the workstation and stood at it")
		var near: int = t.nearest_device(t.player.global_position)
		if near != wsi:
			fail("standing at the desk and what is in reach is %s"
				% ("nothing" if near < 0 else str(t.devices[near].name)))
		else:
			ok("the workstation is what is in reach from the seat")
		t.with_desktop = true
		var sat: String = t.use_here(wsi)
		if not t.desk_open():
			fail("[E] at the workstation did not open the desktop: " + sat)
		else:
			await process_frame
			await process_frame
			if t.desk_de.get_child_count() == 0:
				fail("the desktop came up empty")
			else:
				ok("[E] at the workstation: " + sat.strip_edges())
			var up: String = t.stand_up()
			if t.desk_open():
				fail("could not stand up again: " + up)
			else:
				ok("and [Esc] stands you back up")

	print("tower: %d failures" % bad)
	quit(1 if bad else 0)


# Walk there. Steers every frame and drives forward under the same physics the
# stair climb uses -- no teleporting, no ignoring walls. False if it never
# arrives, which is what a rack across the route looks like from in here.
func _walk_to(tree: SceneTree, t: Node3D, target: Vector2, budget: int) -> bool:
	t.player.drive_active = true
	t.player.drive = Vector2(0, 1)
	var arrived := false
	for i in range(budget):
		var p: Vector3 = t.player.global_position
		var to := target - Vector2(p.x, p.z)
		if to.length() < 0.35:
			arrived = true
			break
		# -Z is forward, so this is the yaw that points at the target.
		t.player.look_at_yaw(atan2(-to.x, -to.y))
		await tree.process_frame
	t.player.drive_active = false
	t.player.drive = Vector2.ZERO
	for i in range(4):
		await tree.process_frame
	return arrived


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
