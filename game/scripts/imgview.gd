# imgview.gd — the image viewer, for a machine that has no images.
#
# THIS IS NOT A PHOTOGRAPH VIEWER PRETENDING. There is no image decoder in
# this OS, there are no image files on the disk, and `/home/nomowner/Pictures`
# contains one file, a README, whose first line is "There are no pictures on
# this machine and there never were." An app that drew a sunset over that
# would be the only thing in NOMINAL that lies to the player.
#
# So it views what IS picture-shaped here: text. ASCII art, `cowsay` output
# saved to a file, a .plan, a banner in a README. That is a real category of
# thing on this disk and it wants exactly what an image viewer gives you --
# fit to window, one-to-one, zoom, pan, and arrow keys to walk the directory.
#
# Everything on screen is one command: `cat <path>` for the content, `ls -l`
# on the containing directory for the entry list and the sizes. When cat
# refuses -- "cannot read" for a directory, "binary file, 7776 bytes" for an
# ELF image -- that line is shown VERBATIM and nothing is drawn. Refusing to
# render is information: it tells you what the file actually is.
#
# The zoom is a canvas transform, not a font size. Scaling the font size
# quantises to whole pixels, so "fit" lands on the wrong scale and the last
# column of a piece of art falls off the edge -- which for ASCII art means the
# right-hand side of every face is missing.

extends Control

var mono: Font
var machine: Object = null
# Same hook as sysmon: the desktop can point this at the customer's machine
# without the app knowing there are two.
var sh: Callable = Callable()

var dir := "/home/nomowner/Pictures"
var path := ""
var entries: Array = []      # {name, size, mode, type} from `ls -l`, files only
var idx := -1

var lines: PackedStringArray = []
var err := ""                # cat's own words, when it would not show the file
var raw_bytes := 0           # what ls -l said the file is, in bytes

var zoom := 1.0
var pan := Vector2.ZERO
# The measured size of the text at zoom 1, and the view size that the current
# fit was computed for. Both exist because of the same bug: _ready() loads a
# file before the desktop has given this control its size, so the first fit
# was computed against a zero-sized window and every file opened at the
# minimum zoom in the top-left corner. Fit is now recomputed whenever the
# window it was fitted to is no longer the window we have.
var content := Vector2.ONE
var fitted_for := Vector2.ZERO
var auto_fit := true
var dragging := false
var drag_from := Vector2.ZERO
var pan_from := Vector2.ZERO
var listing_err := ""

const TOP := 22.0
const BOT := 30.0
const BASE_FS := 12          # the size the text is drawn at before zoom
const MIN_ZOOM := 0.15
const MAX_ZOOM := 8.0

const CHROME := Color("#d6d3ce")
const INK := Color("#1b1b1b")
const DIM := Color("#5f6469")
const FAINT := Color("#9aa0a6")
const RED := Color("#b0281a")
const PAPER := Color("#fdfdfb")
const ART := Color("#232a30")


func _ready() -> void:
	focus_mode = Control.FOCUS_ALL
	mouse_filter = Control.MOUSE_FILTER_STOP
	if mono == null:
		mono = preload("res://scripts/uifont.gd").mono()
	# The canvas is panned and zoomed, so it CAN reach outside the control.
	# Clipping is the guarantee that it never paints over the window frame.
	clip_contents = true
	refresh()
	set_process(true)


func take_focus() -> void:
	grab_focus()


func _process(_dt: float) -> void:
	pass


func _sh(cmd: String) -> String:
	if sh.is_valid():
		return str(sh.call(cmd))
	if machine == null:
		return ""
	return str(machine.sh_on(0, cmd))


# ---------------------------------------------------------------- loading

func _join(base: String, nm: String) -> String:
	return (base if base.ends_with("/") else base + "/") + nm


# The desktop opens this with a path that may be either kind of thing, and a
# viewer that says "not a file" when handed a directory is useless -- opening
# a folder of art should show you the first piece of art.
func open_path(p: String) -> void:
	if machine == null and not sh.is_valid():
		err = "no machine attached"
		queue_redraw()
		return
	var st := _sh("stat " + p)
	if st.find("kind  directory") >= 0 or st.find("kind  dir") >= 0:
		dir = p
		path = ""
	else:
		dir = p.get_base_dir()
		if dir == "":
			dir = "/"
		path = p
	refresh()


