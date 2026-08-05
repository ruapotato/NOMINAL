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
	"stale", "no boot log", "not mounted", "did not start", "waiting for",
	"giving up"]
const OK_WORDS := ["started", "mounted", "ok", "up", "listening"]

var raw: PackedStringArray = []
var src := 0                 # which source we are showing
var sources: Array = []      # [{label, cmd, text}]
var filter := ""
var typing := false
var picking := false         # the source list is open over the log
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

func _sh(who: int, cmd: String) -> String:
	if machine == null:
		return ""
	return str(machine.sh_on(who, cmd))


# THERE WAS NO WAY TO CHOOSE A LOG.
#
# The window showed the customer's console and nothing else -- a playtester who
# wanted /var/log/messages, the boot log, the audit log or a dmesg had nowhere
# to ask, and the one key that changed the source was a bare `s` in the footer
# hint that did nothing at all unless a second source had happened to appear.
# Those four logs are the four things you actually want, and `ls /var/log` on
# any of these machines lists all four of them.
#
# WHICH MACHINE each one comes off is part of its name, because that is the
# whole question this window exists to answer. Their box answers for itself
# while it is up; while it is down the same files are readable off their disk
# at /mnt; and your own workstation is on the list on purpose -- it is a
# healthy install of the same system, so "what does this line look like on a
# machine that works" is one keypress away instead of a second window.
#
# Nothing is invented: every row is a command you could type, it is printed
# next to the label, and a source that comes back empty or refused is not
# offered. That is why the list changes under you -- attach the console and one
# appears, mount their disk and four do.
func _candidates() -> Array:
	var out: Array = []
	out.append([0, "rcon console", "their console -- %s (%s)" % [addr, cust]])
	if machine != null and machine.booted():
		out.append([1, "dmesg", "their boot log, live -- dmesg"])
		out.append([1, "tail -n 400 /var/log/messages", "their /var/log/messages, live"])
		out.append([1, "cat /var/log/audit.log", "their /var/log/audit.log, live"])
	out.append([0, "dmesg -r /mnt -1", "their PREVIOUS boot, off the disk at /mnt"])
	out.append([0, "cat /mnt/var/log/boot.log", "their boot log, off the disk at /mnt"])
	out.append([0, "tail -n 400 /mnt/var/log/messages", "their /var/log/messages, off the disk"])
	out.append([0, "tail -n 200 /mnt/var/log/messages.1", "their ROTATED messages.1, off the disk"])
	out.append([0, "cat /mnt/var/log/audit.log", "their /var/log/audit.log, off the disk"])
	out.append([0, "dmesg", "YOUR workstation's boot log -- dmesg"])
	out.append([0, "tail -n 400 /var/log/messages", "YOUR workstation's /var/log/messages"])
	out.append([0, "cat /var/log/audit.log", "YOUR workstation's /var/log/audit.log"])
	return out


# A command that answered with a refusal answered nothing. These are the words
# this system's own tools use when a thing is not there -- the same list the
# window would colour red if it printed them as a log line.
const NOTHING := ["not attached", "no boot log", "cannot read", "not found",
	"no such", "no shell here", "cannot open"]

func _has_content(t: String) -> bool:
	if t.strip_edges() == "":
		return false
	var head := t.substr(0, 200).to_lower()
	for w in NOTHING:
		if head.find(w) >= 0:
			return false
	return true


