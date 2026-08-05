# de.gd — the desktop, as a support engineer would find it.
#
# Modelled on the Hamnix desktop David pointed at: light panels top and
# bottom, a blue wallpaper, a column of launchers down the left, a window list
# in the top panel and a workspace switcher in the bottom one. Bright, not
# dark. Classic, not fashionable.
#
# THE SHAPE OF A SHIFT:
#   the desktop starts EMPTY, because nothing has happened yet
#   a message arrives -- the panel lights up and a notification slides in
#   you open the chat; the customer says their machine is down and reads you
#     the address off the sticker on the front
#   you open a terminal ON YOUR OWN MACHINE and run `rcon connect <address>`
#   that opens a SECOND terminal, titled with where it is, showing their
#     console -- and your own shell is still sitting there beside it
#
# Nothing here diagnoses anything. Every command goes through one machine's
# own /bin/sh, and WHICH machine is a property of the window you typed it in.

extends Control

var machine: Object = null
var mono: Font

# --- palette, from the reference: light panels, blue wall ---
const PANEL_BG   := Color("#d8d8d8")
const PANEL_EDGE := Color("#9aa0a6")
const PANEL_INK  := Color("#1b1b1b")
const WALL_TOP   := Color("#3f6699")
const WALL_BOT   := Color("#1d3050")
const WIN_BG     := Color("#ededed")
const WIN_EDGE   := Color("#8b929b")
const TITLE_ON   := Color("#3c6eb4")
const TITLE_OFF  := Color("#b6bcc4")
const TITLE_INK  := Color("#ffffff")
const TITLE_INK2 := Color("#48505a")
const INK        := Color("#1b1b1b")
const DIM        := Color("#5c6570")
const TERM_BG    := Color("#12161c")
const TERM_FG    := Color("#d7dee6")
const GREEN      := Color("#4fb06a")
const ALERT      := Color("#d64541")

const PANEL_H := 26.0
const FOOT_H  := 24.0

var wall: Control
var panel: Control
var foot: Control
var windows: Array = []
var focused: Control = null

var seed_no := 4823
var faults := 1
var addr := ""
var cust := "the customer"

var chat: Control
var alerts := 0
var _alert_msg := ""
var _clock := 0.0
var _shot_path := ""
var _shot_after := 0
var _frames := 0

const LAUNCHERS := [
	["Terminal",   "term"],
	["Chat",       "chat"],
	["Files",      "files"],
	["Notes",      "notes"],
	["Log Viewer", "log"],
	["Manual",     "manual"],
	["2048",       "g2048"],
]
const TITLES := {"term": "terminal - your", "chat": "chat", "files": "files",
	"notes": "notes", "log": "log viewer", "manual": "manual", "g2048": "2048"}


func _ready() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	mono = ThemeDB.fallback_font
	if ClassDB.class_exists("NominalStation"):
		machine = ClassDB.instantiate("NominalStation")
	else:
		push_error("NominalStation is not registered - the GDExtension did not load")
		return
	_build_shell()
	_new_ticket()
	_parse_args()


func _build_shell() -> void:
	wall = Control.new()
	wall.set_anchors_preset(Control.PRESET_FULL_RECT)
	wall.draw.connect(_draw_wall)
	wall.gui_input.connect(_wall_input)
	add_child(wall)

	panel = Control.new()
	panel.set_anchors_preset(Control.PRESET_TOP_WIDE)
	panel.size.y = PANEL_H
	panel.draw.connect(_draw_panel)
	panel.gui_input.connect(_panel_input)
	add_child(panel)

	foot = Control.new()
	foot.set_anchors_preset(Control.PRESET_BOTTOM_WIDE)
	foot.size.y = FOOT_H
	foot.mouse_filter = Control.MOUSE_FILTER_IGNORE
	foot.draw.connect(_draw_foot)
	add_child(foot)


