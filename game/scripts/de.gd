# de.gd — the desktop a technician actually works at.
#
# The previous front end was three fixed panels and two LineEdits, and David's
# verdict on it was correct: "the terminal especially is a piece of junk... a
# stagnant input line... it doesn't look like a desktop environment at all."
#
# So this is a desktop: a wallpaper, a panel across the top with launchers and
# a clock, and windows you can move, focus and close. The applications are the
# ones the job needs and nothing else.
#
#   Terminal   a REAL terminal. Keys go into the terminal surface, there is
#              one cursor, the scrollback is the transcript, and there is no
#              input box anywhere.
#   Chat       three contacts, not one: the customer, a colleague who knows
#              only what you tell him, and the engineer who wrote the runbook.
#   Console    what the machine said while it was booting.
#   Files      the mounted disk, browsable.
#   Manual     the boot process and every tool, written out.
#
# Nothing here diagnoses anything. Every command goes through the machine's
# own /bin/sh, so the desktop cannot know something the console does not.

extends Control

var machine: Object = null
var mono: Font

# --- palette: dark, close to the Hamnix reference ---
const DESK_TOP   := Color("#20293a")
const DESK_BOT   := Color("#0d1219")
const PANEL_BG   := Color("#161b24")
const PANEL_EDGE := Color("#2b3444")
const WIN_BG     := Color("#1b212c")
const WIN_EDGE   := Color("#39445a")
const TITLE_ON   := Color("#2f6fb5")
const TITLE_OFF  := Color("#2a3140")
const INK        := Color("#c9d3e0")
const DIM        := Color("#78849a")
const TERM_BG    := Color("#0b0e13")
const TERM_FG    := Color("#cfd8e3")
const GREEN      := Color("#6fdc96")
const RED        := Color("#e06c75")
const YELLOW     := Color("#d3b06a")

var desk: Control
var panel: Control
var clock_lbl: Label
var status_lbl: Label
var windows: Array = []
var focused: Control = null

var seed_no := 4823
var faults := 1

var term: Control          # the terminal app
var chat: Control
var console_box: RichTextLabel
var files_box: RichTextLabel
var files_path := "/"

var _shot_path := ""
var _shot_after := 0
var _frames := 0
var _clock := 0.0


func _ready() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	mono = ThemeDB.fallback_font

	if ClassDB.class_exists("NominalStation"):
		machine = ClassDB.instantiate("NominalStation")
	else:
		push_error("NominalStation is not registered - the GDExtension did not load")
		return

	_build_desktop()
	_new_ticket()
	_parse_args()


# ------------------------------------------------------------- the desktop --

func _build_desktop() -> void:
	desk = Control.new()
	desk.set_anchors_preset(Control.PRESET_FULL_RECT)
	desk.mouse_filter = Control.MOUSE_FILTER_IGNORE
	desk.draw.connect(func():
		var h := desk.size.y
		for i in range(64):
			var t := float(i) / 63.0
			desk.draw_rect(Rect2(0, h * t, desk.size.x, h / 63.0 + 1),
				DESK_TOP.lerp(DESK_BOT, t)))
	add_child(desk)

	_build_panel()

	# The windows, laid out so they do not sit on top of each other.
	var w := size.x if size.x > 100 else 1600.0
	var h := size.y if size.y > 100 else 1000.0
	var col := (w - 36.0) / 2.0

	console_box = RichTextLabel.new()
	_term_style(console_box, 13)
	_window("console — the customer's machine", Rect2(12, 40, col, h * 0.44), console_box)

	files_box = RichTextLabel.new()
	files_box.bbcode_enabled = true
	files_box.add_theme_font_override("normal_font", mono)
	files_box.add_theme_font_size_override("normal_font_size", 12)
	files_box.meta_clicked.connect(_on_file_meta)
	_window("files — /mnt", Rect2(24 + col, 40, col, h * 0.44), files_box)

	term = _make_terminal()
	_window("terminal", Rect2(12, 52 + h * 0.44, col, h * 0.52 - 64), term)

	chat = _make_chat()
	_window("chat", Rect2(24 + col, 52 + h * 0.44, col, h * 0.52 - 64), chat)

	_focus(term)
	_rebuild_tasks()


