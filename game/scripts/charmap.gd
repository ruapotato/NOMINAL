# charmap.gd — the character map.
#
# HONEST ABOUT WHAT IT CANNOT DO, TWICE OVER:
#
# 1. There is no Unicode name table anywhere in this project. A real charmap
#    searches "LATIN SMALL LETTER A WITH ACUTE"; this one has no such data and
#    will not fake a table of made-up names, because a name that is nearly
#    right is worse than no name -- you would quote it in a ticket. The search
#    box therefore matches what the machine actually knows: the character
#    itself, its code point in hex or decimal, and the block name. The box
#    says so.
#
# 2. The desktop draws with ONE font -- DejaVu Sans Mono, under game/fonts/ --
#    and it does not cover Unicode. Every cell is probed with mono.has_char()
#    and the ones it cannot draw are greyed and struck out rather than
#    rendered, because an undrawable glyph comes out as a hollow box, a hollow
#    box IS a character (U+25A1), and a grid full of them looks like a working
#    font showing the wrong letters. Measured on this font: Basic Latin 95/95,
#    Latin-1 Supplement 96/96, Cyrillic 180/256, Greek 116/144, Mathematical
#    Operators 178/256, Box Drawing 128/128, Arrows 112/112, Letterlike
#    Symbols 18/80. The greyed-out part of this window is a true statement
#    about the font.
#
# The buffer at the bottom is the point of the app: click characters, then C
# to put them on the system clipboard, which is Godot's clipboard and belongs
# to your workstation -- not the emulated machine, which has no clipboard and
# no concept of one.

extends Control

var mono: Font
var machine: Object = null   # unused: a font is not a fact about the machine

# start, end (inclusive), name. Ordered as a person would look for them, not
# as Unicode orders them.
const BLOCKS := [
	[0x0020, 0x007E, "Basic Latin"],
	[0x00A0, 0x00FF, "Latin-1 Supplement"],
	[0x0100, 0x017F, "Latin Extended-A"],
	[0x0180, 0x024F, "Latin Extended-B"],
	[0x0370, 0x03FF, "Greek"],
	[0x0400, 0x04FF, "Cyrillic"],
	[0x2000, 0x206F, "General Punctuation"],
	[0x20A0, 0x20BF, "Currency Symbols"],
	[0x2100, 0x214F, "Letterlike Symbols"],
	[0x2190, 0x21FF, "Arrows"],
	[0x2200, 0x22FF, "Mathematical Operators"],
	[0x2500, 0x257F, "Box Drawing"],
	[0x2580, 0x259F, "Block Elements"],
	[0x25A0, 0x25FF, "Geometric Shapes"],
	[0x2600, 0x26FF, "Miscellaneous Symbols"],
]

var block := 0
var cells: Array = []        # code points of the current block, in order
var sel := 0
var scroll := 0              # first visible row
var buffer := ""
var query := ""
var searching := false
var hits: Array = []         # code points matching the query, across all blocks
var copied := 0.0            # seconds since C was pressed, for the confirmation

const TOP := 22.0
const BLK_H := 18.0
const BOT := 46.0
const CELL := 22.0

const CHROME := Color("#d6d3ce")
const WHITE := Color("#ffffff")
const INK := Color("#1b1b1b")
const DIM := Color("#5f6469")
const FAINT := Color("#9aa0a6")
const SEL := Color("#3465a4")
const RED := Color("#b0281a")
const GREEN := Color("#1f6b3a")
const MISS := Color("#c8c5c0")


func _ready() -> void:
	focus_mode = Control.FOCUS_ALL
	mouse_filter = Control.MOUSE_FILTER_STOP
	if mono == null:
		mono = preload("res://scripts/uifont.gd").mono()
	_load_block()
	set_process(true)


func take_focus() -> void:
	grab_focus()


func _process(dt: float) -> void:
	if copied > 0.0:
		copied = maxf(0.0, copied - dt)
		queue_redraw()


