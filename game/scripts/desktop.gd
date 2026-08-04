# desktop.gd — the mainframe console.
#
# MATE-shaped on purpose: a top panel with an Applications menu and a clock, a
# bottom taskbar with a window list, and real windows you drag, raise and close.
# Familiar grammar, strange dialect.
#
# It owns no game logic. Every window is a view onto the same NominalStation,
# and the Terminal calls the identical shell_exec() the TCP socket uses, so the
# desktop and a telnet session cannot drift apart.
#
# The desktop belongs to the MAINFRAME, which lives in the core. Teleport to a
# segment and you are not sitting at it any more: you get the handheld and the
# physical rack instead. See D16.
extends Control

# Hamnix is a LIGHT desktop: steel-blue wallpaper, blue gradient title bars
# with white text, light grey window bodies, colourful icons. Matching it is
# most of the difference between "a terminal on black" and "a workstation".
const WALL_TOP  := Color("#4a6ea5")   # desktop wallpaper, top
const WALL_BOT  := Color("#1b2a47")   # ...and bottom
const PANEL     := Color("#d8d8d8")   # top and bottom panels
const PANEL_EDGE:= Color("#9a9a9a")
const TITLE_HI  := Color("#4d8ddb")   # active title bar, light end
const TITLE_LO  := Color("#2a5fa8")   # ...and dark end
const TITLE_OFF := Color("#8e9aa8")   # an unfocused window
const BODY      := Color("#f1f1f1")   # window content
const BODY_ALT  := Color("#e2e6ea")
const EDGE      := Color("#2a5fa8")
const INK       := Color("#14181c")   # text on light
const INK_DIM   := Color("#5c6670")
const SEL       := Color("#3a7bd5")   # selection blue
const OKGREEN   := Color("#1f8a4c")
const AMBER     := Color("#c47b12")
const RED       := Color("#c02f2f")
const CYAN      := Color("#1c6f9c")
# terminal-inside-a-window stays dark, because a terminal is dark
const TERM_BG   := Color("#1a1d21")
const TERM_FG   := Color("#d8e0d8")
const TERM_ACC  := Color("#6fdc96")
const TERM_DIM  := Color("#7e8c84")
# aliases the older drawing code still uses
const BG        := Color("#1b2a47")
const PANEL_HI  := Color("#f1f1f1")
const PHOSPHOR  := Color("#1f8a4c")
const DIM       := Color("#5c6670")
const TEXT      := Color("#14181c")

# The station interior, after looking at what Tower Networking Inc actually
# does: saturated colour, thick inked outlines on everything, warm fluorescent
# tubes, and fat black cable bundles draping from the ceiling. Not a cave.
const INKLINE   := Color("#141c22")   # the outline every object gets
const HULL      := Color("#63c3a6")   # the big mint bulkhead
const HULL_DK   := Color("#4ba189")
const HULL_LT   := Color("#8ad9c0")
const SEAM      := Color("#2f6d5c")
const DECK      := Color("#8a6a44")   # warm deck plate
const DECK_DK   := Color("#6d5334")
const HAZARD    := Color("#e8b230")
const RACK_BODY := Color("#9aa5ac")   # racks read LIGHT against the wall
const RACK_DK   := Color("#77838b")
const LAMP      := Color("#fff6d8")
const LAMP_TUBE := Color("#d8e8b0")
const BUNDLE    := Color("#1b2228")   # the drapey cable looms

var station
var tel: Dictionary = {}
var slots: Array = []
var segs: Array = []
var links: Array = []
var running := true
var speed := 3
var log_shown := 0
var mono: Font

var wallpaper: Control
var icons: Control
var desk: Control
var always: Control   # the handheld: survives leaving the core
var room: Control
var taskbar: HBoxContainer
var statuslabel: Label
var msgbadge: Label
var clocklabel: Label
var windows: Array = []
var focused: Control = null

var _shot_path := ""
var _shot_after := -1
var _frames := 0


func _ready() -> void:
	mono = ThemeDB.fallback_font
	set_anchors_preset(Control.PRESET_FULL_RECT)
	station = ClassDB.instantiate("NominalStation")
	if station == null:
		push_error("NominalStation missing — run: make gdext")
		return
	station.load_home(ProjectSettings.globalize_path("res://../home"))

	_build_shell()
	_boot()

	for a in OS.get_cmdline_user_args():
		if a.begins_with("--shot="): _shot_path = a.substr(7)
		elif a.begins_with("--shot-after="): _shot_after = int(a.substr(13))
		elif a == "--close-all":
			for w in windows.duplicate():
				if is_instance_valid(w): _close_window(w)
		elif a.begins_with("--open="):
			for one in a.substr(7).split(","):
				_open_app(one)
		elif a.begins_with("--run="):
			for one in a.substr(6).split(";"):
				_exec(one)


func _boot() -> void:
	_exec("detach")
	for n in ["boot.nom", "serve.nom"]:
		_exec("attach /home/scripts/" + n)
	_exec("launch")
	_open_app("Hardware")
	_open_app("Monitor")
	_open_app("Messages")
	_open_app("Terminal")


# ================================================================= chrome
func _flat(fill: Color, border: Color, w := 1) -> StyleBoxFlat:
	var sb := StyleBoxFlat.new()
	sb.bg_color = fill
	sb.border_color = border
	sb.set_border_width_all(w)
	sb.content_margin_left = 6
	sb.content_margin_right = 6
	sb.content_margin_top = 2
	sb.content_margin_bottom = 2
	return sb


func _build_shell() -> void:
	# steel-blue gradient wallpaper, like the reference
	var bg := Control.new()
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	bg.mouse_filter = Control.MOUSE_FILTER_IGNORE
	bg.draw.connect(func():
		var h := bg.size.y
		var bands := 48
		for i in range(bands):
			var t := float(i) / float(bands - 1)
			bg.draw_rect(Rect2(Vector2(0, h * t), Vector2(bg.size.x, h / bands + 1)),
					WALL_TOP.lerp(WALL_BOT, t)))
	add_child(bg)
	wallpaper = bg

	icons = Control.new()
	icons.set_anchors_preset(Control.PRESET_FULL_RECT)
	icons.mouse_filter = Control.MOUSE_FILTER_STOP
	icons.draw.connect(func(): _draw_icons(icons))
	icons.gui_input.connect(_icon_input)
	add_child(icons)

	desk = Control.new()
	desk.set_anchors_preset(Control.PRESET_FULL_RECT)
	desk.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(desk)

	room = Control.new()
	room.set_anchors_preset(Control.PRESET_FULL_RECT)
	room.mouse_filter = Control.MOUSE_FILTER_STOP
	room.visible = false
	room.draw.connect(func(): _draw_room(room))
	room.gui_input.connect(_room_input)
	add_child(room)

	# The handheld sits above the room, because you are holding it.
	always = Control.new()
	always.set_anchors_preset(Control.PRESET_FULL_RECT)
	always.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(always)

	_build_top_panel()
	_build_taskbar()


func _build_top_panel() -> void:
	var bar := Panel.new()
	bar.set_anchors_preset(Control.PRESET_TOP_WIDE)
	bar.custom_minimum_size = Vector2(0, 26)
	bar.add_theme_stylebox_override("panel", _flat(PANEL, PANEL_EDGE))
	add_child(bar)

	var row := HBoxContainer.new()
	row.set_anchors_preset(Control.PRESET_FULL_RECT)
	row.add_theme_constant_override("separation", 12)
	bar.add_child(row)

	var apps := MenuButton.new()
	apps.text = "Applications"
	apps.flat = true
	apps.add_theme_color_override("font_color", INK)
	var pm := apps.get_popup()
	for a in ["Terminal", "Messages", "Hardware", "Monitor", "Files", "Logs"]:
		pm.add_item(a)
	pm.id_pressed.connect(func(id): _open_app(pm.get_item_text(id)))
	row.add_child(apps)

	for spec in [["Run", "_do_run"], ["Pause", "_do_pause"], ["Step", "_do_step"]]:
		var b := Button.new()
		b.text = spec[0]
		b.flat = true
		b.add_theme_color_override("font_color", INK_DIM)
		b.add_theme_color_override("font_hover_color", SEL)
		b.pressed.connect(Callable(self, spec[1]))
		row.add_child(b)

	statuslabel = Label.new()
	statuslabel.add_theme_color_override("font_color", INK)
	statuslabel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(statuslabel)

	msgbadge = Label.new()
	msgbadge.add_theme_color_override("font_color", RED)
	row.add_child(msgbadge)

	clocklabel = Label.new()
	clocklabel.add_theme_color_override("font_color", INK)
	row.add_child(clocklabel)
	var pad := Label.new()
	pad.text = "  "
	row.add_child(pad)


func _build_taskbar() -> void:
	var bar := Panel.new()
	bar.set_anchors_preset(Control.PRESET_BOTTOM_WIDE)
	bar.custom_minimum_size = Vector2(0, 24)
	bar.add_theme_stylebox_override("panel", _flat(PANEL, PANEL_EDGE))
	bar.set_anchor_and_offset(SIDE_TOP, 1.0, -24)
	add_child(bar)

	taskbar = HBoxContainer.new()
	taskbar.set_anchors_preset(Control.PRESET_FULL_RECT)
	taskbar.add_theme_constant_override("separation", 4)
	bar.add_child(taskbar)