func _build_panel() -> void:
	panel = Control.new()
	panel.set_anchors_preset(Control.PRESET_TOP_WIDE)
	panel.custom_minimum_size = Vector2(0, 30)
	panel.size.y = 30
	panel.draw.connect(func():
		panel.draw_rect(Rect2(0, 0, panel.size.x, 30), PANEL_BG)
		panel.draw_rect(Rect2(0, 29, panel.size.x, 1), PANEL_EDGE))
	add_child(panel)

	var x := 8.0
	for spec in [["new ticket", _new_ticket], ["boot", _boot],
				 ["rescue", _boot_rescue], ["manual", _open_manual]]:
		var b := Button.new()
		b.text = spec[0]
		b.position = Vector2(x, 3)
		b.custom_minimum_size = Vector2(0, 24)
		b.add_theme_font_size_override("font_size", 12)
		b.pressed.connect(spec[1])
		panel.add_child(b)
		x += b.get_minimum_size().x + 18.0

	status_lbl = Label.new()
	status_lbl.position = Vector2(x + 12, 6)
	status_lbl.z_index = 1
	status_lbl.add_theme_font_size_override("font_size", 12)
	status_lbl.add_theme_color_override("font_color", DIM)
	panel.add_child(status_lbl)

	task_x = x + 12
	task_holder = Control.new()
	task_holder.set_anchors_preset(Control.PRESET_TOP_WIDE)
	task_holder.mouse_filter = Control.MOUSE_FILTER_IGNORE
	panel.add_child(task_holder)

	clock_lbl = Label.new()
	clock_lbl.add_theme_font_size_override("font_size", 12)
	clock_lbl.add_theme_color_override("font_color", INK)
	clock_lbl.set_anchors_preset(Control.PRESET_TOP_RIGHT)
	clock_lbl.position = Vector2(-90, 6)
	panel.add_child(clock_lbl)


# A window: a title bar you can drag, a close box, a resize grip in the
# corner, and focus that means something. Without close and resize it is not a
# window, it is a rectangle -- and the first thing anyone does with a desktop
# is move something out of the way and make the terminal bigger.
func _window(title: String, rect: Rect2, content: Control) -> Control:
	var win := Control.new()
	win.position = rect.position
	win.size = rect.size
	win.set_meta("title", title)
	win.draw.connect(func():
		var on: bool = focused == content
		win.draw_rect(Rect2(0, 0, win.size.x, win.size.y), WIN_BG)
		win.draw_rect(Rect2(0, 0, win.size.x, win.size.y), WIN_EDGE, false, 1.0)
		win.draw_rect(Rect2(1, 1, win.size.x - 2, 20), TITLE_ON if on else TITLE_OFF)
		win.draw_string(mono, Vector2(8, 15), str(win.get_meta("title")),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 12, INK if on else DIM)
		# close box
		var cx := win.size.x - 16
		win.draw_string(mono, Vector2(cx, 15), "x",
			HORIZONTAL_ALIGNMENT_LEFT, -1, 13, INK if on else DIM)
		# resize grip
		for i in range(3):
			var d := 4.0 + i * 4.0
			win.draw_line(Vector2(win.size.x - d, win.size.y - 2),
				Vector2(win.size.x - 2, win.size.y - d), WIN_EDGE, 1.0))
	add_child(win)

	var bar := Control.new()
	bar.position = Vector2(0, 0)
	bar.size = Vector2(rect.size.x, 21)
	bar.gui_input.connect(func(e): _drag(win, e))
	win.add_child(bar)
	win.set_meta("bar", bar)

	var grip := Control.new()
	grip.size = Vector2(16, 16)
	grip.position = Vector2(rect.size.x - 16, rect.size.y - 16)
	grip.gui_input.connect(func(e): _resize(win, e))
	win.add_child(grip)
	win.set_meta("grip", grip)

	content.position = Vector2(4, 24)
	content.size = Vector2(rect.size.x - 8, rect.size.y - 28)
	win.add_child(content)
	windows.append(win)
	win.set_meta("content", content)
	return win


# Keep the pieces in step after a move or a resize.
func _relayout(win: Control) -> void:
	var bar: Control = win.get_meta("bar")
	var grip: Control = win.get_meta("grip")
	var content: Control = win.get_meta("content")
	bar.size = Vector2(win.size.x, 21)
	grip.position = Vector2(win.size.x - 16, win.size.y - 16)
	content.size = Vector2(win.size.x - 8, win.size.y - 28)
	win.queue_redraw()


func _close(win: Control) -> void:
	win.visible = false
	if focused == win.get_meta("content"):
		for w in windows:
			if w.visible:
				_focus(w.get_meta("content"))
				break
	_rebuild_tasks()


