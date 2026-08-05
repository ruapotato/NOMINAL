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
# HAS THE TICKET BEEN FINISHED. David fixed a machine, told the customer, and
# nothing happened -- "no, oh thank you it's booting now, no closure or new
# issue came up". A job you have done should end, visibly, and the next one
# should arrive.
var closed := false
var _alert_msg := ""
var _clock := 0.0
var _shot_path := ""
var _shot_after := 0
var _frames := 0

# THE DESKTOP DOES NOT KNOW WHAT APPLICATIONS EXIST. It reads the .desktop
# entries in /usr/share/applications, the way every real desktop does. Delete
# one and its icon goes; corrupt one and it goes; and both are findable with
# `ls` and `cat`. This list is only the fallback for a machine whose registry
# is unreadable -- which is itself a thing worth seeing.
var LAUNCHERS: Array = [["Terminal", "term", "term"]]

func _load_apps() -> void:
	var out: String = machine.de_apps()
	var got: Array = []
	for row in out.split("\n"):
		if row.strip_edges() == "":
			continue
		var f: PackedStringArray = row.split("\t")
		if f.size() >= 3:
			got.append([f[1], f[0], f[2]])
		elif f.size() == 2:
			got.append([f[1], f[0], "app"])
	if got.is_empty():
		got = [["Terminal", "term", "term"]]
	LAUNCHERS = got
const TITLES := {"term": "terminal - your", "chat": "chat", "files": "files",
	"notes": "notes", "log": "log viewer", "manual": "manual", "g2048": "2048",
	"gflappy": "flappy", "gworms": "worms", "browser": "browser",
	"gsnake": "snake", "gmines": "minesweeper", "gblocks": "blocks",
	"gsolitaire": "solitaire", "gliquid": "liquid war", "music": "music", "gsand": "falling sand", "gsetris": "sand tetris",
	"clock": "clock", "imgview": "image viewer", "archman": "archive manager",
	"duview": "disk usage", "charmap": "character map", "search": "search",
	"calc": "calculator", "sysmon": "system monitor",
	"pkgman": "package manager", "svcman": "service manager"}


func _ready() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	mono = preload("res://scripts/uifont.gd").mono()
	if ClassDB.class_exists("NominalStation"):
		machine = ClassDB.instantiate("NominalStation")
	else:
		push_error("NominalStation is not registered - the GDExtension did not load")
		return
	_build_shell()
	_new_ticket()
	# After the ticket, because the workstation is created with it.
	_load_apps()
	wall.queue_redraw()
	# AFTER THE FIRST LAYOUT. `--open=` builds windows, and window placement is
	# arithmetic on the size of the desktop, which is still zero until the tree
	# has laid itself out once. Every window opened from the command line came
	# out in the same clamped corner.
	await get_tree().process_frame
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

	# THE MENU IS ITS OWN LAYER, ABOVE EVERY WINDOW.
	#
	# It was drawn on the wallpaper, which is the bottom of the stack, so it
	# appeared behind whatever was open -- a menu you cannot see is not a
	# menu. It is kept last in the child order along with the panel, and the
	# window raise code puts them back on top.
	menu_layer = Control.new()
	menu_layer.set_anchors_preset(Control.PRESET_FULL_RECT)
	menu_layer.mouse_filter = Control.MOUSE_FILTER_IGNORE
	menu_layer.draw.connect(_draw_menu_layer)
	add_child(menu_layer)

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
	var slots := _desk_slots()
	for i in range(LAUNCHERS.size()):
		var spec: Array = LAUNCHERS[i]
		_draw_icon(wall, slots[i].position + Vector2(10, 4), spec[0],
			spec[2] if spec.size() > 2 else spec[1])

	if false:
		var r := _menu_rect()
		wall.draw_rect(r, Color("#f6f6f6"))
		wall.draw_rect(r, Color("#8b929b"), false, 1.0)
		var my := r.position.y + 6
		for spec in LAUNCHERS:
			wall.draw_string(mono, Vector2(r.position.x + 30, my + 17), spec[0],
				HORIZONTAL_ALIGNMENT_LEFT, -1, 12, Color("#1b1b1b"))
			_draw_icon_small(wall, Vector2(r.position.x + 6, my + 4),
				spec[2] if spec.size() > 2 else spec[1])
			my += MENU_ROW


# THE ICONS ARE A SET, AND THEY LIVE SOMEWHERE ELSE.
#
# This used to be two `match` statements: eight hand-drawn shapes at 34px,
# eight flat coloured squares at 16px, and a grey box with an asterisk for
# everything else -- which, once there were twenty apps, was most of them.
# `icons.gd` draws all twenty-one from one palette in normalised coordinates,
# so the launcher, the Applications menu and the window list ask for the same
# icon at three sizes and get three sizes of the same drawing.
const Icons := preload("res://scripts/icons.gd")

