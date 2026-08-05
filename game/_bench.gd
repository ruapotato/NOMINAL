# THROWAWAY benchmark/verification harness. Delete after use.
extends SceneTree

var app: Control
var mode := "sand"
var f := 0
var sim_us := 0.0
var sim_n := 0
var frame_us := 0.0
var frame_n := 0
var t_last := 0
var shots := "/tmp/claude-1000/-home-david-NOMINAL/161d7f95-8740-424d-b6ae-9273ea5c0cba/scratchpad"
var phase := 0
var log_lines: Array = []


func _initialize() -> void:
	var ua := OS.get_cmdline_user_args()
	if ua.size() > 0:
		mode = ua[0]
	DisplayServer.window_set_size(Vector2i(1000, 720))
	Engine.max_fps = 0
	if DisplayServer.has_feature(DisplayServer.FEATURE_NATIVE_DIALOG):
		pass
	var scr := load("res://scripts/gsand.gd") if mode == "sand" else load("res://scripts/gsetris.gd")
	app = scr.new()
	app.mono = ThemeDB.fallback_font
	root.add_child(app)
	app.set_anchors_preset(Control.PRESET_FULL_RECT)
	app.size = Vector2(1000, 720)
	app.position = Vector2.ZERO
	t_last = Time.get_ticks_usec()


func _setup_sand() -> void:
	# A rock shelf, a pool of water with oil on top, a wooden post, plant seeds,
	# then a slab of sand dropped over the lot.
	app.paused = true
	app.sel = app.STONE
	app.brush = 1
	for x in range(16, 112):
		app._paint_at(x, 80)
		app._paint_at(x, 81)
	for y in range(56, 81):
		app._paint_at(16, y)
		app._paint_at(111, y)
	app.sel = app.WATER
	for y in range(70, 80):
		for x in range(17, 72):
			app._paint_at(x, y)
	app.sel = app.OIL
	for y in range(66, 70):
		for x in range(24, 56):
			app._paint_at(x, y)
	app.sel = app.WOOD
	for y in range(66, 80):
		app._paint_at(96, y)
		app._paint_at(97, y)
	app.sel = app.PLANT
	app._paint_at(76, 79)
	app.sel = app.SAND
	for y in range(16, 32):
		for x in range(48, 80):
			app._paint_at(x, y)
	app.paused = false


func _setup_tris() -> void:
	pass


func _process(_dt: float) -> bool:
	f += 1
	if f == 1:
		# _ready runs on the first tree iteration, not inside _initialize, so
		# the grid does not exist until now.
		if mode == "sand":
			_setup_sand()
			app.paused = true          # we drive _step ourselves, once per frame
		else:
			_setup_tris()
		return false
	var now := Time.get_ticks_usec()
	if f > 30:
		frame_us += float(now - t_last)
		frame_n += 1
	t_last = now
	if mode == "sand":
		return _sand_frame()
	return _tris_frame()


