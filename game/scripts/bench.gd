# bench.gd — the support bench.
#
# A ticket arrives: a customer machine that will not boot. The console on the
# left is the machine's own output, produced by actually booting it. The panel
# on the right is the rescue environment: you are outside the broken system,
# looking in, which is what a live image gives you.
#
# Nothing here diagnoses anything for the player. The front end can only ask
# the machine the same questions a person at a console could.

extends Control

var machine: Object = null

var mono: Font
var console_text := ""
var seed_no := 4823
var faults := 1

# --- Hamnix light palette, from the reference screenshots ---
const WALL_TOP  := Color("#4a6ea5")
const WALL_BOT  := Color("#1b2a47")
const PANEL     := Color("#d8d8d8")
const BODY      := Color("#f1f1f1")
const EDGE      := Color("#9aa4ae")
const TITLE_HI  := Color("#4d8ddb")
const TITLE_LO  := Color("#2a5fa8")
const TERM_BG   := Color("#14181c")
const TERM_FG   := Color("#d7dee6")
const PHOSPHOR  := Color("#6fdc96")
const AMBER     := Color("#e0a33e")
const RED       := Color("#d2504a")
const TEXT      := Color("#14181c")
const DIM       := Color("#5b646d")

var console_box: RichTextLabel
var tree_box: RichTextLabel
var detail_box: RichTextLabel
var status_bar: Label
var cwd := "/"


func _ready() -> void:
	mono = ThemeDB.fallback_font
	set_anchors_preset(Control.PRESET_FULL_RECT)

	var wall := Control.new()
	wall.set_anchors_preset(Control.PRESET_FULL_RECT)
	wall.mouse_filter = Control.MOUSE_FILTER_IGNORE
	wall.draw.connect(func(): _draw_wall(wall))
	add_child(wall)

	if ClassDB.class_exists("NominalStation"):
		machine = ClassDB.instantiate("NominalStation")
	else:
		push_error("NominalStation is not registered - the GDExtension did not load")
		return

	_build_ui()
	_new_ticket()
	_parse_args()


# Headless capture, so the look can be checked without a human at the screen.
var _shot_path := ""
var _shot_after := 0

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
			_new_ticket()
		elif a.begins_with("--show="):
			_show_file(a.substr(7))
		elif a.begins_with("--cd="):
			cwd = a.substr(5)
			_refresh_tree()
		elif a == "--verify":
			_verify()
		elif a == "--healthy":
			machine.install(seed_no)
			_boot()
			_refresh_tree()


func _process(_dt: float) -> void:
	if _shot_path == "":
		return
	if _shot_after > 0:
		_shot_after -= 1
		return
	await RenderingServer.frame_post_draw
	var img := get_viewport().get_texture().get_image()
	img.save_png(_shot_path)
	print("screenshot: ", _shot_path)
	_shot_path = ""
	get_tree().quit()


func _draw_wall(c: Control) -> void:
	var h := c.size.y
	for i in range(48):
		var t := float(i) / 47.0
		c.draw_rect(Rect2(Vector2(0, h * t), Vector2(c.size.x, h / 48.0 + 1)),
				WALL_TOP.lerp(WALL_BOT, t))


func _panel(title: String, r: Rect2) -> Control:
	var win := Panel.new()
	win.position = r.position
	win.size = r.size
	win.clip_contents = true
	var sb := StyleBoxFlat.new()
	sb.bg_color = BODY
	sb.border_color = EDGE
	sb.set_border_width_all(2)
	win.add_theme_stylebox_override("panel", sb)
	add_child(win)

	var strip := Control.new()
	strip.position = Vector2(2, 2)
	strip.size = Vector2(r.size.x - 4, 22)
	strip.mouse_filter = Control.MOUSE_FILTER_IGNORE
	strip.draw.connect(func():
		for i in range(12):
			var t := float(i) / 11.0
			strip.draw_rect(Rect2(Vector2(0, strip.size.y * t),
					Vector2(strip.size.x, strip.size.y / 12.0 + 1)),
					TITLE_HI.lerp(TITLE_LO, t)))
	win.add_child(strip)

	var tl := Label.new()
	tl.text = "  " + title
	tl.add_theme_color_override("font_color", Color.WHITE)
	tl.add_theme_font_size_override("font_size", 12)
	tl.size = strip.size
	tl.mouse_filter = Control.MOUSE_FILTER_IGNORE
	strip.add_child(tl)

	var content := MarginContainer.new()
	content.position = Vector2(3, 26)
	content.size = Vector2(r.size.x - 6, r.size.y - 29)
	content.clip_contents = true
	for m in ["margin_left", "margin_right", "margin_top", "margin_bottom"]:
		content.add_theme_constant_override(m, 6)
	win.add_child(content)
	return content


