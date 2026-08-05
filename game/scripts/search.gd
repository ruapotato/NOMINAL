# search.gd — the graphical front end to `find`.
#
# It is a front end and nothing else. It builds one command line, runs it, and
# shows every line that came back in the order it came back, errors included
# and unedited. The command it ran is printed across the top so you can retype
# it in the terminal and get the identical output -- if this window and the
# shell ever disagree, the window is the one that is wrong, and it has no
# opinions of its own to be wrong with.
#
# THE FLAGS ARE THE REAL ONES. `find` with no arguments prints:
#
#     usage: find <dir> [-name <pattern>] [-type f|d]
#
# and that is all this app offers. No -size, no -mtime, no -exec: this find
# does not have them, and a greyed-out row of options that never worked is a
# promise the game cannot keep.
#
# THE PATTERN IS QUOTED, and that is a bug fix, not tidiness. The shell here
# globs, so an unquoted `find /etc -name *.conf` gets its pattern expanded by
# the shell against the CURRENT directory before find ever sees it -- I
# measured it: quoted returns twelve .conf files, unquoted returns one. The
# app single-quotes the pattern always, and refuses a pattern containing a
# single quote rather than building a command line it cannot reason about.
#
# ONE THING YOU SHOULD KNOW ABOUT THIS `find`, since the window will show it
# to you: on a recursive walk it prints only the FIRST entry of each
# subdirectory it descends into (compare `find /usr/share/man` against
# `ls -l /usr/share/man`). This app does NOT paper over that by walking the
# tree itself with `ls`. It is a front end to find; if find under-reports, the
# window under-reports identically, and the two agree. The disk usage window
# is the one that walks with `ls -l`, and it says why.

extends Control

var mono: Font
var machine: Object = null
var sh: Callable = Callable()

var path := "/"
var pattern := ""
var kind := 0                 # 0 any, 1 -type f, 2 -type d

var field := 0                # which form row the keyboard is editing
var pane := 0                 # 0 form, 1 results

var cmd := ""                 # exactly what was run
var results: Array = []       # {text, err}
var sel := 0
var scroll := 0
var ran := false
var note := ""                # find's "(nothing matched)", or our refusal

# The desktop wires this up; double-clicking a hit calls it with the path.
var on_open: Callable = func(_p: String) -> void: pass

const TOP := 22.0
const ROW_H := 14.0
const BOT := 30.0
const FORM_ROWS := 3
const LABELS := ["look in", "name is", "only"]
const KINDS := ["anything", "files (-type f)", "directories (-type d)"]

const CHROME := Color("#d6d3ce")
const WHITE := Color("#ffffff")
const INK := Color("#1b1b1b")
const DIM := Color("#5f6469")
const FAINT := Color("#9aa0a6")
const SEL := Color("#3465a4")
const SELTX := Color("#ffffff")
const RED := Color("#b0281a")
const GREEN := Color("#1f6b3a")


func _ready() -> void:
	focus_mode = Control.FOCUS_ALL
	mouse_filter = Control.MOUSE_FILTER_STOP
	if mono == null:
		mono = preload("res://scripts/uifont.gd").mono()
	set_process(true)


func take_focus() -> void:
	grab_focus()


func _process(_dt: float) -> void:
	pass


func _sh(c: String) -> String:
	if sh.is_valid():
		return str(sh.call(c))
	if machine == null:
		return ""
	return str(machine.sh_on(0, c))


func _has_machine() -> bool:
	return machine != null or sh.is_valid()


# ------------------------------------------------------------------ running

func run() -> void:
	results = []
	note = ""
	sel = 0
	scroll = 0
	ran = true
	if not _has_machine():
		cmd = ""
		note = "no machine attached"
		queue_redraw()
		return
	# A single quote inside the pattern would close the quoting and hand the
	# rest of the pattern to the shell as words. There is no escaping worth
	# building for a find that has three flags; refusing is honest and the
	# message says exactly what to do.
	if pattern.find("'") >= 0:
		cmd = ""
		note = "the pattern contains a single quote, which this app will not try to escape"
		queue_redraw()
		return
	var p := path.strip_edges()
	if p == "":
		p = "/"
	cmd = "find " + p
	if pattern.strip_edges() != "":
		cmd += " -name '" + pattern.strip_edges() + "'"
	if kind == 1:
		cmd += " -type f"
	elif kind == 2:
		cmd += " -type d"

	var out := _sh(cmd)
	for line in out.split("\n"):
		var t := line.strip_edges()
		if t == "":
			continue
		if t == "(nothing matched)":
			# find's own way of saying zero hits. It is not a path and must
			# never end up in a list you can double-click.
			note = t
			continue
		if t.begins_with("find:") or t.begins_with("usage:") or t.begins_with("sh:"):
			results.append({"text": t, "err": true})
			continue
		results.append({"text": t, "err": false})
	queue_redraw()