# Desktop launchers down the left, as the reference has them.
const ICON_COL := 108   # windows start right of the launcher column

const ICONS := [
	["Terminal", "#1a1d21", "#6fdc96"],
	["Messages", "#2a5fa8", "#ffffff"],
	["Monitor",  "#1f6b3a", "#c8f0d8"],
	["Hardware", "#7a5a2a", "#f0dcb0"],
	["Files",    "#c9a227", "#5a4408"],
	["Logs",     "#4a4f57", "#dfe4ea"],
]

func _icon_rect(i: int) -> Rect2:
	return Rect2(Vector2(16, 40 + i * 74), Vector2(76, 68))


func _draw_icons(c: Control) -> void:
	for i in range(ICONS.size()):
		var r := _icon_rect(i)
		var glyph := Rect2(r.position + Vector2(20, 2), Vector2(36, 34))
		c.draw_rect(Rect2(glyph.position + Vector2(2, 3), glyph.size), Color(0, 0, 0, 0.25))
		c.draw_rect(glyph, Color(ICONS[i][1]))
		c.draw_rect(glyph, Color(1, 1, 1, 0.35), false, 1.0)
		c.draw_string(mono, glyph.position + Vector2(8, 24), str(ICONS[i][0]).substr(0, 2),
				HORIZONTAL_ALIGNMENT_LEFT, -1, 15, Color(ICONS[i][2]))
		c.draw_string(mono, r.position + Vector2(2, 52), str(ICONS[i][0]),
				HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color(1, 1, 1, 0.92))


func _icon_input(e: InputEvent) -> void:
	if e is InputEventMouseButton and e.pressed and e.button_index == MOUSE_BUTTON_LEFT:
		for i in range(ICONS.size()):
			if _icon_rect(i).has_point(e.position):
				_open_app(str(ICONS[i][0]))
				return


# ================================================================ windows
func _make_window(title: String, rect: Rect2) -> Control:
	var win := Panel.new()
	win.position = rect.position
	win.size = rect.size
	win.add_theme_stylebox_override("panel", _flat(BODY, EDGE, 2))
	win.clip_contents = true   # content must never spill onto the desktop
	win.set_meta("title", title)
	desk.add_child(win)
	windows.append(win)

	var strip := Control.new()
	strip.position = Vector2(2, 2)
	strip.size = Vector2(rect.size.x - 4, 22)
	strip.mouse_filter = Control.MOUSE_FILTER_STOP
	strip.draw.connect(func():
		var active: bool = (win == focused)
		for i in range(11):
			var t := float(i) / 10.0
			var col: Color = TITLE_HI.lerp(TITLE_LO, t) if active else TITLE_OFF.lerp(TITLE_OFF.darkened(0.2), t)
			strip.draw_rect(Rect2(Vector2(0, strip.size.y * t), Vector2(strip.size.x, strip.size.y / 10.0 + 1)), col))
	win.add_child(strip)

	var tl := Label.new()
	tl.text = " " + title
	tl.add_theme_color_override("font_color", Color.WHITE)
	tl.add_theme_font_size_override("font_size", 12)
	tl.mouse_filter = Control.MOUSE_FILTER_IGNORE
	tl.size = strip.size
	strip.add_child(tl)

	# minimise / maximise / close, as the reference has them
	var bx := rect.size.x - 62
	for spec2 in [["_", "min"], ["\u25a1", "max"], ["\u2715", "close"]]:
		var b2 := Button.new()
		b2.text = spec2[0]
		b2.flat = true
		b2.position = Vector2(bx, 1)
		b2.size = Vector2(18, 18)
		b2.add_theme_font_size_override("font_size", 11)
		b2.add_theme_color_override("font_color", Color.WHITE)
		b2.add_theme_color_override("font_hover_color", Color("#ffd9d9"))
		if spec2[1] == "close":
			b2.pressed.connect(func(): _close_window(win))
		strip.add_child(b2)
		bx += 20

	strip.gui_input.connect(func(e): _drag(win, e))

	var content := MarginContainer.new()
	content.position = Vector2(3, 26)
	content.size = Vector2(rect.size.x - 6, rect.size.y - 29)
	content.add_theme_constant_override("margin_left", 6)
	content.add_theme_constant_override("margin_right", 6)
	content.add_theme_constant_override("margin_top", 4)
	content.add_theme_constant_override("margin_bottom", 4)
	content.clip_contents = true
	win.add_child(content)
	_rebuild_taskbar()
	_raise(win)
	return content


var _dragging: Control = null
var _drag_off := Vector2.ZERO

func _drag(win: Control, e: InputEvent) -> void:
	if e is InputEventMouseButton and e.button_index == MOUSE_BUTTON_LEFT:
		if e.pressed:
			_dragging = win
			_drag_off = win.get_global_mouse_position() - win.position
			_raise(win)
		else:
			_dragging = null
	elif e is InputEventMouseMotion and _dragging == win:
		win.position = win.get_global_mouse_position() - _drag_off
		win.position.y = max(win.position.y, 27.0)


func _raise(win: Control) -> void:
	focused = win
	var parent := win.get_parent()
	if parent != null:
		parent.move_child(win, parent.get_child_count() - 1)
	for w in windows:
		if is_instance_valid(w):
			for ch in w.get_children():
				if ch is Control and not (ch is MarginContainer): ch.queue_redraw()
	_rebuild_taskbar()


func _close_window(win: Control) -> void:
	windows.erase(win)
	win.queue_free()
	_rebuild_taskbar()


func _rebuild_taskbar() -> void:
	if taskbar == null:
		return
	for c in taskbar.get_children():
		c.queue_free()
	for w in windows:
		if not is_instance_valid(w):
			continue
		var b := Button.new()
		b.text = str(w.get_meta("title"))
		b.flat = true
		b.add_theme_font_size_override("font_size", 11)
		b.add_theme_color_override("font_color", SEL if w == focused else INK_DIM)
		b.pressed.connect(func(): _raise(w))
		taskbar.add_child(b)


# The window was laid out by hand, so moving it means fixing up its children.
func _resize_terminal(win: Control) -> void:
	for ch in win.get_children():
		if ch is Control and not (ch is MarginContainer):
			ch.size = Vector2(win.size.x - 4, 22)
			var bx2 := win.size.x - 66
			for g in ch.get_children():
				if g is Button:
					g.position = Vector2(bx2, 1)
					bx2 += 20
				elif g is Label:
					g.size = ch.size
		elif ch is MarginContainer:
			ch.size = Vector2(win.size.x - 6, win.size.y - 29)


func _find_window(title: String) -> Control:
	for w in windows:
		if is_instance_valid(w) and str(w.get_meta("title")) == title:
			return w
	return null


# =================================================================== apps
var term_out: RichTextLabel
var term_in: LineEdit
var msg_out: RichTextLabel
var log_out: RichTextLabel
var files_out: RichTextLabel
var hardware: Control
var monitor: Control

func _open_app(name: String) -> void:
	var existing := _find_window(name)
	if existing != null:
		_raise(existing)
		return
	match name:
		"Terminal":  _app_terminal()
		"Messages":  _app_messages()
		"Hardware":  _app_hardware()
		"Monitor":   _app_monitor()
		"Files":     _app_files()
		"Logs":      _app_logs()


func _mono_label() -> RichTextLabel:
	var r := RichTextLabel.new()
	r.bbcode_enabled = true
	r.scroll_following = true
	r.add_theme_font_override("normal_font", mono)
	r.add_theme_font_size_override("normal_font_size", 12)
	return r


func _app_terminal() -> void:
	var c := _make_window("Terminal", Rect2(ICON_COL, 36, 630 - ICON_COL, 372))
	var col := VBoxContainer.new()
	col.add_theme_constant_override("separation", 3)
	c.add_child(col)

	var screen := PanelContainer.new()
	screen.size_flags_vertical = Control.SIZE_EXPAND_FILL
	screen.add_theme_stylebox_override("panel", _flat(TERM_BG, Color("#3a4149")))
	col.add_child(screen)
	term_out = _mono_label()
	screen.add_child(term_out)

	var rowi := HBoxContainer.new()
	col.add_child(rowi)
	var p := Label.new()
	p.text = "root@station:~$"
	p.add_theme_color_override("font_color", OKGREEN)
	p.add_theme_font_override("font", mono)
	p.add_theme_font_size_override("font_size", 12)
	rowi.add_child(p)

	term_in = LineEdit.new()
	term_in.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	term_in.flat = true
	term_in.add_theme_color_override("font_color", INK)
	term_in.add_theme_color_override("caret_color", SEL)
	term_in.add_theme_font_override("font", mono)
	term_in.add_theme_font_size_override("font_size", 12)
	var blank := StyleBoxEmpty.new()
	for st in ["normal", "focus", "read_only"]:
		term_in.add_theme_stylebox_override(st, blank)
	term_in.text_submitted.connect(_on_command)
	rowi.add_child(term_in)
	term_in.grab_focus()

	_emit(term_out, "[color=#7e8c84]NOMINAL station mainframe.  `help` for commands, `man` for the manual.[/color]")
	_emit(term_out, "[color=#7e8c84]the same shell answers on tcp/7777.[/color]")