# Ask again from scratch. The listing is `ls -l` on the directory, parsed the
# same way the file browser parses it -- fixed columns, no tabs.
func refresh() -> void:
	entries = []
	listing_err = ""
	var out := _sh("ls -l " + dir)
	for line in out.split("\n"):
		var t := line.strip_edges()
		if t == "":
			continue
		if t.begins_with("ls:"):
			listing_err = t
			continue
		var e := _parse(line)
		if e.is_empty():
			continue
		# Directories are not entries you can page through with an arrow key.
		# They are somewhere to go, and the file browser is how you go there.
		if e["type"] == "d":
			continue
		entries.append(e)
	idx = -1
	for i in range(entries.size()):
		if _join(dir, entries[i]["name"]) == path:
			idx = i
	if idx < 0 and not entries.is_empty():
		idx = 0
		path = _join(dir, entries[0]["name"])
	_load()


# `ls -l`: one type character, four octal mode digits, two spaces, the size
# right-aligned in eight columns, two spaces, the name. A dangling symlink
# prints "????" for the mode and "?" for the size, so neither field can be
# assumed numeric -- int("?") is 0 and that is fine, but "?" must survive to
# the status line, because a file whose size is unknown is a finding.
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
	var rest := line.substr(i).replace("(DANGLING)", "").strip_edges()
	var arrow := rest.find(" -> ")
	if arrow >= 0:
		rest = rest.substr(0, arrow)
	var nm := rest.strip_edges()
	if nm == "":
		return {}
	return {"name": nm, "type": t, "mode": mode, "size": sz}


func _load() -> void:
	lines = PackedStringArray()
	err = ""
	raw_bytes = 0
	if path == "":
		err = "nothing here to show"
		if listing_err != "":
			err = listing_err
		queue_redraw()
		return
	if idx >= 0 and idx < entries.size():
		raw_bytes = int(entries[idx]["size"])
	var out := _sh("cat " + path)
	# cat says exactly why it will not print a file, and its sentence is more
	# accurate than any summary of it: "binary file, 7776 bytes" tells you the
	# thing is an ELF image, "cannot read" tells you it is a directory or the
	# mode is 000. Both are shown as cat wrote them.
	var first := out.split("\n")[0].strip_edges() if out != "" else ""
	if first.begins_with("cat:"):
		err = first
		queue_redraw()
		return
	lines = out.split("\n")
	# A trailing newline makes an empty last line that shifts "fit" down by
	# one row for every file on the disk.
	while lines.size() > 1 and lines[lines.size() - 1] == "":
		lines.remove_at(lines.size() - 1)
	_measure()
	auto_fit = true
	fitted_for = Vector2.ZERO
	fit()


func _step(by: int) -> void:
	if entries.is_empty():
		return
	idx = posmod(idx + by, entries.size())
	path = _join(dir, entries[idx]["name"])
	_load()


# ------------------------------------------------------------------ view

func _view() -> Rect2:
	return Rect2(0, TOP, size.x, maxf(20.0, size.y - TOP - BOT))


func _line_h() -> float:
	return maxf(float(BASE_FS) + 3.0, mono.get_height(BASE_FS))


# The widest line is MEASURED, not counted. The desktop's font is whatever
# Godot's fallback is and it is proportional, so "longest line in characters"
# is not the widest line in pixels -- fitting on a character count left every
# file scaled too small and pushed to the left of the window.
func _measure() -> void:
	var w := 1.0
	for l in lines:
		w = maxf(w, mono.get_string_size(l, HORIZONTAL_ALIGNMENT_LEFT, -1, BASE_FS).x)
	content = Vector2(w, maxf(1.0, float(lines.size()) * _line_h()))


func _content() -> Vector2:
	return content


func fit() -> void:
	var v := _view().size - Vector2(16, 16)
	if v.x < 8.0 or v.y < 8.0:
		# No window yet. Do not record this as the fit; _draw will redo it.
		return
	zoom = clampf(minf(v.x / content.x, v.y / content.y), MIN_ZOOM, MAX_ZOOM)
	auto_fit = true
	fitted_for = _view().size
	_centre()
	queue_redraw()