func _hits() -> int:
	var n := 0
	for r in results:
		if not r["err"]:
			n += 1
	return n


# ------------------------------------------------------------------ layout

func _form_h() -> float:
	return float(FORM_ROWS) * 18.0 + 8.0


func _list_rect() -> Rect2:
	var y := TOP + _form_h()
	return Rect2(0, y, size.x, maxf(ROW_H, size.y - y - BOT))


func _visible() -> int:
	return maxi(1, int(_list_rect().size.y / ROW_H))


func _clamp() -> void:
	sel = clampi(sel, 0, maxi(0, results.size() - 1))
	var vis := _visible()
	if sel < scroll:
		scroll = sel
	elif sel >= scroll + vis:
		scroll = sel - vis + 1
	scroll = clampi(scroll, 0, maxi(0, results.size() - vis))


# ---------------------------------------------------------------- input

func _gui_input(e: InputEvent) -> void:
	if e is InputEventMouseButton and e.pressed:
		grab_focus()
		var mb := e as InputEventMouseButton
		if mb.button_index == MOUSE_BUTTON_WHEEL_UP:
			scroll = maxi(0, scroll - 3)
			accept_event()
			queue_redraw()
			return
		if mb.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			scroll = clampi(scroll + 3, 0, maxi(0, results.size() - _visible()))
			accept_event()
			queue_redraw()
			return
		if mb.button_index != MOUSE_BUTTON_LEFT:
			return
		var fy := mb.position.y - TOP
		if fy >= 0.0 and fy < _form_h():
			var r := int(fy / 18.0)
			if r >= 0 and r < FORM_ROWS:
				pane = 0
				field = r
				# The third row is a choice, not text: clicking it cycles it,
				# because there is no popup menu in this desktop to open.
				if r == 2:
					kind = (kind + 1) % KINDS.size()
		else:
			var lr := _list_rect()
			var i := scroll + int((mb.position.y - lr.position.y) / ROW_H)
			if i >= 0 and i < results.size():
				pane = 1
				sel = i
				if mb.double_click and not results[i]["err"]:
					on_open.call(str(results[i]["text"]))
		accept_event()
		queue_redraw()
		return

	if not (e is InputEventKey) or not e.pressed:
		return
	var k := e as InputEventKey

	if k.keycode == KEY_TAB:
		pane = 1 - pane
		accept_event()
		queue_redraw()
		return
	if k.keycode == KEY_F5:
		run()
		accept_event()
		queue_redraw()
		return

	if pane == 1:
		match k.keycode:
			KEY_UP: sel = maxi(0, sel - 1)
			KEY_DOWN: sel = mini(maxi(0, results.size() - 1), sel + 1)
			KEY_PAGEUP: sel = maxi(0, sel - _visible())
			KEY_PAGEDOWN: sel = mini(maxi(0, results.size() - 1), sel + _visible())
			KEY_HOME: sel = 0
			KEY_END: sel = maxi(0, results.size() - 1)
			KEY_ENTER, KEY_KP_ENTER:
				if sel >= 0 and sel < results.size() and not results[sel]["err"]:
					on_open.call(str(results[sel]["text"]))
			KEY_ESCAPE:
				pane = 0
			_:
				return
		_clamp()
		accept_event()
		queue_redraw()
		return

	match k.keycode:
		KEY_UP:
			field = maxi(0, field - 1)
		KEY_DOWN:
			field = mini(FORM_ROWS - 1, field + 1)
		KEY_ENTER, KEY_KP_ENTER:
			run()
			if not results.is_empty():
				pane = 1
		KEY_BACKSPACE:
			if field == 0:
				path = path.substr(0, maxi(0, path.length() - 1))
			elif field == 1:
				pattern = pattern.substr(0, maxi(0, pattern.length() - 1))
		KEY_LEFT:
			if field == 2:
				kind = posmod(kind - 1, KINDS.size())
		KEY_RIGHT:
			if field == 2:
				kind = posmod(kind + 1, KINDS.size())
		KEY_SPACE:
			if field == 2:
				kind = (kind + 1) % KINDS.size()
			else:
				_type(32)
		_:
			var u := k.unicode
			if u >= 32 and u < 127:
				_type(u)
			else:
				return
	accept_event()
	queue_redraw()


func _type(u: int) -> void:
	var c := String.chr(u)
	if field == 0:
		path += c
	elif field == 1:
		pattern += c


# ---------------------------------------------------------------- drawing