func _show_window(win: Control) -> void:
	win.visible = true
	move_child(win, get_child_count() - 1)
	_focus(win.get_meta("content"))
	_rebuild_tasks()


var task_holder: Control
var task_x := 0.0

# A closed window has to be reachable again, or closing it is destruction
# rather than tidying. The panel carries one button per window, and the
# button says whether it is on screen.
func _rebuild_tasks() -> void:
	if task_holder == null:
		return
	for c in task_holder.get_children():
		c.queue_free()
	var x := task_x
	for w in windows:
		var b := Button.new()
		b.text = str(w.get_meta("title")).split(" ")[0]
		b.position = Vector2(x, 3)
		b.custom_minimum_size = Vector2(0, 24)
		b.add_theme_font_size_override("font_size", 11)
		if not w.visible:
			b.add_theme_color_override("font_color", DIM)
		var target := w
		b.pressed.connect(func(): _show_window(target))
		task_holder.add_child(b)
		x += b.get_minimum_size().x + 10.0


var _dragging: Control = null
var _drag_from := Vector2.ZERO

func _drag(win: Control, e: InputEvent) -> void:
	if e is InputEventMouseButton and e.button_index == MOUSE_BUTTON_LEFT:
		if e.pressed:
			if e.position.x > win.size.x - 20:
				_close(win)
				return
			_dragging = win
			_drag_from = e.position
			move_child(win, get_child_count() - 1)
			_focus(win.get_meta("content"))
		else:
			_dragging = null
	elif e is InputEventMouseMotion and _dragging == win:
		win.position += e.position - _drag_from
		win.position.y = max(31.0, win.position.y)


var _sizing: Control = null

func _resize(win: Control, e: InputEvent) -> void:
	if e is InputEventMouseButton and e.button_index == MOUSE_BUTTON_LEFT:
		if e.pressed:
			_sizing = win
			move_child(win, get_child_count() - 1)
			_focus(win.get_meta("content"))
		else:
			_sizing = null
	elif e is InputEventMouseMotion and _sizing == win:
		win.size.x = max(240.0, win.size.x + e.relative.x)
		win.size.y = max(120.0, win.size.y + e.relative.y)
		_relayout(win)


func _focus(c: Control) -> void:
	focused = c
	for w in windows:
		w.queue_redraw()
	if c and c.has_method("take_focus"):
		c.call("take_focus")


# ---------------------------------------------------------- a real terminal --
#
# No LineEdit. The control owns its own line buffer and cursor, keys arrive
# through _gui_input, and what you type appears in the same surface as the
# output -- which is the entire difference between a terminal and a text box
# with a log above it.

func _make_terminal() -> Control:
	var t := preload("res://scripts/terminal.gd").new()
	t.mono = mono
	t.bg = TERM_BG
	t.fg = TERM_FG
	t.accent = GREEN
	t.on_command = func(line: String) -> String:
		return _run(line)
	t.prompt_fn = func() -> String:
		return "rescue# " if machine.on_rescue() else "root@node# "
	return t


func _run(line: String) -> String:
	var s := line.strip_edges()
	if s == "":
		return ""
	if s == "boot":
		_boot(); return ""
	if s == "rescue":
		_boot_rescue(); return ""
	if s.begins_with("ask "):
		chat.call("post", 0, s.substr(4)); return ""
	if s.begins_with("sam "):
		chat.call("post", 1, s.substr(4)); return ""
	if s.begins_with("boss "):
		chat.call("post", 2, s.substr(5)); return ""
	var out: String = machine.sh(s)
	_refresh_files()
	return out


# ------------------------------------------------------------------- chat --

func _make_chat() -> Control:
	var c := preload("res://scripts/chat.gd").new()
	c.mono = mono
	c.machine = machine
	c.ink = INK
	c.dim = DIM
	c.bg = WIN_BG
	return c


# ------------------------------------------------------------------ files --