func _app_messages() -> void:
	var c := _make_window("Messages", Rect2(640, 36, 626, 236))
	msg_out = _mono_label()
	c.add_child(msg_out)


func _app_logs() -> void:
	var c := _make_window("Logs", Rect2(200, 120, 800, 400))
	log_out = _mono_label()
	c.add_child(log_out)
	log_shown = 0


func _app_files() -> void:
	var c := _make_window("Files", Rect2(260, 160, 620, 400))
	var col := VBoxContainer.new()
	c.add_child(col)
	var path := LineEdit.new()
	path.text = "/"
	path.add_theme_font_override("font", mono)
	col.add_child(path)
	files_out = _mono_label()
	files_out.size_flags_vertical = Control.SIZE_EXPAND_FILL
	col.add_child(files_out)
	var refresh := func():
		var r: String = station.exec("ls " + path.text)
		files_out.clear()
		for line in r.split("\n"):
			if line.begins_with("+OK") or line == "." or line == "":
				continue
			files_out.append_text("[color=#14181c]" + line + "[/color]\n")
	path.text_submitted.connect(func(_t): refresh.call())
	refresh.call()


func _app_hardware() -> void:
	var c := _make_window("Hardware", Rect2(ICON_COL, 416, 1266 - ICON_COL, 344))
	hardware = Control.new()
	hardware.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	hardware.size_flags_vertical = Control.SIZE_EXPAND_FILL
	hardware.draw.connect(func(): _draw_patch(hardware))
	c.add_child(hardware)


func _app_monitor() -> void:
	var c := _make_window("Monitor", Rect2(640, 280, 626, 300))
	monitor = Control.new()
	monitor.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	monitor.size_flags_vertical = Control.SIZE_EXPAND_FILL
	monitor.draw.connect(func(): _draw_monitor(monitor))
	c.add_child(monitor)


# =============================================================== terminal
func _on_command(text: String) -> void:
	term_in.clear()
	if text.strip_edges() == "":
		return
	_emit(term_out, "[color=#6fdc96]$[/color] " + text)
	_exec(text)


func _exec(cmd: String) -> void:
	var resp: String = station.exec(cmd)
	if term_out == null:
		return
	for line in resp.split("\n"):
		if line == "." or line == "":
			continue
		if line.begins_with("+OK") or line.begins_with("+DATA"):
			_emit(term_out, "[color=#6fdc96]" + line + "[/color]")
		elif line.begins_with("-ERR"):
			_emit(term_out, "[color=#ff8080]" + line + "[/color]")
		else:
			_emit(term_out, "[color=#d8e0d8]" + line.replace("[", "[lb]") + "[/color]")


func _emit(r: RichTextLabel, bb: String) -> void:
	if r != null:
		r.append_text(bb + "\n")


# ================================================================== frame
func _do_run() -> void:   running = true
func _do_pause() -> void: running = false
func _do_step() -> void:  station.tick(1)


func _process(_dt: float) -> void:
	if station == null:
		return
	if running:
		station.tick(speed)

	_parse_telemetry(station.telemetry())
	_pump_logs()
	_pump_messages()

	var here: String = str(tel.get("here", "core"))
	var at_core := here == "core"
	desk.visible = at_core
	icons.visible = at_core
	room.visible = not at_core
	if not at_core:
		_physics_step(min(_dt, 0.05))
		room.queue_redraw()
	var tw := _find_window("Terminal")
	if tw != null and is_instance_valid(tw):
		if at_core and tw.has_meta("stowed"):
			if tw.get_parent() != desk:
				tw.get_parent().remove_child(tw)
				desk.add_child(tw)
			tw.position = tw.get_meta("home_pos")
			tw.size = tw.get_meta("home_size")
			_resize_terminal(tw)
			tw.remove_meta("stowed")
		elif not at_core and not tw.has_meta("stowed"):
			if tw.get_parent() != always:
				tw.get_parent().remove_child(tw)
				always.add_child(tw)
			tw.set_meta("home_pos", tw.position)
			tw.set_meta("home_size", tw.size)
			tw.set_meta("stowed", true)
			tw.position = Vector2(size.x - 470, size.y - 300)
			tw.size = Vector2(450, 268)
			_resize_terminal(tw)

	clocklabel.text = "tick %d" % int(tel.get("tick", 0))
	var unread := int(tel.get("unread", 0))
	msgbadge.text = ("  %d unread  " % unread) if unread > 0 else ""
	statuslabel.text = "  %s   %.0f cr (%+.2f/tick)   O2 %.0f%%   bay %.0fC   pool %d   %s" % [
		here, float(tel.get("credits", 0.0)),
		float(tel.get("income", 0.0)) - float(tel.get("bill", 0.0)),
		float(tel.get("o2", 0.0)), float(tel.get("bay", 0.0)),
		int(tel.get("pool", 0)),
		"THROTTLED" if int(tel.get("throttled", 0)) == 1 else ""]

	if wallpaper != null and is_instance_valid(wallpaper): wallpaper.queue_redraw()
	if hardware != null and is_instance_valid(hardware): hardware.queue_redraw()
	if monitor != null and is_instance_valid(monitor):   monitor.queue_redraw()

	_frames += 1
	if _shot_after > 0 and _frames >= _shot_after:
		_shot_after = -1
		await RenderingServer.frame_post_draw
		if _shot_path != "":
			get_viewport().get_texture().get_image().save_png(_shot_path)
			print("screenshot: ", _shot_path)
		get_tree().quit()


# Device files, telemetry and the desktop all speak "key value", so there is
# exactly one parser in the whole program.
func _parse_telemetry(text: String) -> void:
	tel.clear()
	slots.clear()
	segs.clear()
	links.clear()
	for line in text.split("\n"):
		if line == "":
			continue
		var sp := line.find(" ")
		if sp < 0:
			continue
		var key := line.substr(0, sp)
		var val := line.substr(sp + 1)
		if key.begins_with("slot"):
			var f: PackedStringArray = val.split(" ")
			if f.size() >= 12 and f[0] != "empty":
				slots.append({"dev": f[0], "part": f[1], "kind": f[2], "state": int(f[3]),
							  "health": int(f[4]), "rail": f[5], "spine": f[6],
							  "draw": float(f[7]), "data": float(f[8]), "needs": int(f[9]),
							  "x": float(f[10]), "y": float(f[11])})
		elif key.begins_with("seg"):
			var g: PackedStringArray = val.split(" ")
			if g.size() >= 10:
				segs.append({"name": g[0], "kind": g[1], "service": float(g[2]),
							 "pay": float(g[3]), "rail": g[4], "spine": g[5],
							 "power": float(g[6]), "data": float(g[7]),
							 "x": float(g[8]), "y": float(g[9])})
		elif key.begins_with("link"):
			var h: PackedStringArray = val.split(" ")
			if h.size() >= 5:
				links.append({"dev": h[0], "kind": h[1], "cap": float(h[2]),
							  "load": float(h[3]), "ports": int(h[4])})
		elif val.is_valid_float():
			tel[key] = val.to_float()
		else:
			tel[key] = val


func _pump_logs() -> void:
	if log_out == null or not is_instance_valid(log_out):
		return
	var n: int = station.event_count()
	if n <= log_shown:
		return
	var evtext: String = station.events(log_shown)
	for line in evtext.split("\n"):
		if line == "":
			continue
		var sp: int = line.find(" ")
		var t: String = line.substr(0, sp)
		var m: String = line.substr(sp + 1)
		var col := "#14181c"
		if m.contains("SYMPTOM") or m.contains("BROWNOUT"): col = "#c47b12"
		if m.contains("FAILED") or m.contains("error"):     col = "#c02f2f"
		if m.contains("docked") or m.contains("online"):    col = "#1f8a4c"
		log_out.append_text("[color=#8b95a0]%6s[/color] [color=%s]%s[/color]\n" % [t, col, m])
	log_shown = n


var _msg_shown := 0
func _pump_messages() -> void:
	if msg_out == null or not is_instance_valid(msg_out):
		return
	var all: String = station.messages()
	var lines: PackedStringArray = all.split("\n")
	var have := lines.size() - 1
	if have <= _msg_shown:
		return
	for i in range(_msg_shown, have):
		var line: String = lines[i]
		if line.strip_edges() == "":
			continue
		var parts: PackedStringArray = line.strip_edges().split(" ", false, 1)
		if parts.size() < 2:
			continue
		var rest: PackedStringArray = parts[1].strip_edges().split(" ", false, 1)
		var who: String = rest[0]
		var body: String = rest[1] if rest.size() > 1 else ""
		msg_out.append_text("[color=#1c6f9c][b]%s[/b][/color] [color=#8b95a0]t%s[/color]\n" % [who, parts[0]])
		msg_out.append_text("  [color=#14181c]%s[/color]\n\n" % body)
	_msg_shown = have


