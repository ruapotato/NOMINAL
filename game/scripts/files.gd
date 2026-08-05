# files.gd — the file browser on YOUR workstation.
#
# David: "The filebrowser on your computer should match what your local
# terminal says." So it does not keep its own idea of anything: every listing
# is `ls -l` run through the machine's own shell, and the path bar is a real
# cwd. If the browser and the terminal ever disagree, one of them is lying,
# and it will not be this one.
#
# Two views, because they answer two different questions. Icons to find a
# thing by eye, which is what a browser is for; the detail list to read modes
# and sizes, which is the whole job when something is broken. `v` swaps them
# and neither is a second-class citizen.

extends Control

var mono: Font
var machine: Object = null
var path := "/"
var rows: PackedStringArray = []   # raw `ls -l` lines, exactly as printed
var items: Array = []              # those lines, parsed, in listing order
var err := ""                      # what ls said when it could not list
var sel := 0
var scroll := 0                    # first visible grid row / list row
var icons := true

const TOP := 24.0        # path bar
const BOT := 19.0        # status line
const CELL_W := 92.0
const CELL_H := 78.0
const LINE_H := 15.0

const GOLD := Color("#e0a338")
const GOLD_D := Color("#b07d22")
const INK := Color("#1b1b1b")
const DIM := Color("#6a737d")


func _ready() -> void:
	focus_mode = Control.FOCUS_ALL
	mouse_filter = Control.MOUSE_FILTER_STOP
	if mono == null:
		mono = ThemeDB.fallback_font
	refresh()


func take_focus() -> void:
	grab_focus()


# The desktop calls this after every command, so it has to be the whole
# refresh: ask the machine again and believe nothing that was here before.
func refresh() -> void:
	var out: String = machine.sh_on(0, "ls -l " + path)
	rows = out.split("\n")
	items = []
	err = ""
	if path != "/":
		# ".." is the one entry that is not on the disk. It is navigation, not
		# a claim about what this directory contains, so it gets its own kind
		# and is never confused with a real name.
		items.append({"name": "..", "kind": "up", "type": "d",
			"mode": "", "size": "", "target": "", "dangling": false})
	for line in rows:
		if line.strip_edges() == "":
			continue
		if line.begins_with("ls:"):
			err = line.strip_edges()
			continue
		var e := _parse(line)
		if not e.is_empty():
			items.append(e)
	sel = clampi(sel, 0, maxi(0, items.size() - 1))
	_ensure_visible()
	queue_redraw()


# One `ls -l` line, taken apart. The format is fixed by /bin/ls: a type
# character, four octal mode digits, two spaces, the size right-aligned in
# eight columns, two spaces, then the name -- and for a symlink, " -> target"
# and possibly "   (DANGLING)". There are NO TABS in it; ls used to separate
# the columns with one and stopped, because the terminal does not expand tabs
# and every folder came out reading "0bin".
func _parse(line: String) -> Dictionary:
	if line.length() < 8 or "dl-".find(line[0]) < 0:
		return {}
	var t := line[0]
	var mode := line.substr(1, 4)
	var i := 5
	while i < line.length() and line[i] == " ":
		i += 1
	var s := i
	while i < line.length() and line[i] != " ":
		i += 1
	var sz := line.substr(s, i - s)
	while i < line.length() and line[i] == " ":
		i += 1
	var rest := line.substr(i)
	var dangling := rest.find("(DANGLING)") >= 0
	rest = rest.replace("(DANGLING)", "").strip_edges()
	var tgt := ""
	var arrow := rest.find(" -> ")
	if arrow >= 0:
		tgt = rest.substr(arrow + 4).strip_edges()
		rest = rest.substr(0, arrow)
	var nm := rest.strip_edges()
	if nm == "":
		return {}
	var kind := "bin"
	if t == "d":
		kind = "dir"
	elif t == "l":
		kind = "link"
	elif _executable(mode):
		kind = "exec"
	elif _texty(nm):
		kind = "text"
	return {"name": nm, "kind": kind, "type": t, "mode": mode,
		"size": sz, "target": tgt, "dangling": dangling}


# An execute bit is the odd bit of an octal digit, so the file is executable
# by somebody exactly when one of the last three digits is odd. The old check
# was `line.find("755")`, which also matched a file of 755 bytes.
func _executable(mode: String) -> bool:
	if mode.length() < 4:
		return false
	for c in mode.substr(1, 3):
		if "1357".find(c) >= 0:
			return true
	return false


func _texty(nm: String) -> bool:
	for suf in [".txt", ".conf", ".cfg", ".svc", ".log", ".state", ".d",
			".defs", ".tab", ".release", ".so.conf", ".boot"]:
		if nm.ends_with(suf):
			return true
	# Most of /etc has no extension at all -- fstab, passwd, hosts, motd --
	# and every one of those is a file you open and read.
	return nm.find(".") < 0


