# gworms.gd — two worms, one hill, and a hill that does not survive the match.
#
# The desktop already had 2048, which proved a window could take keys and keep
# state. This one has to prove something harder: that the world in the window
# changes. An artillery game is only fun if the terrain remembers where the
# last shell landed, so the ground here is a real heightmap and every explosion
# writes back into it. Miss low and you dig your own trench; miss long twice
# and the hill you were hiding behind is a stump.
#
# There are no sprites in this project and there is no audio. Everything below
# is draw_polygon, draw_line, draw_rect, draw_circle and draw_string, which is
# a constraint that turns out to suit a game made of dirt and parabolas.
#
# The match tally lives in /root/.worms, written through the machine's own
# shell exactly the way 2048 writes its high score, so `cat /root/.worms` in
# the terminal shows the same numbers the HUD is showing. Even the toy is
# honest.

extends Control

var mono: Font
var machine: Object = null

# Physics happens in a fixed virtual world and is stretched to whatever size
# the window is. Otherwise a resize mid-flight would change gravity, and the
# terrain would have to be rebuilt every time the user grabs a corner.
const VW := 1000.0
const VH := 600.0
const COLS := 200                  # heightmap resolution; 5 virtual units wide
const GRAV := 340.0                # virtual units / s^2
const WORM_R := 9.0
const BLAST := 58.0
const MUZZLE := 20.0               # shell starts this far out, so you cannot shoot yourself

# The desktop is a light, classic MATE-ish environment, so: calm sky, earthy
# dirt, dark ink for text. Nothing here glows.
const SKY_TOP := Color("#bcd6e8")
const SKY_LOW := Color("#e4eef2")
const DIRT := Color("#8a6a45")
const DIRT_DARK := Color("#6d5334")
const GRASS := Color("#6f8f4a")
const INK := Color("#2f2a24")
const FAINT := Color("#7d7468")
const P1 := Color("#3f6ea8")
const P2 := Color("#a85138")

var ground: PackedFloat32Array = PackedFloat32Array()   # y of the surface, per column, y grows down
var worms: Array = []              # [{x, y, hp, col}]
var turn := 0
var angle := 45.0                  # degrees from +x, 90 is straight up
var power := 60.0                  # 10..100
var wind := 0.0                    # virtual units / s^2, sideways

var state := "aim"                 # aim | fly | boom | over
var shot := Vector2.ZERO
var vel := Vector2.ZERO
var trail: Array = []
var boom_at := Vector2.ZERO
var boom_t := 0.0
var winner := -1
var msg := ""

var wins := [0, 0]
var rng := RandomNumberGenerator.new()


func _ready() -> void:
	focus_mode = Control.FOCUS_ALL
	mouse_filter = Control.MOUSE_FILTER_STOP
	if mono == null:
		mono = ThemeDB.fallback_font
	rng.randomize()
	if machine:
		var t: String = machine.sh_on(0, "cat /root/.worms")
		var p := t.strip_edges().split(" ", false)
		if p.size() >= 2 and p[0].is_valid_int() and p[1].is_valid_int():
			wins = [int(p[0]), int(p[1])]
	_new_game()
	set_process(true)


func take_focus() -> void:
	grab_focus()


# ------------------------------------------------------------------- world --

func _new_game() -> void:
	_make_terrain()
	worms = [
		{"x": VW * 0.12, "y": 0.0, "hp": 100, "col": P1},
		{"x": VW * 0.88, "y": 0.0, "hp": 100, "col": P2},
	]
	for w in worms:
		_settle(w)
	turn = rng.randi_range(0, 1)
	angle = 45.0 if turn == 0 else 135.0
	power = 60.0
	state = "aim"
	winner = -1
	msg = ""
	trail = []
	_new_wind()
	queue_redraw()


# A few sines of different periods, then a smoothing pass. Pure noise gives you
# a mountain range you cannot read; sines give you hills you can aim over.
func _make_terrain() -> void:
	ground = PackedFloat32Array()
	ground.resize(COLS + 1)
	var a := rng.randf_range(0.0, TAU)
	var b := rng.randf_range(0.0, TAU)
	var c := rng.randf_range(0.0, TAU)
	for i in range(COLS + 1):
		var u := float(i) / float(COLS)
		var h := 0.0
		h += sin(u * TAU * 0.7 + a) * 62.0
		h += sin(u * TAU * 1.9 + b) * 34.0
		h += sin(u * TAU * 3.7 + c) * 15.0
		h += rng.randf_range(-6.0, 6.0)
		ground[i] = clamp(VH * 0.68 - h, VH * 0.22, VH * 0.94)
	for _pass in range(3):
		var out := ground.duplicate()
		for i in range(1, COLS):
			out[i] = (ground[i - 1] + ground[i] * 2.0 + ground[i + 1]) * 0.25
		ground = out


