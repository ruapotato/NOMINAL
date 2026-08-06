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

	# AND BEHIND THE FRAMES, WHICH IS WHERE YOU CABLE THEM.
	#
	# Arithmetic said 0.60 m was a gap. The player capsule is 0.56 m across, so
	# it was two centimetres a side, and the owner reported that he could not get
	# behind the servers. He reported this room as sealed twice -- once for the
	# doorway, once for this -- so it walks a real body down the rear aisle
	# rather than measuring it, because measuring it is what got it wrong.
	var zs: Array = []
	var rx := 1.0e9
	for k in t.racks:
		if int(k.room) == mdf:
			zs.append(float(k.z))
			rx = min(rx, float(k.x))
	if zs.size() >= 2:
		zs.sort()
		t.teleport(Vector3(rx * 0.5, 0.3, zs[0] - 1.2))
		for i in range(12):
			await process_frame
		var goal: float = zs[zs.size() - 1] + 1.2
		if await _walk_to(self, t, Vector2(rx * 0.5, goal), 900):
			ok("walked the rear aisle behind every frame")
		else:
			fail("blocked behind the frames at (%.1f, %.1f), heading for z %.1f"
				% [t.player.global_position.x, t.player.global_position.z, goal])

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

	# ---- YOU CAN GET TO THE STAIRS FROM WHERE THE DAY STARTS.
	#
	# The owner: "I would also suggest that there were multiple floors, but
	# there's no staircase, at least not one that's accessible." Every check
	# below this one teleports to the foot of a flight and then climbs, so we
	# had proved the flights carry a body and never once proved a player can
	# reach one -- which is exactly the mistake that made the racks look fine.
	# So this walks it: out of the MDF, along the corridor, to the bottom step.
	var stair0: int = t.find_room(0, t.K_STAIR)
	if stair0 < 0:
		fail("the ground floor has no stairwell at all")
	else:
		var foot := Vector2.ZERO
		for s in t.stairs:
			if int(s.floor) == 0:
				var fp: Vector3 = _foot_of(t, s)
				foot = Vector2(fp.x, fp.z)
		t.teleport(sp + Vector3(0, 0.3, 0))
		for i in range(20):
			await process_frame
		# through the MDF door, along the corridor ring, into the stairwell.
		# The waypoints are the rooms the building itself says are on the way,
		# not a hand-drawn path: each one is the centre of the next room along
		# the shortest route bld_walk() knows about.
		var legs: Array = _route_rooms(t, mdf, stair0)
		var got_there := true
		var legn := 0
		for w in legs:
			legn += 1
			if not await _walk_to(self, t, w, 1400):
				got_there = false
				print("    stuck on leg %d of %d, heading for (%.1f, %.1f)"
					% [legn, legs.size(), w.x, w.y])
				break
		var ended: int = t.player_room()
		if not got_there or ended != stair0:
			fail("walked from the spawn towards the stairs and ended in %s at (%.1f, %.1f)"
				% ["nowhere" if ended == t.NOROOM else str(t.rooms[ended].name),
					t.player.global_position.x, t.player.global_position.z])
		else:
			ok("walked from the MDF to the foot of the stairs, %d rooms" % legs.size())
			# and the signage says which way, computed off the building's own
			# walking distances -- a stairwell nobody can find has no stairs
			var signs := 0
			for n in t._signs.get_children():
				if str(n.text).begins_with("TO STAIRS"):
					signs += 1
			if signs == 0:
				fail("nothing anywhere in the building points at the stairs")
			else:
				ok("%d doors are signed TO STAIRS" % signs)
		# AND THE STAIRWELL DOOR IS CLEAR, on every floor, of anything the room
		# graph does not know about: a rack, or a delivery standing on the floor.
		var fouled2 := 0
		for f in range(t.nfloors):
			var st: int = t.find_room(f, t.K_STAIR)
			if st < 0:
				continue
			for dd in t.room_doors(st):
				for dev in t.devices:
					var box := Rect2(dev.mn.x, dev.mn.z, dev.size.x, dev.size.z)
					if absf(dev.mn.y - f * t.fheight) < t.fheight * 0.5 \
							and box.intersects(dd.clear):
						fouled2 += 1
						fail("%s stands in the clear floor of the floor %d stairwell door"
							% [dev.name, f])
		if fouled2 == 0:
			ok("every stairwell doorway is clear on every floor")

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

	# ---- OPENING A FLOOR MAKES IT REACHABLE -- AND YOU HAVE TO BE ON IT.
	#
	# core/session.c: "the lift does not stop at a floor nobody has opened, so
	# the only way onto it is the stairwell, and walking up it is metres of
	# building like any other." It charges the fit-out too. So this climbs a
	# real flight with a real body before it signs anything off, which proves
	# the floor is reachable before it proves it can be opened -- and the 3D
	# needs no rule of its own for any of it, because [O] is `open` down the
	# same session_line() a socket client uses.
	var before: int = t.floors_in_service
	if before < t.nfloors:
		var refused: String = t.open_next_floor()
		if t.floors_in_service != before:
			fail("floor %d was signed off from the ground floor: %s" % [before, refused])
		elif refused.find("standing on it") < 0:
			fail("opening a floor from the wrong floor did not explain itself: " + refused)
		else:
			ok("opening floor %d from below is refused: %s"
				% [before, refused.split("\n")[0].strip_edges()])
		# up the last flight to it, under the same physics as every other climb
		var got_up := false
		for f in [before - 1]:
			for st2 in t.stairs:
				if int(st2.floor) != f:
					continue
				t.teleport(_foot_of(t, st2))
				for i in range(12):
					await process_frame
				t.player.drive_active = true
				t.player.drive = Vector2(0, 1)
				t.player.look_at_yaw(_yaw_of(st2))
				for i in range(1200):
					await process_frame
					if t.player.global_position.y > (f + 1) * t.fheight - 0.35:
						got_up = true
						break
				# ONTO THE LANDING, not stopped on the top step. The run comes
				# up through a hole in the slab and the top step is at the edge
				# of it; a body that stops there and is then left alone for a
				# moment goes back down the way it came.
				for i in range(40):
					await process_frame
				t.player.drive_active = false
				t.player.drive = Vector2.ZERO
				break
		for i in range(20):
			await process_frame
		if not got_up or t.player_floor() != before:
			fail("could not climb to floor %d to sign it off (on %d)"
				% [before, t.player_floor()])
		else:
			var opened: String = t.open_next_floor()
			if t.floors_in_service != before + 1:
				fail("standing on floor %d and it would not open: %s" % [before, opened])
			elif t.lift_go(before).find("not in service") >= 0:
				fail("floor %d opened and the lift still refuses it" % before)
			else:
				ok("stood on floor %d and signed it off: %s"
					% [before, opened.split("\n")[0].strip_edges()])
			for i in range(900):
				await process_frame
				if not t.lift_busy():
					break
			t.lift_go(0)
			for i in range(900):
				await process_frame
				if not t.lift_busy():
					break

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
			# AND YOU CAN TYPE AT IT. Plugging in takes the keyboard, the
			# terminal on the screen is the real terminal.gd, and the line
			# goes down machine.sh_on() -- the same call the socket console
			# makes and the one console_speaks.gd gates.
			if not mob.focused:
				fail("a serial lead went in and the handset did not take the keyboard")
			else:
				ok("plugging in zooms the handset and takes the keyboard")
			var before_lines: int = mob.screen_text().split("\n").size()
			mob.type_line("uname -a")
			var after: String = mob.screen_text()
			if after.split("\n").size() <= before_lines:
				fail("typed a line at the handset and nothing appeared on it")
			elif after.find("NomnixOS") < 0:
				fail("the handset screen does not show what the machine answered")
			else:
				ok("typed at the handset and the machine answered on its screen")
			if mob.let_go().find("still in") < 0:
				fail("[Esc] did not put the handset down")
			elif mob.focused:
				fail("[Esc] and the handset still has the keyboard")
			else:
				ok("[Esc] puts the handset down and leaves the lead in")
			m = mob.plug(ws, "hdmi")
			if m.find("display on") < 0: fail("the workstation would not drive a screen: " + m)
			else: ok("HDMI on the workstation: " + m)
			mob.unplug()

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

	# ---- THE CROSSHAIR NAMES WHAT IT IS AIMED AT.
	#
	# "It was really hard to know when I was actually hitting E on the right
	# thing." The ray has to hit the thing the player thinks it hits, so this
	# aims the head at a real port on a real box and asks what the crosshair
	# says -- which is the same call the reticle draws from.
	var uplink := _device(t, "uplink")
	if uplink < 0:
		fail("the ISP handoff is not drawn in the MDF")
	else:
		var pf: Array = t.devices[uplink].ports
		if pf.is_empty():
			fail("the uplink has no ports drawn on it at all")
		else:
			var hole: Dictionary = pf[0]
			var stand: Vector3 = hole.c + hole.n * 0.9
			stand.y = float(t.rooms[mdf].floor) * t.fheight + 0.2
			t.teleport(stand)
			for i in range(15):
				await process_frame
			t.aim_at(hole.c)
			await process_frame
			var a: Dictionary = t.aim()
			if a.is_empty():
				fail("stood 0.9 m in front of uplink port 0, looked at it, and the crosshair found nothing")
			elif a.kind != "port" or int(a.dev) != uplink or int(a.port) != 0:
				fail("aimed at uplink port 0 and the crosshair says %s" % str(a))
			else:
				var nm: Array = t.aim_text(a)
				ok("the crosshair on a port: '%s   %s'" % [nm[0], nm[1]])
			# and the ports are the model's, not the view's
			var np := 0
			for d in t.site_devs():
				if str(d.name) == "uplink": np = int(d.nports)
			if pf.size() != np:
				fail("the site says uplink has %d ports and the view drew %d"
					% [np, pf.size()])
			else:
				ok("uplink is drawn with the %d port%s the site model gives it"
					% [np, "" if np == 1 else "s"])

	# ---- A CABLE RUN IN 3D IS THE SAME RUN THE MODEL HAS.
	#
	# Spool, one end in a port, walk to the other end, other end in -- exactly
	# the four things core/session.c makes a socket client do, because that is
	# what the 3D now calls. What comes out has to be a link the site model
	# holds, at the length the building measured, drawn between those two
	# holes.
	var pre: int = t.site_links().size()
	var uplink2 := _device(t, "uplink")
	var boxi := -1
	for i in range(t.devices.size()):
		if int(t.devices[i].get("site", -1)) >= 0 and t.devices[i].name == "core":
			boxi = i
	if uplink2 < 0 or boxi < 0:
		fail("expected the handoff and the switch in the MDF to cable together")
	else:
		# stand at each end in turn, the way a person does
		t.teleport(t.room_centre(mdf) + Vector3(0, 0.4, 0))
		for i in range(12):
			await process_frame
		var one: String = t.cable_at(uplink2, 0)
		var mid: Dictionary = t.ses_state()
		var cabend: Array = mid.get("cab", [-1, -1])
		if int(cabend[0]) < 0:
			fail("put one end of a cable into uplink port 0 and the session has no end in anything: " + one)
		else:
			ok("one end into uplink port 0: the session is holding the drum")
		var two: String = t.cable_at(boxi, 3)
		var links: Array = t.site_links()
		if links.size() != pre + 1:
			fail("ran a cable in 3D and the site has %d links, not %d: %s"
				% [links.size(), pre + 1, two])
		else:
			var l: Dictionary = links[links.size() - 1]
			if int(l.bport) != 3 and int(l.aport) != 3:
				fail("plugged the far end into port 3 and the model records %d/%d"
					% [l.aport, l.bport])
			elif int(l.metres) <= 0:
				fail("the run is %d metres long" % l.metres)
			else:
				ok("a cable run in 3D is a link in the model: %d m, %d, port %d to port %d"
					% [l.metres, l.cost, l.aport, l.bport])
			# and it is DRAWN, as slack copper between the two holes
			var pts: Array = t._cable_route(int(l.a), int(l.aport), int(l.b),
				int(l.bport), int(l.i))
			if pts.size() < 3:
				fail("the run is drawn as %d points -- a straight line" % pts.size())
			else:
				var straight: float = (pts[0] as Vector3).distance_to(pts[pts.size() - 1])
				var along := 0.0
				var line: Array = t._sag(t._round_corners(pts, 0.09), int(l.i))
				for i in range(line.size() - 1):
					along += (line[i] as Vector3).distance_to(line[i + 1])
				if along <= straight * 1.02:
					fail("the drawn cable is %.2f m between ends %.2f m apart: no slack in it"
						% [along, straight])
				else:
					ok("drawn as %.1f m of copper between two holes %.1f m apart"
						% [along, straight])
			# the link light on that port says up, out of the model
			var stt: int = t.port_state(int(l.a), int(l.aport))
			var stb: int = t.port_state(int(l.b), int(l.bport))
			if stt != int(l.state) or stb != int(l.state):
				fail("the port lights say %d/%d and the link is %d" % [stt, stb, l.state])
			else:
				ok("both ports read state %d out of the model" % stt)
		# ---- AND A CABLE BETWEEN TWO FLOORS GOES UP THE RISER, NOT THROUGH THE
		# SLAB. A playtester photographed the floor 3 comms cupboard with blue
		# tubes climbing straight through 160 mm of concrete over the rack: the
		# route stamped every corner at the STARTING floor's tray height and put
		# the single vertical at the last corner, which is the one beside the box
		# you are plugging into. It looks fine in the numbers -- the metres were
		# always right -- and only a screenshot or this check catches it.
		#
		# IT CHECKS THE LINE THAT IS DRAWN, and it checks every pair of rooms
		# rather than the one pair the playtester happened to photograph. The
		# first version of this check did neither: it re-derived the leg heights
		# from _tray_route() itself, so it was testing its own copy of the
		# drawing rather than the drawing, and it only ever ran floor 0 to
		# floor 3 -- a route with no riser on it at all, or a riser that does
		# not line up with the one below, would have sailed straight through.
		# _route_between() is now the one place the heights are stamped and
		# this asks it directly.
		var sealed_slabs := func(line: Array) -> int:
			var through := 0
			for i in range(line.size() - 1):
				var p: Vector3 = line[i]
				var q: Vector3 = line[i + 1]
				if absf(p.y - q.y) < 0.01:
					continue
				for fl in range(1, t.nfloors + 1):
					if min(p.y, q.y) > fl * t.fheight \
							or max(p.y, q.y) < fl * t.fheight - t.SLAB_T:
						continue
					var holed := false
					for h in t._hole_on(fl):
						if (h as Rect2).has_point(Vector2(p.x, p.z)):
							holed = true
					if not holed:
						through += 1
			return through
		# every run that is actually in the model, as it is actually drawn
		var drawn_bad := 0
		for l in t.site_links():
			if int(l.state) < 0:
				continue
			var line: Array = t._cable_route(int(l.a), int(l.aport), int(l.b),
				int(l.bport), int(l.i))
			if line.size() >= 2:
				drawn_bad += sealed_slabs.call(line)
		if drawn_bad > 0:
			fail("a link in the model is drawn through %d sealed slabs" % drawn_bad)
		else:
			ok("every link in the model is drawn without crossing a sealed slab")
		# and every pair of rooms in the tower, floor by floor, because the run
		# the player makes next is not the one that was photographed
		var pairs := 0
		var bad_pairs := 0
		var worst := ""
		for ra in t.rooms:
			if not t.TRAY_KINDS.has(int(ra.kind)):
				continue
			for rb in t.rooms:
				if int(rb.i) <= int(ra.i) or not t.TRAY_KINDS.has(int(rb.kind)):
					continue
				var ca: Vector3 = t.room_centre(int(ra.i))
				var cb: Vector3 = t.room_centre(int(rb.i))
				# where a box in that room would sit: on the floor, not at the
				# centroid's height, which is what _route_between reads the
				# storey off
				ca.y += 0.6
				cb.y += 0.6
				pairs += 1
				var n: int = sealed_slabs.call(t._route_between(ca, cb))
				if n > 0:
					bad_pairs += 1
					if worst == "":
						worst = "%s (f%d) to %s (f%d), %d slabs" \
							% [str(ra.name), int(ra.floor), str(rb.name),
								int(rb.floor), n]
		if bad_pairs > 0:
			fail("%d of %d room-to-room routes climb through a sealed slab: %s"
				% [bad_pairs, pairs, worst])
		else:
			ok("all %d room-to-room routes climb only where there is a hole" % pairs)
		# and the same run over the socket's own words gets the same refusal
		var again: String = t.site("plug uplink:0")
		if again.find("already") < 0 and again.find("cable in it") < 0:
			fail("a port with a cable in it accepted a second one: " + again)
		else:
			ok("and a port that is full refuses, in core's words")

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

	# ================================================ THE LOOP, IN THE WORLD
	#
	# core/siteday.c has had the clock, the tenants, the rent and the ending
	# since D25, and for as long as the 3D existed none of it was reachable
	# from inside the building: no date, no money, no tenants, no losing. Every
	# check below is that the WINDOW shows what the socket verbs say, and each
	# one was proved by breaking the thing it watches and watching it fail.

	# ---- A DAY ADVANCES, AND THE HUD SAYS SO.
	var day0: int = int(t.ses_state().get("day", -1))
	var money0: int = int(t.ses_state().get("money", 0))
	var rep: String = t.advance_day()
	var day1: int = int(t.ses_state().get("day", -1))
	if day1 != day0 + 1:
		fail("advanced a day and the session went from day %d to %d" % [day0, day1])
	elif rep.find("day %d:" % day1) < 0:
		fail("the day report does not report the day: '%s'" % rep.split("\n")[0])
	else:
		ok("a day passes: " + rep.split("\n")[0].strip_edges())
	if t.ledger_text().find("day %d." % day1) < 0:
		fail("the day advanced and the ledger still reads '%s'"
			% t.ledger_text().split("\n")[0])
	elif t.ledger_text().find("in hand") < 0:
		fail("the ledger says nothing about money: '%s'" % t.ledger_text())
	elif t.ledger_text().find("billed in") < 0:
		fail("the ledger never says when the next bill lands: '%s'" % t.ledger_text())
	else:
		ok("the HUD carries the date and the money: "
			+ t.ledger_text().replace("\n", "  "))
	if not t.report_open():
		fail("a day passed and the window showed no report of it")
	else:
		ok("and the day's own report is up on the screen, %d lines"
			% t.report_text.split("\n").size())
	t._dismiss_report()
	if money0 == 0:
		fail("the ledger's money came out of nowhere")

	# ---- A TENANCY MOVES IN AND THE BUILDING SHOWS IT.
	#
	# On this seed the first tenancy has the keys on day 49 and their twenty
	# computers arrive in the room they rent, plugged into nothing. That is the
	# player's whole job and until now nothing in the world said it had begun.
	var was_dev: int = t.devices.size()
	t.command("day 49")
	await process_frame
	var rows: Array = t.service_rows()
	if rows.is_empty():
		fail("forty-nine days passed and `service` lists nobody")
	else:
		var row: Dictionary = rows[0]
		ok("tenant %d moved in on floor %d: %d desks, %d up"
			% [int(row.tenant), int(row.floor), int(row.desks), int(row.up)])
		# their computers are DRAWN, in the room they rent
		# Across every room they rent, for the same reason as the seats
		# below: their twenty computers are spread over their whole suite.
		var drawn := 0
		for d in t.devices:
			var df: int = int(floor((d.pos.y + 0.3) / t.fheight))
			var dr: int = t.room_of(df, int(floor(d.pos.x)), int(floor(d.pos.z)))
			if t.room_rented_by(dr, int(row.tenant)):
				drawn += 1
		if drawn < int(row.desks):
			fail("the tenancy has %d desks and the view draws %d of them across the rooms they rent"
				% [int(row.desks), drawn])
		else:
			ok("%d of their computers stand in the rooms they rent (%d devices, was %d)"
				% [drawn, t.devices.size(), was_dev])
		# ---- AND SOMEBODY IS SITTING AT EVERY ONE OF THEM.
		#
		# A person per desk device, in the room the tenancy rents, derived from
		# the model rather than scattered: the count is `service`'s desk count,
		# the room is the one core let to them, and the postures are that
		# tenancy's own columns. Nothing here is drawn as a node, so this is
		# read off the instance buffers.
		var pc: Array = t.people_counts()
		var npeople: int = pc[0] + pc[1] + pc[2] + pc[3]
		var ndesk := 0
		var nup := 0
		for r2 in rows:
			ndesk += int(r2.desks)
			nup += int(r2.up)
		if npeople != ndesk:
			fail("%d desks in the building and %d people at them" % [ndesk, npeople])
		else:
			ok("%d people at %d desks, in %d instance buffers"
				% [npeople, ndesk, t._people_buffers()])
		# NOTHING IS CABLED YET, so nobody in the building can do any work and
		# the rooms have to say so. `up 0` is the model's; hands up is the
		# view's rendering of it, and one following the other is the point.
		if nup == 0 and pc[0] > 0:
			fail("no desk in the building has a port and %d people are working" % pc[0])
		elif nup == 0:
			ok("no port in any of them, so all %d have their hand up"
				% [pc[1] + pc[2]])
		# ---- AND NOBODY IS SITTING IN A DOORWAY OR IN A RACK AISLE.
		#
		# People have no collision -- a person is not a wall -- so the walking
		# tests above cannot catch this. It is the same claim the rack check
		# makes as data, for the same reason: a chair in the door of an office
		# you never walk through is a regression that would go quiet.
		var seat_bad := 0
		var seats: Array = t.people_seats()
		for st in seats:
			var s: Vector3 = st.pos
			var sf: int = int(floor((s.y + 0.3) / t.fheight))
			var sx: int = int(floor(s.x))
			var sz: int = int(floor(s.z))
			var sr: int = t.room_of(sf, sx, sz)
			# ANY room this tenancy rents, not "the" one. A tenancy holds up
			# to eleven rooms and its desks are apportioned across all of
			# them by area, so asking tenant_room() -- which returns the
			# FIRST -- called ten rooms out of eleven a misplacement. The
			# claim worth keeping is that nobody is seated in a doorway, a
			# rack aisle, or somebody else's office.
			if not t.room_rented_by(sr, int(st.tenant)):
				seat_bad += 1
				fail("somebody of tenant %d is sitting at (%.1f, %.1f), which is not the room their tenancy rents"
					% [int(st.tenant), s.x, s.z])
				continue
			var cell := Rect2(sx, sz, 1, 1)
			for dd in t.room_doors(sr):
				if cell.intersects(dd.clear):
					seat_bad += 1
					fail("somebody is sitting in the clear floor of the %s door"
						% t.rooms[sr].name)
			for ri in t.racks_in(sr):
				var k: Dictionary = t.racks[ri]
				var kw: float = t.RACK_W if k.along_x else t.RACK_D
				var kd: float = t.RACK_D if k.along_x else t.RACK_W
				if cell.intersects(Rect2(k.x, k.z, kw, kd)):
					seat_bad += 1
					fail("somebody is sitting inside a rack in the %s"
						% t.rooms[sr].name)
		if seat_bad == 0 and not seats.is_empty():
			ok("all %d of them are in their own room, clear of every doorway"
				% seats.size())
		# ---- AND YOU CAN STILL WALK THE ROOM THEY ARE IN. Desks, chairs and
		# people between the door and the far wall: the chequerboard is what
		# leaves a way through, and this is a real body walking it rather than
		# the arithmetic that says it should fit.
		#
		# Their FIRST room, which since the desks were apportioned across a
		# tenancy's whole suite is no longer the whole of it -- but it is a
		# real room with real furniture in it, which is what this walk needs.
		var troom: int = t.tenant_room(int(row.tenant))
		var oroom: Dictionary = t.rooms[troom]
		var od: Array = t.room_doors(troom)
		if not od.is_empty():
			var gate2: Vector2 = od[0].gate
			var into: Vector2 = gate2 - od[0].out * 1.2
			var far := Vector2(float(oroom.x1) - 0.8, float(oroom.y1) - 0.8)
			if into.distance_to(far) < 3.0:
				far = Vector2(float(oroom.x0) + 0.8, float(oroom.y0) + 0.8)
			# WHERE THE BODY WAS. `_be_here()` tells the site the room you are
			# standing in, and the checks after this one ask the session which
			# floor you are on -- so this walk puts the body back afterwards.
			var was_at: Vector3 = t.player.global_position
			t.teleport(Vector3(into.x, float(oroom.floor) * t.fheight + 0.3, into.y))
			for i in range(16):
				await process_frame
			if await _walk_to(self, t, far, 900):
				ok("walked from their door to the far corner of the office")
			else:
				fail("blocked crossing an occupied office at (%.1f, %.1f), heading for (%.1f, %.1f)"
					% [t.player.global_position.x, t.player.global_position.z,
						far.x, far.y])
			t.teleport(was_at)
			for i in range(8):
				await process_frame

		# and you can find out without typing a verb: over their door, and on
		# the HUD, in `service`'s own columns
		if t.waiting_signs() == 0:
			fail("a tenancy is waiting and nothing in the building says so")
		else:
			ok("%d signs hang over the doors of tenancies with no ports"
				% t.waiting_signs())
		if t.hud_lines().find("tenant %d" % int(row.tenant)) < 0:
			fail("the HUD does not name the tenancy that is waiting:\n" + t.hud_lines())
		else:
			ok("the HUD names them: " + _line_with(t.hud_lines(), "tenant "))

	# ---- WHAT THE NEXT FLOOR NEEDS, BEFORE THE KEY IS PRESSED, IN CORE'S WORDS.
	if t.floors_in_service < t.nfloors:
		var says: String = t.hud_lines()
		if t.ses_floor() == t.floors_in_service:
			if says.find("[O] sign floor") < 0:
				fail("standing on the next floor and the HUD does not offer [O]")
			else:
				ok("standing on it, the HUD offers [O]")
		elif says.find("standing on it") < 0 or says.find("cost") < 0:
			fail("the HUD offers a floor without saying what it needs or costs:\n" + says)
		else:
			ok("the HUD says what [O] needs first: "
				+ _line_with(says, "it will cost").strip_edges())

	# ---- A COMMAND FROM OUTSIDE DRIVES THE RUNNING WINDOW.
	#
	# The owner's reason for all of this: "Claude playing a video game in 3D
	# space by taking screenshots of the actual user interface is not a
	# fantastic way for it to iterate. It should operate with commands over the
	# port." So an agent opens a socket to the window that is already running,
	# types the same verbs a socket client types, and the world keeps up: the
	# body walks, and a cable it asks for is really in the tray.
	var port: int = t.wire_port()
	if port <= 0:
		fail("the running tower is not listening on any port")
	else:
		var c := StreamPeerTCP.new()
		c.connect_to_host("127.0.0.1", port)
		var joined := false
		for i in range(240):
			await process_frame
			c.poll()
			if c.get_status() == StreamPeerTCP.STATUS_CONNECTED:
				joined = true
				break
		if not joined:
			fail("nothing answered on port %d" % port)
		else:
			ok("connected to the running tower on 127.0.0.1:%d" % port)
			var pre_links: int = t.site_links().size()
			var goods2: int = t.find_room(0, t.K_GOODS)
			# walk the body somewhere else, from outside
			t.teleport(t.room_centre(t.find_room(0, t.K_MDF)) + Vector3(0, 0.4, 0))
			for i in range(10):
				await process_frame
			var said3: String = await _tell(self, c, "go goods")
			for i in range(6):
				await process_frame
			if t.player_room() != goods2:
				fail("`go goods` over the socket and the body is in room %d: %s"
					% [t.player_room(), said3.strip_edges()])
			else:
				ok("`go goods` over the socket walked the player into %s"
					% t.rooms[goods2].name)
			# and cable something, from outside, and see it in the world:
			# fetch the server out of goods in, put it in the MDF beside the
			# switch, and patch it. Six lines, no mouse, and the transcript is
			# kept so that a failure says which of them the tower refused.
			# `spool back` first because both hands are still on the drum from
			# the run above -- core's rule, and the same refusal a player reads.
			var script: Array = ["spool back", "carry files", "go mdf", "drop",
				"spool cat6", "plug files:0", "plug core:9"]
			var talk := ""
			var laid := ""
			for cmd in script:
				laid = await _tell(self, c, cmd)
				talk += "\n    $ " + cmd + " -> " + laid.strip_edges().split("\n")[0]
			for i in range(6):
				await process_frame
			var now_links: Array = t.site_links()
			if now_links.size() != pre_links + 1:
				fail("cabled over the socket and the site holds %d links, not %d:%s"
					% [now_links.size(), pre_links + 1, talk])
			else:
				var l2: Dictionary = now_links[now_links.size() - 1]
				if t.cables_drawn() < 8:
					fail("a cable was run over the socket and the world draws %d vertices of copper"
						% t.cables_drawn())
				else:
					ok("a cable run from outside is %d m of copper in the world: port %d to port %d"
						% [int(l2.metres), int(l2.aport), int(l2.bport)])
			# ---- AND WHAT IS STRUGGLING IS READ OFF `load`, in its own row.
			# The HUD only says it when a port is really dropping, so what is
			# gated here is that the row is being read at all: a port with a
			# cable in it, named the way `load` names it, with its drops in the
			# column `load` puts them in.
			t.command("day")
			var worst: String = t.load_worst()
			if worst.find(":") < 0:
				fail("ports are cabled and `load` names none of them: '%s'" % worst)
			elif t.load_drops() < 0:
				fail("the drops column of `load` did not read as a number: '%s'" % worst)
			else:
				ok("the busiest port, in `load`'s own row: " + worst)
			# and the window can be READ from outside too, which is the other
			# half of playing it blind
			var hud2: String = await _tell(self, c, "hud")
			if hud2.find("in hand") < 0:
				fail("`hud` over the socket does not show the ledger: " + hud2)
			else:
				ok("`hud` over the socket reads back what the window shows")
			c.disconnect_from_host()

	# ---- THE RUN ENDS, AND SAYS WHY.
	#
	# A game you cannot lose is not a game. The cheapest honest way to lose
	# this one is to buy a circuit you cannot pay for: the standing charge
	# lands on the thirtieth day whatever the network did with it, and an
	# account that goes overdrawn with no rent coming in ends the run. Nothing
	# here is a test hook -- it is `isp`, then days.
	var have: int = int(t.ses_state().get("money", 0))
	t.command("isp %d" % int(max(10, (have / 3) / 3)))
	for i in range(14):
		if t.run_over:
			break
		t.command("day 30")
		await process_frame
	if not t.run_over:
		fail("bought a circuit worth %d with %d in the bank and the run never ended: %s"
			% [int(max(10, (have / 3) / 3)), have, t.ledger_text()])
	else:
		# THE REASON, NOT THE LAST THING PRINTED. `report_text` is only ever
		# the most recent day's output, and a `day` issued after the run is
		# over answers with a terse "the run ended on day N" rather than
		# repeating why -- so this used to read whichever of the two the
		# timing happened to leave behind. Since D27 put a tenancy in on day
		# one, complaints end a run well before the circuit bill does, and the
		# ending this section tries to CAUSE has usually already happened by
		# the time it runs. The window keeps the sentence now, once, for good.
		if t.run_over_why.strip_edges() == "":
			fail("the run ended and the window never kept why. report_text was: <<%s>>"
				% t.report_text)
		elif t.run_over_why.find("day") < 0:
			fail("the run-ending sentence does not say which day: " + t.run_over_why)
		else:
			ok("the run ended and the window kept why: " + t.run_over_why)
		var hud3: String = t.command("hud")
		if hud3.find(t.run_over_why.strip_edges()) < 0:
			fail("`hud` does not tell a socket client why the run ended")
		else:
			ok("and `hud` tells a socket client the same reason")
		if t.hud_lines().find("THE RUN IS OVER") < 0:
			fail("the run is over and the HUD still offers the next day")
		else:
			ok("and the HUD stops offering a day that will not come")

	print("tower: %d failures" % bad)
	quit(1 if bad else 0)


