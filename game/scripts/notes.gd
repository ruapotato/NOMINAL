# notes.gd — the notepad, and it is a FILE.
#
# David: "You can take notes on your computer, via GUI or command line, and
# view them vice versa."
#
# So there is no note store in the desktop. This window edits
# /root/notes.txt on your workstation through the machine's own shell, which
# means `cat /root/notes.txt` in a terminal shows what you typed here, and
# `echo "check the fstab" >> /root/notes.txt` shows up here. One file, two
# ways in -- which is the only arrangement that cannot drift.

extends Control

var mono: Font
var machine: Object = null
var lines: PackedStringArray = []
var cur := ""
var caret := 0
var blink := 0.0
const PATH := "/root/notes.txt"
const LINE_H := 15


func _ready() -> void:
	focus_mode = Control.FOCUS_ALL
	mouse_filter = Control.MOUSE_FILTER_STOP
	if mono == null:
		mono = ThemeDB.fallback_font
	set_process(true)
	refresh()


func take_focus() -> void:
	grab_focus()


func refresh() -> void:
	var t: String = machine.sh_on(0, "cat " + PATH)
	if t.find("cannot read") >= 0 or t.find("not found") >= 0:
		t = ""
	lines = t.split("\n")
	queue_redraw()


func _append(s: String) -> void:
	# Through the shell, so the file is the only state.
	machine.sh_on(0, 'echo "%s" >> %s' % [s.replace('"', "'"), PATH])
	refresh()


func _process(dt: float) -> void:
	blink += dt
	if blink > 0.5:
		blink -= 0.5
		queue_redraw()


func _gui_input(e: InputEvent) -> void:
	if e is InputEventMouseButton and e.pressed:
		grab_focus()
		return
	if not (e is InputEventKey) or not e.pressed:
		return
	var k := e as InputEventKey
	accept_event()
	match k.keycode:
		KEY_ENTER, KEY_KP_ENTER:
			if cur.strip_edges() != "":
				_append(cur)
			cur = ""
			caret = 0
			queue_redraw(); return
		KEY_BACKSPACE:
			if caret > 0:
				cur = cur.substr(0, caret - 1) + cur.substr(caret)
				caret -= 1
			queue_redraw(); return
		KEY_LEFT:
			caret = max(0, caret - 1); queue_redraw(); return
		KEY_RIGHT:
			caret = min(cur.length(), caret + 1); queue_redraw(); return
	var ch := char(k.unicode)
	if k.unicode >= 32 and ch != "":
		cur = cur.insert(caret, ch)
		caret += 1
		queue_redraw()


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color("#fffef4"))
	draw_rect(Rect2(0, 0, size.x, 20), Color("#efeee0"))
	draw_string(mono, Vector2(8, 15), PATH + "   (also `cat` it from a terminal)",
		HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color("#6a6a55"))

	var y := 38.0
	var visible := int((size.y - 56) / LINE_H)
	var first: int = max(0, lines.size() - visible)
	for i in range(first, lines.size()):
		if y > size.y - 26:
			break
		draw_line(Vector2(6, y + 3), Vector2(size.x - 6, y + 3), Color("#e6e4d2"))
		draw_string(mono, Vector2(10, y), lines[i],
			HORIZONTAL_ALIGNMENT_LEFT, size.x - 20, 12, Color("#2a2a20"))
		y += LINE_H

	var iy := size.y - 8
	draw_line(Vector2(6, iy - 16), Vector2(size.x - 6, iy - 16), Color("#d8d6c4"))
	draw_string(mono, Vector2(10, iy), cur,
		HORIZONTAL_ALIGNMENT_LEFT, -1, 12, Color("#2a2a20"))
	if has_focus() and blink < 0.25:
		var cw := mono.get_string_size(cur.substr(0, caret),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 12).x
		draw_rect(Rect2(10 + cw, iy - 11, 7, 13), Color("#2a2a20"))