func one_to_one() -> void:
	zoom = 1.0
	auto_fit = false
	fitted_for = _view().size
	_centre()
	queue_redraw()


func _centre() -> void:
	var c := content * zoom
	var v := _view().size
	pan = Vector2(maxf(8.0, (v.x - c.x) / 2.0), maxf(8.0, (v.y - c.y) / 2.0))


# Keep at least a corner of the art on screen. Without this, one flick of the
# wheel while the pointer is near an edge pans the whole picture into the
# margin and the window looks broken.
func _clamp_pan() -> void:
	var c := _content() * zoom
	var v := _view().size
	if c.x <= v.x:
		pan.x = (v.x - c.x) / 2.0
	else:
		pan.x = clampf(pan.x, v.x - c.x - 8.0, 8.0)
	if c.y <= v.y:
		pan.y = (v.y - c.y) / 2.0
	else:
		pan.y = clampf(pan.y, v.y - c.y - 8.0, 8.0)


# Zoom about a point, so the character under the pointer stays under it.
func _zoom_at(f: float, at: Vector2) -> void:
	var v := _view()
	var local := at - v.position - pan
	var old := zoom
	zoom = clampf(zoom * f, MIN_ZOOM, MAX_ZOOM)
	if old > 0.0:
		pan -= local * (zoom / old - 1.0)
	auto_fit = false
	fitted_for = v.size
	_clamp_pan()
	queue_redraw()


# ---------------------------------------------------------------- input

func _gui_input(e: InputEvent) -> void:
	if e is InputEventMouseButton:
		var mb := e as InputEventMouseButton
		if mb.pressed:
			grab_focus()
			match mb.button_index:
				MOUSE_BUTTON_WHEEL_UP:
					_zoom_at(1.12, mb.position)
				MOUSE_BUTTON_WHEEL_DOWN:
					_zoom_at(1.0 / 1.12, mb.position)
				MOUSE_BUTTON_LEFT:
					dragging = true
					drag_from = mb.position
					pan_from = pan
		elif mb.button_index == MOUSE_BUTTON_LEFT:
			dragging = false
		accept_event()
		return

	if e is InputEventMouseMotion and dragging:
		pan = pan_from + ((e as InputEventMouseMotion).position - drag_from)
		_clamp_pan()
		accept_event()
		queue_redraw()
		return

	if not (e is InputEventKey) or not e.pressed:
		return
	var k := e as InputEventKey
	match k.keycode:
		KEY_LEFT: _step(-1)
		KEY_RIGHT: _step(1)
		KEY_UP: pan.y += 24.0; _clamp_pan()
		KEY_DOWN: pan.y -= 24.0; _clamp_pan()
		KEY_PAGEUP: pan.y += _view().size.y * 0.8; _clamp_pan()
		KEY_PAGEDOWN: pan.y -= _view().size.y * 0.8; _clamp_pan()
		KEY_F: fit()
		KEY_1: one_to_one()
		KEY_EQUAL, KEY_PLUS, KEY_KP_ADD: _zoom_at(1.25, _view().get_center())
		KEY_MINUS, KEY_KP_SUBTRACT: _zoom_at(0.8, _view().get_center())
		KEY_F5, KEY_R: refresh()
		_:
			return
	accept_event()
	queue_redraw()


# ---------------------------------------------------------------- drawing

func _fit_text(t: String, w: float, fs: int) -> String:
	if mono.get_string_size(t, HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x <= w:
		return t
	var s := t
	while s.length() > 1 and \
			mono.get_string_size(s + "...", HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x > w:
		s = s.substr(0, s.length() - 1)
	return s + "..."


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), CHROME)
	var v := _view()
	# The window has been resized (or has just been given a size for the first
	# time) since the current scale was worked out. In fit mode, refit; if the
	# user has zoomed by hand, leave their scale alone and just keep the text
	# from sliding off the edge.
	if v.size != fitted_for:
		if auto_fit:
			fit()
			v = _view()
		else:
			fitted_for = v.size
			_clamp_pan()
	draw_rect(v, PAPER)

	draw_rect(Rect2(0, 0, size.x, TOP), Color("#e4e4e4"))
	draw_line(Vector2(0, TOP), Vector2(size.x, TOP), Color("#b9bfc6"))
	draw_string(mono, Vector2(8, 15),
		_fit_text(path if path != "" else dir, size.x - 96.0, 11),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 11, INK)
	if not entries.is_empty():
		draw_string(mono, Vector2(-8, 15), "%d/%d" % [idx + 1, entries.size()],
			HORIZONTAL_ALIGNMENT_RIGHT, size.x, 10, DIM)

	if err != "":
		_draw_refusal(v)
	else:
		_draw_art(v)

	_draw_foot()