func _draw_wall() -> void:
	var h := wall.size.y
	for i in range(72):
		var t := float(i) / 71.0
		wall.draw_rect(Rect2(0, h * t, wall.size.x, h / 71.0 + 1),
			WALL_TOP.lerp(WALL_BOT, t))
	var y := PANEL_H + 14.0
	for spec in LAUNCHERS:
		_draw_icon(wall, Vector2(24, y), spec[0], spec[1])
		y += 62

	if menu_open:
		var r := _menu_rect()
		wall.draw_rect(r, Color("#f6f6f6"))
		wall.draw_rect(r, Color("#8b929b"), false, 1.0)
		var my := r.position.y + 6
		for spec in LAUNCHERS:
			wall.draw_string(mono, Vector2(r.position.x + 30, my + 17), spec[0],
				HORIZONTAL_ALIGNMENT_LEFT, -1, 12, Color("#1b1b1b"))
			_draw_icon_small(wall, Vector2(r.position.x + 6, my + 4), spec[1])
			my += MENU_ROW


func _draw_icon(c: Control, at: Vector2, label: String, kind: String) -> void:
	var r := Rect2(at.x, at.y, 34, 30)
	match kind:
		"term":
			c.draw_rect(r, Color("#1c1c1c"))
			c.draw_rect(r, Color("#5a5a5a"), false, 1.0)
			c.draw_string(mono, at + Vector2(5, 21), ">_",
				HORIZONTAL_ALIGNMENT_LEFT, -1, 13, Color("#79d17a"))
		"chat":
			c.draw_rect(r, Color("#f2f2f2"))
			c.draw_rect(r, Color("#8b929b"), false, 1.0)
			c.draw_rect(Rect2(at.x + 5, at.y + 6, 24, 12), Color("#3c6eb4"))
		"files":
			c.draw_rect(Rect2(at.x, at.y + 4, 34, 24), Color("#e0a338"))
			c.draw_rect(Rect2(at.x, at.y, 15, 6), Color("#e0a338"))
		"notes":
			c.draw_rect(r, Color("#fbfbf4"))
			c.draw_rect(r, Color("#8b929b"), false, 1.0)
			for i in range(4):
				c.draw_line(at + Vector2(5, 8 + i * 5), at + Vector2(29, 8 + i * 5),
					Color("#9fb4cc"))
		"log":
			c.draw_rect(r, Color("#1c1c1c"))
			c.draw_rect(r, Color("#5a5a5a"), false, 1.0)
			c.draw_line(at + Vector2(5, 22), at + Vector2(14, 10), Color("#79d17a"))
			c.draw_line(at + Vector2(14, 10), at + Vector2(22, 18), Color("#79d17a"))
			c.draw_line(at + Vector2(22, 18), at + Vector2(30, 7), Color("#79d17a"))
		_:
			c.draw_rect(r, Color("#f6f6f6"))
			c.draw_rect(r, Color("#8b929b"), false, 1.0)
			c.draw_string(mono, at + Vector2(11, 21), "M",
				HORIZONTAL_ALIGNMENT_LEFT, -1, 13, INK)
	c.draw_string(mono, Vector2(at.x - 8, at.y + 44), label,
		HORIZONTAL_ALIGNMENT_CENTER, 50, 11, Color("#ffffff"))


# A 16px version of the launcher icon, for the menu.
func _draw_icon_small(c: Control, at: Vector2, kind: String) -> void:
	var r := Rect2(at.x, at.y, 16, 14)
	match kind:
		"term": c.draw_rect(r, Color("#1c1c1c"))
		"chat": c.draw_rect(r, Color("#3c6eb4"))
		"files": c.draw_rect(r, Color("#e0a338"))
		"notes": c.draw_rect(r, Color("#fbfbf4"))
		"log": c.draw_rect(r, Color("#1c1c1c"))
		_: c.draw_rect(r, Color("#dcdcdc"))
	c.draw_rect(r, Color("#8b929b"), false, 1.0)