# One line over the socket, and the answer back. The prompt is the frame: the
# wire writes it after every answer, so a client knows the answer is complete.
func _tell(tree: SceneTree, c: StreamPeerTCP, line: String) -> String:
	# Anything already waiting is the greeting, or the prompt after the last
	# answer. Draining it first is what makes an answer belong to its command
	# rather than to whichever line happened to be one behind.
	c.poll()
	var left := c.get_available_bytes()
	if left > 0:
		c.get_data(left)
	c.put_data((line + "\n").to_utf8_buffer())
	var out := ""
	for i in range(900):
		await tree.process_frame
		c.poll()
		var n := c.get_available_bytes()
		if n > 0:
			var g: Array = c.get_data(n)
			if int(g[0]) == OK:
				out += (g[1] as PackedByteArray).get_string_from_utf8()
		if out.strip_edges().ends_with(">"):
			break
	return out


func _line_with(text: String, want: String) -> String:
	for line in text.split("\n"):
		if line.find(want) >= 0:
			return line
	return ""


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


# The rooms between `from` and `to`, as points to walk to: at each step, the
# door-neighbour that is nearer the destination by the building's own walking
# metric. It is a route the building says exists, so a test that cannot walk it
# has found something standing in the way rather than a bad guess at a path.
# Every room you can get to from `r` without opening anything: through a door,
# or -- for two corridors -- through the opening between them, which is the
# rule core/building.c\'s step_ok() keeps and which the wall pass now keeps too.
func _joined(t: Node3D, r: int) -> Array:
	var out: Array = []
	for door in t.doors:
		if door.a != r and door.b != r:
			continue
		var other: int = door.b if door.a == r else door.a
		if other < t.rooms.size():
			out.append(other)
	if int(t.rooms[r].kind) == t.K_CORRIDOR:
		for o in t.rooms:
			if o.i == r or o.floor != t.rooms[r].floor or o.kind != t.K_CORRIDOR:
				continue
			# they touch if their rectangles share an edge
			var a := Rect2(t.rooms[r].x0, t.rooms[r].y0,
				t.rooms[r].x1 - t.rooms[r].x0, t.rooms[r].y1 - t.rooms[r].y0)
			var b := Rect2(o.x0, o.y0, o.x1 - o.x0, o.y1 - o.y0)
			if a.grow(0.01).intersects(b):
				out.append(int(o.i))
	return out


