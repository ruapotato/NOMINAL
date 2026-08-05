# logview.gd — the log viewer, which is no longer the console in a second frame.
#
# A playtester's verdict on the old one: "it is the console window again, in a
# second window, with no filtering, no severity, no search. Either give it
# grep/level filtering or delete it." They were right -- it was literally a
# terminal.gd with the prompt taken out, showing the same text the console
# window beside it was already showing. A second copy of a thing is worth less
# than nothing, because you have to check whether it is the same copy.
#
# What a log viewer is FOR is finding the one line that matters in five hundred
# that do not. So this one does the three things the console cannot:
#
#   / filters      substring, live, case-insensitive, and the match is
#                  highlighted in the line. This is `grep` and it says so.
#   levels         w shows warnings and worse, e shows errors only, a shows
#                  everything, and the counts are in the header so you can see
#                  what you are hiding.
#   sources        s cycles console / previous boot off their disk / their
#                  /var/log/messages. The console window has exactly one.
#
# SEVERITY IS A GUESS AND THE WINDOW SAYS SO. NomnixOS does not stamp a level
# on a log line -- there is no `<3>` and no `err:` field, and this viewer will
# not invent one, because a line coloured red on no evidence sends you after a
# fault that is not there. What it does instead is match the words the system's
# own tools print when they fail, and the legend at the bottom names them, so a
# line's colour is a claim you can check. An unmatched line stays plain: not
# "INFO", just unclassified.
#
# It reads through machine.sh_on() like everything else here. It runs no
# command the player could not type, and it shows the command in the header.

extends Control

var mono: Font
var machine: Object = null
var addr := ""
var cust := "the customer"

const BG    := Color("#12161c")
const FG    := Color("#c3ccd6")
const DIMC  := Color("#6d7885")
const HEAD  := Color("#1b2129")
const RED   := Color("#e06c75")
const AMBER := Color("#d3b06a")
const GREEN := Color("#4fb06a")
const HIT   := Color("#3b4c2a")

const TOP := 20.0
const BOT := 34.0
const LINE_H := 14.0
const PAD := 6.0

# The words the tools in this system actually use when something went wrong.
# Kept here, in one list, because the legend at the bottom of the window prints
# from it -- a rule the player cannot read is a rule they cannot trust.
const ERR_WORDS := ["cannot", "failed", "fail", "no such", "not found",
	"refus", "panic", "oom", "corrupt", "unreadable", "no account",
	"read-only", "denied", "timed out", "aborted", "unknown"]
const WARN_WORDS := ["warn", "retry", "missing", "degraded", "skipped",
	"stale", "no boot log", "not mounted"]
const OK_WORDS := ["started", "mounted", "ok", "up", "listening"]

var raw: PackedStringArray = []
var src := 0                 # which source we are showing
var sources: Array = []      # [{label, cmd, text}]
var filter := ""
var typing := false
var level := 0               # 0 everything, 1 warnings and worse, 2 errors
var scroll := 0              # lines up from the bottom
var blink := 0.0


func _ready() -> void:
	focus_mode = Control.FOCUS_ALL
	mouse_filter = Control.MOUSE_FILTER_STOP
	if mono == null:
		mono = preload("res://scripts/uifont.gd").mono()
	set_process(true)
	refresh()


func take_focus() -> void:
	grab_focus()


func _process(dt: float) -> void:
	if not typing:
		return
	blink += dt
	if blink > 0.5:
		blink -= 0.5
		queue_redraw()


# ------------------------------------------------------------------ reading --

func _sh(cmd: String) -> String:
	if machine == null:
		return ""
	# Always from YOUR workstation: the console and the mounted disk are both
	# things your machine can reach. Asking the customer's box for its own boot
	# log needs it to have booted, which is the case you are here about.
	return str(machine.sh_on(0, cmd))