func _load_block() -> void:
	cells = []
	var b: Array = BLOCKS[block]
	for c in range(int(b[0]), int(b[1]) + 1):
		cells.append(c)
	sel = 0
	scroll = 0
	queue_redraw()


func _present(c: int) -> bool:
	return mono != null and mono.has_char(c)


func _covered(bi: int) -> int:
	var b: Array = BLOCKS[bi]
	var n := 0
	for c in range(int(b[0]), int(b[1]) + 1):
		if _present(c):
			n += 1
	return n


# ---------------------------------------------------------------- searching

# What can honestly be searched: the literal character, a code point written
# as hex (with or without U+ / 0x) or decimal, and the block name. Anything
# else returns nothing and the footer explains why rather than pretending the
# query was a name that did not exist.
func _run_search() -> void:
	hits = []
	var q := query.strip_edges()
	if q == "":
		return
	var lower := q.to_lower()
	var want := -1
	var hexish := lower
	if hexish.begins_with("u+"):
		hexish = hexish.substr(2)
	elif hexish.begins_with("0x"):
		hexish = hexish.substr(2)
	if hexish.is_valid_hex_number(false) and hexish.length() <= 6:
		want = hexish.hex_to_int()
	var dec := -1
	if q.is_valid_int():
		dec = int(q)
	for bi in range(BLOCKS.size()):
		var b: Array = BLOCKS[bi]
		var name_hit := str(b[2]).to_lower().find(lower) >= 0
		for c in range(int(b[0]), int(b[1]) + 1):
			var hit := false
			if name_hit:
				hit = true
			elif c == want or c == dec:
				hit = true
			elif q.length() <= 2 and q.find(String.chr(c)) >= 0:
				hit = true
			if hit:
				hits.append(c)
			if hits.size() >= 512:
				return


# ------------------------------------------------------------------ layout

func _grid_rect() -> Rect2:
	return Rect2(0, TOP + BLK_H, size.x, maxf(CELL, size.y - TOP - BLK_H - BOT))


func _cols() -> int:
	return maxi(1, int(_grid_rect().size.x / CELL))


func _rows_visible() -> int:
	return maxi(1, int(_grid_rect().size.y / CELL))


func _shown() -> Array:
	return hits if searching and not hits.is_empty() else cells


func _ensure_visible() -> void:
	var per := _cols()
	var row := sel / per
	if row < scroll:
		scroll = row
	elif row >= scroll + _rows_visible():
		scroll = row - _rows_visible() + 1
	var last := maxi(0, (_shown().size() - 1) / per - _rows_visible() + 1)
	scroll = clampi(scroll, 0, last)


# ---------------------------------------------------------------- input