# When cat will not print it, this window says so in cat's own words and then
# explains what that means -- and draws NOTHING in the canvas. An empty canvas
# with a sentence in it is the honest picture of a file that has no picture.
func _draw_refusal(v: Rect2) -> void:
	var y := v.position.y + 24.0
	draw_string(mono, Vector2(12, y), _fit_text(err, v.size.x - 24.0, 12),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 12, RED)
	y += 20.0
	var why := "this viewer shows text; the machine has no image decoder and no image files"
	if err.find("binary file") >= 0:
		why = "that is a program, not a picture. `ldd` and `pkg owns` are the tools for it"
	elif err.find("cannot read") >= 0:
		why = "a directory, or a mode that forbids reading it -- `stat` and `ls -l` say which"
	elif err == "nothing here to show":
		why = "`ls -l %s` listed no files" % dir
		if listing_err != "":
			why = listing_err
	for w in _wrap(why, v.size.x - 24.0, 10):
		draw_string(mono, Vector2(12, y), w, HORIZONTAL_ALIGNMENT_LEFT, -1, 10, DIM)
		y += 13.0


func _wrap(t: String, w: float, fs: int) -> PackedStringArray:
	var out := PackedStringArray()
	var cur := ""
	for word in t.split(" "):
		var test := word if cur == "" else cur + " " + word
		if mono.get_string_size(test, HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x > w and cur != "":
			out.append(cur)
			cur = word
		else:
			cur = test
	if cur != "":
		out.append(cur)
	return out


func _draw_art(v: Rect2) -> void:
	if lines.is_empty():
		draw_string(mono, Vector2(12, v.position.y + 24), "(empty file)",
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, FAINT)
		return
	var lh := _line_h()
	# Only the rows that can land inside the canvas are drawn. Without this a
	# thousand-line file is a thousand draw_string calls per frame at every
	# zoom level, most of them behind the window frame.
	var first: int = maxi(0, int((-pan.y) / (lh * zoom)) - 1)
	var last: int = mini(lines.size(), first + int(v.size.y / (lh * zoom)) + 3)
	draw_set_transform(v.position + pan, 0.0, Vector2(zoom, zoom))
	for i in range(first, last):
		draw_string(mono, Vector2(0, float(i + 1) * lh - 3.0), lines[i],
			HORIZONTAL_ALIGNMENT_LEFT, -1, BASE_FS, ART)
	draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)


func _draw_foot() -> void:
	var y := size.y - BOT
	draw_rect(Rect2(0, y, size.x, BOT), Color("#e6e3de"))
	draw_line(Vector2(0, y), Vector2(size.x, y), Color("#b3b0ab"))
	var cols := 0
	for l in lines:
		cols = maxi(cols, l.length())
	var msg := ""
	if err != "":
		msg = "%s   %s" % [path.get_file(), "%d bytes" % raw_bytes if raw_bytes > 0 else ""]
	else:
		msg = "%d x %d characters   %d bytes   zoom %d%%" % [
			cols, lines.size(), raw_bytes, int(round(zoom * 100.0))]
	draw_string(mono, Vector2(8, y + 12), _fit_text(msg, size.x - 16.0, 10),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 10, RED if err != "" else INK)
	draw_string(mono, Vector2(8, y + 23),
		_fit_text("left/right file   F fit   1 actual size   drag or wheel   `cat` is the whole app",
			size.x - 16.0, 9), HORIZONTAL_ALIGNMENT_LEFT, -1, 9, Color("#7c8085"))