# Every source that has something in it today. The list is rebuilt on every
# refresh because it changes under you: attach the console and one appears,
# mount their disk and another does.
func refresh() -> void:
	sources = []
	var con := _sh("rcon console")
	if con.strip_edges() != "" and con.find("not attached") < 0:
		sources.append({"label": "console of %s (%s)" % [addr, cust],
			"cmd": "rcon console", "text": con})
	var prev := _sh("dmesg -r /mnt -1")
	if prev.strip_edges() != "" and prev.find("no boot log") < 0:
		sources.append({"label": "their previous boot, off the disk at /mnt",
			"cmd": "dmesg -r /mnt -1", "text": prev})
	var msg := _sh("tail -n 400 /mnt/var/log/messages")
	if msg.strip_edges() != "" and msg.find("cannot read") < 0:
		sources.append({"label": "their /var/log/messages (last 400 lines)",
			"cmd": "tail -n 400 /mnt/var/log/messages", "text": msg})
	src = clampi(src, 0, maxi(0, sources.size() - 1))
	raw = PackedStringArray()
	if not sources.is_empty():
		raw = str(sources[src]["text"]).split("\n")
		while raw.size() > 0 and str(raw[raw.size() - 1]).strip_edges() == "":
			raw.remove_at(raw.size() - 1)
	queue_redraw()


# ---------------------------------------------------------------- severity --

# 2 error, 1 warning, 0 unclassified. Whichever word matches first wins, and
# error beats warning, because "cannot" in a line that also says "retry" is
# still a thing that did not happen.
func _sev(line: String) -> int:
	var l := line.to_lower()
	for w in ERR_WORDS:
		if l.find(w) >= 0:
			return 2
	for w in WARN_WORDS:
		if l.find(w) >= 0:
			return 1
	return 0


func _colour(line: String) -> Color:
	match _sev(line):
		2: return RED
		1: return AMBER
	var l := line.to_lower()
	for w in OK_WORDS:
		if l.find(w + " ") >= 0 or l.ends_with(" " + w):
			return GREEN
	return FG


# What is on screen after the filter and the level: the lines, and for each one
# the index it had in the file, because "line 214 of 523" is what you write in
# a ticket.
func _shown() -> Array:
	var out: Array = []
	var f := filter.to_lower()
	for i in range(raw.size()):
		var line := str(raw[i])
		if level > 0 and _sev(line) < (2 if level == 2 else 1):
			continue
		if f != "" and line.to_lower().find(f) < 0:
			continue
		out.append([i, line])
	return out


# ------------------------------------------------------------------- input --

func _gui_input(e: InputEvent) -> void:
	if e is InputEventMouseButton and e.pressed:
		grab_focus()
		var mb := e as InputEventMouseButton
		if mb.button_index == MOUSE_BUTTON_WHEEL_UP:
			scroll += 3; queue_redraw(); return
		if mb.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			scroll = maxi(0, scroll - 3); queue_redraw(); return
		return

	if not (e is InputEventKey) or not e.pressed:
		return
	var k := e as InputEventKey
	accept_event()

	if typing:
		match k.keycode:
			KEY_ESCAPE:
				# Esc leaves the box AND drops the filter -- the two-step
				# version, where the text is still filtering after you have
				# left the box, is how you end up reading a log with lines
				# missing and no idea why.
				filter = ""
				typing = false
			KEY_ENTER, KEY_KP_ENTER:
				typing = false
			KEY_BACKSPACE:
				if filter != "":
					filter = filter.substr(0, filter.length() - 1)
			_:
				if k.unicode >= 32:
					filter += char(k.unicode)
		scroll = 0
		queue_redraw()
		return

	match k.keycode:
		KEY_SLASH:
			typing = true
			blink = 0.0
		KEY_A:
			level = 0
		KEY_W:
			level = 1
		KEY_E:
			level = 2
		KEY_S:
			if sources.size() > 1:
				src = (src + 1) % sources.size()
				scroll = 0
				refresh()
		KEY_R, KEY_F5:
			refresh()
		KEY_ESCAPE:
			filter = ""
			level = 0
		KEY_PAGEUP:
			scroll += _rows()
		KEY_PAGEDOWN:
			scroll = maxi(0, scroll - _rows())
		KEY_HOME:
			scroll = maxi(0, _shown().size() - _rows())
		KEY_END:
			scroll = 0
		_:
			return
	queue_redraw()


# ----------------------------------------------------------------- drawing --

func _rows() -> int:
	return maxi(1, int((size.y - TOP - BOT) / LINE_H))


