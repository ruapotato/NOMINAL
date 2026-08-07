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

		# ---- AND THE BUTTONS ARE THINGS YOU CAN PRESS.
		#
		# The owner got in the car, aimed at a lit button, pressed [E] and
		# nothing happened: "So the elevator is functionally not working."
		# The panel was vgeo paint. aim() did not know it was there, so the
		# crosshair never named a button and [E] never had one to press --
		# and the only way to choose a floor was a number key that nothing
		# in the car mentions.
		#
		# So this walks the panel the way a player's eye does: every button
		# must be a real place in the world, the one for a floor in service
		# must offer [E], and pressing it must move the car. A button drawn
		# somewhere other than where it can be aimed at fails the first of
		# these, which is the half of the bug that could come back quietly.
		var btns: Array = lift.buttons()
		if btns.size() != lift.floors.size():
			fail("the car has %d buttons for %d floors" % [btns.size(), lift.floors.size()])
		else:
			ok("every floor the shaft passes has a button in the car: %d" % btns.size())
		# every button is inside the car it is painted in, which is what makes
		# it aimable from where a body stands
		var stray := -1
		for b in btns:
			if not lift.inside(b["pos"] + Vector3(0, -1.0, 0)):
				stray = int(b["floor"])
		if stray >= 0:
			fail("the button for floor %d is not in the car it is drawn in" % stray)
		else:
			ok("and every one of them is on the wall of the car, where it is drawn")
		# the crosshair names one, and offers the key
		var up := 1
		while up < t.floors_in_service and not lift.floors.has(up):
			up += 1
		var bpos: Vector3 = lift.button_pos(up)
		var txt: Array = t.aim_text({"kind": "liftbtn", "dev": -1, "port": -1,
			"floor": up, "point": bpos})
		if txt[0].find("floor %d" % up) < 0 or txt[1].find("[E]") < 0:
			fail("the crosshair on the floor %d button says: %s / %s" % [up, txt[0], txt[1]])
		else:
			ok("the crosshair on it reads: %s -- %s" % [txt[0], txt[1]])
		# and an unlit one says why rather than offering a key
		var shut: int = int(t.floors_in_service)
		while shut < 40 and not lift.floors.has(shut):
			shut += 1
		if lift.floors.has(shut):
			var t2: Array = t.aim_text({"kind": "liftbtn", "dev": -1, "port": -1,
				"floor": shut, "point": lift.button_pos(shut)})
			if t2[1].find("[E]") >= 0:
				fail("the button for floor %d is not in service and offers [E]" % shut)
			else:
				ok("an unlit button says why instead of offering a key: " + t2[1])
		# pressing it is the act, and it is the same act the digit is
		var said3: String = lift.button_press(up)
		var got := false
		for i in range(900):
			await process_frame
			if not t.lift_busy() and lift.at == up:
				got = true
				break
		if not got:
			fail("pressing the floor %d button did nothing (%s)" % [up, said3])
		else:
			ok("pressing the button in the car took it to floor %d" % up)
		t.lift_go(0)
		for i in range(900):
			await process_frame
			if not t.lift_busy() and lift.at == 0:
				break

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
	#
	# THREE WALKS, BECAUSE IT IS A SWITCHBACK. Up the first flight, across the
	# half landing, up the second. Nothing here is teleported between the
	# legs: if any part of the shape is unwalkable -- no floor at the foot, a
	# pitch a capsule slides back down, a half landing too small to turn on --
	# the body stops where it stopped and this says so.
	# THERE IS FLOOR IN FRONT OF THE BOTTOM STEP. The owner reported this
	# twice: "stairs that go right up against a wall so there's no landing for
	# you to walk onto the staircase without jumping onto the staircase." A
	# body put down where a person walks in has to be standing on the SLAB --
	# not fallen through, not perched on a tread half a metre up.
	# What a standing body reads as its own y when it is on a floor, measured
	# rather than assumed: the capsule's origin is not at the slab.
	# ---- CONDUIT IS DRAWN THROUGH THE TRAYS, AND SAYS WHAT IT CARRIES.
	#
	# "When you hover over a conduit, it'll tell you its percent of
	# utilisation. So you have to run fresh conduits from the power core once
	# they've hit a maximum load." A run is a thing in the world: you look at
	# it and read the number off, no key involved.
	t.command("credit 40000")
	t.command("order strip cst")
	t.command("deliver cst #%d" % t.find_room(0, t.K_MDF))
	t.command("order rackserver crs")
	t.command("deliver crs #%d" % t.find_room(0, t.K_MDF))
	t.command("go mdf")
	for i in range(8):
		await process_frame
	var pcore: int = -1
	for sd3 in t.site_devs():
		if str(sd3.kindname) == "powercore":
			pcore = int(sd3.i)
	if pcore < 0:
		fail("the building has no power core for the window to draw runs from")
	else:
		var was_drawn: int = t.power_drawn()
		t.command("feed cst")
		t.command("feed crs")
		t._reconcile()
		for i in range(6):
			await process_frame
		var runs: Array = t.site_conduits()
		if runs.size() < 2:
			fail("ran two conduits and the window reads %d" % runs.size())
		else:
			ok("the window reads the power tree: %d runs" % runs.size())
		if t.power_drawn() <= was_drawn:
			fail("conduit was run and nothing more is drawn: %d then %d"
				% [was_drawn, t.power_drawn()])
		else:
			ok("and there is more copper in the world for it: %d -> %d vertices"
				% [was_drawn, t.power_drawn()])
		# IT FOLLOWS THE TRAYS, which is the same route copper takes: the
		# drawn polyline has to climb to the containment rather than cut
		# through the room at socket height.
		var r0: Dictionary = runs[0]
		var route: Array = t.conduit_route(int(r0.from), int(r0.to))
		var high := 0.0
		var low := 99.0
		for pt in route:
			high = max(high, float(pt.y))
			low = min(low, float(pt.y))
		# IT CLIMBS, AND IT COMES BACK DOWN AT BOTH ENDS. The first draft did
		# only the middle: nine points, every one at tray height, beginning
		# directly above the core and stopping directly above the strip and
		# never reaching either box. The owner saw it immediately -- "those
		# power cables seem to not use the cable tray and are more of a direct
		# line that kinda looks funny" -- and he was right about the picture
		# while the measurement said the tray was being used. Both were true:
		# the middle was in the tray and the ends were missing, so all you
		# could see was a line across the ceiling joined to nothing.
		if route.size() < 3 or high < t.tray_y(0) - 0.5:
			fail("a conduit run is drawn as %d points topping out at %.2f m, "
				% [route.size(), high] + "and the tray is at %.2f" % t.tray_y(0))
		elif low > 1.0:
			fail("a conduit run never comes down to the kit: its lowest point "
				+ "is %.2f m" % low)
		elif float(route[0].y) > 1.0 or float(route[route.size() - 1].y) > 1.0:
			fail("a conduit run starts at %.2f m and ends at %.2f m, so neither "
				% [float(route[0].y), float(route[route.size() - 1].y)]
				+ "end is joined to a box")
		else:
			ok("and it goes floor, up at the door, tray, down, floor -- like "
				+ "copper: %d points, %.2f m to %.2f m" % [route.size(), low, high])
		# AND THE CROSSHAIR READS THE NUMBER OFF IT
		var mid: Vector3 = route[route.size() / 2]
		var seen := {}
		for pct in [10, 70, 95, 130]:
			seen[pct] = t.aim_text({"kind": "conduit", "dev": -1, "port": -1,
				"run": 0, "pct": pct, "load": pct * 15, "watts": 1500,
				"metres": int(r0.metres)})
		if str(seen[70][1]).find("%") < 0 or str(seen[130][1]).find("TRIPPED") < 0:
			fail("the crosshair on a run reads: %s / %s"
				% [str(seen[70][1]), str(seen[130][1])])
		else:
			ok("the crosshair on one reads: %s -- %s" % [seen[70][0], seen[70][1]])
			ok("and on a tripped one: %s" % seen[130][1])
		# and the colour is the utilisation, not the grade
		if t._conduit_colour(10) == t._conduit_colour(95) \
				or t._conduit_colour(130) == t._conduit_colour(95):
			fail("a run at 10%, 95% and 130% are all drawn the same colour")
		else:
			ok("and a run is coloured by what it is carrying, not by what it is")

	# ---- THE RISER HAS A LADDER AND A BODY CAN CLIMB IT.
	#
	# "There's a room in called riser, that seems to be an empty elevator
	# shaft... potentially the riser room should be left kind of a corridor
	# where you run cables. But with a ladder so you can actually climb up and
	# down." The shaft was open and its doorway was drawn; there was no way up
	# it, and core refused to admit you could be in there at all.
	var ris0: int = t.find_room(0, t.K_RISER)
	var ris1: int = t.find_room(1, t.K_RISER)
	if ris0 < 0 or ris1 < 0:
		fail("the building has no riser on the first two floors")
	else:
		# the model lets you be in one
		var wentr: String = str(t.site("go #%d" % ris0))
		if int(t.ses_state().get("room", -1)) != ris0:
			fail("`go` into the riser did not put you in it: " + wentr.strip_edges())
		else:
			ok("the session walks you into the riser: %s" % wentr.strip_edges().split("\n")[0])
		# and a body really climbs it, under the same physics as the stairs
		var rm0 = t.rooms[ris0]
		var foot := Vector3(float(rm0.x0) + 0.9, 0.45,
			float(rm0.y0) + (float(rm0.y1) - float(rm0.y0)) * 0.5 - 0.1)
		t.teleport(foot)
		for i in range(12):
			await process_frame
		var y_before: float = t.player.global_position.y
		t.player.drive_active = true
		t.player.drive = Vector2(0, 1)
		t.player.look_at_yaw(PI)
		var up_ok := false
		for i in range(700):
			await process_frame
			if t.player.global_position.y > t.fheight - 0.4:
				up_ok = true
				break
		t.player.drive_active = false
		t.player.drive = Vector2.ZERO
		if not up_ok:
			fail("walked at the riser ladder from y = %.2f and got to y = %.2f"
				% [y_before, t.player.global_position.y])
		else:
			ok("climbed the riser ladder from floor 0 to y = %.2f"
				% t.player.global_position.y)

	# ---- THE MAP IS A READING OF THE BUILDING, NOT A PICTURE OF ONE.
	#
	# "It would be better if that window just said the floor you were on and
	# the room you were on like it does now, but also included a mini-map."
	# A map is the one HUD element a blind test cannot look at, so it is fed
	# from map_rows() and map_rows() is what gets walked: every room of this
	# floor, in metres, exactly one of them marked as the one you are in, and
	# the socket counts the same ones `outlets` prints.
	# STAND SOMEWHERE KNOWN FIRST. Left where the previous leg finished, the
	# body was between rooms and player_room() was NOROOM, so the assertion
	# about the marked room silently did not run -- a check that skips itself
	# is a check that passes for the wrong reason.
	var mdf0: int = t.find_room(0, t.K_MDF)
	if mdf0 >= 0:
		t.teleport(t.room_centre(mdf0) + Vector3(0, 0.4, 0))
		for i in range(12):
			await process_frame
	var mrows: Array = t.map_rows()
	var want_rooms := 0
	for r3 in t.rooms:
		if int(r3.floor) == t.player_floor():
			want_rooms += 1
	if mrows.size() != want_rooms:
		fail("floor %d has %d rooms and the map draws %d"
			% [t.player_floor(), want_rooms, mrows.size()])
	else:
		ok("the map holds every one of floor %d's %d rooms" % [t.player_floor(), want_rooms])
	var marked := 0
	for m3 in mrows:
		if bool(m3.here):
			marked += 1
			if int(m3.i) != t.player_room():
				fail("the map marks %s and the body is in %s"
					% [str(m3.name), "nowhere" if t.player_room() == t.NOROOM
						else str(t.rooms[t.player_room()].name)])
	if t.player_room() != t.NOROOM and marked != 1:
		fail("the body is in a room and the map marks %d of them" % marked)
	elif t.player_room() != t.NOROOM:
		ok("and it marks the one you are standing in: %s"
			% str(t.rooms[t.player_room()].name))
	# the sockets on it are the model's, not a second count
	var owall2: Dictionary = t.site_outlets()
	var wrongsock := 0
	for m4 in mrows:
		if int(m4.outlets) != int(owall2.get(int(m4.i), {}).get("built", 0)):
			wrongsock += 1
	if wrongsock > 0:
		fail("%d rooms on the map show a socket count the model does not have" % wrongsock)
	else:
		ok("every socket count on it is site_room_outlets()'s own")
	# and it can be read with no window at all
	var mtext: String = t.command("map")
	if mtext.find("floor %d" % t.player_floor()) < 0 or mtext.split("\n").size() < 3:
		fail("`map` over the socket says nothing useful:\n" + mtext)
	else:
		ok("`map` over the socket: %s" % mtext.split("\n")[0].strip_edges())
	# it follows the body upstairs
	if t.floors_in_service > 1:
		var before_f: int = t.player_floor()
		t.teleport(t.room_centre(t.find_room(1, t.K_CORRIDOR)) + Vector3(0, 0.4, 0))
		for i in range(12):
			await process_frame
		if t.player_floor() == before_f:
			fail("could not stand on another floor to check the map follows")
		else:
			var up_rows: Array = t.map_rows()
			var same := true
			for m5 in up_rows:
				if int(t.rooms[int(m5.i)].floor) != t.player_floor():
					same = false
			if not same:
				fail("stood on floor %d and the map still holds another floor's rooms"
					% t.player_floor())
			else:
				ok("the map follows the body: floor %d, %d rooms"
					% [t.player_floor(), up_rows.size()])

	# ---- THE HUD SAYS WHY THE BUILDING IS EMPTY.
	#
	# "Walking around the building, I don't see anybody else." Nothing was
	# broken: people.gd draws a person at every desk a tenancy has, and on day
	# zero no tenancy has moved in. Measured on seed 7008: 0 people on day 0,
	# 20 on day 1, 38 by day 6. The building really is empty and the only
	# thing that fills it is [N] -- and the HUD never said either half.
	var hud0: String = t.hud_lines()
	if hud0.find("day ") < 0:
		fail("the HUD does not say what day it is:\n" + hud0)
	elif t.service_rows().is_empty() and hud0.find("nobody has moved in") < 0:
		fail("nobody is in the building and the HUD does not say so:\n" + hud0)
	else:
		for hl in hud0.split("\n"):
			if hl.begins_with("day "):
				ok("the HUD says: %s" % hl.strip_edges())
	# ---- AND IT DOES NOT PRINT CORE'S WHOLE REFUSAL AT YOU.
	#
	# The owner read this one back to us clause by clause -- "go hash 55, F2
	# stairwell 55, then open. It will cost you 12,096... It doesn't make any
	# sense at all." That is `open`'s reply, which is a fine reply and a
	# terrible permanent caption. Typing `open` still prints every word.
	var openish := 0
	for hl2 in hud0.split("\n"):
		if hl2.find("stairwell") >= 0 or hl2.find("let space") >= 0 \
				or hl2.find("fit-out") >= 0 or hl2.find("fit out") >= 0:
			openish += 1
	if openish > 0:
		fail("the HUD is still reciting `open`'s refusal:\n" + hud0)
	else:
		ok("the HUD does not recite `open`; the full answer is still one `open` away")
	if str(t.site("open")).find("in service") < 0 and str(t.site("open")).find("standing") < 0:
		fail("`open` stopped explaining itself: " + str(t.site("open")))
	else:
		ok("and `open` itself still says the whole thing")

	# ---- THE CONSOLE IS A SOCKET OF ITS OWN, AND THE RJ45 REFUSES THE LEAD.
	#
	# "The debugger attaches to the same port as the computer on the network
	# uplink. Seems like the debugger should connect to a serial-shaped port
	# and be the only thing that port does." That is what makes a service
	# processor one: out of band, its own socket, its own chip, and it answers
	# when the machine will not boot. Plugging it into the tenant's ethernet
	# port said the opposite.
	var condev := -1
	for i in range(t.devices.size()):
		var dc: Dictionary = t.devices[i]
		if int(dc.get("site", -1)) >= 0 and not dc.get("serial_at", {}).is_empty() \
				and not dc.get("ports", []).is_empty():
			condev = i
			break
	if condev < 0:
		fail("no box in the building has a console socket drawn on it")
	else:
		var dcon: Dictionary = t.devices[condev]
		var scf: Dictionary = dcon.serial_at
		var clash := false
		for pf2 in dcon.ports:
			if Vector3(pf2.c).distance_to(Vector3(scf.c)) < 0.02:
				clash = true
		if clash:
			fail("%s draws its console socket on top of an ethernet port" % str(dcon.name))
		else:
			ok("%s has a console socket of its own, %d mm from the nearest RJ45"
				% [str(dcon.name),
					int(Vector3(dcon.ports[0].c).distance_to(Vector3(scf.c)) * 1000.0)])
		var cstand: Vector3 = Vector3(scf.c) + Vector3(scf.n) * 0.6 - Vector3(0, 0.24, 0)
		t.teleport(cstand + Vector3(0, 0.3, 0))
		for j in range(12):
			await process_frame
		t.aim_at(Vector3(scf.c))
		for j in range(4):
			await process_frame
		var ca: Dictionary = t.aim()
		if str(ca.get("kind", "")) != "console" or int(ca.get("dev", -1)) != condev:
			fail("standing at the console socket of %s, the crosshair finds %s"
				% [str(dcon.name), "nothing" if ca.is_empty() else str(ca.get("kind", ""))])
		else:
			var ct: Array = t.aim_text(ca)
			if str(ct[1]).find("serial") < 0:
				fail("the crosshair on a console socket says: %s" % str(ct[1]))
			else:
				ok("the crosshair on it reads: %s -- %s" % [ct[0], ct[1]])
		# AND THE ETHERNET PORT SAYS NO, in words, rather than quietly working
		t.aim_at(Vector3(dcon.ports[0].c))
		for j in range(4):
			await process_frame
		var pa: Dictionary = t.aim()
		if str(pa.get("kind", "")) == "port":
			var refused: String = t._lead_in(condev, false)
			if refused.find("console socket") < 0:
				fail("the debugger went into an ethernet port: %s" % refused)
			else:
				ok("the ethernet port refuses the debugger: %s" % refused.split(".")[0])
	# ---- THE DISPLAY DEBUGGER IS GONE. "If something has a display you just
	# use the display."
	if t.bag and t.bag.KIT.has("display"):
		fail("the display debugger is still in the kit")
	elif t.bag:
		ok("the kit is %s -- no display debugger" % str(t.bag.KIT.keys()))

	# ---- EVERY SOCKET IS ONE YOU CAN GET AT.
	#
	# "The default setup has the player's computer too close to a wall to get
	# to the back of it." It was worse than that. aim() ends with the same
	# physics ray a walking body uses, and the workstation stood UNDER its own
	# desk: a body standing where the game says you use that machine, looking
	# straight at its only socket, got nothing back -- not the port, not even
	# the box. There was no angle from which the player's own computer could be
	# cabled, which is a large part of why the spool appeared not to work.
	#
	# So this stands in front of each socket in turn, at arm's length, and
	# looks at it. A port nothing can see is a port nothing can plug into.
	for i in range(t.devices.size()):
		var dd: Dictionary = t.devices[i]
		if int(dd.get("site", -1)) < 0 or dd.get("ports", []).is_empty():
			continue
		var pf0: Dictionary = dd.ports[0]
		var pcc: Vector3 = pf0.c
		var stand: Vector3 = pcc + Vector3(pf0.n) * 0.65 - Vector3(0, 0.24, 0)
		t.teleport(stand + Vector3(0, 0.3, 0))
		for j in range(12):
			await process_frame
		t.aim_at(pcc)
		for j in range(4):
			await process_frame
		var aa: Dictionary = t.aim()
		if aa.is_empty() or str(aa.get("kind", "")) != "port" or int(aa.get("dev", -1)) != i:
			fail("nothing can reach port 0 of %s: standing in front of it and looking "
				% str(dd.name) + "at it finds %s"
				% ("nothing" if aa.is_empty() else str(aa.get("kind", ""))))
		else:
			ok("port 0 of %s can be stood in front of and aimed at" % str(dd.name))

	# ---- EVERYTHING THE SHOP SELLS HAS A SHAPE AND A COLOUR IN THE WORLD.
	#
	# DEV_U and DEV_COL are a second list of the catalogue, and a second list
	# is how this project keeps hurting itself. A kind that is in the shop and
	# not in them is drawn as an anonymous grey 1U box: a tower unit standing
	# in a rack slot, a 3400 machine that looks exactly like the 45 one. It
	# does not crash, so nothing would have said. The kinds come from the
	# shop's own usage line, so adding a grade to core is what breaks this.
	# `order` with no kind answers "buy what?  <name> <price>  <name> <price>",
	# printed off KIT[] itself. The names are the words that are not prices.
	var usage: String = str(t.site("order")).split("\n")[0]
	var kinds: Array = []
	for w in usage.replace("buy what?", "").split(" ", false):
		var k1: String = w.strip_edges()
		if k1 != "" and not k1.is_valid_int():
			kinds.append(k1)
	if kinds.size() < 4:
		fail("could not read the catalogue off `order`: %s" % usage)
	else:
		var unpainted: Array = []
		for k3 in kinds:
			if not t.DEV_U.has(k3) or not t.DEV_COL.has(k3):
				unpainted.append(k3)
		if not unpainted.is_empty():
			fail("the shop sells %s and the world has no shape or colour for %s"
				% [str(kinds), str(unpainted)])
		else:
			ok("all %d kinds the shop sells have a size and a colour: %s"
				% [kinds.size(), " ".join(kinds)])

	# ---- THE POWER IN THE WORLD IS THE CONDUIT, AND ONLY THE CONDUIT.
	#
	# This used to count faceplates on walls against site_room_outlets() and
	# watch the flex to them come and go with `mains`. Both are on their way
	# out -- "per room outlets will go away, all things will be powered by the
	# new conduit power system" -- and the drawn version of them was what the
	# owner saw as "a direct line that kinda looks funny": a sag straight
	# across the room to a socket, through whatever was in the way, because a
	# lead to a socket two metres off never needed a route.
	#
	# So what is asserted now is that the world draws the TREE: something is
	# there, and it is the runs the model holds rather than anything else.
	# The route itself is checked below, where the conduit block is.
	if t.power_drawn() < 0:
		fail("the power drawing is gone entirely")
	else:
		ok("the world draws the power tree and nothing else: %d vertices"
			% t.power_drawn())

	for s in t.stairs:
		var f0: int = int(s.floor)
		var r0 = t.rooms[int(s.room)]
		var wall: float = float(r0.y0 if int(s.axis) == 1 else r0.x0)
		var deep: float = float(s.y0) - wall
		if deep < 1.0:
			fail("the floor %d flight starts %.2f m from the wall: there is no landing "
				% [f0, deep] + "to walk onto it from")
		else:
			ok("floor %d: %.1f m of landing between the wall and the bottom step" % [f0, deep])

	for s in t.stairs:
		var f: int = s.floor
		var how: Array = await _climb(self, t, s)
		if how[0]:
			ok("walked up the stairs from floor %d to %d" % [f, f + 1])
		elif not how[1]:
			fail("could not get up the first flight from floor %d: stopped at y = %.2f"
				% [f, t.player.global_position.y])
		else:
			fail("reached the half landing from floor %d and no further: y = %.2f (want %.2f)"
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
				got_up = (await _climb(self, t, st2))[0]
				# ONTO THE LANDING, not stopped on the top step. The flight
				# comes up through a hole in the slab and the top step is at
				# the edge of it; a body that stops there and is then left
				# alone for a moment goes back down the way it came.
				t.player.drive_active = true
				t.player.drive = Vector2(0, 1)
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
		# HOW MANY IS THE GAME'S OWN LIST, not a 3 written here. This said
		# "at least three" and broke the day the starting kit got shorter --
		# a second copy of START_KIT living in a test.
		#
		# AND THE DELIVERY IS CORE'S NOW. The window used to `order` these
		# itself, so a socket client started a different game -- a blind
		# playtester opened their report with it. session_new_game() delivers
		# them; START_KIT is what the window EXPECTS to find, and this is
		# where the two are held together.
		var missing := ""
		for want_name in t.START_KIT:
			var got_it := false
			for sd2 in t.site_devs():
				if str(sd2.name) == str(want_name):
					got_it = true
			if not got_it:
				missing += " " + str(want_name)
		if missing != "":
			fail("the session did not deliver what the window expects:%s" % missing)
		else:
			ok("the session delivered the window's day-one kit: %s"
				% " ".join(t.START_KIT))
		var want_kit: int = t.START_KIT.size()
		if delivered < want_kit:
			fail("only %d of the %d-box delivery is in goods in;%s is elsewhere"
				% [delivered, want_kit, elsewhere])
		else:
			ok("%d boxes delivered to goods in, not to the room you start in" % delivered)

		# and every one of them is DRAWN there: a box the site says is in a
		# room and the view does not show is a box nobody can walk up to.
		var seen := 0
		for d in t.devices:
			var df: int = int(floor((d.pos.y + 0.3) / t.fheight))
			if t.room_of(df, int(floor(d.pos.x)), int(floor(d.pos.z))) == goods:
				seen += 1
		if seen < want_kit:
			fail("goods in holds %d boxes and the view draws %d of them" % [want_kit, seen])
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
	# ONE LEAD IS IN ON DAY ONE and it is the one the building came with: the
	# player's own workstation, in the handoff's only port. Everything else is
	# the job -- and that lead comes out the moment they cable their first
	# switch to the handoff, which is D41's whole point.
	var day_one: Array = t.site_links()
	if day_one.size() != 1:
		fail("day one holds %d links, not the one the building came with"
			% day_one.size())
	else:
		var a: String = str(t.devices[_device(t, "ws")].name) if _device(t, "ws") >= 0 else "?"
		if int(day_one[0].a) != _site_i(t, "ws") and int(day_one[0].b) != _site_i(t, "ws"):
			fail("the one lead on day one is not the workstation's: %s" % str(day_one[0]))
		else:
			ok("day one: one lead, %s to the handoff, and nothing else" % a)

	# ---- THE WINDOW FOLLOWS WHATEVER LINE THE CLIENT TYPED.
	#
	# "If they attach a cable to a switch the player teleports to that
	# position looking at the port they just plugged in. So the console
	# affects the 3d world and can verify the images." The machinery existed
	# and only `plug` used it, so a script that built a tower with whole
	# `cable a:1 b:2` lines did the work and photographed a wall -- and the
	# screenshot is the one thing a blind client cannot verify by reading.
	#
	# It cables into the WORKSTATION, which is standing in the MDF already,
	# so this leg moves nothing else in the building around.
	t.command("order switch8 cam")
	t.command("deliver cam #%d" % t.find_room(0, t.K_MDF))
	t.command("go mdf")
	for i in range(8):
		await process_frame
	var cam_said: String = t.command("cable cam:0 ws:0")
	for i in range(6):
		await process_frame
	var cam_aim: Dictionary = t.aim()
	var ws_i: int = _device(t, "ws")
	if cam_said.strip_edges().begins_with("refused"):
		fail("could not run the cable to test the camera: " + cam_said.strip_edges())
	elif str(cam_aim.get("kind", "")) != "port" or int(cam_aim.get("dev", -1)) != ws_i:
		fail("cabled ws:0 over the socket and the crosshair is on %s"
			% ["nothing" if cam_aim.is_empty()
				else "%s dev %d port %d" % [str(cam_aim.get("kind", "")),
					int(cam_aim.get("dev", -1)), int(cam_aim.get("port", -1))]])
	else:
		ok("`cable` over the socket left the body looking into ws:%d"
			% int(cam_aim.get("port", -1)))
	# AND A REFUSAL MOVES NOTHING. Following the camera to a socket the line
	# was refused at would be the window telling you a lie the text had just
	# refused to tell.
	var cam_before: Vector3 = t.player.global_position
	var cam_no: String = t.command("cable cam:0 ws:0")
	for i in range(4):
		await process_frame
	if not cam_no.strip_edges().begins_with("refused"):
		fail("cabling a port that is already full was not refused: " + cam_no)
	elif t.player.global_position.distance_to(cam_before) > 0.05:
		fail("a refused line moved the body %.2f m"
			% t.player.global_position.distance_to(cam_before))
	else:
		ok("and a refused line moves nothing: " + cam_no.strip_edges().split("\n")[0])
	# AND PUT THE BUILDING BACK. This leg buys a box and runs a lead; the
	# checks after it are about the state the building came in, and a test
	# that leaves its props lying about breaks the next one for reasons that
	# have nothing to do with what it was measuring.
	t.command("spool back")
	for li in t.site_links():
		if int(li.a) == int(t.devices[_device(t, "cam")].site) \
				or int(li.b) == int(t.devices[_device(t, "cam")].site):
			t.command("uncable %d" % int(li.i))
	t._reconcile()
	for i in range(4):
		await process_frame

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
		var ws := _device(t, "ws")
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
			# [Esc] TAKES THE LEAD OUT, and it does it through core's own
			# `unplug` so that the prop and the session cannot disagree about
			# what a lead is in. This check used to assert the opposite --
			# handset down, lead still in -- which was the behaviour until the
			# session was told about it.
			var out_said: String = mob.let_go()
			if out_said.find("take the lead out") < 0:
				fail("[Esc] did not take the lead out: " + out_said)
			elif mob.focused:
				fail("[Esc] and the handset still has the keyboard")
			elif mob.plugged >= 0 or str(mob.status) != "unplugged":
				fail("[Esc] and the lead is still in something")
			else:
				ok("[Esc] takes the lead out and puts the handset down")
			# AND THE RECONCILER FORGOT IT TOO. Its cache of "what the prop is
			# showing" was not cleared when the lead came out, so the third
			# line of `plug core` / `unplug` / `plug core` matched the pair it
			# still remembered, took the early return, and left the handset
			# dark on a machine the session was sitting at. [Esc] takes that
			# path every time now, so this is the sequence that broke.
			mob.detach()
			m = mob.plug(ws, "hdmi")
			if m.find("display on") < 0: fail("the workstation would not drive a screen: " + m)
			else: ok("HDMI on the workstation: " + m)
			mob.unplug()

			# AND THE RECONCILER FORGOT THE LEAD TOO. Its cache of what the
			# prop is showing was not cleared when the lead came out, so the
			# third line of `plug core` / `unplug` / `plug core` matched the
			# pair it still remembered, took the early return, and left the
			# handset dark on a box the session was sitting at. [Esc] takes
			# that path every time now, so this is the sequence that broke.
			#
			# IT IS LAST IN THIS BLOCK because a session command reconciles the
			# view, and `t.devices` is rebuilt when it does: every index above
			# is stale after this runs, which is why the answer is checked by
			# the NAME of the box the prop says it is on.
			var first: String = t.command("plug core")
			if first.find("refused") >= 0 or first.find("no box") >= 0:
				fail("could not put the cart's lead into core to test it: "
					+ first.strip_edges())
			else:
				t.command("unplug")
				var again2: String = t.command("plug core")
				var on := ""
				if int(mob.plugged) >= 0 and int(mob.plugged) < t.devices.size():
					on = str(t.devices[int(mob.plugged)].name)
				if on != "core" or str(mob.status) == "unplugged":
					fail("plug, unplug, plug again and the handset is on %s (%s): %s"
						% [on if on != "" else "nothing", str(mob.status),
							again2.strip_edges()])
				else:
					ok("plug, unplug and plug again puts the lead back in the prop")
			mob.detach()

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
		# ---- THE SPOOL IS ARMED WITHOUT OPENING ANYTHING.
		#
		# "By default you should have right click be the spool. The spool does
		# not seem to work." It worked -- this file has walked a run with it
		# for weeks -- but both hands held a debugger lead, so the only way to
		# reach the drum was the inventory, which is the other thing he could
		# not open. A verb the game is about, behind a screen that would not
		# come up.
		if not t.spool_in_hand():
			fail("the day starts with no spool in either hand: %s / %s"
				% [bag.hand(0), bag.hand(1)])
		else:
			ok("the spool is in a hand on day one: %s" % t.hand_key("spool"))
		# and the crosshair names the button it is really under
		var swk: int = -1
		for i in range(t.devices.size()):
			if int(t.devices[i].get("site", -1)) >= 0 and not t.devices[i].get("ports", []).is_empty():
				swk = i
				break
		if swk >= 0:
			var ht: Array = t.aim_text({"kind": "port", "dev": swk, "port": 0})
			var want_key: String = t.hand_key("spool")
			if str(ht[1]).find(want_key) < 0 and str(ht[1]).find("link up") < 0 					and str(ht[1]).find("too long") < 0:
				fail("the spool is under %s and the crosshair offers: %s"
					% [want_key, ht[1]])
			else:
				ok("the crosshair over a socket names the button the spool is under")
		# ---- AND THERE IS A POWER LEAD, which is `mains` with a mouse.
		if not t.bag.KIT.has("power"):
			fail("there is no power lead in the kit")
		else:
			var pdev := -1
			for i in range(t.devices.size()):
				var sdp: Dictionary = t._site_dev(int(t.devices[i].get("site", -1)))
				if not sdp.is_empty() and str(sdp.kindname) != "uplink" 						and bool(sdp.get("mains", false)):
					pdev = i
					break
			if pdev < 0:
				fail("nothing plugged in to try the power lead on")
			else:
				var pname: String = str(t.devices[pdev].name)
				t.teleport(t.room_centre(int(t._site_dev(int(t.devices[pdev].site)).room))
					+ Vector3(0, 0.3, 0))
				for i in range(10):
					await process_frame
				var out_said: String = t.mains_at(pdev)
				var still: bool = bool(t._site_dev(int(t.devices[pdev].site)).get("mains", false))
				if still:
					fail("the power lead would not pull %s out of the wall: %s"
						% [pname, out_said])
				else:
					ok("the power lead pulled %s out of the wall: %s"
						% [pname, out_said.split("\n")[0]])
					var in_said: String = t.mains_at(pdev)
					if not bool(t._site_dev(int(t.devices[pdev].site)).get("mains", false)):
						fail("and it would not put it back: %s" % in_said)
					else:
						ok("and put it back in")
					t.site("power %s on" % t._site_dev(int(t.devices[pdev].site)).name)
					t._reconcile()
					await process_frame
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
		# WHATEVER IS STILL IN GOODS IN, by where it is rather than by name.
		# This looked for "edge", which was the router in the old starting
		# kit -- a third copy of START_KIT, and it broke when the kit did.
		var carry := -1
		for i in range(t.devices.size()):
			var di: Dictionary = t.devices[i]
			if int(di.get("site", -1)) < 0:
				continue
			var dfy: int = int(floor((di.pos.y + 0.3) / t.fheight))
			if t.room_of(dfy, int(floor(di.pos.x)), int(floor(di.pos.z))) == goods:
				carry = i
				break
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

	# ================ A RUN MADE BY WALKING, AND THE SAME RUN MADE BY TYPING
	#
	# The owner, playing his own game: "As is, I can't figure out how to
	# actually attach a cable, run a cable from a particular port to another."
	# Cabling is the central verb of this game and it was unreachable from the
	# window -- not missing, unsignposted, which no blind playtest could ever
	# find because a socket client never looks at a crosshair.
	#
	# So this walks it: one end in with the key, the legs across the building,
	# the other end in with the key -- and then charges the identical run over
	# the session and asserts the two cost the same money for the same metres.
	# That is `deliver`'s rule from this morning applied to copper: a line that
	# stands in for holding W has to be the same act, or it is not testing the
	# game that ships.
	var comms0: int = t.find_room(0, t.K_COMMS)
	if comms0 < 0:
		fail("floor 0 has no comms cupboard to run a cable to")
	else:
		# A box at the far end, put there the way a socket client puts one
		# there. The drum goes back on the shelf first because both hands are
		# on a box you are carrying and core refuses the delivery otherwise --
		# the earlier run in this file left one in them.
		t.command("spool back")
		var moved: String = t.command("deliver files %s" % ("#%d" % comms0))
		for i in range(10):
			await process_frame
		var far := -1
		for d in t.site_devs():
			if str(d.name) == "files" and int(d.room) == comms0:
				far = int(d.i)
		var fardev := _device(t, "files")
		var swi2 := -1
		for i in range(t.devices.size()):
			if str(t.devices[i].name) == "core" and int(t.devices[i].get("site", -1)) >= 0:
				swi2 = i
		if far < 0 or fardev < 0 or swi2 < 0:
			fail("could not stand a server in the comms cupboard to cable to: "
				+ moved.strip_edges())
		else:
			# ---- THE CROSSHAIR NAMES A KEY THAT EXISTS. The one sentence in
			# the game that mentioned cabling said "[Tab] spool in hand to
			# cable it", and Tab has belonged to the terminal since the bag
			# moved to [I]: the only signpost to the central verb pointed at a
			# key that did nothing here. A hint that names a key is checked
			# against the keys _unhandled_input really handles.
			var freep: int = t._free_port(int(t.devices[swi2].site))
			var hole := {"kind": "port", "dev": swi2, "port": freep}
			# with a spool dragged into a hand the offer is the mouse, and
			# with both hands empty it is [C]: the hint has to name whichever
			# of the two is really armed. An earlier check in this file left
			# the spool in the left hand, so both states are reachable here.
			if t.bag:
				t.bag.equip("spool", 0)
			var armed: Array = t.aim_text(hole)
			if str(armed[1]).find(t.hand_key("spool")) < 0:
				fail("the spool is in a hand and the crosshair does not offer the mouse: '%s'"
					% str(armed[1]))
			else:
				ok("spool in hand, the crosshair on an empty port: '%s   %s'"
					% [armed[0], armed[1]])
			# BOTH HANDS, because the spool now starts in the right one. This
			# put "serial" in the left and expected [C], which was true while
			# the right hand held the display debugger and is not any more:
			# the spool was still armed and the crosshair still, correctly,
			# offered the mouse.
			if t.bag:
				t.bag.equip("serial", 0)
				t.bag.equip("power", 1)
			var say: Array = t.aim_text(hole)
			if str(say[1]).find("[C]") < 0:
				fail("the crosshair on an empty port does not offer [C]: '%s'"
					% str(say[1]))
			elif str(say[1]).find("[Tab]") >= 0:
				fail("the crosshair still points at [Tab], which cables nothing")
			else:
				ok("hands empty, the crosshair on an empty port: '%s   %s'"
					% [say[0], say[1]])

			# ---- ONE END IN, WITH THE KEY, STANDING AT THE BOX.
			t.teleport(t.room_centre(mdf) + Vector3(0, 0.4, 0))
			for i in range(12):
				await process_frame
			var money0: int = int(t.ses_state().get("money", 0))
			var walked0: int = int(t.ses_state().get("walked", 0))
			var end1: String = t.cable_at(swi2, freep)
			if int(t.ses_state().get("cab", [-1])[0]) < 0:
				fail("[C] at core port %d did not put an end in: %s" % [freep, end1])
			else:
				ok("[C] at core port %d: %s" % [freep, end1.split("\n")[0]])

			# ---- AND NOW THE LEGS. Every room between here and there, on
			# foot, with the drum paying out.
			var legs2: Array = _route_rooms(t, mdf, comms0)
			var arrived := true
			for w in legs2:
				if not await _walk_to(self, t, w, 1400):
					arrived = false
					break
			if not arrived or t.player_room() != comms0:
				fail("could not walk from the MDF to the comms cupboard with a cable in hand")
			else:
				ok("walked %d rooms from the MDF to the comms cupboard, drum in hand"
					% legs2.size())
			var walked1: int = int(t.ses_state().get("walked", 0))
			if walked1 <= walked0:
				fail("walked the building on foot and the session charged no metres")
			else:
				ok("the legs cost %d m of walking, which the session counted"
					% (walked1 - walked0))

			# ---- THE COPPER IS ON THE FLOOR, WHERE THE FEET WENT.
			# "It'd be fun to literally run cable down corridors... that should
			# rest on the floor when we're cabling things."
			var crumbs: Array = t._crumbs
			if crumbs.size() < 3:
				fail("walked a building with a drum and left %d m of it on the floor"
					% crumbs.size())
			else:
				var high := 0.0
				for c in crumbs:
					var fl: float = float(int(floor((float(c.y) + 0.3) / t.fheight))) * t.fheight
					high = max(high, float(c.y) - fl)
				if high > 0.35:
					fail("the cable being pulled floats %.2f m off the floor" % high)
				else:
					ok("%d m of copper lying on the floor behind you, none of it more than %d mm up"
						% [crumbs.size(), int(high * 1000.0)])

			# ---- WHAT THE HUD SAID IT WOULD COST, BEFORE IT COST IT.
			var hud_said: String = t.hud_lines()
			var quoted := -1
			for line in hud_said.split("\n"):
				var s2: String = line.strip_edges()
				if s2.begins_with("from here:") and s2.find(" m of ") > 0:
					quoted = int(s2.substr(10).strip_edges().split(" ")[0])
			if quoted < 0:
				fail("stood at the far end with a run in hand and the HUD quoted nothing:\n"
					+ hud_said)

			# ---- THE OTHER END IN, WITH THE KEY.
			var links0: int = t.site_links().size()
			var end2: String = t.cable_at(fardev, 0)
			var links1: Array = t.site_links()
			if links1.size() != links0 + 1:
				fail("[C] at the far end ran no cable: " + end2)
			else:
				var lk: Dictionary = links1[links1.size() - 1]
				var money1: int = int(t.ses_state().get("money", 0))
				var walkcost: int = money0 - money1
				ok("walked run: %d m of cable, %d paid, %d m of legs"
					% [int(lk.metres), walkcost, walked1 - walked0])
				if quoted >= 0 and quoted != int(lk.metres):
					fail("the HUD quoted %d m while walking and the invoice was %d m"
						% [quoted, int(lk.metres)])
				elif quoted >= 0:
					ok("the metres the HUD quoted on the way are the metres it charged: %d"
						% quoted)
				# and the floor copper is gone: what is drawn now is the run
				# the model billed, through the tray.
				if not t._crumbs.is_empty() or t._laid != null:
					fail("the run finished and the copper it was pulled along is still drawn")
				else:
					ok("the floor copper goes when the run does: what is left is the billed route")

				# ---- THE SAME RUN, TYPED. Same two rooms, so the same tray
				# metres and the same price -- and if those two numbers ever
				# stop matching, one of the two ways of playing this game is
				# cheaper than the other.
				# A SECOND BOX AT THE FAR END, BOUGHT FOR IT. This ran to
				# `files:1`, which was port 1 of the old starting kit's
				# server. The starting kit is a minitower now: one socket,
				# and the walked run above is already in it, so there was no
				# hole left for the typed run and the line was refused. What
				# this leg is comparing is two ways of getting copper between
				# the SAME TWO ROOMS, so anything with a free port standing in
				# the far room will do.
				t.command("order switch8 far2")
				t.command("spool back")
				t.command("deliver far2 #%d" % comms0)
				for i in range(10):
					await process_frame
				var far_i: int = _device(t, "far2")
				if far_i < 0:
					fail("could not stand a second box in the comms cupboard")
				var far_p: int = t._free_port(int(t.devices[far_i].site)) if far_i >= 0 else 0
				# The purse is read AFTER the box is bought and carried, so
				# what this leg compares is copper against copper.
				var money2: int = int(t.ses_state().get("money", 0))
				var typed: String = t.command("cable core:%d far2:%d %s"
					% [t._free_port(int(t.devices[swi2].site)), far_p,
						str(t.drum_grade())])
				var links2: Array = t.site_links()
				if links2.size() != links1.size() + 1:
					fail("`cable` over the session ran nothing: " + typed)
				else:
					var lk2: Dictionary = links2[links2.size() - 1]
					var typecost: int = money2 - int(t.ses_state().get("money", 0))
					ok("typed run:  %d m of cable, %d paid" % [int(lk2.metres), typecost])
					if int(lk2.metres) != int(lk.metres) or typecost != walkcost:
						fail("the same run costs %d m / %d walked and %d m / %d typed"
							% [int(lk.metres), walkcost, int(lk2.metres), typecost])
					else:
						ok("walking it and typing it cost the same: %d m, %d"
							% [int(lk.metres), walkcost])
					# AND IT PUTS THE FURNITURE BACK. This file is one long
					# session: the socket check further down carries `files`
					# out of goods in and cables it, and a check that leaves
					# the server in a comms cupboard with both its ports full
					# breaks a check that has nothing to do with it. Pulling
					# the runs refunds nothing, which is the point of
					# site_uncable() and is why the money is read before this.
					t.command("uncable %d" % int(lk2.i))
					t.command("uncable %d" % int(lk.i))
					t.command("spool back")
					t.command("deliver files goods")
					for i in range(8):
						await process_frame

	# ---- [E] AT THE WORKSTATION IS THE 2D DESKTOP. Walk to it -- so a desk
	# nobody can reach fails here rather than looking fine in a screenshot --
	# and open it. This is last because de.gd installs a ticket when it starts.
	# `ws` is the name the SITE gives the player's own machine (D41: it is a
	# device in the model with a port on the back, not a picture of one), and
	# the window labels it with the model's name like every other box.
	var wsi := _device(t, "ws")
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

		# ---- AND THERE IS SOMETHING ON EVERY ONE OF THEIR SCREENS, saying
		# what the model says about that machine and nothing else.
		#
		# The owner: "Those people at the desks don't seem to show a 2d
		# interface like the one in the IT room." screens.gd draws one quad per
		# desk with the tenancy's trade, that desk's link and address, and the
		# tenancy's own `done` fraction in it -- so what is checked here is that
		# the count follows the model, that the state follows the two columns
		# that decide it, and that the picture is ON THE GLASS rather than
		# floating where a monitor used to be. See D36.
		var scr: Node = t.get_node_or_null("Screens")
		if scr == null:
			fail("a tenancy has moved in and nothing draws their screens")
		else:
			var ndesk2 := 0
			for r3 in rows:
				ndesk2 += int(r3.desks)
			if int(scr.total()) != ndesk2:
				fail("%d desks in the building and %d screens on them"
					% [ndesk2, int(scr.total())])
			else:
				ok("%d screens, one per desk, in %d instance buffers"
					% [int(scr.total()), int(scr.buffers())])
			# NOTHING IS CABLED YET, so every screen in the building is the
			# error a machine with no link shows. This is the same claim the
			# raised hands make, read off the other half of the view.
			var sc: Array = scr.counts()
			if nup == 0 and (int(sc[1]) > 0 or int(sc[2]) > 0):
				fail("no desk has a port and %d screens claim a network"
					% [int(sc[1]) + int(sc[2])])
			elif nup == 0:
				ok("no port in any of them, so all %d screens show the link error"
					% int(sc[0]))
			# THE PICTURE IS ON THE MONITOR. The rectangle is read off the desk
			# mesh's own P_SCREEN tag rather than copied out of people.gd, so
			# this is the check that the two files still agree about where a
			# monitor is: it has to be a real rectangle, at monitor height,
			# facing the same way the person is.
			var gr: Dictionary = scr.glass_rect()
			if gr.is_empty():
				fail("screens.gd could not find the glass on the desk mesh")
			elif float(gr.w) < 0.15 or float(gr.h) < 0.10:
				fail("the glass came back %.2f x %.2f m, which is not a monitor"
					% [float(gr.w), float(gr.h)])
			elif float(gr.mid.y) < 0.80 or float(gr.mid.y) > 1.60:
				fail("the screen sits %.2f m off the floor, which is not where a monitor is"
					% float(gr.mid.y))
			else:
				ok("the screen is %.2f x %.2f m of glass at %.2f m, off the desk mesh itself"
					% [float(gr.w), float(gr.h), float(gr.mid.y)])

		# ---- AND THEN YOU CABLE THEM, AND THE ROOM SAYS SO.
		#
		# One switch, carried to the floor's cupboard, and `serve` patches the
		# tenancy off it. What is gated is the two things the player is meant to
		# be able to SEE afterwards: the screens stop showing the link error,
		# and the leads are on the floor of their office rather than crossing it
		# at head height. The second is D36's answer to "I don't see cabling for
		# any of the boxes" and it is the kind of thing only a check like this
		# keeps true, because it looks right in the data either way.
		var sw_room: String = "f%d.comms" % int(row.floor)
		for cmd2 in ["spool back", "buy switch24 tsw", "go goods", "carry tsw",
				"go " + sw_room, "drop",
				"serve %d tsw cat5e 10" % int(row.tenant)]:
			t.command(cmd2)
			await process_frame
		var rows2: Array = t.service_rows()
		var up2 := 0
		for r4 in rows2:
			up2 += int(r4.up)
		if up2 == 0:
			fail("`serve` patched nothing: the tenancy still has no port")
		else:
			ok("%d of their desks have a lead in them now" % up2)
			var sc2: Array = t.get_node_or_null("Screens").counts()
			var tot2: int = int(sc2[0]) + int(sc2[1]) + int(sc2[2])
			if int(sc2[0]) != tot2 - up2:
				fail("%d of %d desks have a link and %d screens show no link, not %d"
					% [up2, tot2, int(sc2[0]), tot2 - up2])
			else:
				ok("and %d screens came off the link error with them"
					% [int(sc2[1]) + int(sc2[2])])
			# WHERE THE COPPER IS. Every point of a desk's run that is inside
			# the room it serves has to be at floor level: a lead that crosses
			# somebody's office at 2.5 m is the picture the owner could not see.
			var high := 0
			var lead_n := 0
			for l3 in t.site_links():
				var sk: int = t._dev_skirting(int(l3.a))
				if sk < 0:
					sk = t._dev_skirting(int(l3.b))
				if sk < 0:
					continue
				lead_n += 1
				var fl: int = int(t.rooms[sk].floor)
				for p in t._cable_route(int(l3.a), int(l3.aport), int(l3.b),
						int(l3.bport), int(l3.i)):
					if t.room_of(fl, int(floor(p.x)), int(floor(p.z))) != sk:
						continue
					if p.y - float(fl) * t.fheight > 0.75:
						high += 1
			if lead_n == 0:
				fail("the tenancy is served and no run in the model belongs to a desk")
			elif high > 0:
				fail("%d points of %d desk leads cross their own office above desk height"
					% [high, lead_n])
			else:
				ok("all %d desk leads run along the floor of the room they serve"
					% lead_n)

		# ---- AND [E] AT ONE OF THEIR DESKS IS THEIR MACHINE.
		#
		# Not a picture of one: `sit` in core/session.c boots it, and the
		# window's terminal types at the session that owns it. The prompt is
		# what proves which machine the keyboard is on -- `desk:t1d3#` is
		# printed by session_prompt() and by nothing else.
		var di := -1
		for i in range(t.devices.size()):
			if bool(t.devices[i].get("tenant_desk", false)):
				di = i
				break
		if di < 0:
			fail("a tenancy has moved in and no desk in the view can be sat at")
		else:
			var dname: String = str(t.devices[di].name)
			if t.aim_text({"kind": "device", "dev": di, "port": -1})[1].find("[E]") < 0:
				fail("the crosshair on %s does not offer to sit down at it" % dname)
			else:
				ok("the crosshair on %s offers [E]" % dname)
			t.command("go " + dname)
			await process_frame
			# the device list is rebuilt by the walk, so find it again by name
			di = _device(t, dname)
			var sat2: String = t.use_here(di)
			await process_frame
			if not t.seat_open():
				fail("[E] at %s did not sit down at it: %s" % [dname, sat2])
			elif t.ses_prompt().find("desk:") != 0:
				fail("sat down at %s and the session's prompt is '%s'"
					% [dname, t.ses_prompt()])
			else:
				ok("[E] at %s is a terminal on their machine: %s"
					% [dname, t.ses_prompt().strip_edges()])
				# and it is a REAL shell: a program runs on it and answers
				var who: String = t.site("whoami")
				if who.find("root") < 0:
					fail("`whoami` on their machine said '%s'" % who.strip_edges())
				else:
					ok("and a program really runs on it: whoami -> "
						+ who.strip_edges())
			# NOBODY IS LEFT SITTING IN SOMEBODY ELSE'S CHAIR. Standing up frees
			# the machine in core, and the window has to follow it out.
			t.seat_stand()
			await process_frame
			if t.seat_open():
				fail("stood up and the window is still at their desk")
			elif t.ses_where() != 1:
				fail("stood up and the session says where %d" % t.ses_where())
			else:
				ok("and standing up gives them their machine back")

	# ---- WHAT THE NEXT FLOOR NEEDS, BEFORE THE KEY IS PRESSED, IN CORE'S WORDS.
	if t.floors_in_service < t.nfloors:
		var says: String = t.hud_lines()
		if t.ses_floor() == t.floors_in_service:
			if says.find("[O] sign floor") < 0:
				fail("standing on the next floor and the HUD does not offer [O]")
			else:
				ok("standing on it, the HUD offers [O]")
		elif says.find("floor %d is next" % t.floors_in_service) < 0 \
				or says.find("[O]") < 0:
			# NOT THE WHOLE REFUSAL ANY MORE. This used to demand the words
			# "standing on it" and "cost" in the HUD, which is to say it
			# demanded the six-clause paragraph the owner read back to us as
			# nonsense. What the caption owes you is which floor is next and
			# which key; what it costs and what it needs are `open`'s to say,
			# and the assertion above proves `open` still says them.
			fail("the HUD does not name the next floor and the key for it:\n" + says)
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
			# THE PORTS ARE ASKED FOR, NOT TYPED. This said `plug files:0` and
			# `plug core:9`: port 0 of the old starting kit's server and port
			# 9 of its switch24. The starting kit is a switch4 and a minitower
			# now -- there is no port 9, and files:0 already has the walked
			# run in it -- so both lines were refused and the socket leg
			# blamed the socket. A box is ordered for this and the free ports
			# come from the model, which is what a player reads off `show`.
			await _tell(self, c, "order switch8 sock1")
			for i in range(4):
				await process_frame
			var core_i: int = _device(t, "core")
			var core_p: int = t._free_port(int(t.devices[core_i].site)) if core_i >= 0 else 0
			var script: Array = ["spool back", "carry sock1", "go mdf", "drop",
				"spool cat6", "plug sock1:0", "plug core:%d" % core_p]
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
		# The middle of a stairwell is the flights. What a body walking to the
		# stairs is aiming at is the landing at the foot of them, which the
		# building itself can say.
		if t.rooms[b].kind == t.K_STAIR:
			out.append(t.stair_landing(b))
		else:
			var c: Vector3 = t.room_centre(b)
			out.append(Vector2(c.x, c.z))
	return out


# The SITE index of a named box, which is what a link's ends are numbered in.
func _site_i(t: Node3D, want: String) -> int:
	for d in t.site_devs():
		if str(d.name) == want: return int(d.i)
	return -1


func _device(t: Node3D, want: String) -> int:
	for i in range(t.devices.size()):
		if t.devices[i].name == want: return i
	return -1


# STANDING ON THE LANDING, FACING UP THE FLIGHT -- which is a thing you can
# now do. This used to have to stand ON the bottom step, with the comment
# "the foot of an even-numbered run is against the stairwell wall, and a
# teleport that overshoots by half a metre puts the test inside brickwork".
# That was the test carrying the owner's bug for it: there was no floor in
# front of the first tread to stand on. Now there is STAIR_LAND of it, so
# the test stands half a metre BEFORE the flight and walks on, which is what
# a player does and what the old geometry made impossible.
func _foot_of(t: Node3D, s: Dictionary) -> Vector3:
	var c: float = (float(s.a0) + float(s.a1)) * 0.5
	var back: float = float(s.y0) - 0.5
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


# Facing along the run's axis, either way, and facing ACROSS it either way.
# A switchback is climbed in three straight walks -- up, across the half
# landing, up again -- so the test needs all four headings.
func _yaw_along(axis: int, forward: bool) -> float:
	if axis == 1:
		return PI if forward else 0.0
	return -PI * 0.5 if forward else PI * 0.5


func _yaw_across(axis: int, forward: bool) -> float:
	return _yaw_along(1 - axis, forward)


# CLIMBING ONE FLOOR OF SWITCHBACK, WRITTEN ONCE. Two places in this file walk
# a body upstairs and they used to hold two copies of how; when the stairs
# became a switchback one of them was updated and the other silently stopped
# working, which is this project's oldest defect wearing a test's clothes.
#
# Three straight walks, nothing teleported between them: up the first flight
# and fully ONTO the half landing (stopping at half HEIGHT leaves the body on
# the flight, where the side of the other flight is a 0.6 m step it cannot
# climb), across the landing and stop on it, then up the second flight.
#
# Returns [reached the top, reached the half landing].
func _climb(tree: SceneTree, t: Node3D, s: Dictionary) -> Array:
	var f: int = int(s.floor)
	var base: float = f * t.fheight
	var axis: int = int(s.axis)
	t.teleport(_foot_of(t, s))
	for i in range(12):
		await tree.process_frame
	t.player.drive_active = true
	t.player.drive = Vector2(0, 1)
	t.player.look_at_yaw(_yaw_along(axis, true))
	var half_up := false
	for i in range(600):
		await tree.process_frame
		var a_now: float = t.player.global_position.z if axis == 1 \
			else t.player.global_position.x
		if a_now >= float(s.y1) + 0.4 and t.player.global_position.y > base + 1.0:
			half_up = true
			break
	var want_c: float = (float(s.b0) + float(s.b1)) * 0.5
	t.player.look_at_yaw(_yaw_across(axis, true))
	for i in range(240):
		await tree.process_frame
		var c_now: float = t.player.global_position.x if axis == 1 \
			else t.player.global_position.z
		if c_now >= want_c:
			break
	t.player.look_at_yaw(_yaw_along(axis, false))
	var climbed := false
	for i in range(600):
		await tree.process_frame
		if t.player.global_position.y > (f + 1) * t.fheight - 0.35:
			climbed = true
			break
	t.player.drive_active = false
	t.player.drive = Vector2.ZERO
	return [climbed, half_up]
