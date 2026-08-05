# chat.gd — the person on the phone, and the things you can say to them.
#
# This was three contacts and a text box, backed by a 3B language model. All
# three are gone. Measured reasons, from four blind playtests: a reply took
# 60-120 seconds and once nine minutes, which was the most-cited fun-killer in
# every report; the model shipped as 1.84 GB; and two of the three people were
# rated useless-to-harmful -- "an empty chair is less annoying than a colleague
# who repeats you".
#
# What made the customer good was never the prose. It was that she is an
# INSTRUMENT WITH REAL LIMITS: she can only see the last few lines of a screen,
# she will only type so much before losing her place, and she misreads things.
# Those are rules, rules are always true, and they answer instantly.
#
# So: one contact, and a list of what you can say. The list comes from the
# machine -- `customer_options` -- and changes with the state of the call, so it
# can never offer something that cannot work. One option opens a field and takes
# ANY command, because choosing the right command is the whole puzzle on a
# ticket where she is your only terminal.

extends Control

var mono: Font
var machine: Object = null
var ink := Color("#c9d3e0")
var dim := Color("#78849a")
var bg := Color("#1b212c")

# THE LIST HAS TO BE READABLE OR IT IS NOT A MENU.
#
# A fixed 210px clipped "ask her to read the whole screen back" to "...the
# whole s". The options are sentences, not verbs, so the column takes a share
# of the window and the labels wrap to a second line rather than being cut.
const LINE_H := 15
const ROW_H := 17.0

var cust_name := "the customer"
var log: Array = []          # [speaker, text]
var options: Array = []      # [id, label]
var sel := 0
# The one option that takes an argument opens this field. Everything else is
# a single click.
var typing := false
var typed := ""
var caret := 0
var type_for := -1
var blink := 0.0
var scroll := 0


func _ready() -> void:
	focus_mode = Control.FOCUS_ALL
	mouse_filter = Control.MOUSE_FILTER_STOP
	if mono == null:
		mono = ThemeDB.fallback_font
	set_process(true)
	refresh()


func take_focus() -> void:
	grab_focus()


# THE LIST IS THE MACHINE'S ANSWER, NOT A CONSTANT.
#
# It is asked again after every exchange, because what you can say depends on
# what is true right now: no point offering to take the disc out when the tray
# is empty. The one thing it always offers is dictating a command -- at a
# machine with no prompt she explains what she sees instead, which is more
# useful than the option quietly vanishing.
func refresh() -> void:
	if machine == null:
		return
	options = []
	for row in str(machine.customer_options()).split("\n"):
		var t := row.strip_edges()
		if not t.begins_with("["):
			continue
		var close := t.find("]")
		if close < 0:
			continue
		# The numbers are STABLE IDS, NOT POSITIONS -- the list is filtered,
		# never repacked, so option 2 is always the same option.
		var id := int(t.substr(1, close - 1))
		options.append([id, t.substr(close + 1).strip_edges()])
	sel = clampi(sel, 0, max(0, options.size() - 1))
	queue_redraw()


func reset(name: String) -> void:
	cust_name = name
	log = [["", "%s is on the line. They are not technical, and they are the only pair of hands in the room." % name]]
	scroll = 0
	typing = false
	typed = ""
	refresh()


func seed_first(text: String) -> void:
	log.append([cust_name, text])
	queue_redraw()


func say_from_customer(text: String) -> void:
	log.append([cust_name, text])
	scroll = 0
	queue_redraw()


func _process(dt: float) -> void:
	blink += dt
	if blink > 0.5:
		blink -= 0.5
		queue_redraw()


func _say(id: int, arg: String) -> void:
	if machine == null:
		return
	var label := ""
	for o in options:
		if o[0] == id:
			label = str(o[1])
	log.append(["you", ("can I have you run: " + arg) if arg != "" else label])
	# INSTANT. This used to run on a Thread, because the model took seconds and
	# doing it on the main thread froze the whole desktop -- the clock stopped
	# and the game looked crashed every time you said hello. There is nothing
	# to wait for now, so there is no thread to join on the way out either.
	var reply: String = str(machine.customer_choose(id, arg))
	log.append([cust_name, reply])
	scroll = 0
	refresh()