func _draw_panel() -> void:
	panel.draw_rect(Rect2(0, 0, panel.size.x, PANEL_H), PANEL_BG)
	panel.draw_rect(Rect2(0, PANEL_H - 1, panel.size.x, 1), PANEL_EDGE)
	panel.draw_string(mono, Vector2(10, 18), "Applications",
		HORIZONTAL_ALIGNMENT_LEFT, -1, 12, PANEL_INK)
	panel.draw_line(Vector2(96, 4), Vector2(96, PANEL_H - 4), PANEL_EDGE)

	var x := 106.0
	for w in windows:
		if not is_instance_valid(w) or not w.visible:
			continue
		var t := str(w.get_meta("title"))
		var wd: float = min(210.0,
			mono.get_string_size(t, HORIZONTAL_ALIGNMENT_LEFT, -1, 11).x + 18)
		var on: bool = focused == w.get_meta("content")
		panel.draw_rect(Rect2(x, 3, wd, PANEL_H - 7),
			Color("#c2c8ce") if on else Color("#cfd4d9"))
		panel.draw_rect(Rect2(x, 3, wd, PANEL_H - 7), PANEL_EDGE, false, 1.0)
		panel.draw_string(mono, Vector2(x + 6, 18), t,
			HORIZONTAL_ALIGNMENT_LEFT, wd - 10, 11, PANEL_INK)
		w.set_meta("tab", Rect2(x, 3, wd, PANEL_H - 7))
		x += wd + 4

	var rx := panel.size.x - 196
	if alerts > 0:
		panel.draw_rect(Rect2(rx, 6, 14, 14), ALERT)
		panel.draw_string(mono, Vector2(rx + 4, 18), str(alerts),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color("#ffffff"))
	panel.draw_string(mono, Vector2(panel.size.x - 170, 18),
		"node-%d   Mon 09:%02d" % [seed_no % 10000, int(_clock) % 60],
		HORIZONTAL_ALIGNMENT_LEFT, -1, 12, PANEL_INK)


func _draw_foot() -> void:
	foot.draw_rect(Rect2(0, 0, foot.size.x, FOOT_H), PANEL_BG)
	foot.draw_rect(Rect2(0, 0, foot.size.x, 1), PANEL_EDGE)
	for i in range(4):
		var x := foot.size.x - 96 + i * 22
		foot.draw_rect(Rect2(x, 4, 18, FOOT_H - 8),
			Color("#3c6eb4") if i == 0 else Color("#e8e8e8"))
		foot.draw_rect(Rect2(x, 4, 18, FOOT_H - 8), PANEL_EDGE, false, 1.0)
		foot.draw_string(mono, Vector2(x + 6, FOOT_H - 8), str(i + 1),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11,
			Color("#ffffff") if i == 0 else PANEL_INK)
	if _alert_msg != "":
		var w := 440.0
		var r := Rect2(foot.size.x - w - 8, -78, w, 72)
		foot.draw_rect(r, Color("#fbfbfb"))
		foot.draw_rect(r, Color("#3c6eb4"), false, 2.0)
		foot.draw_rect(Rect2(r.position.x, r.position.y, w, 20), Color("#3c6eb4"))
		foot.draw_string(mono, r.position + Vector2(8, 15), "new message",
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color("#ffffff"))
		foot.draw_string(mono, r.position + Vector2(8, 40), _alert_msg,
			HORIZONTAL_ALIGNMENT_LEFT, w - 16, 12, INK)
		foot.draw_string(mono, r.position + Vector2(8, 60),
			"open Chat to answer", HORIZONTAL_ALIGNMENT_LEFT, -1, 10, DIM)


func _wall_input(e: InputEvent) -> void:
	if e is InputEventMouseButton and e.pressed and e.button_index == MOUSE_BUTTON_LEFT:
		if menu_open:
			var r := _menu_rect()
			if r.has_point(e.position):
				var idx := int((e.position.y - r.position.y - 6) / MENU_ROW)
				if idx >= 0 and idx < LAUNCHERS.size():
					_launch(LAUNCHERS[idx][1])
			menu_open = false
			wall.queue_redraw()
			return
		var y := PANEL_H + 14.0
		for spec in LAUNCHERS:
			if Rect2(14, y - 4, 70, 58).has_point(e.position):
				_launch(spec[1])
				return
			y += 62