func _gui_input(e: InputEvent) -> void:
	if e is InputEventMouseButton and e.pressed:
		grab_focus()
		var mb := e as InputEventMouseButton
		if mb.button_index == MOUSE_BUTTON_WHEEL_UP:
			scroll = maxi(0, scroll - 1)
			accept_event()
			queue_redraw()
			return
		if mb.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			scroll += 1
			_ensure_visible()
			accept_event()
			queue_redraw()
			return
		if mb.button_index == MOUSE_BUTTON_LEFT:
			if mb.position.y >= TOP and mb.position.y < TOP + BLK_H:
				# The block strip: left half steps back, right half forward.
				# A dropdown would need a popup and this window has none.
				block = posmod(block + (1 if mb.position.x > size.x / 2.0 else -1),
					BLOCKS.size())
				searching = false
				_load_block()
				accept_event()
				return
			var g := _grid_rect()
			if g.has_point(mb.position):
				var c := int((mb.position.x - g.position.x) / CELL)
				var r := int((mb.position.y - g.position.y) / CELL)
				var i := (scroll + r) * _cols() + c
				if c < _cols() and i >= 0 and i < _shown().size():
					sel = i
					_append()
			accept_event()
			queue_redraw()
			return
		accept_event()
		return

	if not (e is InputEventKey) or not e.pressed:
		return
	var k := e as InputEventKey

	if searching:
		match k.keycode:
			KEY_ESCAPE:
				searching = false
				query = ""
				hits = []
			KEY_ENTER, KEY_KP_ENTER:
				_run_search()
				sel = 0
				scroll = 0
			KEY_BACKSPACE:
				query = query.substr(0, maxi(0, query.length() - 1))
				_run_search()
				sel = 0
				scroll = 0
			_:
				var u := k.unicode
				if u >= 32:
					query += String.chr(u)
					_run_search()
					sel = 0
					scroll = 0
		accept_event()
		queue_redraw()
		return

	var per := _cols()
	match k.keycode:
		KEY_SLASH:
			searching = true
			query = ""
			hits = []
		KEY_LEFT: sel = maxi(0, sel - 1)
		KEY_RIGHT: sel = mini(maxi(0, _shown().size() - 1), sel + 1)
		KEY_UP: sel = maxi(0, sel - per)
		KEY_DOWN: sel = mini(maxi(0, _shown().size() - 1), sel + per)
		KEY_HOME: sel = 0
		KEY_END: sel = maxi(0, _shown().size() - 1)
		KEY_PAGEUP:
			block = posmod(block - 1, BLOCKS.size())
			searching = false
			hits = []
			_load_block()
		KEY_PAGEDOWN:
			block = posmod(block + 1, BLOCKS.size())
			searching = false
			hits = []
			_load_block()
		KEY_ENTER, KEY_KP_ENTER, KEY_SPACE:
			_append()
		KEY_BACKSPACE:
			buffer = buffer.substr(0, maxi(0, buffer.length() - 1))
		KEY_DELETE:
			buffer = ""
		KEY_C:
			if buffer != "":
				DisplayServer.clipboard_set(buffer)
				copied = 1.6
		_:
			return
	_ensure_visible()
	accept_event()
	queue_redraw()


# A character the font cannot draw still goes into the buffer if you ask for
# it: the clipboard is bytes, and the machine you paste into may have a better
# font than this one. What it must not do is claim to have SHOWN it to you.
func _append() -> void:
	var s := _shown()
	if sel >= 0 and sel < s.size():
		buffer += String.chr(int(s[sel]))
		if buffer.length() > 200:
			buffer = buffer.substr(buffer.length() - 200)


# ---------------------------------------------------------------- drawing

func _fit(t: String, w: float, fs: int) -> String:
	if mono.get_string_size(t, HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x <= w:
		return t
	var s := t
	while s.length() > 1 and \
			mono.get_string_size(s + "...", HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x > w:
		s = s.substr(0, s.length() - 1)
	return s + "..."


func _hex(c: int) -> String:
	var h := String.num_int64(c, 16).to_upper()
	while h.length() < 4:
		h = "0" + h
	return "U+" + h


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), CHROME)
	_draw_search()
	_draw_blockbar()
	_draw_grid()
	_draw_foot()


func _draw_search() -> void:
	draw_rect(Rect2(0, 0, size.x, TOP), Color("#e4e4e4"))
	draw_line(Vector2(0, TOP), Vector2(size.x, TOP), Color("#b9bfc6"))
	var box := Rect2(6, 3, size.x - 12.0, TOP - 6.0)
	draw_rect(box, WHITE if searching else Color("#efeeec"))
	draw_rect(box, SEL if searching else Color("#b9bfc6"), false, 1.0)
	var t := query + ("_" if searching else "")
	if t == "":
		t = "/ to search: a character, U+00E9, 233, or a block name"
	draw_string(mono, Vector2(11, 16), _fit(t, box.size.x - 10.0, 10),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 10, INK if query != "" else FAINT)


