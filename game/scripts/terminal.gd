# terminal.gd — a terminal, not a text box with a log above it.
#
# David: "The terminal especially is a piece of junk. I don't want a history
# scroll back and a stagnant input line. I want it to resemble a real
# terminal. When you type, it goes into the actual terminal space."
#
# So there is no LineEdit anywhere. This control owns the screen: the
# transcript and the line being typed are the same buffer, drawn by the same
# code, with one block cursor sitting where the next character will land. Keys
# arrive through _gui_input and are handled here -- printable characters,
# backspace, left and right, home and end, and a command history on the up and
# down arrows, which is the thing you miss within about ten seconds of not
# having it.
#
# The control never interprets a command. It hands the line to on_command and
# prints whatever comes back, so this file cannot know anything the machine
# does not.

extends Control

var mono: Font
var bg := Color("#0b0e13")
var fg := Color("#cfd8e3")
var accent := Color("#6fdc96")

# Set by the desktop.
var on_command: Callable = func(_s: String) -> String: return ""
var prompt_fn: Callable = func() -> String: return "$ "

# Set by whoever opens the window, because a terminal on YOUR workstation and
# a console on somebody else's machine are not the same thing and must not
# claim to be.
var banner: PackedStringArray = []
var lines: PackedStringArray = []
var cur := ""              # the line being typed
var caret := 0             # where in it the cursor is
var history: PackedStringArray = []
var hpos := -1
var scroll := 0            # how many lines up from the bottom we are looking
var blink := 0.0
var busy := false
# A COMMAND THAT IS NOT FINISHED YET.
#
# Typing `for i in dev sys proc` and pressing enter answered "expected do",
# so you could not type the chroot line the way the boot output prints it --
# across lines, like every shell on earth. If a line opens a construct, the
# terminal keeps it and shows a continuation prompt instead of running it.
var pending := ""

const LINE_H := 15
const PAD := 6
const MAX_LINES := 4000


func _ready() -> void:
	if lines.is_empty():
		lines = banner.duplicate()
	focus_mode = Control.FOCUS_ALL
	mouse_filter = Control.MOUSE_FILTER_STOP
	if mono == null:
		mono = ThemeDB.fallback_font
	set_process(true)


func take_focus() -> void:
	grab_focus()


func _process(dt: float) -> void:
	blink += dt
	if blink > 0.5:
		blink -= 0.5
		queue_redraw()


# ------------------------------------------------------------------ output --

func write(s: String) -> void:
	if s == "":
		return
	var parts := s.split("\n")
	for i in range(parts.size()):
		if i == parts.size() - 1 and parts[i] == "":
			break
		lines.append(parts[i])
	_trim()
	scroll = 0
	queue_redraw()


func _trim() -> void:
	while lines.size() > MAX_LINES:
		lines.remove_at(0)


# Type a whole string as though at the keyboard, for tests and screenshots.
func feed(s: String) -> void:
	for ch in s:
		if ch == "\n":
			_enter()
		else:
			cur = cur.insert(caret, ch)
			caret += 1
	queue_redraw()


func _incomplete(s2: String) -> bool:
	var t := s2.strip_edges()
	if t.ends_with("\\"):
		return true
	# `for` is open until `done`; a trailing `do` or `;` is also unfinished.
	var has_for := t.begins_with("for ") or t.find("; for ") >= 0
	if has_for and t.find("done") < 0:
		return true
	if t.ends_with(";") or t.ends_with("do") or t.ends_with("&&") or t.ends_with("||"):
		return true
	return false


func _enter() -> void:
	var line := cur
	lines.append((prompt_fn.call() if pending == "" else "> ") + line)
	cur = ""
	caret = 0

	# Join it to whatever came before, and if the whole thing is still open,
	# ask for more rather than running a fragment.
	var whole := line if pending == "" else pending + "; " + line
	if whole.strip_edges().ends_with("\\"):
		whole = whole.strip_edges().substr(0, whole.strip_edges().length() - 1)
	if _incomplete(whole):
		pending = whole
		queue_redraw()
		return
	pending = ""
	line = whole
	if line.strip_edges() != "":
		history.append(line)
	hpos = -1
	_trim()
	queue_redraw()

	busy = true
	var out: String = on_command.call(line)
	busy = false
	write(out)


# ------------------------------------------------------------------- input --