func _col(x: float) -> int:
	return int(clamp(round(x / VW * COLS), 0, COLS))


func _ground_at(x: float) -> float:
	return ground[_col(x)]


# Put a worm on top of whatever ground is under it now. Called at spawn and
# after every explosion, because the floor may have just left.
func _settle(w: Dictionary) -> void:
	w["y"] = _ground_at(w["x"]) - WORM_R


func _new_wind() -> void:
	wind = rng.randf_range(-70.0, 70.0)
	if abs(wind) < 8.0:
		wind = 0.0


# ------------------------------------------------------------------ firing --

func _fire() -> void:
	var w: Dictionary = worms[turn]
	var r := deg_to_rad(angle)
	var dir := Vector2(cos(r), -sin(r))
	shot = Vector2(w["x"], w["y"]) + dir * MUZZLE
	vel = dir * (power * 8.0)
	trail = [shot]
	state = "fly"
	msg = ""


func _process(dt: float) -> void:
	if state == "boom":
		boom_t -= dt
		if boom_t <= 0.0:
			_after_boom()
		queue_redraw()
		return
	if state != "fly":
		return
	# Small fixed substeps: a shell at full power crosses several columns per
	# frame, and a heightmap you sample once per frame is a heightmap you fly
	# straight through.
	var steps := 6
	var h: float = min(dt, 0.05) / float(steps)
	for _s in range(steps):
		vel.x += wind * h
		vel.y += GRAV * h
		shot += vel * h
		if shot.y > VH + 40.0 or shot.x < -300.0 or shot.x > VW + 300.0:
			msg = "off the map"
			_end_shot()
			return
		if shot.x >= 0.0 and shot.x <= VW and shot.y >= _ground_at(shot.x):
			_explode(shot)
			return
		for i in range(2):
			var wm: Dictionary = worms[i]
			if wm["hp"] > 0 and shot.distance_to(Vector2(wm["x"], wm["y"])) < WORM_R + 3.0:
				_explode(shot)
				return
	trail.append(shot)
	if trail.size() > 240:
		trail.remove_at(0)
	queue_redraw()


# Carve a disc out of the heightmap and hurt anybody standing in it.
#
# A heightmap cannot hold a cave, so a crater is "lower the surface to the
# bottom of the disc wherever the disc reaches the surface". That is the honest
# limit of the representation, and in practice it looks exactly like a bite
# taken out of the hill, which is what it is.
func _explode(at: Vector2) -> void:
	boom_at = at
	boom_t = 0.35
	state = "boom"
	var lo := _col(at.x - BLAST)
	var hi := _col(at.x + BLAST)
	for i in range(lo, hi + 1):
		var cx := float(i) / float(COLS) * VW
		var dx: float = abs(cx - at.x)
		if dx >= BLAST:
			continue
		var dy := sqrt(BLAST * BLAST - dx * dx)
		if ground[i] >= at.y - dy:
			ground[i] = min(VH, max(ground[i], at.y + dy))
	for i in range(2):
		var w: Dictionary = worms[i]
		if w["hp"] <= 0:
			continue
		var d := at.distance_to(Vector2(w["x"], w["y"]))
		if d < BLAST + WORM_R:
			var dmg := int(round(46.0 * (1.0 - d / (BLAST + WORM_R))))
			w["hp"] = max(0, int(w["hp"]) - max(dmg, 4))
	for w2 in worms:
		_settle(w2)
	queue_redraw()


func _end_shot() -> void:
	state = "boom"
	boom_t = 0.15
	boom_at = Vector2(-999, -999)


func _after_boom() -> void:
	var dead := [worms[0]["hp"] <= 0, worms[1]["hp"] <= 0]
	if dead[0] or dead[1]:
		if dead[0] and dead[1]:
			winner = -2                  # both of you, at once, somehow
		else:
			winner = 1 if dead[0] else 0
		if winner >= 0:
			wins[winner] += 1
			_save()
		state = "over"
		queue_redraw()
		return
	turn = 1 - turn
	angle = clamp(angle, 0.0, 180.0)
	_new_wind()
	state = "aim"
	queue_redraw()


func _save() -> void:
	if machine:
		machine.sh_on(0, 'echo "%d %d" > /root/.worms' % [wins[0], wins[1]])


# ------------------------------------------------------------------- input --