func _draw_blockbar() -> void:
	var y := TOP
	draw_rect(Rect2(0, y, size.x, BLK_H), Color("#eceae7"))
	draw_line(Vector2(0, y + BLK_H), Vector2(size.x, y + BLK_H), Color("#cfccc7"))
	var b: Array = BLOCKS[block]
	var have := _covered(block)
	var span: int = int(b[1]) - int(b[0]) + 1
	draw_string(mono, Vector2(14, y + 13), "<", HORIZONTAL_ALIGNMENT_LEFT, -1, 11, DIM)
	draw_string(mono, Vector2(-14, y + 13), ">", HORIZONTAL_ALIGNMENT_RIGHT,
		size.x, 11, DIM)
	var label: String = str(b[2])
	if searching and not hits.is_empty():
		label = "search: %d matches" % hits.size()
	elif searching:
		label = "search: nothing matched"
	else:
		label += "   %d/%d in this font" % [have, span]
	draw_string(mono, Vector2(0, y + 13), _fit(label, size.x - 60.0, 11),
		HORIZONTAL_ALIGNMENT_CENTER, size.x, 11,
		INK if (have > 0 or searching) else RED)


func _draw_grid() -> void:
	var g := _grid_rect()
	draw_rect(g, WHITE)
	var s := _shown()
	var cols := _cols()
	var first := scroll * cols
	var last := mini(s.size(), first + _rows_visible() * cols)
	for i in range(first, last):
		var c: int = int(s[i])
		var col := (i - first) % cols
		var row := (i - first) / cols
		var r := Rect2(g.position.x + col * CELL, g.position.y + row * CELL,
			CELL - 1.0, CELL - 1.0)
		var ok := _present(c)
		if i == sel:
			draw_rect(r, Color("#dbe7f6"))
			draw_rect(r, SEL, false, 1.0)
		elif not ok:
			draw_rect(r, Color("#f2f1ef"))
		if ok:
			draw_string(mono, Vector2(r.position.x, r.position.y + CELL - 6.0),
				String.chr(c), HORIZONTAL_ALIGNMENT_CENTER, r.size.x, 14, INK)
		else:
			# A diagonal, drawn by us. Not a glyph -- see the header: the
			# font's own "missing" box is itself a character and would lie.
			draw_line(r.position + Vector2(5, 5),
				r.position + Vector2(r.size.x - 5, r.size.y - 5), MISS, 1.0)
	if s.is_empty():
		var msg := "nothing matched"
		if searching:
			msg = "nothing matched -- there is no Unicode name table here to search"
		draw_string(mono, Vector2(g.position.x + 8, g.position.y + 18),
			_fit(msg, g.size.x - 16.0, 10), HORIZONTAL_ALIGNMENT_LEFT, -1, 10, DIM)


func _draw_foot() -> void:
	var y := size.y - BOT
	draw_rect(Rect2(0, y, size.x, BOT), Color("#e6e3de"))
	draw_line(Vector2(0, y), Vector2(size.x, y), Color("#b3b0ab"))

	var s := _shown()
	var line := "nothing selected"
	var col := DIM
	if sel >= 0 and sel < s.size():
		var c: int = int(s[sel])
		var ok := _present(c)
		line = "%s   decimal %d   %s" % [_hex(c), c,
			("in this font" if ok else "NOT IN THIS FONT -- it would draw as a box")]
		if not ok:
			col = RED
		else:
			col = INK
	draw_string(mono, Vector2(8, y + 12), _fit(line, size.x - 16.0, 10),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 10, col)

	var bl := "buffer: " + buffer
	if buffer == "":
		bl = "buffer: (empty)  -- click or enter to add, backspace removes, C copies"
	draw_string(mono, Vector2(8, y + 25), _fit(bl, size.x - 70.0, 11),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 11, INK if buffer != "" else FAINT)
	if copied > 0.0:
		draw_string(mono, Vector2(-8, y + 25), "copied",
			HORIZONTAL_ALIGNMENT_RIGHT, size.x, 10, GREEN)

	draw_string(mono, Vector2(8, y + 38),
		_fit("PgUp/PgDn block   / search   C copies to YOUR clipboard; the machine has none",
			size.x - 16.0, 9), HORIZONTAL_ALIGNMENT_LEFT, -1, 9, Color("#7c8085"))