# Clicking a file OPENS it. A browser that can only show you a listing is a
# listing, not a browser -- and David asked for text files to open in an
# editor, which is what anyone expects when they double-click a .conf.
var on_open_text: Callable = func(_p: String) -> void: pass


func _join(base: String, nm: String) -> String:
	return (base if base.ends_with("/") else base + "/") + nm


func _open(e: Dictionary) -> void:
	if e["kind"] == "up":
		_up()
		return
	var t := _join(path, e["name"])
	# `ls` reports the type of the NAME; for a symlink that is the link, not
	# what it points at. stat follows it, and stat is the same machine asked a
	# second question -- so a link into a directory navigates like a
	# directory, and a directory never reaches the text editor.
	var st: String = machine.sh_on(0, "stat " + t)
	if st.find("kind  dir") >= 0:
		_goto(t)
		return
	if st.find("kind ") < 0:
		# Dangling: stat says there is nothing on the other end. Opening an
		# empty editor on it would hide the fault, which is the finding.
		return
	on_open_text.call(t)


func _goto(p: String) -> void:
	path = p
	sel = 0
	scroll = 0
	refresh()


func _up() -> void:
	if path != "/":
		var b := path.get_base_dir()
		_goto(b if b != "" else "/")


func _cols() -> int:
	return maxi(1, int((size.x - 8) / CELL_W))


func _visible_rows() -> int:
	var h := size.y - TOP - BOT
	return maxi(1, int(h / (CELL_H if icons else LINE_H)))


func _ensure_visible() -> void:
	var per := _cols() if icons else 1
	var row := sel / per
	var vis := _visible_rows()
	if row < scroll:
		scroll = row
	elif row >= scroll + vis:
		scroll = row - vis + 1
	var last := maxi(0, (items.size() - 1) / per - vis + 1)
	scroll = clampi(scroll, 0, last)


func _hit(pos: Vector2) -> int:
	if pos.y < TOP or pos.y > size.y - BOT:
		return -1
	var i := -1
	if icons:
		var c := int((pos.x - 4) / CELL_W)
		if c < 0 or c >= _cols():
			return -1
		i = (scroll + int((pos.y - TOP) / CELL_H)) * _cols() + c
	else:
		i = scroll + int((pos.y - TOP) / LINE_H)
	return i if i >= 0 and i < items.size() else -1


func _gui_input(e: InputEvent) -> void:
	if e is InputEventMouseButton and e.pressed:
		grab_focus()
		var mb := e as InputEventMouseButton
		if mb.button_index == MOUSE_BUTTON_WHEEL_UP:
			scroll = maxi(0, scroll - 1)
			queue_redraw()
			return
		if mb.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			scroll += 1
			_clamp_scroll()
			queue_redraw()
			return
		if mb.button_index != MOUSE_BUTTON_LEFT:
			return
		if mb.position.y < TOP:
			return
		var i := _hit(mb.position)
		if i >= 0:
			# Single click selects, double click opens. A browser where one
			# click opens cannot be used to look at anything.
			sel = i
			if mb.double_click:
				_open(items[i])
			queue_redraw()
		return

	if not (e is InputEventKey) or not e.pressed:
		return
	var k := e as InputEventKey
	var per := _cols() if icons else 1
	match k.keycode:
		KEY_V:
			icons = not icons
			_ensure_visible()
		KEY_LEFT:
			sel = maxi(0, sel - 1)
			_ensure_visible()
		KEY_RIGHT:
			sel = mini(items.size() - 1, sel + 1)
			_ensure_visible()
		KEY_UP:
			sel = maxi(0, sel - per)
			_ensure_visible()
		KEY_DOWN:
			sel = mini(items.size() - 1, sel + per)
			_ensure_visible()
		KEY_HOME:
			sel = 0
			_ensure_visible()
		KEY_END:
			sel = maxi(0, items.size() - 1)
			_ensure_visible()
		KEY_ENTER, KEY_KP_ENTER:
			if sel >= 0 and sel < items.size():
				_open(items[sel])
		KEY_BACKSPACE:
			_up()
		KEY_F5:
			refresh()
		_:
			return
	accept_event()
	queue_redraw()


func _clamp_scroll() -> void:
	var per := _cols() if icons else 1
	var last := maxi(0, items.size() - 1) / per - _visible_rows() + 1
	scroll = clampi(scroll, 0, maxi(0, last))


# ---------------------------------------------------------------- icon art