func _gui_input(e: InputEvent) -> void:
	if e is InputEventMouseButton and e.pressed:
		grab_focus()
		return
	if not (e is InputEventKey) or not e.pressed:
		return
	var k := e as InputEventKey
	accept_event()
	if k.keycode == KEY_R:
		_new_game(); return
	if state != "aim":
		return
	# Shift is the fine adjustment, because the difference between hitting and
	# cratering the ground in front of them is usually one degree.
	var da := 0.5 if k.shift_pressed else 2.0
	var dp := 1.0 if k.shift_pressed else 3.0
	match k.keycode:
		KEY_LEFT, KEY_A:
			angle = clamp(angle + da, 0.0, 180.0)
		KEY_RIGHT, KEY_D:
			angle = clamp(angle - da, 0.0, 180.0)
		KEY_UP, KEY_W:
			power = clamp(power + dp, 10.0, 100.0)
		KEY_DOWN, KEY_S:
			power = clamp(power - dp, 10.0, 100.0)
		KEY_SPACE, KEY_ENTER, KEY_KP_ENTER:
			_fire()
		_:
			return
	queue_redraw()


# ------------------------------------------------------------------ render --

func _sx(vx: float) -> float:
	return vx / VW * size.x


func _sy(vy: float) -> float:
	return vy / VH * size.y


func _pt(v: Vector2) -> Vector2:
	return Vector2(_sx(v.x), _sy(v.y))


# One number for circles, which cannot be stretched the way the terrain is.
func _scale() -> float:
	return min(size.x / VW, size.y / VH)


func _draw() -> void:
	if size.x < 20.0 or size.y < 20.0:
		return
	# Sky, in a few bands rather than a gradient texture, because there are no
	# textures here.
	var bands := 6
	for i in range(bands):
		var t := float(i) / float(bands - 1)
		draw_rect(Rect2(0, size.y * float(i) / bands, size.x, size.y / bands + 1.0),
			SKY_TOP.lerp(SKY_LOW, t))
	draw_circle(Vector2(size.x * 0.86, size.y * 0.14), max(6.0, 18.0 * _scale()),
		Color("#f2e2b0"))

	_draw_terrain()
	_draw_worms()

	if state == "fly":
		_draw_shot()
	elif state == "boom" and boom_at.x > -500.0:
		var f: float = clamp(boom_t / 0.35, 0.0, 1.0)
		var r: float = BLAST * _scale() * (1.35 - 0.5 * f)
		draw_circle(_pt(boom_at), r, Color(0.95, 0.72, 0.35, f * 0.8))
		draw_circle(_pt(boom_at), r * 0.55, Color(1.0, 0.94, 0.78, f))

	_draw_hud()


func _draw_terrain() -> void:
	var pts := PackedVector2Array()
	pts.append(Vector2(0, size.y))
	for i in range(COLS + 1):
		pts.append(Vector2(_sx(float(i) / COLS * VW), _sy(ground[i])))
	pts.append(Vector2(size.x, size.y))
	draw_polygon(pts, PackedColorArray([DIRT]))
	# A grass line on top, and a darker seam under it, so the surface reads as
	# a surface and not as the edge of a filled shape.
	for i in range(COLS):
		var a := Vector2(_sx(float(i) / COLS * VW), _sy(ground[i]))
		var b := Vector2(_sx(float(i + 1) / COLS * VW), _sy(ground[i + 1]))
		draw_line(a, b, GRASS, max(1.0, 3.0 * _scale()))
		draw_line(a + Vector2(0, 4), b + Vector2(0, 4), DIRT_DARK, max(1.0, 2.0 * _scale()))


func _draw_worms() -> void:
	for i in range(2):
		var w: Dictionary = worms[i]
		if w["hp"] <= 0:
			# A small cross where they were. It matters that the map remembers.
			var g := _pt(Vector2(w["x"], _ground_at(w["x"]) - 6.0))
			draw_line(g + Vector2(-5, 0), g + Vector2(5, 0), FAINT, 2.0)
			draw_line(g + Vector2(0, -7), g + Vector2(0, 3), FAINT, 2.0)
			continue
		var c := _pt(Vector2(w["x"], w["y"]))
		var r: float = max(4.0, WORM_R * _scale())
		draw_circle(c, r, w["col"])
		draw_circle(c + Vector2(0, -r * 0.25), r * 0.32, Color("#fbf7ef"))
		# health pip above the head
		var bw := r * 3.0
		draw_rect(Rect2(c.x - bw / 2, c.y - r * 2.4, bw, 4), Color(1, 1, 1, 0.7))
		draw_rect(Rect2(c.x - bw / 2, c.y - r * 2.4, bw * float(w["hp"]) / 100.0, 4),
			Color("#4f8f4f") if w["hp"] > 35 else Color("#b4562f"))
		if i == turn and state == "aim":
			var rad := deg_to_rad(angle)
			var dir := Vector2(cos(rad), -sin(rad))
			var tip: Vector2 = c + Vector2(_sx(dir.x * (18.0 + power * 0.5)),
				_sy(dir.y * (18.0 + power * 0.5)))
			draw_line(c, tip, INK, 2.0)
			draw_circle(tip, 3.0, INK)