# WHERE THE DESKTOP ICONS SIT.
#
# They used to march straight down the left edge at 62px a step, which was
# fine for the six apps that existed then and walks off the bottom of the
# screen now that the system ships more than twenty. Icons wrap into a second
# column, and a third, the way every desktop has since 1984. One function
# owns the geometry so the click test cannot disagree with the drawing --
# they were two copies of the same arithmetic before, which is how you get an
# icon that launches its neighbour.
const CELL := Vector2(78, 62)

func _desk_slots() -> Array:
	var out: Array = []
	var top := PANEL_H + 10.0
	var usable: float = max(CELL.y, wall.size.y - top - FOOT_H - 6.0)
	var per_col := int(usable / CELL.y)
	for i in range(LAUNCHERS.size()):
		out.append(Rect2(
			Vector2(14 + float(i / per_col) * CELL.x, top + float(i % per_col) * CELL.y),
			CELL))
	return out

# ICON LABELS GET THE WHOLE CELL, AND A SECOND LINE IF THEY NEED ONE.
#
# They were clipped to 50px under a 78px cell, so the desktop read "Minesw",
# "Calculato", "Liquid W", "Disk Usa", "Characte", "Image Vi" -- six launchers
# you have to click to identify. A cut-off word is worse than a small one: it
# looks like the name of the program.
const LABEL_W := CELL.x - 8.0

func _draw_icon(c: Control, at: Vector2, label: String, kind: String) -> void:
	Icons.draw_icon(c, at, 34.0, kind)
	# Two lines at 11px, and if a single word still will not fit on a line --
	# "Minesweeper" is the only one -- the whole label steps down a size rather
	# than losing its ending. A name in smaller type is still the name.
	var fs := 11
	var lines := _label_lines(label, fs)
	while fs > 8 and not _fits(lines, fs):
		fs -= 1
		lines = _label_lines(label, fs)
	var x := at.x + 17.0 - LABEL_W / 2.0
	var y := at.y + 44.0
	for l in lines:
		c.draw_string(mono, Vector2(x, y), l,
			HORIZONTAL_ALIGNMENT_CENTER, LABEL_W, fs, Color("#ffffff"))
		y += fs + 1.0