func _gui_input(e: InputEvent) -> void:
	if e is InputEventMouseButton and e.pressed:
		grab_focus()
		if e.button_index == MOUSE_BUTTON_WHEEL_UP:
			scroll += 3; queue_redraw(); return
		if e.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			scroll = max(0, scroll - 3); queue_redraw(); return
		if e.button_index == MOUSE_BUTTON_LEFT and e.position.x < _list_w():
			var geo := _rows()
			for i in range(geo.size()):
				if e.position.y >= geo[i][0] - 12 and e.position.y < geo[i][0] + geo[i][1]:
					sel = i
					_choose()
					break
		return

	if not (e is InputEventKey) or not e.pressed:
		return
	var k := e as InputEventKey
	accept_event()

	if typing:
		match k.keycode:
			KEY_ENTER, KEY_KP_ENTER:
				var cmd := typed
				typing = false
				typed = ""
				caret = 0
				if cmd.strip_edges() != "":
					_say(type_for, cmd)
				queue_redraw()
				return
			KEY_ESCAPE:
				typing = false; typed = ""; caret = 0; queue_redraw(); return
			KEY_BACKSPACE:
				if caret > 0:
					typed = typed.substr(0, caret - 1) + typed.substr(caret)
					caret -= 1
				queue_redraw(); return
			KEY_LEFT:
				caret = max(0, caret - 1); queue_redraw(); return
			KEY_RIGHT:
				caret = min(typed.length(), caret + 1); queue_redraw(); return
		var ch := char(k.unicode)
		if k.unicode >= 32 and k.unicode != 127 and ch != "":
			typed = typed.insert(caret, ch)
			caret += 1
			queue_redraw()
		return

	match k.keycode:
		KEY_UP:
			sel = max(0, sel - 1); queue_redraw(); return
		KEY_DOWN:
			sel = min(options.size() - 1, sel + 1); queue_redraw(); return
		KEY_ENTER, KEY_KP_ENTER:
			_choose(); return
	# A digit picks the option with that id, the way `ask <n>` does at a prompt.
	if k.unicode >= 48 and k.unicode <= 57:
		var want := k.unicode - 48
		for i in range(options.size()):
			if options[i][0] == want:
				sel = i
				_choose()
				return


func _choose() -> void:
	if sel < 0 or sel >= options.size():
		return
	var o: Array = options[sel]
	# The dictate option is the one that carries an argument, and the machine
	# marks it by putting a placeholder in the label.
	if str(o[1]).find("<command>") >= 0:
		typing = true
		typed = ""
		caret = 0
		type_for = o[0]
		queue_redraw()
		return
	_say(o[0], "")


# A share of the window, floored so it is usable and capped so the transcript
# keeps the room it needs.
func _list_w() -> float:
	return clampf(size.x * 0.34, 190.0, 320.0)


# Where each option sits, so the click test and the drawing cannot disagree --
# a bug this project has hit three times in other windows.
func _rows() -> Array:
	var out: Array = []
	var w: float = _list_w() - 30.0
	var y := 44.0
	for i in range(options.size()):
		var lines: PackedStringArray = _wrap(str(options[i][1]), w, 11)
		out.append([y, float(lines.size()) * ROW_H, lines])
		y += float(lines.size()) * ROW_H + 3.0
	return out