func _fit(t: String, w: float, fs: int) -> String:
	if mono.get_string_size(t, HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x <= w:
		return t
	var s := t
	while s.length() > 1 and \
			mono.get_string_size(s + "...", HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x > w:
		s = s.substr(0, s.length() - 1)
	return s + "..."


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), CHROME)
	_draw_cmd()
	_draw_form()
	_draw_list()
	_draw_foot()


# The command bar. This is the whole honesty of the app in twelve pixels:
# whatever is on this line is what ran, and you can paste it into a terminal.
func _draw_cmd() -> void:
	draw_rect(Rect2(0, 0, size.x, TOP), Color("#1f2328"))
	var t := cmd if cmd != "" else "$ (nothing run yet -- enter runs it)"
	if cmd != "":
		t = "$ " + cmd
	draw_string(mono, Vector2(8, 15), _fit(t, size.x - 16.0, 11),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 11,
		Color("#8fd08f") if cmd != "" else Color("#78808a"))


func _draw_form() -> void:
	var y := TOP
	draw_rect(Rect2(0, y, size.x, _form_h()), Color("#eceae7"))
	var lw: float = 58.0
	for i in range(FORM_ROWS):
		var ry := y + 4.0 + i * 18.0
		var focused := pane == 0 and field == i
		draw_string(mono, Vector2(6, ry + 12), LABELS[i],
			HORIZONTAL_ALIGNMENT_LEFT, -1, 10, DIM)
		var box := Rect2(lw, ry + 1.0, maxf(30.0, size.x - lw - 8.0), 15.0)
		draw_rect(box, WHITE if i < 2 else Color("#f6f5f3"))
		draw_rect(box, SEL if focused else Color("#c3c0bb"), false, 1.0)
		var val := ""
		match i:
			0: val = path
			1: val = pattern if pattern != "" else "(any name)"
			_: val = KINDS[kind] + "    < >"
		if focused and i < 2:
			val += "_"
		draw_string(mono, Vector2(box.position.x + 4, ry + 12),
			_fit(val, box.size.x - 8.0, 10), HORIZONTAL_ALIGNMENT_LEFT, -1, 10,
			INK if (i != 1 or pattern != "") else FAINT)
	draw_line(Vector2(0, y + _form_h()), Vector2(size.x, y + _form_h()),
		Color("#b3b0ab"))


func _draw_list() -> void:
	var r := _list_rect()
	draw_rect(r, WHITE)
	if results.is_empty():
		var msg := "press enter to run it"
		if ran:
			msg = note if note != "" else "find printed nothing at all"
		draw_string(mono, Vector2(8, r.position.y + 18),
			_fit(msg, r.size.x - 16.0, 11), HORIZONTAL_ALIGNMENT_LEFT, -1, 11,
			RED if (ran and note != "" and note != "(nothing matched)") else DIM)
		return
	var vis := _visible()
	for i in range(scroll, mini(results.size(), scroll + vis)):
		var row: Dictionary = results[i]
		var y := r.position.y + (i - scroll) * ROW_H
		var picked := pane == 1 and i == sel
		if picked:
			draw_rect(Rect2(0, y, r.size.x, ROW_H), SEL)
		elif i % 2 == 1:
			draw_rect(Rect2(0, y, r.size.x, ROW_H), Color("#f5f4f2"))
		var col := INK
		if row["err"]:
			col = RED
		if picked:
			col = SELTX
		draw_string(mono, Vector2(6, y + 11), _fit(str(row["text"]), r.size.x - 12.0, 11),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, col)


func _draw_foot() -> void:
	var y := size.y - BOT
	draw_rect(Rect2(0, y, size.x, BOT), Color("#e6e3de"))
	draw_line(Vector2(0, y), Vector2(size.x, y), Color("#b3b0ab"))
	var msg := ""
	var col := INK
	if not ran:
		msg = "tab moves between the form and the results"
	elif note != "":
		msg = note
		col = RED if note != "(nothing matched)" else DIM
	else:
		msg = "%d results" % _hits()
		var errs := results.size() - _hits()
		if errs > 0:
			msg += ", %d error lines" % errs
			col = RED
		elif _hits() > 0:
			col = GREEN
	if pane == 1 and sel >= 0 and sel < results.size():
		msg = str(results[sel]["text"])
		col = RED if results[sel]["err"] else INK
	draw_string(mono, Vector2(8, y + 12), _fit(msg, size.x - 16.0, 10),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 10, col)
	draw_string(mono, Vector2(8, y + 23),
		_fit("enter runs   double-click opens   only -name and -type exist on this find",
			size.x - 16.0, 9), HORIZONTAL_ALIGNMENT_LEFT, -1, 9, Color("#7c8085"))