func _fits(lines: PackedStringArray, fs: int) -> bool:
	for l in lines:
		if mono.get_string_size(l, HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x > LABEL_W:
			return false
	return true


# One line if it fits, otherwise split at the last space that does. Two lines
# is what the cell has room for; a third would run into the icon below it.
func _label_lines(label: String, fs: int) -> PackedStringArray:
	if mono.get_string_size(label, HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x <= LABEL_W:
		return PackedStringArray([label])
	var out := PackedStringArray()
	var line := ""
	for word in label.split(" ", false):
		var t := word if line == "" else line + " " + word
		if mono.get_string_size(t, HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x > LABEL_W \
				and line != "":
			out.append(line)
			line = word
		else:
			line = t
	if line != "":
		out.append(line)
	while out.size() > 2:
		out.remove_at(out.size() - 1)
	return out


# A 16px version of the launcher icon, for the menu.
func _draw_icon_small(c: Control, at: Vector2, kind: String) -> void:
	Icons.draw_icon(c, at, 16.0, kind)


func _draw_panel() -> void:
	panel.draw_rect(Rect2(0, 0, panel.size.x, PANEL_H), PANEL_BG)
	panel.draw_rect(Rect2(0, PANEL_H - 1, panel.size.x, 1), PANEL_EDGE)
	panel.draw_string(mono, Vector2(10, 18), "Applications",
		HORIZONTAL_ALIGNMENT_LEFT, -1, 12, PANEL_INK)
	panel.draw_line(Vector2(96, 4), Vector2(96, PANEL_H - 4), PANEL_EDGE)

	# The window list stops where the clock starts. It used to run straight
	# under it, so the sixth window's title and the time were drawn on top of
	# each other: `console - 10.0.2.84 (Fiona)node-4824 Mon 09:00`.
	var list_end := panel.size.x - 200.0
	var vis: Array = []
	for w in windows:
		if is_instance_valid(w) and w.visible:
			vis.append(w)
	var x := 106.0
	for i in range(vis.size()):
		var w: Control = vis[i]
		var t := str(w.get_meta("title"))
		var wd: float = min(210.0,
			mono.get_string_size(t, HORIZONTAL_ALIGNMENT_LEFT, -1, 11).x + 18)
		if x + wd > list_end:
			# No room. Say how many are not shown rather than drawing them over
			# the clock, and leave their tabs unset so a click by the clock
			# cannot land on a window whose button is not there.
			panel.draw_string(mono, Vector2(x + 2, 18), "+%d" % (vis.size() - i),
				HORIZONTAL_ALIGNMENT_LEFT, -1, 11, PANEL_INK)
			break
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
		"node-%d   %s" % [seed_no % 10000, _wallclock()],
		HORIZONTAL_ALIGNMENT_LEFT, -1, 12, PANEL_INK)


# THE CLOCK RAN BACKWARDS. It printed `Mon 09:%02d` with the SECONDS since
# launch in the minutes field, so it climbed 09:00 to 09:59 and then fell back
# to 09:00 -- a playtester caught it reading 09:42, 09:00, 09:44, 09:01 in
# consecutive screenshots. Everything else on this desktop is a real reading
# off a real machine, and the one decorative thing on it announced itself by
# contradicting itself. A shift starts at 09:00 on a Monday and the minute
# hand only goes forwards: a minute of the clock is a minute of the shift.
const SHIFT_START := 9 * 60      # 09:00
const DAYS := ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]

func _wallclock() -> String:
	var mins := SHIFT_START + int(_clock / 60.0)
	var day: int = int(mins / (24 * 60)) % 7
	mins = mins % (24 * 60)
	return "%s %02d:%02d" % [DAYS[day], int(mins / 60), mins % 60]


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
		var r := _toast_rect()
		foot.draw_rect(r, Color("#fbfbfb"))
		foot.draw_rect(r, Color("#3c6eb4"), false, 2.0)
		foot.draw_rect(Rect2(r.position.x, r.position.y, r.size.x, 20), Color("#3c6eb4"))
		foot.draw_string(mono, r.position + Vector2(8, 15), "new message",
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color("#ffffff"))
		foot.draw_string(mono, r.position + Vector2(8, 40), _alert_msg,
			HORIZONTAL_ALIGNMENT_LEFT, r.size.x - 16, 12, INK)
		foot.draw_string(mono, r.position + Vector2(8, 60),
			"click to open Chat", HORIZONTAL_ALIGNMENT_LEFT, -1, 10, DIM)


# THE NOTIFICATION THAT WOULD NOT LEAVE. It was cleared in exactly one place --
# opening the chat window -- so a playtester who read the message and got on
# with the job had it sitting over the bottom-right corner of the screen for
# ninety minutes, covering whatever window was under it, ignoring clicks. A
# toast is an interruption, and an interruption that does not end is furniture.
#
# It now expires on its own, and clicking it does the thing it is asking you to
# do. The panel's red badge is NOT on a timer and does not move: the toast is
# the interruption, the badge is the unread count, and the count is only
# cleared by reading the message.
#
# The timer is armed by NOTICING the message change rather than by every place
# that sets one, so a notification raised from anywhere -- a new ticket, a
# closed one, a customer speaking up -- expires without having to remember to
# arm it.
const TOAST_LIFE := 12.0
var _alert_at := 0.0
var _alert_shown := ""

func _toast_tick() -> void:
	if _alert_msg != _alert_shown:
		_alert_shown = _alert_msg
		_alert_at = _clock
		return
	if _alert_msg != "" and _clock - _alert_at > TOAST_LIFE:
		_alert_msg = ""
		_alert_shown = ""
		if foot:
			foot.queue_redraw()

func _toast_rect() -> Rect2:
	return Rect2(foot.size.x - 448.0, -78, 440.0, 72)


# The same rectangle in screen coordinates, for the hit test.
func _toast_screen() -> Rect2:
	var r := _toast_rect()
	return Rect2(r.position + foot.position, r.size)




func _wall_input(e: InputEvent) -> void:
	if e is InputEventMouseButton and e.pressed and e.button_index == MOUSE_BUTTON_LEFT:
		if menu_open:
			var idx := _menu_hit(e.position)
			if idx >= 0:
				_launch(LAUNCHERS[idx][1])
			menu_open = false
			if menu_layer:
				menu_layer.queue_redraw()
			return
		var slots := _desk_slots()
		for i in range(LAUNCHERS.size()):
			if slots[i].has_point(e.position):
				_launch(LAUNCHERS[i][1])
				return


func _panel_input(e: InputEvent) -> void:
	if e is InputEventMouseButton and e.pressed and e.button_index == MOUSE_BUTTON_LEFT:
		if e.position.x < 96:
			menu_open = not menu_open
			if menu_layer:
				move_child(menu_layer, get_child_count() - 1)
				move_child(panel, get_child_count() - 1)
				menu_layer.queue_redraw()
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


# WINDOWS REMEMBER THE SIZE YOU GAVE THEM, per application, for the session.
#
# Chat opens too small to read its own first line, so you resize it; close it,
# open it again, and it is small again. Anything you have to do twice in a
# session you will do ten times in a shift. The key is the application, not the
# window: `console - 10.4.2.1 (Annika)` and `log viewer - Annika` are keyed on
# `console` and `log viewer`, so the next ticket's console opens the size you
# made the last one. Position is NOT remembered -- that is the cascade's job,
# and a remembered position is how two windows end up on top of each other.
var _geom := {}

func _app_key(title: String) -> String:
	var i := title.find(" - ")
	return title.substr(0, i) if i > 0 else title


func _win(title: String, rect: Rect2, content: Control) -> Control:
	var key := _app_key(title)
	if _geom.has(key):
		rect.size = _geom[key]
		# It was sized against a desktop that may since have changed shape.
		rect.size.x = minf(rect.size.x, size.x - 20.0) if size.x > 40.0 else rect.size.x
		rect.size.y = minf(rect.size.y, size.y - PANEL_H - FOOT_H - 16.0) \
			if size.y > 100.0 else rect.size.y
		if size.x > 40.0:
			rect.position.x = clampf(rect.position.x, 0.0, size.x - rect.size.x)
		if size.y > 100.0:
			rect.position.y = clampf(rect.position.y, PANEL_H,
				size.y - FOOT_H - rect.size.y)
	var w := Control.new()
	w.position = rect.position
	w.size = rect.size
	w.set_meta("title", title)
	w.set_meta("key", key)
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

	# CLIP. Without this a control paints wherever it likes, and an app that
	# draws past its own rect scribbles on the desktop -- which is how Flappy
	# came to have pipes visibly falling off the back of its window and across
	# the wallpaper. A window is a window because it has edges.
	content.clip_contents = true
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
	if w.has_meta("key"):
		_geom[w.get_meta("key")] = w.size
	(w.get_meta("bar") as Control).size = Vector2(w.size.x, 20)
	(w.get_meta("grip") as Control).position = Vector2(w.size.x - 18, w.size.y - 18)
	(w.get_meta("content") as Control).size = Vector2(w.size.x - 6, w.size.y - 25)
	w.queue_redraw()


func _raise(w: Control) -> void:
	w.visible = true
	move_child(w, get_child_count() - 1)
	move_child(foot, get_child_count() - 1)
	if menu_layer:
		move_child(menu_layer, get_child_count() - 1)
	if panel:
		move_child(panel, get_child_count() - 1)
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
#
# THE CASCADE STOPPED CASCADING AFTER THREE WINDOWS. The step was fixed at
# seven positions and each one was then CLAMPED to fit the screen, so as soon
# as a window was tall enough that step four hung off the bottom -- which at
# 1280x800 is anything over about 540px, ie. most of them -- every subsequent
# window was clamped to the SAME y. A playtester had five windows in one pile.
# Two things were wrong: the run has to end where the room ends rather than at
# a hardcoded seven, and when it ends the next window starts a new run instead
# of landing on top of the last one. Windows bigger than the desktop are cut
# down to it first, because a window you cannot reach the bottom of is worse
# than a small one.
var _cascade := 0

func _cascade_at(w: float, h: float) -> Rect2:
	# Before the first layout pass `size` is still zero -- which is when
	# `--open=` runs -- and every window came out at the same clamped corner.
	var W: float = size.x if size.x > 1.0 else 1280.0
	var H: float = size.y if size.y > 1.0 else 800.0
	var top := PANEL_H + 10.0
	w = minf(w, W - 40.0)
	h = minf(h, H - FOOT_H - top - 10.0)
	var step := 28.0
	var maxx := W - w - 20.0
	var maxy := H - FOOT_H - h - 10.0
	var steps: int = clampi(int(minf(maxx - 120.0, maxy - top) / step), 1, 8)
	var n: int = _cascade % steps
	var run: int = int(_cascade / steps) % 3
	_cascade += 1
	var x := clampf(120.0 + n * step + run * 13.0, 0.0, maxf(0.0, maxx))
	var y := clampf(top + n * step + run * 13.0, top, maxf(top, maxy))
	return Rect2(x, y, w, h)


# THE APPLICATIONS MENU. The word sat in the panel doing nothing, which is
# worse than not having it -- a menu that does not open reads as a broken
# desktop rather than an unfinished one.
var menu_open := false
var menu_layer: Control


func _draw_menu_layer() -> void:
	if not menu_open:
		return
	var r := _menu_rect()
	menu_layer.draw_rect(Rect2(r.position + Vector2(3, 3), r.size), Color(0, 0, 0, 0.18))
	menu_layer.draw_rect(r, Color("#f6f6f6"))
	menu_layer.draw_rect(r, Color("#8b929b"), false, 1.0)
	var rows := _menu_rows()
	for i in range(LAUNCHERS.size()):
		var spec: Array = LAUNCHERS[i]
		var mx: float = r.position.x + float(i / rows) * MENU_W
		var my: float = r.position.y + 6 + float(i % rows) * MENU_ROW
		if i >= rows:
			menu_layer.draw_line(Vector2(mx, r.position.y + 2),
				Vector2(mx, r.position.y + r.size.y - 2), Color("#dfe1e5"))
		menu_layer.draw_string(mono, Vector2(mx + 30, my + 17), spec[0],
			HORIZONTAL_ALIGNMENT_LEFT, -1, 12, Color("#1b1b1b"))
		_draw_icon_small(menu_layer, Vector2(mx + 6, my + 4),
			spec[2] if spec.size() > 2 else spec[1])
const MENU_W := 190.0
const MENU_ROW := 26.0

# The menu has the same problem the desktop had: twenty-eight rows is taller
# than the screen. It grows sideways instead, and _menu_rows() is the single
# answer to "how many fit in a column", used by the drawing and by the hit
# test so the two cannot drift apart.
func _menu_rows() -> int:
	var usable: float = max(MENU_ROW, size.y - PANEL_H - FOOT_H - 12.0)
	return max(1, int(usable / MENU_ROW))


func _menu_rect() -> Rect2:
	var rows := _menu_rows()
	var cols := int(ceil(float(LAUNCHERS.size()) / float(rows)))
	var used: int = min(LAUNCHERS.size(), rows)
	return Rect2(4, PANEL_H, MENU_W * float(max(1, cols)),
		MENU_ROW * float(used) + 8)


# Which entry is under the pointer, or -1. Column-major, matching the draw.
func _menu_hit(p: Vector2) -> int:
	var r := _menu_rect()
	if not r.has_point(p):
		return -1
	var rows := _menu_rows()
	var col := int((p.x - r.position.x) / MENU_W)
	var row := int((p.y - r.position.y - 6) / MENU_ROW)
	if row < 0 or row >= rows:
		return -1
	var idx := col * rows + row
	return idx if idx >= 0 and idx < LAUNCHERS.size() else -1


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


# .desktop Exec names to the windows this desktop can build.
const EXEC_MAP := {
	"terminal": "term", "term": "term", "chat": "chat", "files": "files",
	"notes": "notes", "logview": "log", "log": "log", "manual": "manual",
	"browser": "browser", "g2048": "g2048", "gflappy": "gflappy",
	"gworms": "gworms", "gsnake": "gsnake", "snake": "gsnake",
	"gmines": "gmines", "minesweeper": "gmines",
	"gblocks": "gblocks", "blocks": "gblocks",
	"gsolitaire": "gsolitaire", "solitaire": "gsolitaire",
	"gliquid": "gliquid", "liquid": "gliquid", "music": "music",
	"gsand": "gsand", "sand": "gsand", "gsetris": "gsetris", "setris": "gsetris",
	"clock": "clock", "imgview": "imgview", "archman": "archman",
	"duview": "duview", "charmap": "charmap", "search": "search",
	"calc": "calc", "calculator": "calc",
	"sysmon": "sysmon", "pkgman": "pkgman", "svcman": "svcman",
}

func _launch(kind0: String) -> void:
	var kind: String = EXEC_MAP.get(kind0, kind0)
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
			# WHAT IS ACTUALLY WRONG WITH IT. This sentence was hard coded
			# to "my computer will not start", which is false on about one
			# ticket in four -- a machine that is up with a service dead is a
			# real and commoner call, and a blind playtester spent three of
			# them hunting a boot failure that had already happened
			# successfully. The engine knows; ask it.
			chat.call("seed_first",
				"%s the sticker on the front says %s"
					% [machine.call("complaint"), addr])
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
			# THE LOG VIEWER SHOWED YOUR OWN BOOT LOG, which is the one log
			# nobody needs -- your workstation is fine. David: "Log view seems
			# to have no point." It follows the CUSTOMER's machine now: their
			# console if you are attached to it, their previous boot or their
			# /var/log/messages if you can reach the disk, and it says which it
			# is showing.
			#
			# It was a terminal.gd with the prompt removed, which made it the
			# console window twice over. It is its own app now, with the three
			# things a console cannot do: grep, severity, and a choice of log.
			var l := preload("res://scripts/logview.gd").new()
			l.mono = mono
			l.machine = machine
			l.addr = addr
			l.cust = cust
			_win("log viewer - %s" % cust, _cascade_at(760, 440), l)
		"manual":
			var d := preload("res://scripts/manual.gd").new()
			d.mono = mono
			_win("manual", _cascade_at(820, 560), d)
		"g2048":
			var g := preload("res://scripts/g2048.gd").new()
			g.mono = mono
			g.machine = machine
			_win("2048", _cascade_at(360, 460), g)
		"gflappy":
			var g2 := preload("res://scripts/gflappy.gd").new()
			g2.mono = mono
			g2.machine = machine
			_win("flappy", _cascade_at(520, 400), g2)
		"gworms":
			var g3 := preload("res://scripts/gworms.gd").new()
			g3.mono = mono
			g3.machine = machine
			_win("worms", _cascade_at(720, 480), g3)
		"browser":
			var b := preload("res://scripts/browser.gd").new()
			b.mono = mono
			b.machine = machine
			_win("browser", _cascade_at(720, 520), b)
		"gsnake":
			var g4 := preload("res://scripts/gsnake.gd").new()
			g4.mono = mono
			g4.machine = machine
			_win("snake", _cascade_at(560, 420), g4)
		"gmines":
			var g5 := preload("res://scripts/gmines.gd").new()
			g5.mono = mono
			g5.machine = machine
			_win("minesweeper", _cascade_at(560, 440), g5)
		"gblocks":
			var g6 := preload("res://scripts/gblocks.gd").new()
			g6.mono = mono
			g6.machine = machine
			_win("blocks", _cascade_at(520, 560), g6)
		"gsolitaire":
			var g7 := preload("res://scripts/gsolitaire.gd").new()
			g7.mono = mono
			g7.machine = machine
			_win("solitaire", _cascade_at(760, 520), g7)
		"gliquid":
			var g8 := preload("res://scripts/gliquid.gd").new()
			g8.mono = mono
			g8.machine = machine
			_win("liquid war", _cascade_at(760, 520), g8)
		"music":
			var mu := preload("res://scripts/music.gd").new()
			mu.mono = mono
			mu.machine = machine
			_win("music", _cascade_at(560, 420), mu)
		"clock":
			var cl := preload("res://scripts/clock.gd").new()
			cl.mono = mono
			cl.machine = machine
			_win("clock", _cascade_at(480, 420), cl)
		"imgview":
			var iv := preload("res://scripts/imgview.gd").new()
			iv.mono = mono
			iv.machine = machine
			_win("image viewer", _cascade_at(640, 480), iv)
		"archman":
			var am := preload("res://scripts/archman.gd").new()
			am.mono = mono
			am.machine = machine
			_win("archive manager", _cascade_at(720, 480), am)
		"duview":
			var dv := preload("res://scripts/duview.gd").new()
			dv.mono = mono
			dv.machine = machine
			_win("disk usage", _cascade_at(720, 500), dv)
		"charmap":
			var cm := preload("res://scripts/charmap.gd").new()
			cm.mono = mono
			cm.machine = machine
			_win("character map", _cascade_at(620, 460), cm)
		"search":
			var se := preload("res://scripts/search.gd").new()
			se.mono = mono
			se.machine = machine
			_win("search", _cascade_at(640, 440), se)
		"gsand":
			var sa := preload("res://scripts/gsand.gd").new()
			sa.mono = mono
			sa.machine = machine
			_win("falling sand", _cascade_at(720, 560), sa)
		"gsetris":
			var st := preload("res://scripts/gsetris.gd").new()
			st.mono = mono
			st.machine = machine
			_win("sand tetris", _cascade_at(560, 620), st)
		"calc":
			var ca := preload("res://scripts/calc.gd").new()
			ca.mono = mono
			ca.machine = machine
			_win("calculator", _cascade_at(420, 420), ca)
		"sysmon":
			_win_tool("system monitor", "res://scripts/sysmon.gd",
				_cascade_at(720, 460))
		"pkgman":
			_win_tool("package manager", "res://scripts/pkgman.gd",
				_cascade_at(760, 480))
		"svcman":
			_win_tool("service manager", "res://scripts/svcman.gd",
				_cascade_at(740, 460))


# The three apps that INSPECT A MACHINE, rather than being a machine's toy.
#
# They all want the same thing: a way to run a command on whichever box is
# actually interesting. That is the customer's, whenever the customer's is
# up -- your own workstation is healthy, and a package manager showing you
# twenty-eight intact packages on a box nobody reported is the log viewer's
# old sin in a new window. It falls back to the workstation when theirs is
# dark, and the app says which it is looking at because `_sh` is asked fresh
# every refresh: when their machine comes back, the next poll follows it.
#
# AND THE TITLE SAYS WHOSE MACHINE IT IS. "system monitor" over a column of
# numbers taken from somebody else's box tells you nothing about whose
# afternoon those numbers describe -- the playtester could not tell, and the
# log viewer has said "log viewer - Annika" for weeks. The title is not fixed
# at open, because the machine these apps read is not fixed either: it follows
# the customer's box while it is up and falls back to your workstation while
# it is down, so the title is rewritten by the same call that chooses. A title
# that named the customer over your own workstation's numbers would be a
# worse lie than the generic one.
func _win_tool(title: String, script: String, rect: Rect2) -> Control:
	var a: Control = load(script).new()
	a.mono = mono
	a.machine = machine
	var box: Array = [null]
	a.sh = func(cmd: String) -> String:
		var theirs: bool = machine.booted()
		var t2 := "%s - %s" % [title, cust if theirs else "your workstation"]
		var w2: Variant = box[0]
		if w2 != null and is_instance_valid(w2) and str(w2.get_meta("title")) != t2:
			w2.set_meta("title", t2)
			w2.queue_redraw()
			if panel:
				panel.queue_redraw()
		return str(machine.sh_on(1 if theirs else 0, cmd))
	var w := _win("%s - %s" % [title,
		cust if machine.booted() else "your workstation"], rect, a)
	box[0] = w
	return a


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
		if target == 1:
			# NO PROMPT ON A MACHINE THAT DID NOT BOOT. Showing `root@node#`
			# and then answering every command with "no shell here" is worse
			# than showing nothing: the prompt is a promise. A console
			# attached to a dead box shows the screen, and the screen has no
			# shell on it.
			return "root@node# " if machine.booted() else ""
		return "you@desk# "
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
		return ""

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

	_check_closed()

	# Anything that changes the customer's power state changes what the
	# console shows, so repaint it.
	# A POWER CYCLE CLEARS THE SCREEN. Appending the new boot underneath the
	# old one is not what a monitor does -- the machine went off, the screen
	# went black, and what you see afterwards is this boot and only this boot.
	if is_rcon and (s.find("power") >= 0 or s.find("boot media") >= 0
			or s.find("boot disk") >= 0):
		var con := _find("console - ")
		if con:
			var c2: Control = con.get_meta("content")
			c2.call("clear")
			c2.call("write", machine.sh_on(0, "rcon console"))
			# SAY WHICH SYSTEM CAME UP, NOT JUST THAT ONE DID.
			#
			# This printed "this machine is UP" over a rescue image, which is
			# the same lie the ticket-closure bug told, in a smaller font. The
			# rescue medium is a complete working system that was never
			# broken; of course it boots. What the player needs to know is
			# whose system is under the prompt.
			if machine.booted():
				if machine.on_rescue():
					c2.call("write",
						"\n*** the RESCUE MEDIUM is up. this is not their system. ***\n"
						+ "*** their disk is /dev/sda1 and is not mounted yet. ***\n")
				else:
					c2.call("write",
						"\n*** this machine is UP, from its OWN disk. ***\n"
						+ "*** you have a shell here now. ***\n")

	for w in windows:
		if not is_instance_valid(w):
			continue
		var c: Variant = w.get_meta("content")
		if c and c.has_method("refresh"):
			c.call("refresh")
	return out


# THE SOUND OF A BOX COMING BACK.
#
# There is exactly one moment in a shift worth a noise, and it is not this
# program starting up: it is the customer's machine reaching a login prompt
# after you fixed whatever was stopping it. That is the thing the whole job is
# for, and until now it happened in silence while you were reading a console.
#
# It is an EDGE, not a state: booted() going false -> true. Playing it on the
# state would retrigger every frame the machine is up, and playing it at
# _ready would fire on your own workstation's startup -- which is not an
# event, it boots healthy every single time and nobody in this game ever
# waited for it. `booted()` is the CUSTOMER's machine and only theirs, so the
# workstation cannot set this off at all; _was_booted is re-armed from the new
# machine in _new_ticket so a ticket that happens to arrive already running
# does not congratulate you for it.
var _jingle: AudioStreamPlayer = null
var _was_booted := false

func _boot_watch() -> void:
	if machine == null:
		return
	var up: bool = machine.booted()
	if up and not _was_booted:
		# Loaded on the first boot, not at scene load: a shift where nothing
		# ever comes up never pays for the sample.
		if _jingle == null:
			_jingle = AudioStreamPlayer.new()
			_jingle.stream = load("res://sounds/boot-jingle.wav")
			add_child(_jingle)
		if _jingle.stream != null:
			_jingle.play()
	_was_booted = up


func _check_closed() -> void:
	if closed or machine == null:
		return
	if not machine.healthy():
		return
	closed = true
	var con := _find("console - ")
	if con:
		var c2: Control = con.get_meta("content")
		c2.call("write",
			"\n===============================================\n"
			+ "  THIS MACHINE IS FIXED.\n"
			+ "  every service that should be running is running.\n"
			+ "===============================================\n")
	if chat:
		chat.call("say_from_customer",
			"Oh brilliant -- it has come back up. Everything is where it was. "
			+ "Thank you, honestly. I will let you go.")
	alerts += 1
	_alert_msg = "%s: it is working again. ticket closed." % cust
	if panel:
		panel.queue_redraw()
	if foot:
		foot.queue_redraw()


func _new_ticket() -> void:
	closed = false
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
	# The badge is the first thing said about the machine, so it has to be
	# true of THIS machine -- see the note at seed_first.
	_alert_msg = "%s: %s are you there?" % [cust, machine.call("complaint")]
	# Re-arm the jingle against THIS machine's state. A ticket whose machine
	# is already up is not a boot you performed.
	_was_booted = machine.booted()
	panel.queue_redraw()
	foot.queue_redraw()


# THE NEXT CALL. A shift is not one ticket.
#
# The playtester finished a job, waited, checked the chat, checked the
# Applications menu, browsed helpdesk.internal, and nothing ever came. The only
# way to a second ticket was relaunching the game with `--seed=`, which is a
# thing a player cannot know and should not have to. Closing a ticket now hands
# you the next call after a breather, announced exactly the way the first one
# is: the badge, the toast, and a customer already talking in the chat.
#
# It is scheduled by WATCHING `closed` rather than by hooking the code that
# sets it, so what counts as a finished job stays the one decision it already
# was, made in one place.
const NEXT_TICKET_AFTER := 25.0
var _next_at := 0.0

func _shift_tick() -> void:
	if closed:
		if _next_at == 0.0:
			_next_at = _clock + NEXT_TICKET_AFTER
	elif _next_at != 0.0:
		_next_at = 0.0
	if _next_at > 0.0 and _clock >= _next_at:
		_next_at = 0.0
		_new_ticket()


# The menu overlays windows, so it must see the click first. _input runs
# before any control's _gui_input, which is exactly the priority a popup needs.
func _input(e: InputEvent) -> void:
	# THE TOAST IS A BUTTON. It says "click to open Chat" and it used to say
	# "open Chat to answer" and swallow nothing, because the footer panel it
	# is drawn on ignores the mouse. It is tested here, ahead of the windows,
	# for the same reason the menu is: it is drawn on top of them.
	if _alert_msg != "" and e is InputEventMouseButton and e.pressed \
			and e.button_index == MOUSE_BUTTON_LEFT \
			and _toast_screen().has_point(get_global_mouse_position()):
		_alert_msg = ""
		_alert_shown = ""
		foot.queue_redraw()
		get_viewport().set_input_as_handled()
		_launch("chat")
		return
	if not menu_open:
		return
	if e is InputEventMouseButton and e.pressed and e.button_index == MOUSE_BUTTON_LEFT:
		var r := _menu_rect()
		var at := get_global_mouse_position()
		if r.has_point(at):
			var idx := int((at.y - r.position.y - 6) / MENU_ROW)
			menu_open = false
			menu_layer.queue_redraw()
			get_viewport().set_input_as_handled()
			if idx >= 0 and idx < LAUNCHERS.size():
				_launch(LAUNCHERS[idx][1])
		else:
			menu_open = false
			menu_layer.queue_redraw()


func _process(dt: float) -> void:
	# DRAIN THE DISPLAY SERVER'S SOCKET. `open g2048` in a terminal writes a
	# line to /run/nomde/requests and this is what turns it into a window --
	# which is the whole of David's "start any of the graphical applications
	# from the command line", and it goes through the OS rather than round it.
	if machine:
		var req: String = machine.de_requests()
		if req.strip_edges() != "":
			for r in req.split("\n"):
				if r.strip_edges() != "":
					_launch(r.strip_edges())

	# Whatever brought the machine up -- a command you typed, a request off
	# /run/nomde/requests, a power cycle from the console -- it came up here.
	_boot_watch()

	_clock += dt
	_toast_tick()
	_shift_tick()
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