func _route_rooms(t: Node3D, from: int, to: int) -> Array:
	# Breadth-first over the DOOR GRAPH. It was a greedy descent on
	# bld_walk()'s room-to-room distances, and greedy is wrong here: a corridor
	# thirty metres long has one centre, and a shop off the middle of it can be
	# nearer the stairwell by that measure while being a dead end. The route
	# below is a sequence of rooms that really are joined by doors.
	var prev := {}
	prev[from] = -1
	var q: Array = [from]
	var head := 0
	while head < q.size():
		var at: int = q[head]
		head += 1
		if at == to:
			break
		for other in _joined(t, at):
			if prev.has(other):
				continue
			prev[other] = at
			q.append(other)
	if not prev.has(to):
		return []
	var chain: Array = []
	var cur := to
	while cur != -1:
		chain.push_front(cur)
		cur = int(prev[cur])
	var out: Array = []
	for i in range(chain.size() - 1):
		var a: int = chain[i]
		var b: int = chain[i + 1]
		# up to the doorway on this side, through it, then into the room
		var gated := false
		for dd in t.room_doors(a):
			var g2: Vector2 = dd.gate
			var o2: Vector2 = dd.out
			var probe := g2 + o2 * 0.8
			if t.room_of(t.rooms[a].floor, int(floor(probe.x)), int(floor(probe.y))) == b:
				out.append(g2 - o2 * 0.7)
				out.append(g2 + o2 * 0.7)
				gated = true
				break
		if not gated:
			# two corridors: walk to the middle of the opening between them
			var ra := Rect2(t.rooms[a].x0, t.rooms[a].y0,
				t.rooms[a].x1 - t.rooms[a].x0, t.rooms[a].y1 - t.rooms[a].y0)
			var rb := Rect2(t.rooms[b].x0, t.rooms[b].y0,
				t.rooms[b].x1 - t.rooms[b].x0, t.rooms[b].y1 - t.rooms[b].y0)
			var ov := ra.intersection(rb.grow(0.05))
			var cb: Vector3 = t.room_centre(b)
			var j := ov.get_center()
			# UP TO THE OPENING, THEN THROUGH IT. A waypoint on the far side
			# alone makes the leg a diagonal across whatever is between here and
			# there, and a corridor ring has rooms on the inside of every corner
			# of it. So: a step short of the junction, then a step past it.
			# ACROSS THE OPENING, SQUARE TO IT. Stepping towards the far
			# room's CENTRE puts the waypoint diagonally into the corner, and a
			# corridor ring has a riser or a cupboard on the inside of every
			# corner: the capsule wedges on the return. The opening has an
			# axis; go through it at right angles.
			var ax := Vector2(0, 1) if ov.size.x >= ov.size.y else Vector2(1, 0)
			var to_b: Vector2 = Vector2(cb.x, cb.z) - j
			var sgn: float = 1.0 if to_b.dot(ax) >= 0.0 else -1.0
			out.append(j - ax * sgn * 1.2)
			out.append(j + ax * sgn * 1.2)
		var c: Vector3 = t.room_centre(b)
		out.append(Vector2(c.x, c.z))
	return out


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
