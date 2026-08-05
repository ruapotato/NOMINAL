# g2048.gd — 2048, because a desktop with no games on it is not a desktop.
#
# David asked for real apps to make the OS feel lived in and worth exploring.
# A game is the clearest test of whether this is a desktop or a diorama: it
# has to take keys, keep state and be actually playable, and none of that is
# faked by the OS underneath.
#
# It writes its high score to /root/.2048 through the machine's own shell, so
# the score survives, and `cat /root/.2048` shows it. Even the toy is honest.

extends Control

var mono: Font
var machine: Object = null
var g := []                 # 4x4, 0 = empty
var score := 0
var best := 0
var over := false
var won := false
var rng := RandomNumberGenerator.new()

const N := 4
const TILE := Color("#eee4da")
const BOARD := Color("#bbada0")
const EMPTY := Color("#cdc1b4")


func _ready() -> void:
	focus_mode = Control.FOCUS_ALL
	mouse_filter = Control.MOUSE_FILTER_STOP
	if mono == null:
		mono = ThemeDB.fallback_font
	rng.randomize()
	if machine:
		var t: String = machine.sh_on(0, "cat /root/.2048")
		if t.strip_edges().is_valid_int():
			best = int(t.strip_edges())
	_new_game()


func take_focus() -> void:
	grab_focus()


func _new_game() -> void:
	g = []
	for i in range(N):
		g.append([0, 0, 0, 0])
	score = 0
	over = false
	won = false
	_spawn(); _spawn()
	queue_redraw()


func _spawn() -> void:
	var free: Array = []
	for r in range(N):
		for c in range(N):
			if g[r][c] == 0:
				free.append([r, c])
	if free.is_empty():
		return
	var p: Array = free[rng.randi_range(0, free.size() - 1)]
	g[p[0]][p[1]] = 4 if rng.randf() < 0.1 else 2


# One row, slid left and merged. Returns the new row and the points scored.
func _slide(rowv: Array) -> Array:
	var a: Array = []
	for v in rowv:
		if v != 0:
			a.append(v)
	var out: Array = []
	var pts := 0
	var i := 0
	while i < a.size():
		if i + 1 < a.size() and a[i] == a[i + 1]:
			out.append(a[i] * 2)
			pts += a[i] * 2
			if a[i] * 2 == 2048:
				won = true
			i += 2
		else:
			out.append(a[i])
			i += 1
	while out.size() < N:
		out.append(0)
	return [out, pts]


func _move(dir: int) -> bool:
	# dir 0 left, 1 right, 2 up, 3 down. Rotate into "left", slide, rotate back.
	var before := str(g)
	var work: Array = []
	for r in range(N):
		var line: Array = []
		for c in range(N):
			match dir:
				0: line.append(g[r][c])
				1: line.append(g[r][N - 1 - c])
				2: line.append(g[c][r])
				3: line.append(g[N - 1 - c][r])
		var res := _slide(line)
		score += res[1]
		work.append(res[0])
	for r in range(N):
		for c in range(N):
			match dir:
				0: g[r][c] = work[r][c]
				1: g[r][N - 1 - c] = work[r][c]
				2: g[c][r] = work[r][c]
				3: g[N - 1 - c][r] = work[r][c]
	return str(g) != before


func _stuck() -> bool:
	for r in range(N):
		for c in range(N):
			if g[r][c] == 0:
				return false
			if c + 1 < N and g[r][c] == g[r][c + 1]:
				return false
			if r + 1 < N and g[r][c] == g[r + 1][c]:
				return false
	return true


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
	if over:
		return
	var d := -1
	match k.keycode:
		KEY_LEFT, KEY_A:  d = 0
		KEY_RIGHT, KEY_D: d = 1
		KEY_UP, KEY_W:    d = 2
		KEY_DOWN, KEY_S:  d = 3
	if d < 0:
		return
	if _move(d):
		_spawn()
		if _stuck():
			over = true
			if score > best:
				best = score
				if machine:
					machine.sh_on(0, 'echo "%d" > /root/.2048' % best)
	queue_redraw()


func _tile_colour(v: int) -> Color:
	match v:
		2:    return Color("#eee4da")
		4:    return Color("#ede0c8")
		8:    return Color("#f2b179")
		16:   return Color("#f59563")
		32:   return Color("#f67c5f")
		64:   return Color("#f65e3b")
		128:  return Color("#edcf72")
		256:  return Color("#edcc61")
		512:  return Color("#edc850")
		1024: return Color("#edc53f")
		_:    return Color("#edc22e")


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color("#faf8ef"))
	draw_string(mono, Vector2(10, 20), "2048",
		HORIZONTAL_ALIGNMENT_LEFT, -1, 18, Color("#776e65"))
	draw_string(mono, Vector2(size.x - 190, 20),
		"score %d    best %d" % [score, best],
		HORIZONTAL_ALIGNMENT_LEFT, -1, 12, Color("#776e65"))
	draw_string(mono, Vector2(10, 36), "arrows or wasd, R restarts",
		HORIZONTAL_ALIGNMENT_LEFT, -1, 10, Color("#a89a8f"))

	var pad := 6.0
	var top := 46.0
	var side: float = min(size.x - 20, size.y - top - 12)
	if side < 40:
		return
	var cell := (side - pad * (N + 1)) / N
	draw_rect(Rect2(10, top, side, side), BOARD)
	for r in range(N):
		for c in range(N):
			var x := 10 + pad + c * (cell + pad)
			var y := top + pad + r * (cell + pad)
			var v: int = g[r][c]
			draw_rect(Rect2(x, y, cell, cell), EMPTY if v == 0 else _tile_colour(v))
			if v != 0:
				var col := Color("#776e65") if v <= 4 else Color("#f9f6f2")
				draw_string(mono, Vector2(x, y + cell * 0.62), str(v),
					HORIZONTAL_ALIGNMENT_CENTER, cell,
					int(clamp(cell * 0.38, 10, 26)), col)
	if over:
		draw_rect(Rect2(10, top, side, side), Color(0.93, 0.89, 0.85, 0.72))
		draw_string(mono, Vector2(10, top + side / 2), "no moves left -- R to try again",
			HORIZONTAL_ALIGNMENT_CENTER, side, 14, Color("#776e65"))
	elif won:
		draw_string(mono, Vector2(10, top + side + 10), "2048! keep going.",
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color("#2f7a3f"))