func _sand_frame() -> bool:
	# The app's own _process runs the physics; time it separately by stepping
	# a copy of the workload here would double it, so instead measure _step
	# directly every frame with the app paused for that instant.
	var t0 := Time.get_ticks_usec()
	app._step()
	sim_us += float(Time.get_ticks_usec() - t0)
	sim_n += 1
	app.queue_redraw()
	if f == 60:
		_shot("sand_a_falling")
		_note("frame %d: alive=%d" % [f, app.alive])
	if f == 200:
		# Light the wood.
		app.sel = app.FIRE
		app.brush = 3
		app._paint_at(96, 66)
		_shot("sand_b_mid")
	if f == 320:
		_note("runs/frame=%d (draw_rect calls for the grid)" % _count_runs())
		_shot("sand_c_fire")
	if f == 500:
		# Lava into the pool.
		app.sel = app.LAVA
		app.brush = 4
		app._paint_at(32, 48)
	if f == 700:
		_shot("sand_d_settled")
		_note("ACTIVE avg sim %.2f ms/step over %d steps" % [sim_us / 1000.0 / float(sim_n), sim_n])
		_note("ACTIVE avg frame %.2f ms (%.1f fps) over %d frames" %
			[frame_us / 1000.0 / float(frame_n), 1000000.0 * float(frame_n) / frame_us, frame_n])
		_note("runs/frame=%d" % _count_runs())
		_note("alive=%d" % app.alive)
		sim_us = 0.0; sim_n = 0; frame_us = 0.0; frame_n = 0
	if f == 900:
		_note("IDLE avg sim %.3f ms/step, frame %.2f ms (%.1f fps)" %
			[sim_us / 1000.0 / float(sim_n), frame_us / 1000.0 / float(frame_n),
			1000000.0 * float(frame_n) / frame_us])
		# Now the stress case: half the grid full of sand, all of it in motion.
		app.sel = app.SAND
		app.brush = 1
		for y in range(0, 48):
			for x in range(0, 128):
				app._paint_at(x, y)
		_note("stress: alive=%d (%d cells = %.0f%% of grid)" %
			[app.alive, app.alive, 100.0 * float(app.alive) / float(app.NCELL)])
		sim_us = 0.0; sim_n = 0; frame_us = 0.0; frame_n = 0
	if f == 960:
		_shot("sand_e_stress")
		_note("STRESS avg sim %.2f ms/step over %d, frame %.2f ms (%.1f fps)" %
			[sim_us / 1000.0 / float(sim_n), sim_n, frame_us / 1000.0 / float(frame_n),
			1000000.0 * float(frame_n) / frame_us])
		_note("STRESS runs/frame=%d" % _count_runs())
	if f == 1000:
		DisplayServer.window_set_size(Vector2i(360, 300))
		app.size = Vector2(360, 300)
		sim_us = 0.0; sim_n = 0; frame_us = 0.0; frame_n = 0
	if f == 1060:
		_note("STRESS at 360x300: sim %.2f ms, frame %.2f ms (%.1f fps)" %
			[sim_us / 1000.0 / float(sim_n), frame_us / 1000.0 / float(frame_n),
			1000000.0 * float(frame_n) / frame_us])
		DisplayServer.window_set_size(Vector2i(1000, 720))
		app.size = Vector2(1000, 720)
	if f == 1200:
		_shot("sand_f_stress_settled")
		_finish()
	return false


func _count_runs() -> int:
	var n := 0
	for y in range(app.GH):
		var rb: int = y * app.GW
		var x := 0
		var prev := -1
		while x < app.GW:
			var m: int = app.mat[rb + x]
			if m != prev:
				if m != 0:
					n += 1
				prev = m
			x += 1
	return n


func _tris_frame() -> bool:
	if f == 40:
		_shot("tris_a_start")
	# Play: drop pieces fast, shuffling them sideways so the board fills.
	if f > 40 and f < 900 and f % 14 == 0:
		var d := randi() % 3
		if d == 0: app._shift(-1)
		elif d == 1: app._shift(1)
		else: app._rotate(1)
	if f > 40 and f < 900 and f % 40 == 0 and not app.over:
		app._hard_drop()
	if f == 400:
		_shot("tris_b_playing")
		_note("frame %d score=%d bridges=%d over=%s" % [f, app.score, app.bridges, str(app.over)])
	if f == 900:
		_shot("tris_c_played")
		_note("after random play: score=%d bridges=%d over=%s" %
			[app.score, app.bridges, str(app.over)])
		_note("PLAY avg frame %.2f ms (%.1f fps)" %
			[frame_us / 1000.0 / float(frame_n), 1000000.0 * float(frame_n) / frame_us])
	# Deliberate bridge: paint one colour straight across the floor and prove it
	# clears, and that a same-colour blob NOT touching both walls does not.
	if f == 910:
		app._new_game()
		for x in range(app.GW):
			app.grid[(app.GH - 1) * app.GW + x] = 2
		for x in range(10, 40):
			app.grid[(app.GH - 20) * app.GW + x] = 2     # floating decoy, no bridge
		_note("planted: floor row of colour 2 + a 30-cell decoy")
	if f == 912:
		_shot("tris_d_bridge_found")
		_note("flash_t=%.2f doomed=%d score=%d" % [app.flash_t, app.doomed.size(), app.score])
	if f == 940:
		var left := 0
		for i in range(app.NCELL):
			if app.grid[i] != 0:
				left += 1
		_note("after clear: cells left=%d (expect the 30-cell decoy, fallen)" % left)
		_shot("tris_e_after_clear")
		_finish()
	return false


func _note(s: String) -> void:
	log_lines.append(s)
	print("BENCH " + s)


func _shot(name: String) -> void:
	RenderingServer.force_draw()
	var img := root.get_texture().get_image()
	img.save_png("%s/%s.png" % [shots, name])


func _finish() -> void:
	for l in log_lines:
		print("RESULT " + l)
	quit()
