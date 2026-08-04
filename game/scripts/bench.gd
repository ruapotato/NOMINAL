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
var cmd_in: LineEdit
var phone_box: RichTextLabel
var ask_in: LineEdit
const PHONE_HINT := "[color=#5b646d]They are not technical, and they are the only pair of hands in the room. Ask what they see, what changed, whether there was a power cut -- or ask them to power cycle it, or to put the rescue disc in.[/color]"

var phone := ""
var cwd := "/"
var term := ""


# The customer, on the phone. Everything the model side of this game does was
# reachable only from the TCP bench, so a player at the desktop never met the
# person whose machine it is.
func _on_ask(text: String) -> void:
	ask_in.clear()
	if text.strip_edges() == "":
		return
	phone += "[color=#2a5fa8]you:[/color] " + _esc(text) + "\n"
	var reply: String = machine.ask(text)
	phone += "[color=#1b1b1b]" + _esc(reply.strip_edges()) + "[/color]\n\n"
	phone_box.text = phone


# Every command goes through the machine's own /bin/sh, via the same
# kernel_run() the TCP socket uses. The desktop is a view, never a shortcut.
func _on_command(text: String) -> void:
	cmd_in.clear()
	if text.strip_edges() == "":
		return
	term += "[color=#6fdc96]rescue#[/color] " + _esc(text) + "\n"
	var out: String = machine.sh(text)
	if out != "":
		term += _esc(out)
		if not out.ends_with("\n"):
			term += "\n"
	detail_box.text = term
	_refresh_tree()
	_update_status(machine.booted())


func _boot_rescue() -> void:
	console_box.text = _colourise(machine.boot_rescue())
	term += "[color=#5b646d]--- booted the rescue medium ---[/color]\n"
	detail_box.text = term
	cwd = "/"
	_refresh_tree()
	status_bar.text = "rescue medium   the customer disk is /dev/sda1, not mounted"
	status_bar.add_theme_color_override("font_color", Color("#2a5fa8"))


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
		elif a.begins_with("--ask="):
			_on_ask(a.substr(6))
		elif a.begins_with("--show="):
			_show_file(a.substr(7))
		elif a.begins_with("--cd="):
			cwd = a.substr(5)
			_refresh_tree()
		elif a == "--rescue":
			_boot_rescue()
		elif a.begins_with("--cmd="):
			_on_command(a.substr(6))
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

	var bx := 620.0
	for spec in [["New ticket", func(): _new_ticket()],
				 ["Boot disk", func(): _boot()],
				 ["Boot rescue disk", func(): _boot_rescue()],
				 ["Harder", func(): faults += 1; _new_ticket()]]:
		var b := Button.new()
		b.text = str(spec[0])
		b.position = Vector2(bx, 2)
		b.size = Vector2(140, 24)
		b.add_theme_font_size_override("font_size", 12)
		b.pressed.connect(spec[1])
		top.add_child(b)
		bx += 148

	# --- the machine's console ---
	var cc := _panel("console - customer machine", Rect2(14, 40, 700, 470))
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

	# --- the customer, on the phone ---
	var pc := _panel("the customer - on the line", Rect2(14, 522, 700, 218))
	var pv := VBoxContainer.new()
	pc.add_child(pv)
	phone_box = RichTextLabel.new()
	phone_box.bbcode_enabled = true
	phone_box.scroll_following = true
	phone_box.custom_minimum_size = Vector2(0, 146)
	phone_box.add_theme_font_size_override("normal_font_size", 13)
	var psb := StyleBoxFlat.new()
	psb.bg_color = BODY
	psb.set_content_margin_all(8)
	phone_box.add_theme_stylebox_override("normal", psb)
	phone_box.text = PHONE_HINT
	pv.add_child(phone_box)

	var arow := HBoxContainer.new()
	pv.add_child(arow)
	var al := Label.new()
	al.text = "ask"
	al.add_theme_color_override("font_color", DIM)
	arow.add_child(al)
	ask_in = LineEdit.new()
	ask_in.placeholder_text = "what do you see on the screen?"
	ask_in.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	ask_in.text_submitted.connect(_on_ask)
	arow.add_child(ask_in)

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

	# --- a real terminal on the machine ---
	var dc := _panel("terminal", Rect2(726, 430, 540, 310))
	var tv := VBoxContainer.new()
	dc.add_child(tv)
	detail_box = RichTextLabel.new()
	detail_box.bbcode_enabled = true
	detail_box.scroll_following = true
	detail_box.custom_minimum_size = Vector2(0, 236)
	detail_box.add_theme_font_override("normal_font", mono)
	detail_box.add_theme_font_size_override("normal_font_size", 12)
	var tsb2 := StyleBoxFlat.new()
	tsb2.bg_color = TERM_BG
	tsb2.set_content_margin_all(6)
	detail_box.add_theme_stylebox_override("normal", tsb2)
	tv.add_child(detail_box)

	var row := HBoxContainer.new()
	tv.add_child(row)
	var pr := Label.new()
	pr.text = "rescue#"
	pr.add_theme_color_override("font_color", PHOSPHOR)
	row.add_child(pr)
	cmd_in = LineEdit.new()
	cmd_in.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	cmd_in.text_submitted.connect(_on_command)
	row.add_child(cmd_in)


func _esc(s: String) -> String:
	return s.replace("[", "[lb]")


func _new_ticket() -> void:
	phone = ""
	if phone_box:
		phone_box.text = PHONE_HINT
	seed_no += 1
	machine.take_ticket(seed_no, faults)
	cwd = "/"
	_boot()
	_refresh_tree()
	term = "[color=#5b646d]A machine arrived and will not boot.\n\n" \
		+ "Boot rescue disk, then:\n" \
		+ "  mount /dev/sda1 /mnt\n" \
		+ "  for i in dev sys proc; do mount /$i /mnt/$i; done\n" \
		+ "  chroot /mnt\n" \
		+ "  pkg verify\n[/color]"
	detail_box.text = term


func _colourise(out: String) -> String:
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
	return body


func _boot() -> void:
	console_box.text = _colourise(machine.boot())
	_update_status(machine.booted())


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