# ============================================================ patch panel
# A rail carrying more than it can is the single most important thing to see at
# a glance, and it is exactly what a terminal renders badly. This window is the
# reason the desktop exists.
func _draw_patch(c: Control) -> void:
	var r := c.size
	c.draw_rect(Rect2(Vector2.ZERO, r), BODY)
	if links.is_empty():
		return

	var lx := 320.0
	var rowh := 24.0

	c.draw_string(mono, Vector2(0, 12), "CARDS", HORIZONTAL_ALIGNMENT_LEFT, -1, 11, DIM)
	c.draw_string(mono, Vector2(lx, 12), "RAILS AND SPINES", HORIZONTAL_ALIGNMENT_LEFT, -1, 11, DIM)
	c.draw_string(mono, Vector2(lx + 300, 12), "SEGMENTS", HORIZONTAL_ALIGNMENT_LEFT, -1, 11, DIM)

	var linkpos := {}
	var ly := 24.0
	for l in links:
		var over: bool = float(l["load"]) > float(l["cap"])
		var box := Rect2(lx, ly, 200, 32)
		c.draw_rect(box, BODY_ALT)
		c.draw_rect(box, RED if over else Color("#9aa4ae"), false, 1.0)
		c.draw_string(mono, Vector2(lx + 8, ly + 13), "%s  %s" % [l["dev"], l["kind"]],
				HORIZONTAL_ALIGNMENT_LEFT, -1, 12, TEXT)
		var frac: float = clampf(float(l["load"]) / max(float(l["cap"]), 0.01), 0.0, 1.0)
		var bar := Rect2(lx + 8, ly + 19, 184, 6)
		c.draw_rect(bar, Color("#cfd6dc"))
		c.draw_rect(Rect2(bar.position, Vector2(bar.size.x * frac, bar.size.y)), RED if over else OKGREEN)
		c.draw_string(mono, Vector2(lx + 100, ly + 13), "%.1f/%.1f%s" %
				[float(l["load"]), float(l["cap"]), "  SATURATED" if over else ""],
				HORIZONTAL_ALIGNMENT_LEFT, -1, 10, RED if over else DIM)
		linkpos[l["dev"]] = Vector2(lx, ly + 16)
		ly += 42.0

	var y := 24.0
	for s in slots:
		if s["kind"] == "pwrbus" or s["kind"] == "databus":
			continue
		var col := TEXT
		if int(s["state"]) == 2: col = AMBER
		if int(s["state"]) == 3: col = RED
		c.draw_string(mono, Vector2(4, y + 11), "%-8s %-13s" % [s["dev"], s["part"]],
				HORIZONTAL_ALIGNMENT_LEFT, -1, 12, col)
		var anchor := Vector2(250, y + 7)
		var needs: int = int(s.get("needs", 0))
		for key in ["rail", "spine"]:
			var bit := 1 if key == "rail" else 2
			if (needs & bit) == 0:
				continue                       # this card never wanted one
			var target: String = str(s[key])
			if target == "-" or not linkpos.has(target):
				c.draw_string(mono, Vector2(232, y + 11), "UNPATCHED",
						HORIZONTAL_ALIGNMENT_LEFT, -1, 10, RED)
				continue
			var lp: Vector2 = linkpos[target]
			var wire := CYAN if key == "spine" else PHOSPHOR
			var mid := anchor.x + 8 + (6 if key == "spine" else 0)
			c.draw_line(anchor, Vector2(mid, anchor.y), wire, 1.0)
			c.draw_line(Vector2(mid, anchor.y), Vector2(mid, lp.y), wire, 1.0)
			c.draw_line(Vector2(mid, lp.y), Vector2(lp.x - 3, lp.y), wire, 1.0)
		y += rowh

	var sx := lx + 300
	var sy := 24.0
	for sg in segs:
		var svc: float = float(sg["service"])
		var col2 := PHOSPHOR if svc >= 95 else (AMBER if svc > 40 else RED)
		c.draw_string(mono, Vector2(sx + 18, sy + 11), "%-9s %-8s %3d%%" %
				[sg["name"], sg["kind"], int(svc)], HORIZONTAL_ALIGNMENT_LEFT, -1, 12, col2)
		for key in ["rail", "spine"]:
			var target2: String = str(sg[key])
			if target2 == "-" or not linkpos.has(target2):
				c.draw_string(mono, Vector2(sx, sy + 11), "!", HORIZONTAL_ALIGNMENT_LEFT, -1, 12, RED)
				continue
			var lp2: Vector2 = linkpos[target2]
			var wire2 := CYAN if key == "spine" else PHOSPHOR
			c.draw_line(Vector2(sx + 12, sy + 7), Vector2(lp2.x + 203, lp2.y), wire2, 1.0)
		sy += rowh


# ================================================================ monitor
func _draw_monitor(c: Control) -> void:
	var r := c.size
	c.draw_rect(Rect2(Vector2.ZERO, r), BODY)
	var y := 18.0
	var bars := [
		["POWER",   float(tel.get("supply", 0.0)),  max(float(tel.get("supply_rated", 1.0)), 0.01)],
		["COMPUTE", float(tel.get("pool", 0.0)),    max(float(tel.get("rated", 1.0)), 0.01)],
		["O2",      float(tel.get("o2", 0.0)),      100.0],
		["BATTERY", float(tel.get("battery", 0.0)), 40.0],
	]
	for b in bars:
		var frac: float = clampf(float(b[1]) / float(b[2]), 0.0, 1.0)
		c.draw_string(mono, Vector2(0, y), str(b[0]), HORIZONTAL_ALIGNMENT_LEFT, -1, 12, TEXT)
		var bar := Rect2(90, y - 10, r.x - 180, 12)
		c.draw_rect(bar, BODY_ALT)
		c.draw_rect(Rect2(bar.position, Vector2(bar.size.x * frac, bar.size.y)),
				OKGREEN if frac > 0.5 else (AMBER if frac > 0.2 else RED))
		c.draw_rect(bar, Color("#9aa4ae"), false, 1.0)
		c.draw_string(mono, Vector2(r.x - 84, y), "%.1f / %.0f" % [float(b[1]), float(b[2])],
				HORIZONTAL_ALIGNMENT_LEFT, -1, 11, DIM)
		y += 26

	y += 8
	c.draw_string(mono, Vector2(0, y), "SEGMENT      SERVICE", HORIZONTAL_ALIGNMENT_LEFT, -1, 11, DIM)
	y += 18
	# Worst-served first: as the station grows past what fits in the window the
	# rows that get clipped are the ones already fine, which is the right way
	# round for a monitor.
	var ordered := segs.duplicate()
	ordered.sort_custom(func(a, b): return float(a["service"]) < float(b["service"]))
	var shown := 0
	for sg in ordered:
		if y > c.size.y - 16:
			c.draw_string(mono, Vector2(0, y), "... %d more, all better than this"
					% (ordered.size() - shown), HORIZONTAL_ALIGNMENT_LEFT, -1, 11, DIM)
			break
		shown += 1
		var svc: float = float(sg["service"])
		var col := PHOSPHOR if svc >= 95 else (AMBER if svc > 40 else RED)
		c.draw_string(mono, Vector2(0, y), "%-11s %3d%%" % [sg["name"], int(svc)],
				HORIZONTAL_ALIGNMENT_LEFT, -1, 12, col)
		var bar2 := Rect2(150, y - 9, 200, 10)
		c.draw_rect(bar2, BODY_ALT)
		c.draw_rect(Rect2(bar2.position, Vector2(bar2.size.x * clampf(svc / 100.0, 0, 1), bar2.size.y)), col)
		c.draw_string(mono, Vector2(366, y), "%.2f cr" % float(sg["pay"]),
				HORIZONTAL_ALIGNMENT_LEFT, -1, 12, TEXT)
		y += 20


# =============================================================== the bay
# Non-desktop space, and a real one: a floor, racks bolted to it, and equipment
# with weight. Pick a card up and it hangs off the cursor; let go and it FALLS.
# It lands in a rack unit if it is over one, on top of whatever is already on
# the floor if it is not, and it will not pass through either.
#
# Modelled on Tower Networking Inc: mountable racks with y-snap, RJ45 and power
# wall sockets, and a cable you grab by the end.

const U_H      := 24.0     # one rack unit
const RACK_W   := 230.0
const CARD_W   := 200.0
const PORT     := Vector2(13, 13)
const GRAVITY  := 2600.0
const FLOOR_Y  := 690.0
const PX_PER_M := 46.0     # the bay has a scale, so a run has a length

# How tall each kind of card is, in rack units. A reactor is not a NIC.
const U_SIZE := {
	"reactor": 3, "battery": 2, "radiator": 2, "scrubber": 2,
	"cpu": 1, "sensor": 1, "thruster": 2,
}

var racks := [
	{"pos": Vector2(392, 120), "units": 13},
	{"pos": Vector2(648, 168), "units": 10},
]
var phys: Dictionary = {}        # dev -> {pos, vel, u, mount}
var held := ""                   # card in hand
var held_off := Vector2.ZERO
var drag_cable_from := ""
var drag_cable_kind := ""
var mouse_in_room := Vector2.ZERO
var hover_socket := ""


func _u_of(kind: String) -> int:
	return int(U_SIZE.get(kind, 1))


func _card_size(dev: String) -> Vector2:
	if _is_switch(str(phys[dev]["kind"])):
		return Vector2(206, 84)
	return Vector2(CARD_W, _u_of(str(phys[dev]["kind"])) * U_H)