func _panel_input(e: InputEvent) -> void:
	if e is InputEventMouseButton and e.pressed and e.button_index == MOUSE_BUTTON_LEFT:
		if e.position.x < 96:
			menu_open = not menu_open
			wall.queue_redraw()
			return
		if e.position.x > panel.size.x - 200 and alerts > 0:
			_launch("chat")
			return
		for w in windows:
			if is_instance_valid(w) and w.has_meta("tab") \
			   and (w.get_meta("tab") as Rect2).has_point(e.position):
				if w.visible and focused == w.get_meta("content"):
					w.visible = false
				else:
					_raise(w)
				panel.queue_redraw()
				return


func _win(title: String, rect: Rect2, content: Control) -> Control:
	var w := Control.new()
	w.position = rect.position
	w.size = rect.size
	w.set_meta("title", title)
	w.draw.connect(func(): _draw_win(w))
	add_child(w)

	var bar := Control.new()
	bar.size = Vector2(rect.size.x, 20)
	bar.gui_input.connect(func(e): _bar_input(w, e))
	w.add_child(bar)
	w.set_meta("bar", bar)

	var grip := Control.new()
	grip.size = Vector2(18, 18)
	grip.position = Vector2(rect.size.x - 18, rect.size.y - 18)
	grip.gui_input.connect(func(e): _grip_input(w, e))
	w.add_child(grip)
	w.set_meta("grip", grip)

	content.position = Vector2(3, 22)
	content.size = Vector2(rect.size.x - 6, rect.size.y - 25)
	w.add_child(content)
	w.set_meta("content", content)
	windows.append(w)
	_raise(w)
	return w


func _draw_win(w: Control) -> void:
	var on: bool = focused == w.get_meta("content")
	w.draw_rect(Rect2(0, 0, w.size.x, w.size.y), WIN_BG)
	w.draw_rect(Rect2(0, 0, w.size.x, w.size.y), WIN_EDGE, false, 1.0)
	w.draw_rect(Rect2(1, 1, w.size.x - 2, 19), TITLE_ON if on else TITLE_OFF)
	w.draw_string(mono, Vector2(8, 15), str(w.get_meta("title")),
		HORIZONTAL_ALIGNMENT_LEFT, w.size.x - 60, 12,
		TITLE_INK if on else TITLE_INK2)
	w.draw_string(mono, Vector2(w.size.x - 18, 15), "x",
		HORIZONTAL_ALIGNMENT_LEFT, -1, 13, TITLE_INK if on else TITLE_INK2)
	for i in range(3):
		var d := 4.0 + i * 4.0
		w.draw_line(Vector2(w.size.x - d, w.size.y - 2),
			Vector2(w.size.x - 2, w.size.y - d), WIN_EDGE, 1.0)


func _relayout(w: Control) -> void:
	(w.get_meta("bar") as Control).size = Vector2(w.size.x, 20)
	(w.get_meta("grip") as Control).position = Vector2(w.size.x - 18, w.size.y - 18)
	(w.get_meta("content") as Control).size = Vector2(w.size.x - 6, w.size.y - 25)
	w.queue_redraw()


func _raise(w: Control) -> void:
	w.visible = true
	move_child(w, get_child_count() - 1)
	move_child(foot, get_child_count() - 1)
	focused = w.get_meta("content")
	for x in windows:
		if is_instance_valid(x):
			x.queue_redraw()
	if focused and focused.has_method("take_focus"):
		focused.call("take_focus")
	panel.queue_redraw()


# WINDOWS OPEN FREE-FLOATING AND CASCADED, the way MATE does it: a sensible
# fixed size, each one down and right of the last, wrapping when it runs out
# of room. Sizing them as fractions of the screen made them LOOK tiled even
# though nothing was tiling them, which David spotted immediately. Tiling only
# ever happens because you dragged a window to an edge.
var _cascade := 0