# Everything below is drawn, not loaded: the desktop ships no image files, and
# an icon set that has to look like a folder at 32 pixels is a handful of
# rectangles anyway.
func _icon(kind: String, at: Vector2, dangling: bool) -> void:
	var x := at.x
	var y := at.y
	match kind:
		"up", "dir":
			# A tab on the back panel, then the front of the folder over it.
			draw_rect(Rect2(x + 1, y + 3, 13, 5), GOLD_D)
			draw_rect(Rect2(x + 1, y + 6, 30, 20), GOLD_D)
			draw_rect(Rect2(x + 2, y + 10, 28, 15), GOLD)
			draw_rect(Rect2(x + 1, y + 6, 30, 20), Color("#8c6218"), false, 1.0)
			if kind == "up":
				var a := PackedVector2Array([
					Vector2(x + 16, y + 11), Vector2(x + 23, y + 18),
					Vector2(x + 19, y + 18), Vector2(x + 19, y + 23),
					Vector2(x + 13, y + 23), Vector2(x + 13, y + 18),
					Vector2(x + 9, y + 18)])
				draw_polygon(a, _fill(Color("#fbf1dd"), a.size()))
		"link":
			_page(x, y, Color("#fdfdfd"), Color("#8a6d1f"))
			# The badge says "this is a pointer"; the red cross says it points
			# at nothing, which is the single commonest fault on the machine.
			var ar := PackedVector2Array([
				Vector2(x + 8, y + 24), Vector2(x + 18, y + 24),
				Vector2(x + 18, y + 20), Vector2(x + 24, y + 25),
				Vector2(x + 18, y + 30), Vector2(x + 18, y + 27),
				Vector2(x + 8, y + 27)])
			draw_polygon(ar, _fill(Color("#8a6d1f"), ar.size()))
			if dangling:
				draw_line(Vector2(x + 8, y + 6), Vector2(x + 22, y + 20),
					Color("#c0392b"), 2.0)
				draw_line(Vector2(x + 22, y + 6), Vector2(x + 8, y + 20),
					Color("#c0392b"), 2.0)
		"exec":
			# A terminal: title bar, dark field, a prompt and a cursor.
			draw_rect(Rect2(x + 2, y + 4, 28, 22), Color("#1c1c1c"))
			draw_rect(Rect2(x + 2, y + 4, 28, 5), Color("#3b4450"))
			draw_rect(Rect2(x + 2, y + 4, 28, 22), Color("#0d0d0d"), false, 1.0)
			draw_string(mono, Vector2(x + 5, y + 20), ">",
				HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color("#79d17a"))
			draw_rect(Rect2(x + 13, y + 13, 6, 8), Color("#79d17a"))
		"text":
			_page(x, y, Color("#ffffff"), Color("#9aa4ae"))
			for li in range(5):
				var w := 15.0 if li == 4 else 19.0
				draw_line(Vector2(x + 9, y + 9 + li * 3.5),
					Vector2(x + 9 + w, y + 9 + li * 3.5), Color("#a9bacd"))
		_:
			# A binary: no lines to read, just bytes.
			_page(x, y, Color("#eceff2"), Color("#9aa4ae"))
			for r in range(3):
				for c in range(4):
					if (r + c) % 2 == 0:
						continue
					draw_rect(Rect2(x + 9 + c * 5, y + 10 + r * 5, 3, 3),
						Color("#7a8794"))


# A sheet of paper with the top-right corner turned over. The fold is what
# makes it read as a document rather than a plain white box.
func _page(x: float, y: float, fill: Color, edge: Color) -> void:
	var body := PackedVector2Array([
		Vector2(x + 5, y + 2), Vector2(x + 21, y + 2), Vector2(x + 27, y + 8),
		Vector2(x + 27, y + 30), Vector2(x + 5, y + 30)])
	draw_polygon(body, _fill(fill, body.size()))
	for i in range(body.size()):
		draw_line(body[i], body[(i + 1) % body.size()], edge, 1.0)
	var fold := PackedVector2Array([
		Vector2(x + 21, y + 2), Vector2(x + 27, y + 8), Vector2(x + 21, y + 8)])
	draw_polygon(fold, _fill(edge.lerp(fill, 0.5), fold.size()))
	draw_line(Vector2(x + 21, y + 2), Vector2(x + 21, y + 8), edge, 1.0)


func _fill(c: Color, n: int) -> PackedColorArray:
	var a := PackedColorArray()
	for i in range(n):
		a.append(c)
	return a


func _colour(kind: String) -> Color:
	match kind:
		"dir", "up": return Color("#1b4f8f")
		"link":      return Color("#8a6d1f")
		"exec":      return Color("#1f6b3a")
		"text":      return Color("#22303f")
		_:           return Color("#4a5560")