func _wrap(s: String, w: float, sz: int) -> PackedStringArray:
	var out: PackedStringArray = []
	var line := ""
	for word in s.split(" "):
		var t := word if line == "" else line + " " + word
		if mono.get_string_size(t, HORIZONTAL_ALIGNMENT_LEFT, -1, sz).x > w and line != "":
			out.append(line)
			line = word
		else:
			line = t
	if line != "":
		out.append(line)
	return out


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), bg)

	# --- what you can say ---
	var LIST_W := _list_w()
	draw_rect(Rect2(0, 0, LIST_W, size.y), Color("#151a23"))
	draw_line(Vector2(LIST_W, 0), Vector2(LIST_W, size.y), Color("#2b3444"))
	draw_string(mono, Vector2(8, 16), cust_name,
		HORIZONTAL_ALIGNMENT_LEFT, -1, 12, Color("#8fd6a4"))
	draw_string(mono, Vector2(8, 28), "on the phone",
		HORIZONTAL_ALIGNMENT_LEFT, -1, 10, dim)
	var geo := _rows()
	for i in range(options.size()):
		var top: float = geo[i][0]
		var h: float = geo[i][1]
		if top > size.y - 6:
			break
		if i == sel:
			draw_rect(Rect2(2, top - 12, LIST_W - 4, h + 2), Color("#2f6fb5"))
		draw_string(mono, Vector2(4, top), "%d" % options[i][0],
			HORIZONTAL_ALIGNMENT_RIGHT, 18, 10,
			ink if i == sel else Color("#5c6570"))
		var ly := top
		for line in (geo[i][2] as PackedStringArray):
			draw_string(mono, Vector2(26, ly), line,
				HORIZONTAL_ALIGNMENT_LEFT, -1, 11,
				ink if i == sel else Color("#a9b4c4"))
			ly += ROW_H

	# --- the call ---
	var x := LIST_W + 10
	var w: float = size.x - x - 18.0
	var rows: PackedStringArray = []
	var cols: PackedColorArray = []
	for entry in log:
		var speaker: String = entry[0]
		var text: String = entry[1]
		if speaker == "":
			for l in _wrap(text, w, 11):
				rows.append(l); cols.append(dim)
			rows.append(""); cols.append(dim)
			continue
		rows.append(("you" if speaker == "you" else speaker) + ":")
		cols.append(Color("#6fa8e8") if speaker == "you" else Color("#8fd6a4"))
		# Her replies carry the machine's own output indented under them. Keep
		# that shape rather than reflowing it: those lines are what the screen
		# said, and a wrapped console line is a lie about the screen.
		for raw in text.split("\n"):
			var t := raw.strip_edges()
			if t.begins_with("|"):
				rows.append("   " + t)
				cols.append(Color("#d3b06a"))
			elif t == "":
				continue
			else:
				for l in _wrap(t, w - 20, 12):
					rows.append("  " + l); cols.append(ink)
		rows.append(""); cols.append(dim)

	var visible := int((size.y - 34) / LINE_H)
	scroll = clampi(scroll, 0, max(0, rows.size() - visible))
	var first: int = max(0, rows.size() - visible - scroll)
	var y2 := 16.0
	for i in range(first, min(rows.size(), first + visible)):
		draw_string(mono, Vector2(x, y2), rows[i],
			HORIZONTAL_ALIGNMENT_LEFT, -1, 12, cols[i])
		y2 += LINE_H

	# --- the one field ---
	var iy := size.y - 8
	draw_line(Vector2(x, iy - 17), Vector2(size.x - 8, iy - 17), Color("#2b3444"))
	if typing:
		var lead := "can I have you run: "
		draw_string(mono, Vector2(x, iy), lead,
			HORIZONTAL_ALIGNMENT_LEFT, -1, 12, Color("#8fd6a4"))
		var lw := mono.get_string_size(lead, HORIZONTAL_ALIGNMENT_LEFT, -1, 12).x
		draw_string(mono, Vector2(x + lw, iy), typed,
			HORIZONTAL_ALIGNMENT_LEFT, -1, 12, ink)
		if has_focus() and blink < 0.25:
			var cw := mono.get_string_size(typed.substr(0, caret),
				HORIZONTAL_ALIGNMENT_LEFT, -1, 12).x
			draw_rect(Rect2(x + lw + cw, iy - 11, 7, 13), ink)
	else:
		draw_string(mono, Vector2(x, iy),
			"pick something to say -- arrows or the number, enter to say it",
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, dim)
