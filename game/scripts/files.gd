# files.gd — the file browser on YOUR workstation.
#
# David: "The filebrowser on your computer should match what your local
# terminal says." So it does not keep its own idea of anything: every listing
# is `ls -l` run through the machine's own shell, and the path bar is a real
# cwd you can type into. If the browser and the terminal ever disagree, one of
# them is lying, and it will not be this one.

extends Control

var mono: Font
var machine: Object = null
var path := "/"
var rows: PackedStringArray = []
var sel := 0
var view := ""          # file contents, when looking at one

const LINE_H := 15


func _ready() -> void:
	focus_mode = Control.FOCUS_ALL
	mouse_filter = Control.MOUSE_FILTER_STOP
	if mono == null:
		mono = ThemeDB.fallback_font
	refresh()


func take_focus() -> void:
	grab_focus()


func refresh() -> void:
	view = ""
	var out: String = machine.sh_on(0, "ls -l " + path)
	rows = out.split("\n")
	queue_redraw()


func _open(name: String) -> void:
	var t := (path if path.ends_with("/") else path + "/") + name
	var st: String = machine.sh_on(0, "stat " + t)
	if st.find("kind  dir") >= 0:
		path = t
		refresh()
	else:
		view = machine.sh_on(0, "cat " + t)
		queue_redraw()


func _gui_input(e: InputEvent) -> void:
	if e is InputEventMouseButton and e.pressed and e.button_index == MOUSE_BUTTON_LEFT:
		grab_focus()
		if e.position.y < 22:
			return
		var i := int((e.position.y - 26) / LINE_H)
		if i >= 0 and i < rows.size():
			sel = i
			var f: PackedStringArray = rows[i].split("\t")
			var nm := f[f.size() - 1].split(" -> ")[0]
			if nm.strip_edges() != "":
				_open(nm.strip_edges())
		return
	if e is InputEventKey and e.pressed:
		accept_event()
		if e.keycode == KEY_BACKSPACE:
			if view != "":
				view = ""
			elif path != "/":
				path = path.get_base_dir()
				refresh()
			queue_redraw()


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color("#ffffff"))
	draw_rect(Rect2(0, 0, size.x, 22), Color("#e4e4e4"))
	draw_line(Vector2(0, 22), Vector2(size.x, 22), Color("#b9bfc6"))
	draw_string(mono, Vector2(8, 16), path,
		HORIZONTAL_ALIGNMENT_LEFT, size.x - 16, 12, Color("#1b1b1b"))
	draw_string(mono, Vector2(size.x - 150, 16), "backspace = up",
		HORIZONTAL_ALIGNMENT_LEFT, -1, 10, Color("#6a737d"))

	if view != "":
		var y := 40.0
		for line in view.split("\n"):
			if y > size.y - 6:
				break
			draw_string(mono, Vector2(8, y), line,
				HORIZONTAL_ALIGNMENT_LEFT, -1, 12, Color("#22303f"))
			y += 14
		return

	var y2 := 38.0
	for i in range(rows.size()):
		if y2 > size.y - 6:
			break
		var line := rows[i]
		if line.strip_edges() == "":
			continue
		var isdir := line.begins_with("d")
		var islink := line.begins_with("l")
		if i == sel:
			draw_rect(Rect2(2, y2 - 11, size.x - 4, LINE_H), Color("#dbe7f6"))
		var col := Color("#1b4f8f") if isdir else (Color("#8a6d1f") if islink else Color("#22303f"))
		draw_string(mono, Vector2(8, y2), line.replace("\t", "  "),
			HORIZONTAL_ALIGNMENT_LEFT, size.x - 16, 12, col)
		y2 += LINE_H