func _cascade_at(w: float, h: float) -> Rect2:
	var step := 28.0
	var n := _cascade % 7
	_cascade += 1
	var x := 120.0 + n * step
	var y := PANEL_H + 30.0 + n * step
	if x + w > size.x - 20:
		x = max(120.0, size.x - w - 20)
	if y + h > size.y - FOOT_H - 20:
		y = max(PANEL_H + 10.0, size.y - FOOT_H - h - 20)
	return Rect2(x, y, w, h)


# THE APPLICATIONS MENU. The word sat in the panel doing nothing, which is
# worse than not having it -- a menu that does not open reads as a broken
# desktop rather than an unfinished one.
var menu_open := false
const MENU_W := 190.0
const MENU_ROW := 26.0

func _menu_rect() -> Rect2:
	return Rect2(4, PANEL_H, MENU_W, MENU_ROW * LAUNCHERS.size() + 8)


func _draw_menu() -> void:
	if not menu_open:
		return
	var r := _menu_rect()
	panel.draw_rect(Rect2(r.position.x, r.position.y - PANEL_H + PANEL_H, r.size.x, r.size.y),
		Color("#f4f4f4"))


var _drag: Control = null
var _dragfrom := Vector2.ZERO
var _sizing: Control = null

func _bar_input(w: Control, e: InputEvent) -> void:
	if e is InputEventMouseButton and e.button_index == MOUSE_BUTTON_LEFT:
		if e.pressed:
			if e.position.x > w.size.x - 22:
				w.visible = false
				panel.queue_redraw()
				return
			_drag = w
			_dragfrom = e.position
			_raise(w)
		else:
			if _drag == w:
				_snap(w)
			_drag = null
	elif e is InputEventMouseMotion and _drag == w:
		w.position += e.position - _dragfrom
		w.position.y = max(PANEL_H, w.position.y)


# SNAPPING. Drag a window against an edge and it takes half the screen; into a
# corner and it takes a quarter; against the top it fills the desktop. It is
# the one window-manager feature people use without being taught, and on a job
# where you are comparing your machine with somebody else's it is not a luxury.
func _snap(w: Control) -> void:
	var W := size.x
	var H := size.y - PANEL_H - FOOT_H
	var m := get_global_mouse_position()
	var l := m.x < 14
	var r := m.x > W - 14
	var t := m.y < PANEL_H + 14
	var b := m.y > size.y - FOOT_H - 14
	var hw := W / 2.0
	var hh := H / 2.0
	if l and t:      _place(w, Rect2(0, PANEL_H, hw, hh))
	elif l and b:    _place(w, Rect2(0, PANEL_H + hh, hw, hh))
	elif r and t:    _place(w, Rect2(hw, PANEL_H, hw, hh))
	elif r and b:    _place(w, Rect2(hw, PANEL_H + hh, hw, hh))
	elif l:          _place(w, Rect2(0, PANEL_H, hw, H))
	elif r:          _place(w, Rect2(hw, PANEL_H, hw, H))
	elif t:          _place(w, Rect2(0, PANEL_H, W, H))


func _place(w: Control, r: Rect2) -> void:
	w.position = r.position
	w.size = r.size
	_relayout(w)


func _grip_input(w: Control, e: InputEvent) -> void:
	if e is InputEventMouseButton and e.button_index == MOUSE_BUTTON_LEFT:
		_sizing = w if e.pressed else null
		if e.pressed:
			_raise(w)
	elif e is InputEventMouseMotion and _sizing == w:
		w.size.x = max(260.0, w.size.x + e.relative.x)
		w.size.y = max(130.0, w.size.y + e.relative.y)
		_relayout(w)


func _find(prefix: String) -> Control:
	for w in windows:
		if is_instance_valid(w) and str(w.get_meta("title")).begins_with(prefix):
			return w
	return null