func _build_ui() -> void:
	# --- top bar ---
	var top := Panel.new()
	top.position = Vector2.ZERO
	top.size = Vector2(1280, 28)
	var tsb := StyleBoxFlat.new()
	tsb.bg_color = PANEL
	top.add_theme_stylebox_override("panel", tsb)
	add_child(top)

	status_bar = Label.new()
	status_bar.position = Vector2(10, 4)
	status_bar.add_theme_color_override("font_color", TEXT)
	status_bar.add_theme_font_size_override("font_size", 13)
	top.add_child(status_bar)

	var bx := 700.0
	for spec in [["New ticket", func(): _new_ticket()],
				 ["Boot", func(): _boot()],
				 ["Verify", func(): _verify()],
				 ["Harder", func(): faults += 1; _new_ticket()]]:
		var b := Button.new()
		b.text = str(spec[0])
		b.position = Vector2(bx, 2)
		b.size = Vector2(120, 24)
		b.add_theme_font_size_override("font_size", 12)
		b.pressed.connect(spec[1])
		top.add_child(b)
		bx += 128

	# --- the machine's console ---
	var cc := _panel("console - customer machine", Rect2(14, 40, 700, 700))
	console_box = RichTextLabel.new()
	console_box.bbcode_enabled = true
	console_box.scroll_following = true
	console_box.add_theme_font_override("normal_font", mono)
	console_box.add_theme_font_size_override("normal_font_size", 13)
	var csb := StyleBoxFlat.new()
	csb.bg_color = TERM_BG
	csb.set_content_margin_all(8)
	console_box.add_theme_stylebox_override("normal", csb)
	cc.add_child(console_box)

	# --- the rescue environment ---
	var rc := _panel("rescue - the broken disk, mounted", Rect2(726, 40, 540, 380))
	var vb := VBoxContainer.new()
	rc.add_child(vb)

	var path_row := HBoxContainer.new()
	vb.add_child(path_row)
	var pl := Label.new()
	pl.text = "path"
	pl.add_theme_color_override("font_color", DIM)
	path_row.add_child(pl)
	var path_in := LineEdit.new()
	path_in.text = "/"
	path_in.custom_minimum_size = Vector2(400, 0)
	path_in.text_submitted.connect(func(t): cwd = t; _refresh_tree())
	path_row.add_child(path_in)

	tree_box = RichTextLabel.new()
	tree_box.bbcode_enabled = true
	tree_box.custom_minimum_size = Vector2(0, 300)
	tree_box.add_theme_font_override("normal_font", mono)
	tree_box.add_theme_font_size_override("normal_font_size", 12)
	tree_box.meta_clicked.connect(func(m):
		var s := str(m)
		if s.begins_with("d:"):
			cwd = s.substr(2)
			path_in.text = cwd
			_refresh_tree()
		else:
			_show_file(s.substr(2)))
	vb.add_child(tree_box)

	# --- what the tools say ---
	var dc := _panel("output", Rect2(726, 430, 540, 310))
	detail_box = RichTextLabel.new()
	detail_box.bbcode_enabled = true
	detail_box.add_theme_font_override("normal_font", mono)
	detail_box.add_theme_font_size_override("normal_font_size", 12)
	dc.add_child(detail_box)


func _esc(s: String) -> String:
	return s.replace("[", "[lb]")