func _gui_input(e: InputEvent) -> void:
	if e is InputEventMouseButton and e.pressed:
		grab_focus()
		if e.button_index == MOUSE_BUTTON_WHEEL_UP:
			scroll = min(scroll + 3, max(0, lines.size() - 4))
			queue_redraw()
		elif e.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			scroll = max(scroll - 3, 0)
			queue_redraw()
		return

	if not (e is InputEventKey) or not e.pressed:
		return
	var k := e as InputEventKey
	accept_event()

	match k.keycode:
		KEY_ENTER, KEY_KP_ENTER:
			_enter()
			return
		KEY_BACKSPACE:
			if caret > 0:
				cur = cur.substr(0, caret - 1) + cur.substr(caret)
				caret -= 1
			queue_redraw(); return
		KEY_DELETE:
			if caret < cur.length():
				cur = cur.substr(0, caret) + cur.substr(caret + 1)
			queue_redraw(); return
		KEY_LEFT:
			caret = max(0, caret - 1); queue_redraw(); return
		KEY_RIGHT:
			caret = min(cur.length(), caret + 1); queue_redraw(); return
		KEY_HOME:
			caret = 0; queue_redraw(); return
		KEY_END:
			caret = cur.length(); queue_redraw(); return
		KEY_UP:
			if history.size() > 0:
				hpos = history.size() - 1 if hpos < 0 else max(0, hpos - 1)
				cur = history[hpos]
				caret = cur.length()
			queue_redraw(); return
		KEY_DOWN:
			if hpos >= 0:
				hpos += 1
				if hpos >= history.size():
					hpos = -1
					cur = ""
				else:
					cur = history[hpos]
				caret = cur.length()
			queue_redraw(); return
		KEY_PAGEUP:
			scroll = min(scroll + 10, max(0, lines.size() - 4)); queue_redraw(); return
		KEY_PAGEDOWN:
			scroll = max(scroll - 10, 0); queue_redraw(); return
		KEY_U:
			if k.ctrl_pressed:
				cur = cur.substr(caret)
				caret = 0
				queue_redraw(); return
		KEY_C:
			if k.ctrl_pressed:
				lines.append(prompt_fn.call() + cur + "^C")
				cur = ""; caret = 0
				queue_redraw(); return

	# TAB AND FRIENDS ARE NOT TEXT. Godot reports Tab with unicode 0, and the
	# old guard let anything >= 32 through -- but the keycode branch below
	# never ran for Tab, so a NUL went into the line buffer and Godot then
	# refused to render it: "Unicode parsing error... Unexpected NUL
	# character". Filter on the CODE POINT being printable, not on the key.
	if k.keycode == KEY_TAB:
		# No completion yet; at least do not corrupt the line.
		accept_event()
		return
	var ch := char(k.unicode)
	if k.unicode >= 32 and k.unicode != 127 and ch != "" and ch != "\u0000":
		cur = cur.insert(caret, ch)
		caret += 1
		scroll = 0
		queue_redraw()


# ------------------------------------------------------------------ render --

func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), bg)

	var rows := int((size.y - PAD * 2) / LINE_H)
	if rows < 1:
		return

	# The prompt line is part of the screen, not a separate widget below it.
	var screen: PackedStringArray = lines.duplicate()
	var prompt: String = "> " if pending != "" else prompt_fn.call()
	screen.append(prompt + cur)

	var last := screen.size() - scroll
	var first := max(0, last - rows)
	var y := PAD + LINE_H
	for i in range(first, last):
		var line := screen[i]
		var col := fg
		if i == screen.size() - 1 and scroll == 0:
			# the line being typed: prompt in the accent colour
			draw_string(mono, Vector2(PAD, y), prompt,
				HORIZONTAL_ALIGNMENT_LEFT, -1, 13, accent)
			var pw := mono.get_string_size(prompt, HORIZONTAL_ALIGNMENT_LEFT, -1, 13).x
			draw_string(mono, Vector2(PAD + pw, y), cur,
				HORIZONTAL_ALIGNMENT_LEFT, -1, 13, fg)
			if has_focus() and blink < 0.25:
				var cw := mono.get_string_size(cur.substr(0, caret),
					HORIZONTAL_ALIGNMENT_LEFT, -1, 13).x
				var w := mono.get_string_size("M", HORIZONTAL_ALIGNMENT_LEFT, -1, 13).x
				draw_rect(Rect2(PAD + pw + cw, y - 11, w, 14), fg)
				if caret < cur.length():
					draw_string(mono, Vector2(PAD + pw + cw, y), cur[caret],
						HORIZONTAL_ALIGNMENT_LEFT, -1, 13, bg)
		else:
			if line.find("cannot") >= 0 or line.find("fail") >= 0 \
			   or line.find("not found") >= 0 or line.find("refusing") >= 0:
				col = Color("#e06c75")
			elif line.begins_with(prompt) or line.find("# ") == 0:
				col = accent
			draw_string(mono, Vector2(PAD, y), line,
				HORIZONTAL_ALIGNMENT_LEFT, -1, 13, col)
		y += LINE_H

	if scroll > 0:
		var note := "-- scrolled back %d lines, PageDown to return --" % scroll
		draw_string(mono, Vector2(PAD, size.y - 4), note,
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color("#d3b06a"))