func refresh() -> void:
	# Which source you were reading, by name: the list grows and shrinks under
	# you as the machine changes, and an index into it means a different log
	# every time that happens.
	var was := "" if src < 0 or src >= sources.size() else str(sources[src]["cmd"])
	sources = []
	for c in _candidates():
		var t: String = _sh(int(c[0]), str(c[1]))
		if _has_content(t):
			sources.append({"label": str(c[2]), "cmd": str(c[1]), "text": t,
				"who": int(c[0])})
	src = 0
	for i in range(sources.size()):
		if str(sources[i]["cmd"]) == was:
			src = i
			break
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
		if mb.button_index != MOUSE_BUTTON_LEFT:
			return
		if picking:
			var i := _pick_hit(mb.position)
			if i >= 0:
				src = i
				scroll = 0
				refresh()
			picking = false
			queue_redraw()
			return
		if mb.position.y < TOP:
			picking = true
			queue_redraw()
		return

	if not (e is InputEventKey) or not e.pressed:
		return
	var k := e as InputEventKey
	accept_event()

	if picking:
		match k.keycode:
			KEY_ESCAPE, KEY_S:
				picking = false
			KEY_UP:
				src = maxi(0, src - 1)
			KEY_DOWN:
				src = mini(sources.size() - 1, src + 1)
			KEY_ENTER, KEY_KP_ENTER:
				picking = false
				scroll = 0
				refresh()
			_:
				# 1..9 pick a source outright, which is what you do the second
				# time you use this window.
				var n := k.keycode - KEY_1
				if n >= 0 and n < mini(9, sources.size()):
					src = n
					picking = false
					scroll = 0
					refresh()
		queue_redraw()
		return

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
			picking = not sources.is_empty()
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
	# window can be checked against a terminal. It is also the button that
	# opens the list, which is why it has an arrow on it: a picker nobody can
	# see is the same as no picker, and that is what this window had.
	var s: Dictionary = sources[src]
	# YOUR OWN LOGS ARE ON THE LIST AND MUST NOT BE MISTAKEN FOR THEIRS. This
	# window's original sin was showing your workstation's boot log to somebody
	# debugging somebody else's machine. It can still show it -- deliberately,
	# as the healthy comparison -- so when it does, the header goes amber.
	var mine: bool = str(s["label"]).begins_with("YOUR")
	draw_string(mono, Vector2(PAD, 14),
		_fit("%s   [%s]" % [s["label"], s["cmd"]], size.x - 168.0, 11),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 11, AMBER if mine else FG)
	var ax := size.x - 158.0
	draw_polygon(PackedVector2Array([Vector2(ax, 8), Vector2(ax + 9, 8),
		Vector2(ax + 4.5, 14)]), PackedColorArray([DIMC]))

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
	_draw_picker()


const PICK_ROW := 17.0

func _pick_rect() -> Rect2:
	var h: float = PICK_ROW * float(sources.size()) + 22.0
	return Rect2(4, TOP, maxf(280.0, size.x - 8.0), minf(h, size.y - TOP - 8.0))


func _pick_hit(p: Vector2) -> int:
	var r := _pick_rect()
	if not r.has_point(p):
		return -1
	var i := int((p.y - r.position.y - 18.0) / PICK_ROW)
	return i if i >= 0 and i < sources.size() else -1


func _draw_picker() -> void:
	if not picking or sources.is_empty():
		return
	var r := _pick_rect()
	draw_rect(r, Color("#1b2129"))
	draw_rect(r, DIMC, false, 1.0)
	draw_string(mono, Vector2(r.position.x + 8, r.position.y + 13),
		"which log -- click one, or 1-9, or Esc",
		HORIZONTAL_ALIGNMENT_LEFT, -1, 10, DIMC)
	var y := r.position.y + 18.0
	for i in range(sources.size()):
		if y + PICK_ROW > r.position.y + r.size.y:
			break
		if i == src:
			draw_rect(Rect2(r.position.x + 1, y, r.size.x - 2, PICK_ROW),
				Color("#2b3a4a"))
		var lab := "%d  %s" % [i + 1, str(sources[i]["label"])]
		draw_string(mono, Vector2(r.position.x + 8, y + 12), lab,
			HORIZONTAL_ALIGNMENT_LEFT, r.size.x * 0.56, 11,
			FG if i == src else Color("#9aa6b3"))
		draw_string(mono, Vector2(r.position.x + r.size.x * 0.58, y + 12),
			_fit(str(sources[i]["cmd"]), r.size.x * 0.40, 10),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 10, DIMC)
		y += PICK_ROW


func _draw_foot(n: int) -> void:
	var y := size.y - BOT
	draw_rect(Rect2(0, y, size.x, BOT), HEAD)
	var lv: String = ["everything", "warnings and worse", "errors only"][level]
	var line := "/ filter   a all  w warn  e err   s which log (%d)   R re-read" \
		% sources.size()
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