# Where port `idx` of a switch sits, in room coordinates.
func _switch_port(dev: String, idx: int) -> Vector2:
	var base: Vector2 = phys[dev]["pos"]
	return base + Vector2(14 + (idx % 6) * 30, 46 + int(idx / 6) * 24)


func _ports_of(dev: String) -> int:
	for l in links:
		if str(l["dev"]) == dev:
			return int(l["ports"])
	return 0


# Each docked segment has a hatch on the wall with a port beside it.
func _hatches() -> Array:
	var out := []
	for sg in segs:
		out.append({"seg": sg, "pos": Vector2(float(sg.get("x", 330.0)), float(sg.get("y", 514.0)))})
	return out


func _hatch_port(h: Dictionary, kind: String) -> Vector2:
	return h["pos"] + Vector2(146 if kind == "rail" else 170, 24)


func _is_switch(kind: String) -> bool:
	return kind == "pwrbus" or kind == "databus"


func _ensure_phys() -> void:
	var seen := {}
	var i := 0
	for sc in slots:
		var kind: String = str(sc["kind"])
		var dev: String = str(sc["dev"])
		seen[dev] = true
		if not phys.has(dev):
			phys[dev] = {"pos": Vector2.ZERO, "vel": 0.0, "kind": kind, "mount": null}
			if _is_switch(kind):
				# switches hang on the wall where the previous owner left them
				var n := 0
				for d2 in phys.keys():
					if _is_switch(str(phys[d2]["kind"])) and d2 != dev: n += 1
				phys[dev]["pos"] = Vector2(58, 150 + n * 132)
				phys[dev]["mount"] = {"wall": true}
			else:
				_rack_it(dev)
		else:
			phys[dev]["kind"] = kind
		i += 1
	for dev in phys.keys():
		if not seen.has(dev):
			phys.erase(dev)


# New kit goes into the first rack unit it fits in, the way the previous owner
# would have left it. Nothing fits — it goes on the floor and falls.
func _rack_it(dev: String) -> void:
	var occ := _occupied()
	var h := _u_of(str(phys[dev]["kind"]))
	for ri in range(racks.size()):
		var rk: Dictionary = racks[ri]
		for u in range(int(rk["units"]) - h + 1):
			var free := true
			for k in range(h):
				if occ.has("%d:%d" % [ri, u + k]):
					free = false
			if not free:
				continue
			phys[dev]["mount"] = {"rack": ri, "u": u}
			phys[dev]["pos"] = Vector2(rk["pos"].x + 15, _u_y(rk, u + h - 1) - h * U_H)
			return
	phys[dev]["pos"] = Vector2(300, 200)     # no room: it goes on the floor


# Where each rack unit's floor sits, top unit first.
func _u_y(rack: Dictionary, u: int) -> float:
	return rack["pos"].y + (u + 1) * U_H


func _occupied() -> Dictionary:
	var occ := {}
	for dev in phys.keys():
		var m = phys[dev]["mount"]
		if m == null or not m.has("rack"):
			continue                     # a wall-hung switch occupies no rack U
		for k in range(_u_of(str(phys[dev]["kind"]))):
			occ["%d:%d" % [int(m["rack"]), int(m["u"]) + k]] = dev
	return occ


func _rack_under(p: Vector2) -> int:
	for i in range(racks.size()):
		var rk: Dictionary = racks[i]
		if p.x > rk["pos"].x - 20 and p.x < rk["pos"].x + RACK_W + 20:
			return i
	return -1


# Everything that is not in hand falls. Rack shelves and the floor stop it, and
# so does the top of whatever is already lying there.
func _physics_step(dt: float) -> void:
	var occ := _occupied()
	for dev in phys.keys():
		if dev == held:
			continue
		var pd: Dictionary = phys[dev]
		if pd["mount"] != null:
			continue
		if _is_switch(str(pd["kind"])):
			continue                    # a switch stays where you put it
		pd["vel"] += GRAVITY * dt
		var sz := _card_size(dev)
		var np: Vector2 = pd["pos"] + Vector2(0, pd["vel"] * dt)
		var h := _u_of(str(pd["kind"]))

		# a rack unit catches it if it is over the rack and there is room
		var ri := _rack_under(np + Vector2(CARD_W * 0.5, 0))
		var landed := false
		if ri >= 0:
			var rk: Dictionary = racks[ri]
			for u in range(int(rk["units"]) - h + 1):
				var free := true
				for k in range(h):
					if occ.has("%d:%d" % [ri, u + k]):
						free = false
				if not free:
					continue
				var shelf := _u_y(rk, u + h - 1)
				if np.y + sz.y >= shelf and pd["pos"].y + sz.y <= shelf + 14:
					pd["pos"] = Vector2(rk["pos"].x + 15, shelf - sz.y)
					pd["vel"] = 0.0
					pd["mount"] = {"rack": ri, "u": u}
					landed = true
					break
		if landed:
			continue

		# otherwise the floor, or the top of the nearest thing already down
		var rest := FLOOR_Y
		for other in phys.keys():
			if other == dev or phys[other]["mount"] != null:
				continue
			var op: Vector2 = phys[other]["pos"]
			if abs(op.x - np.x) > CARD_W - 24:
				continue
			if op.y > pd["pos"].y + 4:
				rest = min(rest, op.y)
		if np.y + sz.y >= rest:
			np.y = rest - sz.y
			pd["vel"] = 0.0
		pd["pos"] = np


func _card_rect(dev: String) -> Rect2:
	return Rect2(phys[dev]["pos"], _card_size(dev))


func _card_port(dev: String, kind: String) -> Vector2:
	var sz := _card_size(dev)
	return phys[dev]["pos"] + Vector2(sz.x - 46 + (0 if kind == "rail" else 24), sz.y * 0.5 - 6)


func _plates() -> Array:
	var out := []
	var y := 170.0
	for l in links:
		out.append({"dev": l["dev"], "kind": l["kind"], "ports": int(l["ports"]),
					"pos": Vector2(52, y), "cap": float(l["cap"]), "load": float(l["load"])})
		y += 104.0
	return out


func _socket_pos(plate: Dictionary, idx: int) -> Vector2:
	return plate["pos"] + Vector2(16 + (idx % 5) * 24, 46 + int(idx / 5) * 22)


func _room_input(e: InputEvent) -> void:
	if not _in_plant():
		return                      # nothing of yours to touch in a tenant's bay
	_ensure_phys()
	if e is InputEventMouseMotion:
		mouse_in_room = e.position
		hover_socket = ""
		for dev in phys.keys():
			if not _is_switch(str(phys[dev]["kind"])):
				continue
			for i in range(_ports_of(dev)):
				if Rect2(_switch_port(dev, i) - Vector2(5, 5), PORT + Vector2(10, 10)).has_point(e.position):
					hover_socket = "%s:%d" % [dev, i]
		if held != "":
			phys[held]["pos"] = e.position - held_off
		room.queue_redraw()
		return

	if not (e is InputEventMouseButton) or e.button_index != MOUSE_BUTTON_LEFT:
		return

	if e.pressed:
		# a cable end off a room's wall port
		for h in _hatches():
			for kind in ["rail", "spine"]:
				if Rect2(_hatch_port(h, kind) - Vector2(5, 5), PORT + Vector2(10, 10)).has_point(e.position):
					drag_cable_from = "seg:" + str(h["seg"]["name"])
					drag_cable_kind = kind
					return
		# or off a card
		for dev in phys.keys():
			if _is_switch(str(phys[dev]["kind"])):
				continue
			for kind in ["rail", "spine"]:
				if not _needs(dev, kind):
					continue
				if Rect2(_card_port(dev, kind) - Vector2(4, 4), PORT + Vector2(8, 8)).has_point(e.position):
					drag_cable_from = dev
					drag_cable_kind = kind
					return
		# topmost card under the cursor gets picked up, and unmounts
		var pick := ""
		for dev in phys.keys():
			if _card_rect(dev).has_point(e.position):
				pick = dev
		if pick != "":
			held = pick
			held_off = e.position - phys[pick]["pos"]
			phys[pick]["mount"] = null
			phys[pick]["vel"] = 0.0
	else:
		if drag_cable_from != "":
			if hover_socket != "":
				var sw: String = hover_socket.split(":")[0]
				var who: String = drag_cable_from.substr(4) if drag_cable_from.begins_with("seg:") else drag_cable_from
				_exec("wire %s %s" % [who, sw])
			else:
				# dropped on nothing: pull the cable out of whatever it was in
				var seg_end: bool = drag_cable_from.begins_with("seg:")
				var who2: String = drag_cable_from.substr(4) if seg_end else drag_cable_from
				var cur := ""
				if seg_end:
					for h2 in _hatches():
						if str(h2["seg"]["name"]) == who2:
							cur = str(h2["seg"]["rail" if drag_cable_kind == "rail" else "spine"])
				else:
					cur = _link_of(who2, drag_cable_kind)
				if cur != "-" and cur != "":
					_exec("unwire %s %s" % [who2, cur])
			drag_cable_from = ""
		if held != "":
			var p2: Vector2 = phys[held]["pos"]
			_exec("place %s %.0f %.0f" % [held, p2.x, p2.y])
		held = ""


func _slot_of(dev: String) -> Dictionary:
	for sc in slots:
		if str(sc["dev"]) == dev:
			return sc
	return {}