func _fit(t: String, w: float, fs: int) -> String:
	if mono.get_string_size(t, HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x <= w:
		return t
	var s := t
	while s.length() > 1 and mono.get_string_size(s + "...",
			HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x > w:
		s = s.substr(0, s.length() - 1)
	return s + "..."


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), BG)
	draw_rect(Rect2(0, 0, size.x, TOP), HEAD)

	if sources.is_empty():
		draw_string(mono, Vector2(PAD, 14), "log viewer -- nothing to read yet",
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, FG)
		var y0 := TOP + 18.0
		for l in [
				"this window follows the CUSTOMER's machine, not yours.",
				"",
				"  `rcon connect %s` attaches to their console, or" % addr,
				"  mount their disk and this reads their last boot and",
				"  their /var/log/messages off it.",
				"",
				"R re-reads. it is empty because all three of those",
				"commands came back empty, not because it did not look."]:
			draw_string(mono, Vector2(PAD, y0), str(l),
				HORIZONTAL_ALIGNMENT_LEFT, -1, 12, DIMC)
			y0 += LINE_H
		return

	# The header names the source AND the command that produced it, so the
	# window can be checked against a terminal.
	var s: Dictionary = sources[src]
	draw_string(mono, Vector2(PAD, 14),
		_fit("%s   [%s]" % [s["label"], s["cmd"]], size.x - 150.0, 11),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 11, FG)

	var shown := _shown()
	var errs := 0
	var warns := 0
	for line in raw:
		var sv := _sev(str(line))
		if sv == 2:
			errs += 1
		elif sv == 1:
			warns += 1
	draw_string(mono, Vector2(size.x - 144, 14),
		"%d err  %d warn  %d/%d" % [errs, warns, shown.size(), raw.size()],
		HORIZONTAL_ALIGNMENT_RIGHT, 138, 11,
		RED if errs > 0 else (AMBER if warns > 0 else DIMC))

	var rows := _rows()
	scroll = clampi(scroll, 0, maxi(0, shown.size() - rows))
	var first: int = maxi(0, shown.size() - rows - scroll)
	var y := TOP + 12.0
	var f := filter.to_lower()
	for i in range(first, mini(shown.size(), first + rows)):
		var row: Array = shown[i]
		var line: String = str(row[1])
		# The line number in the FILE, not in the filtered view: the number you
		# would quote is the number the file has.
		draw_string(mono, Vector2(PAD, y), "%5d" % (int(row[0]) + 1),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, DIMC)
		var x := PAD + 42.0
		if f != "":
			var at := line.to_lower().find(f)
			if at >= 0:
				var cw := mono.get_string_size("M", HORIZONTAL_ALIGNMENT_LEFT,
					-1, 12).x
				draw_rect(Rect2(x + at * cw, y - 10, filter.length() * cw, 13),
					HIT)
		draw_string(mono, Vector2(x, y), line,
			HORIZONTAL_ALIGNMENT_LEFT, size.x - x - PAD, 12, _colour(line))
		y += LINE_H

	if scroll > 0:
		draw_string(mono, Vector2(size.x - 210, TOP + 12.0),
			"-- scrolled back %d --" % scroll,
			HORIZONTAL_ALIGNMENT_RIGHT, 200, 10, AMBER)

	_draw_foot(shown.size())


func _draw_foot(n: int) -> void:
	var y := size.y - BOT
	draw_rect(Rect2(0, y, size.x, BOT), HEAD)
	var lv: String = ["everything", "warnings and worse", "errors only"][level]
	var line := "/ filter   a all  w warn  e err   s source   R re-read"
	if sources.size() < 2:
		line = "/ filter   a all  w warn  e err   R re-read"
	draw_string(mono, Vector2(PAD, y + 26), _fit(line, size.x - 12.0, 10),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 10, DIMC)

	if typing or filter != "":
		var t := "/%s" % filter
		var col := FG if typing else AMBER
		draw_string(mono, Vector2(PAD, y + 13), t,
			HORIZONTAL_ALIGNMENT_LEFT, -1, 12, col)
		var w := mono.get_string_size(t, HORIZONTAL_ALIGNMENT_LEFT, -1, 12).x
		if typing and blink < 0.25:
			draw_rect(Rect2(PAD + w, y + 2, 7, 13), FG)
		draw_string(mono, Vector2(PAD + w + 14, y + 13),
			_fit("%d line(s) match, showing %s" % [n, lv], size.x - w - 30.0, 11),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, DIMC)
	else:
		draw_string(mono, Vector2(PAD, y + 13),
			_fit("showing %s -- red is a line saying cannot/failed/no such/"
				% lv + "refused, amber is warn/retry/missing", size.x - 12.0, 11),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, DIMC)