func _new_ticket() -> void:
	seed_no += 1
	machine.take_ticket(seed_no, faults)
	cwd = "/"
	_boot()
	_refresh_tree()
	detail_box.text = "[color=#5b646d]A machine arrived and will not boot.\n" \
		+ "Read the console. Walk the disk. Find what is wrong.\n\n" \
		+ "verify shows what differs from what each package shipped.[/color]"


func _boot() -> void:
	var out: String = machine.boot()
	var up: bool = machine.booted()
	var body := ""
	for line in out.split("\n"):
		var col := "#d7dee6"
		if line.find("login:") >= 0:
			col = "#6fdc96"
		elif line.find("not found") >= 0 or line.find("denied") >= 0 \
				or line.find("illegal") >= 0 or line.find("panic") >= 0 \
				or line.find("no such") >= 0 or line.find("cannot") >= 0 \
				or line.find("waiting for") >= 0 or line.find("not an ELF") >= 0:
			col = "#ff8a80"
		elif line.begins_with("svcinit: started"):
			col = "#a5d6a7"
		body += "[color=%s]%s[/color]\n" % [col, _esc(line)]
	console_box.text = body
	_update_status(up)


func _update_status(up: bool) -> void:
	var id: String = machine.read_file("/etc/hostname").strip_edges()
	if id == "":
		id = "node-?"
	if up:
		status_bar.text = "%s   UP   %d fault(s) injected" % [id, faults]
		status_bar.add_theme_color_override("font_color", Color("#1f6b3a"))
	else:
		status_bar.text = "%s   DOWN at %s   %s" % [id, machine.boot_stage(),
				machine.boot_reason()]
		status_bar.add_theme_color_override("font_color", Color("#a11f1f"))


func _refresh_tree() -> void:
	var listing: String = machine.list_dir(cwd)
	var body := "[color=#5b646d]%s[/color]\n\n" % _esc(cwd)
	if listing == "":
		body += "[color=#a11f1f]not a directory[/color]"
	for line in listing.split("\n"):
		if line.strip_edges() == "":
			continue
		var f := line.split(" ")
		if f.size() < 4:
			continue
		var name: String = f[0]
		var kind: String = f[1]
		var mode: String = f[2]
		var size: String = f[3]
		var full := (cwd + "/" + name).replace("//", "/")
		var tag := "d:" if kind == "dir" else "f:"
		var col := "#2a5fa8" if kind == "dir" else "#14181c"
		if kind == "link":
			col = "#7a5a2a"
		body += "[color=#5b646d]%s %6s[/color]  [url=%s%s][color=%s]%s%s[/color][/url]\n" % [
				mode, size, tag, full, col, _esc(name), "/" if kind == "dir" else ""]
	tree_box.text = body


func _show_file(path: String) -> void:
	var text: String = machine.read_file(path)
	var owner: String = machine.owns(path)
	var head := "[color=#2a5fa8]%s[/color]" % _esc(path)
	if owner != "":
		head += "  [color=#5b646d]owned by %s[/color]" % owner
	# A binary is not text and pretending otherwise wastes the player's time.
	var printable := 0
	for i in range(min(text.length(), 200)):
		var ch := text.unicode_at(i)
		if ch == 9 or ch == 10 or (ch >= 32 and ch < 127):
			printable += 1
	if text.length() > 0 and float(printable) / float(min(text.length(), 200)) < 0.9:
		detail_box.text = head + "\n\n[color=#5b646d]binary, %d bytes. " % text.length() \
			+ "Reinstall its package rather than editing it.[/color]"
		return
	detail_box.text = head + "\n\n" + _esc(text)


func _verify() -> void:
	var out: String = machine.verify("")
	var body := "[color=#2a5fa8]verify[/color]\n\n"
	for line in out.split("\n"):
		if line.strip_edges() == "":
			continue
		if line.find("all files match") >= 0:
			body += "[color=#1f6b3a]%s[/color]\n" % _esc(line)
			continue
		var path := line.split(" ")[0]
		var owner: String = machine.owns(path)
		body += "[color=#a11f1f]%s[/color]  [color=#5b646d]%s[/color]\n" % [
				_esc(line), owner]
	detail_box.text = body