func _needs(dev: String, kind: String) -> bool:
	var sc := _slot_of(dev)
	if sc.is_empty():
		return false
	var bit := 1 if kind == "rail" else 2
	return (int(sc.get("needs", 0)) & bit) != 0


func _link_of(dev: String, kind: String) -> String:
	var sc := _slot_of(dev)
	return str(sc.get(kind, "-")) if not sc.is_empty() else "-"


# What the measuring tool reads: the slack run, not the straight line, because
# that is what you would actually have to buy.
func _run_metres(dev: String, kind: String, socket: String) -> float:
	var a := _cable_origin(dev, kind)
	var b := mouse_in_room
	if socket != "":
		var f := socket.split(":")
		b = _switch_port(f[0], int(f[1])) + PORT * 0.5
	return a.distance_to(b) * 1.18 / PX_PER_M     # 18% for slack and dressing


func _cable_origin(dev: String, kind: String) -> Vector2:
	if dev.begins_with("seg:"):
		for h in _hatches():
			if str(h["seg"]["name"]) == dev.substr(4):
				return _hatch_port(h, kind) + PORT * 0.5
		return mouse_in_room
	if not phys.has(dev):
		return mouse_in_room
	return _card_port(dev, kind) + PORT * 0.5


func _draw_cable(c: Control, a: Vector2, b: Vector2, col: Color, thick := 2.0) -> void:
	var sag: float = min(70.0, a.distance_to(b) * 0.3)
	var pts := PackedVector2Array()
	for i in range(15):
		var t := float(i) / 14.0
		var p := a.lerp(b, t)
		p.y += sin(t * PI) * sag
		pts.append(p)
	for i in range(pts.size() - 1):
		c.draw_line(pts[i], pts[i + 1], col, thick)


func _in_plant() -> bool:
	return str(tel.get("here", "core")) == "plant"


func _draw_room(c: Control) -> void:
	var r := c.size
	_ensure_phys()
	var here: String = str(tel.get("here", "core"))
	_draw_interior(c, r, here)

	if not _in_plant():
		_draw_segment_bay(c, r, here)
		_draw_hud(c, r, here)
		return

	# ---- racks: dark 19" cabinets against the pale hull
	for i in range(racks.size()):
		var rk: Dictionary = racks[i]
		var h: float = int(rk["units"]) * U_H
		var body := Rect2(rk["pos"], Vector2(RACK_W, h))
		c.draw_rect(Rect2(body.position + Vector2(6, 7), body.size), Color(0, 0, 0, 0.28))
		_ink(c, body, Color("#5d666d"), 3.0)
		# the empty U space inside reads darker than the frame
		c.draw_rect(Rect2(body.position + Vector2(16, 4), Vector2(RACK_W - 32, h - 8)), Color("#39424a"))
		# mounting rails with their U perforations
		for side in [10.0, RACK_W - 16.0]:
			_ink(c, Rect2(rk["pos"] + Vector2(side, 3), Vector2(6, h - 6)), Color("#8e99a1"), 1.0)
		for u in range(int(rk["units"])):
			var y := _u_y(rk, u)
			for side2 in [11.0, RACK_W - 15.0]:
				c.draw_rect(Rect2(rk["pos"] + Vector2(side2, y - rk["pos"].y - 4), Vector2(4, 3)), Color("#1b2026"))
		c.draw_string(mono, Vector2(rk["pos"].x + 2, rk["pos"].y - 9), "RACK %d   %dU" % [i, int(rk["units"])],
				HORIZONTAL_ALIGNMENT_LEFT, -1, 12, Color("#1e3a33"))
		# bolted to the deck
		for foot in [16.0, RACK_W - 16.0]:
			c.draw_line(rk["pos"] + Vector2(foot, h), Vector2(rk["pos"].x + foot, FLOOR_Y), INKLINE, 5.0)

	# ---- room hatches: each segment's door, with its own ports beside it
	for h in _hatches():
		var sg: Dictionary = h["seg"]
		var door := Rect2(h["pos"], Vector2(132, 176))
		c.draw_rect(Rect2(door.position + Vector2(5, 6), door.size), Color(0, 0, 0, 0.22))
		_ink(c, door, Color("#b9c2c6"), 3.0)
		_ink(c, Rect2(door.position + Vector2(14, 118), Vector2(102, 8)), Color("#cfd6d9"), 2.0)
		c.draw_string(mono, door.position + Vector2(16, 52), str(sg["name"]).to_upper(),
				HORIZONTAL_ALIGNMENT_LEFT, -1, 26, Color("#1d2a33"))
		c.draw_string(mono, door.position + Vector2(16, 74), str(sg["kind"]),
				HORIZONTAL_ALIGNMENT_LEFT, -1, 12, Color("#4a5a63"))
		# the floating tenant label, as the reference does it
		var svc: float = float(sg["service"])
		var lab := Rect2(h["pos"] + Vector2(-4, -50), Vector2(158, 40))
		_ink(c, lab, Color("#161d22"), 2.0)
		c.draw_string(mono, lab.position + Vector2(8, 16), str(sg["name"]),
				HORIZONTAL_ALIGNMENT_LEFT, -1, 12, TEXT)
		c.draw_string(mono, lab.position + Vector2(8, 31), "service: %d%%" % int(svc),
				HORIZONTAL_ALIGNMENT_LEFT, -1, 11,
				PHOSPHOR if svc >= 95 else (AMBER if svc > 40 else RED))
		# its two wall ports
		for kind in ["rail", "spine"]:
			var hp := _hatch_port(h, kind)
			var lit: bool = str(sg["rail" if kind == "rail" else "spine"]) != "-"
			var wc: Color = PHOSPHOR if kind == "rail" else CYAN
			_ink(c, Rect2(hp - Vector2(3, 3), PORT + Vector2(6, 6)), Color("#cfd6d9"), 2.0)
			c.draw_rect(Rect2(hp, PORT), Color("#22262a"))
			c.draw_rect(Rect2(hp, PORT), wc if lit else Color("#8a5a5a"), false, 2.0)

	# ---- switches: movable, with their own port banks
	var socket_at := {}
	for dev in phys.keys():
		if not _is_switch(str(phys[dev]["kind"])):
			continue
		var sw := Rect2(phys[dev]["pos"], _card_size(dev))
		var overs := false
		var capv := 0.0
		var loadv := 0.0
		for l in links:
			if str(l["dev"]) == dev:
				capv = float(l["cap"]); loadv = float(l["load"]); overs = loadv > capv
		c.draw_rect(Rect2(sw.position + Vector2(5, 6), sw.size), Color(0, 0, 0, 0.25))
		_ink(c, sw, Color("#aeb8bd") if dev != held else Color("#d6e6dd"), 3.0)
		if overs:
			c.draw_rect(sw, RED, false, 3.0)
		c.draw_string(mono, sw.position + Vector2(12, 22), dev.to_upper(),
				HORIZONTAL_ALIGNMENT_LEFT, -1, 15, Color("#1d2a33"))
		c.draw_string(mono, sw.position + Vector2(112, 22), "%.1f/%.1f" % [loadv, capv],
				HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color("#8a2020") if overs else Color("#3d4a52"))
		for i in range(_ports_of(dev)):
			var sp := _switch_port(dev, i)
			var hot: bool = hover_socket == "%s:%d" % [dev, i]
			c.draw_rect(Rect2(sp, PORT), Color("#22262a"))
			c.draw_rect(Rect2(sp, PORT), PHOSPHOR if hot else Color("#4a5158"), false, 2.0 if hot else 1.0)
			socket_at["%s:%d" % [dev, i]] = sp + PORT * 0.5

	for p in []:
		var over: bool = float(p["load"]) > float(p["cap"])
		var plate := Rect2(p["pos"], Vector2(150, 86))
		c.draw_rect(Rect2(plate.position + Vector2(3, 4), plate.size), Color(0, 0, 0, 0.2))
		c.draw_rect(plate, HULL_LT)
		c.draw_rect(plate, RED if over else SEAM, false, 2.0)
		for sx in [6.0, 144.0]:
			c.draw_circle(plate.position + Vector2(sx, 6), 2.0, SEAM)
			c.draw_circle(plate.position + Vector2(sx, 80), 2.0, SEAM)
		var pc: Color = Color("#0f5f7a") if str(p["kind"]) == "data" else Color("#1a5c3a")
		c.draw_string(mono, Vector2(p["pos"].x + 12, p["pos"].y + 20), str(p["dev"]).to_upper(),
				HORIZONTAL_ALIGNMENT_LEFT, -1, 13, pc)
		c.draw_string(mono, Vector2(p["pos"].x + 12, p["pos"].y + 34),
				"%.1f/%.1f%s" % [float(p["load"]), float(p["cap"]), "  SAT" if over else ""],
				HORIZONTAL_ALIGNMENT_LEFT, -1, 10, Color("#8a2020") if over else INK)
		for i in range(int(p["ports"])):
			var sp := _socket_pos(p, i)
			var hot: bool = hover_socket == "%s:%d" % [p["dev"], i]
			c.draw_rect(Rect2(sp, PORT), Color("#22262a"))
			c.draw_rect(Rect2(sp, PORT), PHOSPHOR if hot else Color("#4a5158"), false, 2.0 if hot else 1.0)
			socket_at["%s:%d" % [p["dev"], i]] = sp + PORT * 0.5

	# ---- cables: rooms and cards both run to switch ports
	var used := {}
	for h in _hatches():
		var sg2: Dictionary = h["seg"]
		for kind in ["rail", "spine"]:
			var tgt := str(sg2["rail" if kind == "rail" else "spine"])
			if tgt == "-":
				continue
			var ix := 0
			while used.has("%s:%d" % [tgt, ix]):
				ix += 1
			used["%s:%d" % [tgt, ix]] = true
			if socket_at.has("%s:%d" % [tgt, ix]):
				_draw_cable(c, _hatch_port(h, kind) + PORT * 0.5, socket_at["%s:%d" % [tgt, ix]],
						Color("#2f7d55") if kind == "rail" else Color("#2a6f8f"), 3.0)
	for dev in phys.keys():
		if _is_switch(str(phys[dev]["kind"])):
			continue
		for kind in ["rail", "spine"]:
			if not _needs(dev, kind):
				continue
			var target := _link_of(dev, kind)
			if target == "-":
				continue
			var idx := 0
			while used.has("%s:%d" % [target, idx]):
				idx += 1
			used["%s:%d" % [target, idx]] = true
			var key := "%s:%d" % [target, idx]
			if socket_at.has(key):
				var wire: Color = Color("#3fbf76") if kind == "rail" else Color("#2f9fd0")
				_draw_cable(c, _card_port(dev, kind), socket_at[key], wire)

	# ---- the equipment
	for dev in phys.keys():
		if _is_switch(str(phys[dev]["kind"])):
			continue
		var sc := _slot_of(dev)
		if sc.is_empty():
			continue
		var box := _card_rect(dev)
		var st := int(sc["state"])
		c.draw_rect(Rect2(box.position + Vector2(4, 5), box.size), Color(0, 0, 0, 0.25))
		c.draw_rect(box, Color("#343b42"))
		var edge := Color("#7d8892")
		if st == 2: edge = AMBER
		if st == 3: edge = RED
		if dev == held: edge = PHOSPHOR
		c.draw_rect(box, edge, false, 2.0 if (dev == held or st > 1) else 1.0)
		# faceplate: vents, ears, a status LED
		for v in range(int(box.size.y / 6.0)):
			c.draw_line(box.position + Vector2(box.size.x - 70, 6 + v * 6),
					box.position + Vector2(box.size.x - 56, 6 + v * 6), Color("#262c32"), 1.0)
		c.draw_rect(Rect2(box.position + Vector2(-7, 3), Vector2(7, box.size.y - 6)), Color("#565f68"))
		c.draw_rect(Rect2(box.position + Vector2(box.size.x, 3), Vector2(7, box.size.y - 6)), Color("#565f68"))
		var led := PHOSPHOR if st == 1 else (AMBER if st == 2 else RED)
		c.draw_circle(box.position + Vector2(box.size.x - 14, box.size.y * 0.5), 3.5, led)
		c.draw_string(mono, box.position + Vector2(12, 16), dev, HORIZONTAL_ALIGNMENT_LEFT, -1, 13, TEXT)
		if box.size.y > 30:
			c.draw_string(mono, box.position + Vector2(12, 31), str(sc["part"]),
					HORIZONTAL_ALIGNMENT_LEFT, -1, 10, Color("#9aa6ae"))
		if phys[dev]["mount"] == null and dev != held:
			c.draw_string(mono, box.position + Vector2(12, box.size.y - 5), "UNRACKED",
					HORIZONTAL_ALIGNMENT_LEFT, -1, 9, AMBER)
		for kind in ["rail", "spine"]:
			if not _needs(dev, kind):
				continue
			var pp := _card_port(dev, kind)
			var wire2: Color = PHOSPHOR if kind == "rail" else CYAN
			var lit: bool = _link_of(dev, kind) != "-"
			c.draw_rect(Rect2(pp, PORT), Color("#191d21"))
			c.draw_rect(Rect2(pp, PORT), wire2 if lit else Color("#8a5a5a"), false, 1.0)

	# ---- the loose end in your hand, with the tool reading out
	if drag_cable_from != "":
		var wire3: Color = PHOSPHOR if drag_cable_kind == "rail" else CYAN
		_draw_cable(c, _cable_origin(drag_cable_from, drag_cable_kind), mouse_in_room,
				Color(wire3.r, wire3.g, wire3.b, 0.95), 3.0)
		c.draw_circle(mouse_in_room, 5, wire3)
		var m := _run_metres(drag_cable_from, drag_cable_kind, hover_socket)
		var rate := float(tel.get("cable_rate", 14.0))
		var cost := m * rate
		var afford: bool = cost <= float(tel.get("credits", 0.0))
		var tag := Rect2(mouse_in_room + Vector2(12, -36), Vector2(154, 36))
		c.draw_rect(tag, Color("#12181d"))
		c.draw_rect(tag, wire3 if afford else RED, false, 1.0)
		c.draw_string(mono, tag.position + Vector2(8, 15), "%.1f m" % m,
				HORIZONTAL_ALIGNMENT_LEFT, -1, 13, TEXT)
		c.draw_string(mono, tag.position + Vector2(8, 29), "%.0f cr" % cost,
				HORIZONTAL_ALIGNMENT_LEFT, -1, 11, wire3 if afford else RED)

	_draw_hud(c, r, here)