# Cut a name down until it fits, then say so. Three dots rather than a real
# ellipsis: the fallback font is whatever Godot has, and a missing glyph is a
# blank box, which reads as part of the filename.
func _fit(t: String, w: float, fs: int) -> String:
	if mono.get_string_size(t, HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x <= w:
		return t
	var s := t
	while s.length() > 1 and \
			mono.get_string_size(s + "...", HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x > w:
		s = s.substr(0, s.length() - 1)
	return s + "..."


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color("#ffffff"))

	# path bar
	draw_rect(Rect2(0, 0, size.x, TOP), Color("#e4e4e4"))
	draw_line(Vector2(0, TOP), Vector2(size.x, TOP), Color("#b9bfc6"))
	draw_string(mono, Vector2(8, 16), _fit(path, size.x - 130, 12),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 12, INK)
	draw_string(mono, Vector2(size.x - 118, 16),
		"v = list" if icons else "v = icons",
		HORIZONTAL_ALIGNMENT_LEFT, -1, 10, DIM)
	draw_string(mono, Vector2(size.x - 60, 16), "bksp = up",
		HORIZONTAL_ALIGNMENT_LEFT, -1, 10, DIM)

	if icons:
		_draw_icons()
	else:
		_draw_list()

	# status line
	var sy := size.y - BOT
	draw_rect(Rect2(0, sy, size.x, BOT), Color("#f1f3f5"))
	draw_line(Vector2(0, sy), Vector2(size.x, sy), Color("#d5dade"))
	var msg := "%d items" % items.size()
	if err != "":
		msg = err
	elif sel >= 0 and sel < items.size():
		var e: Dictionary = items[sel]
		if e["kind"] == "up":
			msg += "    ..  parent directory"
		else:
			msg += "    %s   %s bytes   mode %s" % [e["name"], e["size"], e["mode"]]
			if e["target"] != "":
				msg += "  ->  " + e["target"]
				if e["dangling"]:
					msg += "  (DANGLING)"
	draw_string(mono, Vector2(8, sy + 13), _fit(msg, size.x - 16, 10),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 10,
		Color("#c0392b") if err != "" else Color("#3c464f"))


func _draw_icons() -> void:
	var cols := _cols()
	var vis := _visible_rows()
	var first := scroll * cols
	for i in range(first, mini(items.size(), first + vis * cols)):
		var e: Dictionary = items[i]
		var c := (i - first) % cols
		var r := (i - first) / cols
		var x := 4 + c * CELL_W
		var y := TOP + 2 + r * CELL_H
		if i == sel:
			draw_rect(Rect2(x + 2, y, CELL_W - 8, CELL_H - 8), Color("#dbe7f6"))
			draw_rect(Rect2(x + 2, y, CELL_W - 8, CELL_H - 8),
				Color("#7aa7d8"), false, 1.0)
		_icon(e["kind"], Vector2(x + CELL_W / 2 - 20, y + 8), e["dangling"])
		draw_string(mono, Vector2(x + 2, y + 58),
			_fit(e["name"], CELL_W - 14, 11), HORIZONTAL_ALIGNMENT_CENTER,
			CELL_W - 8, 11, _colour(e["kind"]))
	if items.size() > vis * cols:
		draw_string(mono, Vector2(size.x - 96, TOP + 12),
			"%d/%d" % [first + 1, items.size()],
			HORIZONTAL_ALIGNMENT_LEFT, -1, 9, DIM)


# The detail list is not a fallback. Modes and sizes in columns are how you
# read a directory when you are looking for what is wrong with it, and no
# amount of icon polish replaces "0644 on a file that has to be executable".
func _draw_list() -> void:
	var vis := _visible_rows()
	for i in range(scroll, mini(items.size(), scroll + vis)):
		var e: Dictionary = items[i]
		var y := TOP + 13 + (i - scroll) * LINE_H
		if i == sel:
			draw_rect(Rect2(2, y - 11, size.x - 4, LINE_H), Color("#dbe7f6"))
		# The same art, shrunk into the row, so the two views agree about
		# what each thing is.
		draw_set_transform(Vector2(6, y - 12), 0.0, Vector2(0.38, 0.38))
		_icon(e["kind"], Vector2.ZERO, e["dangling"])
		draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)
		var col := _colour(e["kind"])
		if e["kind"] != "up":
			draw_string(mono, Vector2(22, y), e["type"] + e["mode"],
				HORIZONTAL_ALIGNMENT_LEFT, -1, 12, DIM)
			draw_string(mono, Vector2(70, y), e["size"],
				HORIZONTAL_ALIGNMENT_RIGHT, 58, 12, DIM)
		var nm: String = e["name"]
		if e["target"] != "":
			nm += " -> " + e["target"]
			if e["dangling"]:
				nm += "   (DANGLING)"
				col = Color("#c0392b")
		draw_string(mono, Vector2(136, y), _fit(nm, size.x - 144, 12),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 12, col)
	if items.size() > vis:
		draw_string(mono, Vector2(size.x - 60, TOP + 12),
			"%d/%d" % [scroll + 1, items.size()],
			HORIZONTAL_ALIGNMENT_LEFT, -1, 9, DIM)
