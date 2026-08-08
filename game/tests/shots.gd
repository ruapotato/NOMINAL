extends SceneTree
# Photographs of the station, for review. Builds the tower exactly as the
# window does and puts a camera at eye height in the middle of a room, looking
# along its long axis. No input is synthesised: the camera is placed, the
# frame is rendered, the image is saved.
# WHERE THEY GO. Pass a directory after `--`; the default is the working
# directory, so `Godot --headless --path game -s tests/shots.gd -- /tmp/x`
# fills /tmp/x with one PNG a room.
func _dir() -> String:
	var a := OS.get_cmdline_user_args()
	return (a[0] if a.size() > 0 else ".").rstrip("/")

func _init() -> void:
	var t = preload("res://scripts/tower.gd").new()
	get_root().add_child(t)
	await process_frame
	await process_frame
	var vp := get_root()
	var cam := Camera3D.new()
	cam.fov = 78.0
	vp.add_child(cam)
	cam.make_current()
	# ONE SHOT PER DECK KIND, because that is what the redesign is about:
	# a dock, a reactor, cabins, a promenade, offices and the bridge, each a
	# different shape rather than the same plate with the labels changed.
	var want := [
		["bridge", t.bridge_deck(), t.K_BRIDGE],
		["engineering", 0, t.K_MDF],
		["dock_corridor", 0, t.K_CORRIDOR],
	]
	for f in range(t.nfloors):
		var kn: String = t.deck_name(f)
		if kn == "reactor":
			want.append(["reactor", f, t.K_PLANT])
		elif kn == "cabins":
			want.append(["cabins", f, t.K_RESIDENCE])
		elif kn == "promenade":
			want.append(["promenade", f, t.K_RETAIL])
		elif kn == "office":
			want.append(["office_deck", f, t.K_OFFICE])
	for w in want:
		var ri: int = t.find_room(int(w[1]), int(w[2]))
		if ri < 0:
			continue
		var r: Dictionary = t.rooms[ri]
		var cx := (float(r.x0) + float(r.x1)) * 0.5
		var cz := (float(r.y0) + float(r.y1)) * 0.5
		var y: float = float(r.floor) * t.fheight + 1.62
		var wx: float = float(r.x1 - r.x0)
		var wz: float = float(r.y1 - r.y0)
		var eye: Vector3
		var at: Vector3
		if wx >= wz:
			eye = Vector3(float(r.x0) + 0.7, y, cz)
			at = Vector3(float(r.x1) - 0.5, y - 0.14, cz)
		else:
			eye = Vector3(cx, y, float(r.y0) + 0.7)
			at = Vector3(cx, y - 0.14, float(r.y1) - 0.5)
		if str(w[0]) == "engineering":
			# from the doorway corner, so the rack row is seen down its face
			eye = Vector3(float(r.x0) + 0.8, y, float(r.y1) - 0.8)
			at = Vector3(float(r.x1) - 0.8, y - 0.3, float(r.y0) + 0.8)
		# WALK THE PLAYER THERE FIRST. The HUD is the player's location, not
		# the camera's, so a photograph of the bridge with "deck 0" across
		# the top of it is a photograph of a lie. `go #<n>` is the same verb
		# a socket client uses and it takes the lift on the way.
		print("  ", t.command("go #%d" % ri).strip_edges().split("\n")[0])
		for i in range(2):
			await process_frame
		# A BRIDGE IS PHOTOGRAPHED FROM BEHIND THE CHAIR, looking at the
		# screen, because that is the shot that says what the room is for.
		if int(w[2]) == t.K_BRIDGE:
			var plan: Dictionary = t.bridge_plan(ri)
			var fwd: Vector3 = plan.forward
			eye = Vector3(plan.chair) - fwd * 4.4 + Vector3(0, 1.72, 0)
			at = Vector3(plan.screen) + Vector3(0, 1.35, 0)
		# THE CAMERA LIVES IN MODEL SPACE AND THE WORLD MAY BE BENT. Every
		# vertex goes through tower._bend() on its way into the mesh, so an
		# eye placed from a room's rectangle is somewhere the room no longer
		# is -- the first curved shot was taken from inside a wall.
		cam.global_position = t._bend(eye)
		cam.look_at(t._bend(at), Vector3.UP)
		for i in range(3):
			await process_frame
		vp.get_texture().get_image().save_png("%s/%s.png" % [_dir(), str(w[0])])
		print("shot ", w[0], " deck ", r.floor, " room ", ri)
	quit()