func _launch(kind: String) -> void:
	var existing := _find(str(TITLES.get(kind, "?")))
	if existing:
		_raise(existing)
		if kind == "chat":
			alerts = 0
			_alert_msg = ""
			foot.queue_redraw()
		return

	match kind:
		"term":
			_open_terminal(0, "terminal - your workstation",
				_cascade_at(700, 420))
		"chat":
			chat = preload("res://scripts/chat.gd").new()
			chat.mono = mono
			chat.machine = machine
			chat.ink = INK
			chat.dim = DIM
			chat.bg = WIN_BG
			_win("chat", _cascade_at(620, 380), chat)
			chat.call("reset", cust)
			chat.call("seed_first",
				"my computer will not start. the sticker on the front says %s" % addr)
			alerts = 0
			_alert_msg = ""
			foot.queue_redraw()
		"files":
			var f := preload("res://scripts/files.gd").new()
			f.mono = mono
			f.machine = machine
			f.on_open_text = func(p2: String) -> void: _open_editor(p2)
			_win("files - your workstation", _cascade_at(470, 400), f)
		"notes":
			var n := preload("res://scripts/notes.gd").new()
			n.mono = mono
			n.machine = machine
			_win("notes", _cascade_at(500, 340), n)
		"log":
			var l := preload("res://scripts/terminal.gd").new()
			l.mono = mono
			l.bg = TERM_BG
			l.fg = TERM_FG
			l.accent = GREEN
			l.banner = []
			l.prompt_fn = func() -> String: return ""
			l.on_command = func(_s: String) -> String: return ""
			_win("log viewer - your workstation", _cascade_at(720, 420), l)
			l.call("write", machine.sh_on(0, "dmesg"))
		"manual":
			var d := preload("res://scripts/manual.gd").new()
			d.mono = mono
			_win("manual", _cascade_at(820, 560), d)
		"g2048":
			var g := preload("res://scripts/g2048.gd").new()
			g.mono = mono
			g.machine = machine
			_win("2048", _cascade_at(360, 460), g)


# A text file, in a window, editable. Clicking a .txt in the file manager
# lands here -- and it writes through the machine's own shell, so what you
# save is what `cat` shows.
func _open_editor(path2: String) -> void:
	var ed := preload("res://scripts/editor.gd").new()
	ed.mono = mono
	ed.machine = machine
	ed.path = path2
	_win("edit - " + path2, _cascade_at(560, 400), ed)


# A terminal bound to ONE machine. which: 0 your workstation, 1 the customer's.
func _open_terminal(which: int, title: String, rect: Rect2) -> Control:
	var t := preload("res://scripts/terminal.gd").new()
	t.mono = mono
	t.bg = TERM_BG
	t.fg = TERM_FG
	t.accent = GREEN
	t.banner = (["NomnixOS %s -- the customer's machine, over the service processor." % addr,
		"You are looking at their console. `rcon` from your own shell drives it.", ""]
		if which == 1 else
		["NomnixOS -- YOUR workstation. A healthy install of the same system.",
		"`rcon connect <address>` reaches a customer's machine.", ""])
	var target := which
	t.prompt_fn = func() -> String:
		return "root@node# " if target == 1 else "you@desk# "
	t.on_command = func(line: String) -> String:
		return _run(target, line)
	_win(title, rect, t)
	return t