func _draw_shot() -> void:
	for i in range(trail.size()):
		var f := float(i) / float(max(1, trail.size()))
		draw_circle(_pt(trail[i]), max(1.0, 1.6 * _scale()), Color(0.25, 0.22, 0.18, 0.15 + 0.45 * f))
	draw_circle(_pt(shot), max(2.0, 4.0 * _scale()), Color("#33302b"))


func _draw_hud() -> void:
	var pad := 8.0
	draw_rect(Rect2(0, 0, size.x, 40), Color(1, 1, 1, 0.72))
	var who := "player 1" if turn == 0 else "player 2"
	var col: Color = P1 if turn == 0 else P2
	draw_string(mono, Vector2(pad, 17), "gworms", HORIZONTAL_ALIGNMENT_LEFT, -1, 14, INK)
	draw_string(mono, Vector2(pad, 33),
		"left/right angle   up/down power   space fires   R restarts",
		HORIZONTAL_ALIGNMENT_LEFT, -1, 10, FAINT)

	var wtxt := "calm"
	if wind > 0.0:
		wtxt = "wind >> %d" % int(round(abs(wind) / 7.0))
	elif wind < 0.0:
		wtxt = "wind << %d" % int(round(abs(wind) / 7.0))
	var info := "%s   angle %3d   power %3d   %s" % [who, int(round(angle)), int(round(power)), wtxt]
	var iw := mono.get_string_size(info, HORIZONTAL_ALIGNMENT_LEFT, -1, 12).x
	draw_string(mono, Vector2(max(pad, size.x - iw - pad), 17), info,
		HORIZONTAL_ALIGNMENT_LEFT, -1, 12, col)
	var tally := "match %d - %d" % [wins[0], wins[1]]
	var tw := mono.get_string_size(tally, HORIZONTAL_ALIGNMENT_LEFT, -1, 10).x
	draw_string(mono, Vector2(max(pad, size.x - tw - pad), 33), tally,
		HORIZONTAL_ALIGNMENT_LEFT, -1, 10, FAINT)

	# Health bars, one per corner, under the strip.
	_bar(pad, 46.0, worms[0]["hp"], P1, "P1", false)
	_bar(size.x - pad - 110.0, 46.0, worms[1]["hp"], P2, "P2", true)

	if msg != "" and state != "over":
		draw_string(mono, Vector2(0, size.y - 10), msg,
			HORIZONTAL_ALIGNMENT_CENTER, size.x, 12, FAINT)

	if state == "over":
		draw_rect(Rect2(0, size.y / 2 - 34, size.x, 68), Color(0.98, 0.97, 0.94, 0.88))
		var t := "everybody loses -- R for another round"
		if winner == 0:
			t = "player 1 wins -- R for another round"
		elif winner == 1:
			t = "player 2 wins -- R for another round"
		draw_string(mono, Vector2(0, size.y / 2 + 2), t,
			HORIZONTAL_ALIGNMENT_CENTER, size.x, 15, INK)
		draw_string(mono, Vector2(0, size.y / 2 + 22),
			"match %d - %d, kept in /root/.worms" % [wins[0], wins[1]],
			HORIZONTAL_ALIGNMENT_CENTER, size.x, 10, FAINT)


func _bar(x: float, y: float, hp: int, col: Color, label: String, right: bool) -> void:
	if size.x < 260.0:
		return
	var w := 110.0
	draw_rect(Rect2(x, y, w, 10), Color(1, 1, 1, 0.6))
	var fw := w * float(clamp(hp, 0, 100)) / 100.0
	draw_rect(Rect2(x if not right else x + w - fw, y, fw, 10), col)
	draw_rect(Rect2(x, y, w, 10), Color(0, 0, 0, 0.25), false, 1.0)
	draw_string(mono, Vector2(x, y + 22), "%s  %d" % [label, max(0, hp)],
		HORIZONTAL_ALIGNMENT_RIGHT if right else HORIZONTAL_ALIGNMENT_LEFT, w, 10, INK)
