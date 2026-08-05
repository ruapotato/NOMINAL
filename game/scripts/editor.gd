# editor.gd — a text editor, because clicking a .txt should open one.
#
# It reads and writes through the machine's own shell, so a file saved here is
# a file `cat` shows and `pkg verify` notices. There is no editor buffer that
# can disagree with the disk -- which is the same rule the notes app and the
# file browser follow, and the only rule that keeps a desktop honest.

extends Control

var mono: Font
var machine: Object = null
var path := ""
var lines: PackedStringArray = []
var row := 0
var col := 0
var dirty := false
var msg := ""
var blink := 0.0
const LINE_H := 15


func _ready() -> void:
	focus_mode = Control.FOCUS_ALL
	mouse_filter = Control.MOUSE_FILTER_STOP
	if mono == null:
		mono = preload("res://scripts/uifont.gd").mono()
	set_process(true)
	var t: String = machine.sh_on(0, "cat " + path)
	if t.find("cannot read") >= 0 or t.find("not found") >= 0:
		t = ""
	lines = t.split("\n")
	if lines.is_empty():
		lines = PackedStringArray([""])


func take_focus() -> void:
	grab_focus()


func _process(dt: float) -> void:
	blink += dt
	if blink > 0.5:
		blink -= 0.5
		queue_redraw()


func _save() -> void:
	# Rewrite the file line by line through the shell: truncate with `>` then
	# append the rest. Slow and completely honest.
	var first := true
	for l in lines:
		var safe := l.replace('"', "'")
		machine.sh_on(0, 'echo "%s" %s %s' % [safe, ">" if first else ">>", path])
		first = false
	dirty = false
	msg = "saved " + path
	queue_redraw()


func _gui_input(e: InputEvent) -> void:
	if e is InputEventMouseButton and e.pressed:
		grab_focus()
		var r := int((e.position.y - 30) / LINE_H)
		if r >= 0 and r < lines.size():
			row = r
			col = min(col, lines[row].length())
		queue_redraw()
		return
	if not (e is InputEventKey) or not e.pressed:
		return
	var k := e as InputEventKey
	accept_event()
	if k.ctrl_pressed and k.keycode == KEY_S:
		_save(); return
	match k.keycode:
		KEY_TAB:
			return
		KEY_ENTER, KEY_KP_ENTER:
			var rest := lines[row].substr(col)
			lines[row] = lines[row].substr(0, col)
			lines.insert(row + 1, rest)
			row += 1; col = 0; dirty = true
		KEY_BACKSPACE:
			if col > 0:
				lines[row] = lines[row].substr(0, col - 1) + lines[row].substr(col)
				col -= 1
			elif row > 0:
				col = lines[row - 1].length()
				lines[row - 1] += lines[row]
				lines.remove_at(row)
				row -= 1
			dirty = true
		KEY_UP:
			row = max(0, row - 1); col = min(col, lines[row].length())
		KEY_DOWN:
			row = min(lines.size() - 1, row + 1); col = min(col, lines[row].length())
		KEY_LEFT:
			col = max(0, col - 1)
		KEY_RIGHT:
			col = min(lines[row].length(), col + 1)
		KEY_HOME:
			col = 0
		KEY_END:
			col = lines[row].length()
		_:
			var ch := char(k.unicode)
			if k.unicode >= 32 and k.unicode != 127 and ch != "":
				lines[row] = lines[row].substr(0, col) + ch + lines[row].substr(col)
				col += 1
				dirty = true
	queue_redraw()


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color("#ffffff"))
	draw_rect(Rect2(0, 0, size.x, 22), Color("#e4e4e4"))
	draw_line(Vector2(0, 22), Vector2(size.x, 22), Color("#b9bfc6"))
	draw_string(mono, Vector2(8, 16),
		("* " if dirty else "") + path + "    ctrl-S saves",
		HORIZONTAL_ALIGNMENT_LEFT, size.x - 16, 11, Color("#1b1b1b"))
	if msg != "":
		draw_string(mono, Vector2(size.x - 190, 16), msg,
			HORIZONTAL_ALIGNMENT_LEFT, -1, 10, Color("#2f7a3f"))

	var vis := int((size.y - 34) / LINE_H)
	var first: int = clampi(row - vis / 2, 0, max(0, lines.size() - vis))
	var y := 38.0
	for i in range(first, min(lines.size(), first + vis)):
		draw_string(mono, Vector2(38, y), lines[i],
			HORIZONTAL_ALIGNMENT_LEFT, size.x - 46, 12, Color("#1b2530"))
		draw_string(mono, Vector2(6, y), str(i + 1),
			HORIZONTAL_ALIGNMENT_RIGHT, 26, 10, Color("#a8b2bd"))
		if i == row and has_focus() and blink < 0.25:
			var cw := mono.get_string_size(lines[i].substr(0, col),
				HORIZONTAL_ALIGNMENT_LEFT, -1, 12).x
			draw_rect(Rect2(38 + cw, y - 11, 7, 13), Color("#1b2530"))
		y += LINE_H