# The room itself. Everything gets an ink outline, because that is the single
# biggest thing that makes the reference read as drawn rather than rendered.
func _ink(c: Control, rect: Rect2, fill: Color, w := 3.0) -> void:
	c.draw_rect(rect, fill)
	c.draw_rect(rect, INKLINE, false, w)


# A heavy cable loom sagging between two points, the way the reference has them
# draping across the ceiling.
func _loom(c: Control, a: Vector2, b: Vector2, strands: int, sag: float) -> void:
	for k in range(strands):
		var off := float(k - strands / 2) * 4.0
		var pts := PackedVector2Array()
		for i in range(17):
			var t := float(i) / 16.0
			var p := a.lerp(b, t)
			p.y += sin(t * PI) * (sag + off * 1.6) + off * 0.4
			pts.append(p)
		for i in range(pts.size() - 1):
			c.draw_line(pts[i], pts[i + 1], BUNDLE, 5.0)


func _draw_interior(c: Control, r: Vector2, here: String) -> void:
	# ---- the big wall
	c.draw_rect(Rect2(Vector2.ZERO, r), HULL)
	# panel blocks, slightly varied, with seams
	for j in range(4):
		for i in range(8):
			var pr := Rect2(Vector2(i * 170.0 - 20, 54.0 + j * 160.0), Vector2(170, 160))
			if (i + j) % 3 == 0:
				c.draw_rect(pr, HULL_LT)
			elif (i + j) % 3 == 1:
				c.draw_rect(pr, HULL_DK)
			c.draw_rect(pr, Color(SEAM.r, SEAM.g, SEAM.b, 0.55), false, 2.0)

	# ---- ceiling: rail, tubes, and the loom
	c.draw_rect(Rect2(Vector2(0, 28), Vector2(r.x, 26)), HULL_DK)
	c.draw_line(Vector2(0, 54), Vector2(r.x, 54), INKLINE, 3.0)
	for i in range(5):
		var lx := 90.0 + i * 250.0
		# the glow it throws first
		c.draw_colored_polygon(PackedVector2Array([
			Vector2(lx - 10, 56), Vector2(lx + 180, 56),
			Vector2(lx + 260, FLOOR_Y), Vector2(lx - 90, FLOOR_Y)]),
			Color(1.0, 0.97, 0.84, 0.10))
		_ink(c, Rect2(Vector2(lx, 34), Vector2(170, 18)), LAMP_TUBE, 3.0)
		c.draw_rect(Rect2(Vector2(lx + 6, 38), Vector2(158, 10)), LAMP)
		# the hangers
		c.draw_line(Vector2(lx + 16, 28), Vector2(lx + 16, 34), INKLINE, 3.0)
		c.draw_line(Vector2(lx + 154, 28), Vector2(lx + 154, 34), INKLINE, 3.0)
	# fat cable looms draping across the ceiling
	_loom(c, Vector2(-20, 56), Vector2(360, 56), 5, 40)
	_loom(c, Vector2(330, 56), Vector2(800, 56), 4, 54)
	_loom(c, Vector2(770, 56), Vector2(r.x + 20, 56), 5, 34)

	# ---- viewport onto space
	var vc := Vector2(r.x - 235, 300)
	var hex := PackedVector2Array()
	for i in range(6):
		var ang := PI / 6.0 + float(i) * PI / 3.0
		hex.append(vc + Vector2(cos(ang) * 150, sin(ang) * 105))
	c.draw_colored_polygon(hex, Color("#0a1020"))
	for i in range(6):
		c.draw_line(hex[i], hex[(i + 1) % 6], INKLINE, 5.0)
	var sd := 7
	for i in range(70):
		sd = (sd * 1103515245 + 12345) & 0x7fffffff
		var px := vc.x - 130 + float(sd % 260)
		sd = (sd * 1103515245 + 12345) & 0x7fffffff
		var py := vc.y - 88 + float(sd % 176)
		if Vector2(px, py).distance_to(vc) < 100:
			c.draw_circle(Vector2(px, py), 0.7 + float(sd % 8) * 0.1, Color(1, 1, 1, 0.55))
	c.draw_circle(vc + Vector2(-30, 70), 84, Color("#2f5a7d"))
	c.draw_circle(vc + Vector2(-38, 62), 80, Color("#4179a1"))

	# ---- deck: warm plate, ink line, hazard chevrons at the lip
	c.draw_rect(Rect2(Vector2(0, FLOOR_Y), Vector2(r.x, r.y - FLOOR_Y)), DECK)
	c.draw_line(Vector2(0, FLOOR_Y), Vector2(r.x, FLOOR_Y), INKLINE, 4.0)
	for i in range(int(r.x / 90.0) + 1):
		c.draw_line(Vector2(i * 90.0, FLOOR_Y), Vector2(i * 90.0 - 26, r.y), DECK_DK, 2.0)
	var hz := FLOOR_Y + 54.0
	if hz + 16 < r.y:
		_ink(c, Rect2(Vector2(-4, hz), Vector2(r.x + 8, 16)), Color("#2a2d28"), 3.0)
		var k := 0
		while k * 30 < int(r.x):
			c.draw_colored_polygon(PackedVector2Array([
				Vector2(k * 30, hz + 16), Vector2(k * 30 + 15, hz),
				Vector2(k * 30 + 30, hz), Vector2(k * 30 + 15, hz + 16)]), HAZARD)
			k += 1

	# ---- lived in: a drinks machine, a bin, bottles
	var vm := Rect2(Vector2(r.x - 150, FLOOR_Y - 210), Vector2(120, 210))
	_ink(c, vm, Color("#9aa5ac"), 3.0)
	_ink(c, Rect2(vm.position + Vector2(12, 16), Vector2(96, 120)), Color("#25313a"), 2.0)
	for sh in range(3):
		for b in range(5):
			c.draw_rect(Rect2(vm.position + Vector2(20 + b * 18, 26 + sh * 38), Vector2(9, 26)),
					Color("#7fb8a0"))
	_ink(c, Rect2(vm.position + Vector2(20, 156), Vector2(80, 26)), Color("#1b2228"), 2.0)
	for i in range(3):
		var bx := 120.0 + i * 22.0
		_ink(c, Rect2(Vector2(bx, FLOOR_Y - 26), Vector2(9, 26)), Color("#4c8f5e"), 2.0)
	c.draw_colored_polygon(PackedVector2Array([
		Vector2(300, FLOOR_Y), Vector2(316, FLOOR_Y - 30), Vector2(348, FLOOR_Y - 26),
		Vector2(360, FLOOR_Y)]), Color("#20262b"))

	# ---- stencilled signage
	var sign := "PLANT // PRIMARY DISTRIBUTION" if here == "plant" else here.to_upper() + " // TENANT BAY"
	c.draw_string(mono, Vector2(40, FLOOR_Y - 16), sign, HORIZONTAL_ALIGNMENT_LEFT, -1, 14, SEAM)
	c.draw_string(mono, Vector2(r.x - 420, FLOOR_Y - 16), "DECK 2   RING A",
			HORIZONTAL_ALIGNMENT_LEFT, -1, 13, SEAM)