# Every command goes to a machine's own /bin/sh. The only thing the desktop
# adds is noticing that a successful `rcon connect` deserves a window.
func _run(which: int, line: String) -> String:
	var s := line.strip_edges()
	if s == "":
		return ""

	# `rcon` ALWAYS RUNS ON YOUR WORKSTATION, whichever terminal you typed it
	# in. The service processor belongs to the machine you are reaching FROM;
	# the customer's box has no route to itself. Without this, `rcon power
	# cycle` typed into the console -- the obvious place to type it -- went to
	# a machine with no peer and did nothing at all, which is exactly what
	# David saw.
	var is_rcon := s.begins_with("rcon") and (s.length() == 4 or s[4] == " ")
	var target := 0 if is_rcon else which

	# A CONSOLE ON A DEAD MACHINE HAS NO SHELL. Attaching to a box that died
	# at initrd used to hand you a working prompt, because the shell is on the
	# disk whatever the boot did. A service processor shows you the machine's
	# screen; if it never reached a shell, there is no shell to type at, and
	# that IS the diagnosis.
	if target == 1 and not machine.booted():
		return "\n[no shell here -- this machine did not finish booting]\n" \
			+ "  what it managed to say is above. from YOUR terminal:\n" \
			+ "    rcon console                what it said\n" \
			+ "    rcon media insert           put the rescue medium in\n" \
			+ "    rcon boot media             boot from it next time\n" \
			+ "    rcon power cycle            restart it\n"

	var out: String = machine.sh_on(target, s)

	if is_rcon and s.begins_with("rcon connect") and out.find("attached") >= 0:
		var t := _open_terminal(1, "console - %s (%s)" % [addr, cust],
			_cascade_at(700, 420))
		t.call("write", machine.sh_on(0, "rcon console"))
		if machine.booted():
			t.call("write", "\n-- their machine is up. this is its console. --\n")
		else:
			t.call("write",
				"\n-- their machine is NOT up: there is no shell to type at. --\n" \
				+ "-- drive it from your own terminal with `rcon`. --\n")

	# Anything that changes the customer's power state changes what the
	# console shows, so repaint it.
	if is_rcon and (s.find("power") >= 0 or s.find("boot") >= 0):
		var con := _find("console - ")
		if con:
			var c2: Control = con.get_meta("content")
			c2.call("write", "\n" + machine.sh_on(0, "rcon console"))

	for w in windows:
		if not is_instance_valid(w):
			continue
		var c: Variant = w.get_meta("content")
		if c and c.has_method("refresh"):
			c.call("refresh")
	return out


func _new_ticket() -> void:
	seed_no += 1
	machine.take_ticket(seed_no, faults)
	addr = machine.peer_addr()
	cust = machine.customer_name()
	for w in windows:
		if is_instance_valid(w):
			w.queue_free()
	windows.clear()
	focused = null
	chat = null
	alerts = 1
	_alert_msg = "%s: my computer will not start. are you there?" % cust
	panel.queue_redraw()
	foot.queue_redraw()


func _process(dt: float) -> void:
	_clock += dt
	if panel:
		panel.queue_redraw()
	if foot:
		foot.queue_redraw()
	_frames += 1
	if _shot_path != "" and _frames >= _shot_after:
		var p := _shot_path
		_shot_path = ""
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png(p)
		print("screenshot: ", p)
		get_tree().quit()


func _parse_args() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--shot="):
			_shot_path = a.substr(7)
		elif a.begins_with("--shot-after="):
			_shot_after = int(a.substr(13))
		elif a.begins_with("--seed="):
			seed_no = int(a.substr(7)) - 1
			_new_ticket()
		elif a.begins_with("--open="):
			for k in a.substr(7).split(","):
				_launch(k)
		elif a.begins_with("--run="):
			var t2 := _find("terminal - your")
			if t2:
				(t2.get_meta("content") as Control).call("feed", a.substr(6) + "\n")
		elif a.begins_with("--tile"):
			# A SCREENSHOT FLAG, not a behaviour. Windows never tile
			# themselves: they open where they open, and the only thing that
			# tiles one is you dragging it to an edge. David was explicit, and
			# he is right -- a desktop that rearranges itself is fighting you.
			var n := 0
			var W := size.x
			var H := size.y - PANEL_H - FOOT_H
			for w2 in windows:
				if not is_instance_valid(w2) or not w2.visible:
					continue
				var slots := [Rect2(0, PANEL_H, W / 2.0, H / 2.0),
					Rect2(W / 2.0, PANEL_H, W / 2.0, H / 2.0),
					Rect2(0, PANEL_H + H / 2.0, W / 2.0, H / 2.0),
					Rect2(W / 2.0, PANEL_H + H / 2.0, W / 2.0, H / 2.0)]
				if n < 4:
					_place(w2, slots[n])
				n += 1
		elif a.begins_with("--type="):
			var t := _find("terminal - your")
			if t:
				(t.get_meta("content") as Control).call("feed", a.substr(7))
		elif a.begins_with("--ask="):
			if chat:
				chat.call("post", 0, a.substr(6))