func _refresh_files() -> void:
	if files_box == null:
		return
	# list_dir gives "<name> <kind> <mode> <size>". Render it the way `ls -l`
	# does, because "bin dir 0755 0" is data, not a listing.
	var listing: String = machine.list_dir(files_path)
	var up: String = files_path.get_base_dir()
	var out := "[color=#78849a]%s[/color]\n" % files_path
	if files_path != "/":
		out += "[url=d:%s][color=#6fa8e8]..[/color][/url]\n" % (up if up != "" else "/")
	var dirs: Array = []
	var rest: Array = []
	for row in listing.split("\n"):
		if row.strip_edges() == "":
			continue
		var f: PackedStringArray = row.split(" ")
		if f.size() < 4:
			continue
		var nm: String = f[0]
		var kind: String = f[1]
		var mode: String = f[2]
		var sz: String = f[3]
		var tag := "d" if kind == "dir" else "-"
		if kind == "link":
			tag = "l"
		var target: String = ("" if files_path == "/" else files_path) + "/" + nm
		var colour := "#6fa8e8" if kind == "dir" else ("#d3b06a" if kind == "link" else "#c9d3e0")
		var line := "%s%s %6s  [url=%s:%s][color=%s]%s[/color][/url]" % [
			tag, mode, sz, "d" if kind == "dir" else "f", target, colour, nm]
		if kind == "dir":
			dirs.append(line)
		else:
			rest.append(line)
	dirs.sort()
	rest.sort()
	for l in dirs:
		out += l + "\n"
	for l in rest:
		out += l + "\n"
	files_box.text = out


func _on_file_meta(m) -> void:
	var s := str(m)
	if s.begins_with("d:"):
		files_path = s.substr(2)
		_refresh_files()
	else:
		term.call("write", machine.read_file(s.substr(2)))


# ----------------------------------------------------------------- manual --

func _open_manual() -> void:
	var doc := preload("res://scripts/manual.gd").new()
	doc.mono = mono
	var w := size.x if size.x > 100 else 1600.0
	var h := size.y if size.y > 100 else 1000.0
	var win := _window("manual — how this machine works",
		Rect2(w * 0.18, 70, w * 0.64, h * 0.74), doc)
	move_child(win, get_child_count() - 1)
	_focus(doc)


# ------------------------------------------------------------------ ticket --

func _new_ticket() -> void:
	seed_no += 1
	machine.take_ticket(seed_no, faults)
	files_path = "/"
	if chat:
		chat.call("reset", machine.customer_name() if machine.has_method("customer_name") else "the customer")
	_boot()


func _boot() -> void:
	console_box.text = _colourise(machine.boot())
	_refresh_files()
	_status()


func _boot_rescue() -> void:
	console_box.text = _colourise(machine.boot_rescue())
	files_path = "/"
	_refresh_files()
	_status()
	if term:
		term.call("write", "--- booted the rescue medium ---\n")


func _status() -> void:
	var st: String = machine.boot_stage()
	var up: bool = machine.booted()
	status_lbl.text = "node-%d   %s at %s" % [seed_no % 10000, "UP" if up else "DOWN", st]
	status_lbl.add_theme_color_override("font_color", GREEN if up else RED)


func _colourise(s: String) -> String:
	var out := ""
	for line in s.split("\n"):
		var esc := line.replace("[", "[lb]")
		if line.find("fail") >= 0 or line.find("cannot") >= 0 or line.find("error") >= 0 \
		   or line.find("not found") >= 0 or line.find("refusing") >= 0:
			out += "[color=#e06c75]" + esc + "[/color]\n"
		elif line.begins_with("svcinit: started"):
			out += "[color=#6fdc96]" + esc + "[/color]\n"
		else:
			out += esc + "\n"
	return out


func _term_style(r: RichTextLabel, sz: int) -> void:
	r.bbcode_enabled = true
	r.scroll_following = true
	r.add_theme_font_override("normal_font", mono)
	r.add_theme_font_size_override("normal_font_size", sz)
	var sb := StyleBoxFlat.new()
	sb.bg_color = TERM_BG
	sb.set_content_margin_all(8)
	r.add_theme_stylebox_override("normal", sb)


# ----------------------------------------------------------------- process --

func _process(dt: float) -> void:
	_clock += dt
	if clock_lbl:
		var t := int(_clock)
		clock_lbl.text = "%02d:%02d" % [(9 + t / 3600) % 24, (t / 60) % 60]

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
		elif a.begins_with("--faults="):
			faults = int(a.substr(9))
		elif a.begins_with("--type="):
			term.call("feed", a.substr(7))
		elif a.begins_with("--ask="):
			chat.call("post", 0, a.substr(6))
		elif a.begins_with("--sam="):
			chat.call("post", 1, a.substr(6))
		elif a.begins_with("--boss="):
			chat.call("post", 7 - 0)
		elif a.begins_with("--manual"):
			_open_manual()