func _draw_hud(c: Control, r: Vector2, here: String) -> void:
	# The HUD is an overlay, so it gets its own plate rather than fighting the
	# cable looms for legibility.
	var title := "EQUIPMENT BAY" if here == "plant" else "SEGMENT " + here.to_upper()
	var hud := Rect2(Vector2(16, 34), Vector2(560, 74))
	c.draw_rect(hud, Color(0.06, 0.09, 0.11, 0.82))
	c.draw_rect(hud, Color(1, 1, 1, 0.10), false, 1.0)
	c.draw_string(mono, Vector2(30, 62), title, HORIZONTAL_ALIGNMENT_LEFT, -1, 22, TEXT)
	if _in_plant():
		c.draw_string(mono, Vector2(30, 82),
				"drag kit to move it — it falls. drop it over a rack to mount.",
				HORIZONTAL_ALIGNMENT_LEFT, -1, 12, DIM)
		c.draw_string(mono, Vector2(30, 99),
				"drag a cable from a room port or a card into a switch port.  `goto core` for the mainframe.",
				HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color("#7f9089"))
	else:
		c.draw_string(mono, Vector2(30, 82),
				"a tenant's bay. what arrives here comes down the drops from the plant.",
				HORIZONTAL_ALIGNMENT_LEFT, -1, 12, DIM)
		c.draw_string(mono, Vector2(30, 99),
				"`goto plant` for the racks, `goto core` for the mainframe.",
				HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color("#7f9089"))

	# the cable tool, clipped to your belt
	var tool := Rect2(Vector2(r.x - 250, 78), Vector2(200, 52))
	c.draw_rect(tool, Color("#2b333b"))
	c.draw_rect(tool, SEAM, false, 1.0)
	c.draw_string(mono, tool.position + Vector2(10, 18), "CABLE TOOL",
			HORIZONTAL_ALIGNMENT_LEFT, -1, 10, DIM)
	c.draw_string(mono, tool.position + Vector2(10, 36), "%.0f cr/m   spent %.0f" %
			[float(tel.get("cable_rate", 14.0)), float(tel.get("cable_spent", 0.0))],
			HORIZONTAL_ALIGNMENT_LEFT, -1, 12, TEXT)

	for sg in segs:
		if str(sg["name"]) != here:
			continue
		var svc: float = float(sg["service"])
		c.draw_string(mono, Vector2(r.x - 250, 156), "%s (%s)" % [sg["name"], sg["kind"]],
				HORIZONTAL_ALIGNMENT_LEFT, -1, 15, INK)
		c.draw_string(mono, Vector2(r.x - 250, 180), "service %d%%" % int(svc),
				HORIZONTAL_ALIGNMENT_LEFT, -1, 13,
				Color("#1a6b3a") if svc >= 95 else Color("#9a2020"))


# A tenant's own equipment bay. Empty for now — the station's kit is all in the
# plant — but this is where that segment's own hardware would rack, and it is
# why the trip costs you something.
func _draw_segment_bay(c: Control, r: Vector2, here: String) -> void:
	var rk := Rect2(Vector2(380, 210), Vector2(RACK_W, 10 * U_H))
	c.draw_rect(rk, Color("#0f1216"))
	c.draw_rect(rk, Color("#3a4a52"), false, 1.0)
	for u in range(10):
		var y: float = rk.position.y + (u + 1) * U_H
		c.draw_line(Vector2(rk.position.x + 4, y), Vector2(rk.position.x + 12, y), Color("#26333a"), 1.0)
		c.draw_line(Vector2(rk.position.x + RACK_W - 12, y), Vector2(rk.position.x + RACK_W - 4, y),
				Color("#26333a"), 1.0)
	c.draw_string(mono, rk.position + Vector2(0, -6), "LOCAL RACK   10U   empty",
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, DIM)
	c.draw_line(Vector2(rk.position.x + 10, rk.end.y), Vector2(rk.position.x + 10, FLOOR_Y), Color("#3a4a52"), 3.0)
	c.draw_line(Vector2(rk.end.x - 10, rk.end.y), Vector2(rk.end.x - 10, FLOOR_Y), Color("#3a4a52"), 3.0)

	# the drops that come in from the station's rails and spines
	var y2 := 210.0
	for sg in segs:
		if str(sg["name"]) != here:
			continue
		for pair in [["power", str(sg["rail"]), float(sg["power"]), PHOSPHOR],
					 ["data",  str(sg["spine"]), float(sg["data"]), CYAN]]:
			var plate := Rect2(Vector2(70, y2), Vector2(180, 62))
			var live: bool = str(pair[1]) != "-"
			c.draw_rect(plate, Color("#171210"))
			c.draw_rect(plate, Color("#4a3a2a") if live else RED, false, 1.0)
			c.draw_string(mono, plate.position + Vector2(12, 20), "%s drop" % pair[0],
					HORIZONTAL_ALIGNMENT_LEFT, -1, 12, pair[3] if live else RED)
			c.draw_string(mono, plate.position + Vector2(12, 36),
					("from %s" % pair[1]) if live else "NOT CONNECTED",
					HORIZONTAL_ALIGNMENT_LEFT, -1, 11, DIM if live else RED)
			c.draw_string(mono, plate.position + Vector2(12, 52), "wants %.1f" % float(pair[2]),
					HORIZONTAL_ALIGNMENT_LEFT, -1, 10, DIM)
			if live:
				_draw_cable(c, plate.position + Vector2(180, 30), Vector2(rk.position.x, y2 + 40),
						Color(pair[3].r, pair[3].g, pair[3].b, 0.7))
			y2 += 78.0

	c.draw_string(mono, Vector2(380, FLOOR_Y - 40),
			"this segment has no equipment of its own yet — the station's kit is in the plant",
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color("#4a5f58"))


func _unhandled_key_input(e: InputEvent) -> void:
	if e is InputEventKey and e.pressed and e.keycode == KEY_SPACE:
		if term_in == null or not term_in.has_focus():
			_do_pause()
