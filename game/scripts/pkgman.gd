# pkgman.gd — the package manager, with a window round it.
#
# `pkg` is the tool this game is really about: everything on the disk belongs
# to a package, and the database records the mode, a hash and the path of
# every file. This window does not reimplement one line of that. It runs
# `pkg list`, `pkg verify <name>`, `pkg diff <path>` and `pkg reinstall`, and
# it shows what they printed. If this window and the terminal ever disagree,
# one of them is lying, and it must not be this one.
#
# THAT MATTERS MOST HERE. A monitor that is wrong wastes your afternoon; a
# package manager that is wrong tells you a file matches when it does not, and
# you go and reinstall the wrong package. So the findings list is re-read from
# `pkg verify` every single refresh -- including after a reinstall, so what
# you see is the state of the disk AFTER the repair rather than an optimistic
# guess about what the repair did.
#
# VERIFY IS NOT A FAULT LIST. Every machine has configuration somebody edited
# on purpose, and those show as CHANGED because that is the truth. So the flow
# this window is built around is: pick a package, look at what differs, and
# `pkg diff` it BEFORE reinstalling anything -- a change that reads like an
# admin's decision ("# second one added after the outage in Feb") is not the
# same as one that reads like damage, and only a person can tell which.
#
# --force IS DRAWN AS THE DANGEROUS BUTTON BECAUSE IT IS ONE. Plain reinstall
# leaves edited config alone, which is what dpkg does and for the same reason.
# --force overwrites it and copies what was there to <file>.pkgsave, and that
# copy is the only undo in the game. So the button is amber, it is captioned
# with the consequence, and it takes two clicks -- the second one is the
# confirmation, and it disarms itself the moment anything else changes.

extends Control

var mono: Font
var machine: Object = null
# WHICH MACHINE THIS IS ABOUT.
#
# Pointing a system monitor at your OWN workstation is the mistake the log
# viewer already made: your box is fine, and a screen full of "running,
# running, running" is not a diagnosis. The desktop supplies `sh`, which
# routes to the CUSTOMER's machine whenever theirs is up and falls back to
# the workstation when it is not -- so this app never has to know how the
# two machines are wired, only that it must not invent an answer.
var sh: Callable = Callable()

func _sh(cmd: String) -> String:
	if sh.is_valid():
		return str(sh.call(cmd))
	if machine == null:
		return ""
	return str(machine.sh_on(0, cmd))


var pkgs: Array = []        # {name, ver, desc}
var sel := 0
var pscroll := 0

var findings: Array = []    # {pkg, path, what, detail} from `pkg verify <name>`
var fsel := -1
var vnote := ""             # what verify said when it had no findings to list
var list_note := ""         # what `pkg list` said instead of a list of packages
var diff: PackedStringArray = []   # `pkg diff <path>`, verbatim
var diff_path := ""
var dscroll := 0
var status := ""
var status_bad := false
var armed := false          # the --force button, waiting for its second click
var err := ""

const TOP := 20.0
const ROW_H := 14.0
const BTN_H := 22.0
const BOT_H := 62.0         # two button rows, the warning, and the status line

const CHROME := Color("#d6d3ce")
const WHITE := Color("#ffffff")
const FACE := Color("#dedbd6")
const DANGER := Color("#e8c9a0")
const ARMED := Color("#d98b6a")
const EDGE_L := Color("#f4f2ef")
const EDGE_D := Color("#8e8b86")
const BORDER := Color("#5c5c5c")
const INK := Color("#1b1b1b")
const DIM := Color("#5f6469")
const SEL := Color("#3465a4")
const SELTX := Color("#ffffff")
const RED := Color("#b0281a")
const GREEN := Color("#1f6b3a")
const AMBER := Color("#8a6d1f")


func _ready() -> void:
	focus_mode = Control.FOCUS_ALL
	mouse_filter = Control.MOUSE_FILTER_STOP
	if mono == null:
		mono = preload("res://scripts/uifont.gd").mono()
	refresh()


func take_focus() -> void:
	grab_focus()


# ------------------------------------------------------------ asking again

# The whole window, re-read. The desktop calls this after every command run
# anywhere -- including one typed in a terminal -- so a file edited in the
# editor shows up here as CHANGED without anybody pressing anything.
func refresh() -> void:
	err = ""
	if machine == null:
		err = "no machine attached"
		pkgs = []
		findings = []
		diff = []
		queue_redraw()
		return
	var want := ""
	if sel >= 0 and sel < pkgs.size():
		want = str(pkgs[sel]["name"])
	_read_list(_sh("pkg list"))
	# AN EMPTY WINDOW MUST SAY WHY IT IS EMPTY.
	#
	# A playtester opened this, pressed "verify again", and got nothing: the
	# pane still said "nothing verified yet", no error, no explanation. Every
	# button in here needs a selected package, there was no package to select
	# because `pkg list` had come back with nothing usable, and nothing said
	# so. The package database is ON THE MACHINE and it is one of the things
	# that breaks -- a machine whose pkg-config-data is damaged is a machine
	# where this window is empty FOR A REASON, and that reason is the
	# diagnosis. So the command and its actual output go on the screen.
	if pkgs.is_empty():
		err = "`pkg list` returned no packages."
		if list_note != "":
			err += "  it said: " + list_note
		else:
			err += "  it printed nothing at all."
	# Keep pointing at the same package across a refresh. The list is read
	# from the disk every time and could in principle reorder; following the
	# index rather than the name would silently move the selection.
	if want != "":
		for i in range(pkgs.size()):
			if pkgs[i]["name"] == want:
				sel = i
				break
	sel = clampi(sel, 0, maxi(0, pkgs.size() - 1))
	_verify()
	if diff_path != "":
		_diff(diff_path)
	queue_redraw()


# `pkg list` pads the name to 18 columns and then prints the contents of the
# package's version file, which is a version and a description on one line.
func _read_list(out: String) -> void:
	pkgs = []
	list_note = ""
	for line in out.split("\n"):
		var t := line.strip_edges()
		if t == "":
			continue
		if t.begins_with("pkg:"):
			# What the tool complained about. It used to be dropped on the
			# floor, which is how a window ends up empty and silent.
			list_note += ("  |  " if list_note != "" else "") + t
			continue
		var f := t.split(" ", false)
		if f.size() < 1:
			continue
		var desc := ""
		for i in range(2, f.size()):
			desc += (" " if desc != "" else "") + f[i]
		pkgs.append({"name": f[0], "ver": f[1] if f.size() > 1 else "",
			"desc": desc})


# `pkg verify <name>` prints "<package> <path> <WHAT>" in fixed columns, and
# for a mode mismatch a second line indented under it saying which mode. The
# indented line belongs to the finding above it, not to a file of its own.
func _verify() -> void:
	findings = []
	fsel = -1
	vnote = ""
	if machine == null:
		vnote = "no machine attached -- nothing to ask."
		return
	if sel < 0 or sel >= pkgs.size():
		# Not "nothing verified yet". Nothing CAN be verified, and the reason
		# is one line up in `err`; naming the command that would have done it
		# is what lets you go and run it yourself.
		vnote = "no package selected -- `pkg list` gave this window nothing to verify."
		return
	var name := str(pkgs[sel]["name"])
	var cmd := "pkg verify " + name
	var out: String = _sh(cmd)
	if out.strip_edges() == "":
		vnote = "`%s` printed nothing." % cmd
	# EVERYTHING AFTER THE COUNT IS THE SUMMARY, not more findings. `pkg
	# verify` now explains what a reinstall will and will not put back, over
	# several indented lines, and those lines were being glued on to the last
	# finding as its "detail" -- so the window attributed advice about /etc to
	# whichever file happened to be listed last.
	var summary := false
	for line in out.split("\n"):
		if line.strip_edges() == "":
			continue
		if summary:
			vnote += "\n" + line.strip_edges()
			continue
		if line.begins_with(" ") or line.begins_with("\t"):
			if not findings.is_empty():
				findings[-1]["detail"] = line.strip_edges()
			continue
		var t := line.strip_edges()
		if t.begins_with("pkg:") or t.begins_with("all files match") \
				or t.find("file(s) differ") >= 0:
			vnote = t
			summary = t.find("file(s) differ") >= 0
			continue
		var f := t.split(" ", false)
		if f.size() < 2:
			# A line this window does not understand is still a line the tool
			# printed. Showing it is the only way the two can be reconciled.
			if vnote == "":
				vnote = "`%s` said: %s" % [cmd, t]
			continue
		var what := ""
		for i in range(2, f.size()):
			what += (" " if what != "" else "") + f[i]
		findings.append({"pkg": f[0], "path": f[1], "what": what, "detail": ""})
	if not findings.is_empty():
		fsel = 0
		_diff(str(findings[0]["path"]))
	else:
		diff = []
		diff_path = ""


# What a flagged file actually says, against what the package shipped. This is
# the tool that makes local edits fair, so it is one click away rather than
# something you have to know to go and type.
func _diff(path: String) -> void:
	diff_path = path
	dscroll = 0
	if machine == null:
		diff = []
		return
	diff = _sh("pkg diff " + path).split("\n")


func _run(cmd: String) -> void:
	if machine == null:
		return
	var out := _sh(cmd).strip_edges()
	status = out.replace("\n", "  |  ") if out != "" else cmd + ": (no output)"
	status_bad = out.find("cannot") >= 0 or out.begins_with("pkg:")
	armed = false
	refresh()


# ---------------------------------------------------------------- layout

func _list_w() -> float:
	return clampf(size.x * 0.42, 110.0, 220.0)


func _visible() -> int:
	return maxi(1, int((size.y - TOP - BOT_H) / ROW_H))


func _find_rows() -> int:
	# The findings get the top third of the right-hand pane and the diff gets
	# the rest, because the diff is the long thing and the findings are
	# usually one or two lines.
	return clampi(int((size.y - TOP - BOT_H) / (ROW_H * 3.0)), 1, 6)


func _buttons() -> Array:
	var y := size.y - BTN_H - 4.0
	var w: float = (size.x - 16.0) / 3.0
	return [
		{"t": "verify again", "k": "verify", "r": Rect2(4, y, w, BTN_H)},
		{"t": "reinstall", "k": "reinstall", "r": Rect2(8 + w, y, w, BTN_H)},
		{"t": "reinstall --force" if not armed else "FORCE -- click to confirm",
			"k": "force", "r": Rect2(12 + w * 2.0, y, w, BTN_H)},
	]


# ---------------------------------------------------------------- input

func _gui_input(e: InputEvent) -> void:
	if e is InputEventMouseButton and e.pressed:
		grab_focus()
		var mb := e as InputEventMouseButton
		var lw := _list_w()
		if mb.button_index == MOUSE_BUTTON_WHEEL_UP \
				or mb.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			var d := -3 if mb.button_index == MOUSE_BUTTON_WHEEL_UP else 3
			if mb.position.x < lw:
				pscroll = clampi(pscroll + d, 0,
					maxi(0, pkgs.size() - _visible()))
			else:
				dscroll = clampi(dscroll + d, 0, maxi(0, diff.size() - 1))
			queue_redraw()
			return
		if mb.button_index != MOUSE_BUTTON_LEFT:
			return
		for b in _buttons():
			if (b["r"] as Rect2).has_point(mb.position):
				_click(str(b["k"]))
				return
		if mb.position.y < TOP or mb.position.y > size.y - BOT_H:
			return
		if mb.position.x < lw:
			var i := pscroll + int((mb.position.y - TOP) / ROW_H)
			if i >= 0 and i < pkgs.size() and i != sel:
				sel = i
				armed = false
				status = ""
				diff_path = ""
				_verify()
				queue_redraw()
			return
		# The findings pane: clicking a flagged file diffs it.
		var fy := TOP + 14.0
		var fr := _find_rows()
		if mb.position.y >= fy and mb.position.y < fy + fr * ROW_H:
			var j := int((mb.position.y - fy) / ROW_H)
			if j >= 0 and j < findings.size():
				fsel = j
				armed = false
				_diff(str(findings[j]["path"]))
				queue_redraw()
		return

	if not (e is InputEventKey) or not e.pressed:
		return
	var k := e as InputEventKey
	match k.keycode:
		KEY_UP:
			sel = maxi(0, sel - 1); armed = false; _after_move()
		KEY_DOWN:
			sel = mini(maxi(0, pkgs.size() - 1), sel + 1); armed = false; _after_move()
		KEY_PAGEUP:
			sel = maxi(0, sel - _visible()); armed = false; _after_move()
		KEY_PAGEDOWN:
			sel = mini(maxi(0, pkgs.size() - 1), sel + _visible())
			armed = false; _after_move()
		KEY_HOME:
			sel = 0; armed = false; _after_move()
		KEY_END:
			sel = maxi(0, pkgs.size() - 1); armed = false; _after_move()
		KEY_TAB:
			if not findings.is_empty():
				fsel = (fsel + 1) % findings.size()
				_diff(str(findings[fsel]["path"]))
		KEY_R, KEY_F5:
			refresh()
		KEY_ESCAPE:
			armed = false
		_:
			return
	accept_event()
	queue_redraw()


func _after_move() -> void:
	var vis := _visible()
	if sel < pscroll:
		pscroll = sel
	elif sel >= pscroll + vis:
		pscroll = sel - vis + 1
	status = ""
	diff_path = ""
	_verify()


func _click(k: String) -> void:
	if sel < 0 or sel >= pkgs.size():
		# A BUTTON THAT DOES NOTHING MUST AT LEAST SAY WHAT IT TRIED. This
		# returned in silence when there was no package to act on, so "verify
		# again" looked broken rather than blocked. Re-reading the list is
		# always a legal thing to try, and it either finds packages this time
		# or prints the reason it did not.
		refresh()
		status = "nothing to %s: " % k \
			+ ("`pkg list` said " + list_note if list_note != ""
				else "`pkg list` came back empty")
		status_bad = true
		queue_redraw()
		return
	var name := str(pkgs[sel]["name"])
	match k:
		"verify":
			armed = false
			_verify()
		"reinstall":
			armed = false
			_run("pkg reinstall " + name)
		"force":
			# Two clicks. The first arms it and says so; the second does it.
			# There is no undo but the .pkgsave copy, so one stray click on a
			# button next to "reinstall" must not be able to overwrite an
			# admin's config.
			if not armed:
				armed = true
			else:
				_run("pkg reinstall --force " + name)
	queue_redraw()


# ---------------------------------------------------------------- drawing

func _fit(t: String, w: float, fs: int) -> String:
	if mono.get_string_size(t, HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x <= w:
		return t
	var s := t
	while s.length() > 1 and \
			mono.get_string_size(s + "...", HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x > w:
		s = s.substr(0, s.length() - 1)
	return s + "..."


func _raised(r: Rect2, face: Color) -> void:
	draw_rect(r, face)
	draw_line(r.position + Vector2(0.5, 0.5),
		r.position + Vector2(r.size.x - 0.5, 0.5), EDGE_L)
	draw_line(r.position + Vector2(0.5, 0.5),
		r.position + Vector2(0.5, r.size.y - 0.5), EDGE_L)
	draw_line(r.position + Vector2(0.5, r.size.y - 0.5),
		r.position + Vector2(r.size.x - 0.5, r.size.y - 0.5), EDGE_D)
	draw_line(r.position + Vector2(r.size.x - 0.5, 0.5),
		r.position + Vector2(r.size.x - 0.5, r.size.y - 0.5), EDGE_D)
	draw_rect(r, BORDER, false, 1.0)


func _what_colour(what: String) -> Color:
	if what.begins_with("MISSING") or what.begins_with("UNREADABLE") \
			or what.begins_with("NOT A"):
		return RED
	if what.begins_with("CHANGED") or what.begins_with("REPOINTED"):
		return AMBER
	return DIM


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), CHROME)
	var lw := _list_w()

	draw_rect(Rect2(0, 0, size.x, TOP), Color("#cfccc7"))
	draw_string(mono, Vector2(6, 14), "packages",
		HORIZONTAL_ALIGNMENT_LEFT, -1, 11, INK)
	draw_string(mono, Vector2(lw + 6, 14),
		_fit(diff_path if diff_path != "" else "what differs", size.x - lw - 100.0, 11),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 11, INK)
	draw_string(mono, Vector2(size.x - 92, 14), "R re-reads",
		HORIZONTAL_ALIGNMENT_RIGHT, 86, 9, DIM)
	draw_line(Vector2(0, TOP), Vector2(size.x, TOP), Color("#a9a6a1"))

	if err != "":
		# The buttons stay drawn. The window failed to read a package list;
		# that is not a reason to take away the button that reads it again.
		draw_rect(Rect2(0, TOP, size.x, size.y - TOP - BOT_H), WHITE)
		var ey := TOP + 20.0
		for l in _wrapped(err, size.x - 16.0, 12):
			draw_string(mono, Vector2(8, ey), l,
				HORIZONTAL_ALIGNMENT_LEFT, -1, 12, RED)
			ey += 15.0
		ey += 6.0
		for l in ["this window runs `pkg` on the machine and shows what it said.",
				"a machine whose package database is damaged has no list to give:",
				"that is a finding, not a broken window. try it in a terminal."]:
			draw_string(mono, Vector2(8, ey), _fit(str(l), size.x - 16.0, 10),
				HORIZONTAL_ALIGNMENT_LEFT, -1, 10, DIM)
			ey += 13.0
		_draw_foot()
		return

	_draw_list(lw)
	_draw_right(lw)
	_draw_foot()


# Wrap on spaces at the size it will be drawn at.
func _wrapped(t: String, w: float, fs: int) -> PackedStringArray:
	var out := PackedStringArray()
	var line := ""
	for word in t.split(" ", false):
		var s := word if line == "" else line + " " + word
		if mono.get_string_size(s, HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x > w \
				and line != "":
			out.append(line)
			line = word
		else:
			line = s
	if line != "":
		out.append(line)
	return out


func _draw_list(lw: float) -> void:
	var h := size.y - TOP - BOT_H
	draw_rect(Rect2(0, TOP, lw, h), WHITE)
	draw_line(Vector2(lw, TOP), Vector2(lw, TOP + h), Color("#b3b0ab"))
	var vis := _visible()
	for i in range(pscroll, mini(pkgs.size(), pscroll + vis)):
		var p: Dictionary = pkgs[i]
		var y := TOP + (i - pscroll) * ROW_H
		var col := INK
		if i == sel:
			draw_rect(Rect2(0, y, lw, ROW_H), SEL)
			col = SELTX
		draw_string(mono, Vector2(4, y + 11), _fit(str(p["name"]), lw - 54.0, 11),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, col)
		draw_string(mono, Vector2(lw - 50, y + 11),
			_fit(str(p["ver"]), 46.0, 10), HORIZONTAL_ALIGNMENT_RIGHT, 46, 10,
			col if i == sel else DIM)
	if pkgs.size() > vis:
		draw_string(mono, Vector2(lw - 40, TOP + h - 4),
			"%d/%d" % [pscroll + 1, pkgs.size()],
			HORIZONTAL_ALIGNMENT_LEFT, -1, 9, DIM)


func _draw_right(lw: float) -> void:
	var x := lw + 1.0
	var w := size.x - x
	var h := size.y - TOP - BOT_H
	draw_rect(Rect2(x, TOP, w, h), WHITE)
	var y := TOP + 12.0
	if sel >= 0 and sel < pkgs.size():
		var p: Dictionary = pkgs[sel]
		draw_string(mono, Vector2(x + 4, y),
			_fit("%s  %s  %s" % [p["name"], p["ver"], p["desc"]], w - 8.0, 10),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 10, DIM)
	y += 2.0

	# The findings. CHANGED is amber, not red: somebody may have meant it.
	var fr := _find_rows()
	for j in range(mini(findings.size(), fr)):
		var f: Dictionary = findings[j]
		var ry := y + j * ROW_H
		var col := _what_colour(str(f["what"]))
		if j == fsel:
			draw_rect(Rect2(x, ry, w, ROW_H), Color("#dbe7f6"))
		draw_string(mono, Vector2(x + 4, ry + 11),
			_fit(str(f["path"]), w - 78.0, 11),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, INK)
		draw_string(mono, Vector2(x + w - 76, ry + 11),
			_fit(str(f["what"]) if str(f["what"]) != "" else str(f["detail"]),
				72.0, 10),
			HORIZONTAL_ALIGNMENT_RIGHT, 72, 10, col)
	if findings.is_empty():
		draw_string(mono, Vector2(x + 4, y + 11),
			_fit(vnote if vnote != ""
				else "`pkg verify` has not been run on this package yet -- press R",
				w - 8.0, 11),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, GREEN if vnote.begins_with("all") else DIM)
	elif findings.size() > fr:
		draw_string(mono, Vector2(x + 4, y + fr * ROW_H + 10),
			"+%d more -- tab" % (findings.size() - fr),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 9, DIM)

	var dy := y + maxi(findings.size(), 1) * ROW_H + 6.0
	dy = minf(dy, TOP + 12.0 + (fr + 1) * ROW_H + 6.0)
	draw_line(Vector2(x, dy - 4.0), Vector2(size.x, dy - 4.0), Color("#dcd9d5"))
	var rows := int((size.y - BOT_H - dy) / 13.0)
	if rows < 1:
		return
	if diff.is_empty():
		draw_string(mono, Vector2(x + 4, dy + 10),
			"select a flagged file to see `pkg diff` on it",
			HORIZONTAL_ALIGNMENT_LEFT, -1, 10, DIM)
		return
	dscroll = clampi(dscroll, 0, maxi(0, diff.size() - rows))
	for i in range(dscroll, mini(diff.size(), dscroll + rows)):
		var line := str(diff[i])
		# `pkg diff` marks the shipped copy with --- and the installed one with
		# +++, and puts its own commentary in an indented block. Colour follows
		# that, so the two halves are told apart without reading them.
		var col := INK
		if line.begins_with("---"):
			col = SEL
		elif line.begins_with("+++"):
			col = AMBER
		elif line.begins_with("    "):
			col = RED
		elif line.begins_with("pkg:"):
			col = RED
		draw_string(mono, Vector2(x + 4, dy + 10 + (i - dscroll) * 13.0),
			_fit(line, size.x - x - 8.0, 10), HORIZONTAL_ALIGNMENT_LEFT, -1, 10, col)
	if diff.size() > rows:
		draw_string(mono, Vector2(size.x - 52, size.y - BOT_H - 2),
			"%d/%d" % [dscroll + 1, diff.size()],
			HORIZONTAL_ALIGNMENT_LEFT, -1, 9, DIM)


func _draw_foot() -> void:
	var y := size.y - BOT_H
	draw_rect(Rect2(0, y, size.x, BOT_H), Color("#e6e3de"))
	draw_line(Vector2(0, y), Vector2(size.x, y), Color("#b3b0ab"))

	# The consequence, in the window, next to the button that causes it. A
	# warning you have to go and read somewhere else is not a warning.
	var warn := "--force overwrites config you edited. The old copy becomes <file>.pkgsave -- that is the only undo."
	if armed:
		warn = "click FORCE again to overwrite edited config in this package. Esc cancels."
	draw_string(mono, Vector2(6, y + 11), _fit(warn, size.x - 12.0, 9),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 9, RED if armed else AMBER)

	if status != "":
		draw_string(mono, Vector2(6, y + 24), _fit(status, size.x - 12.0, 10),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 10, RED if status_bad else GREEN)
	else:
		draw_string(mono, Vector2(6, y + 24),
			_fit("reinstall leaves edited config alone; diff first, then decide",
				size.x - 12.0, 10),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 10, DIM)

	for b in _buttons():
		var r: Rect2 = b["r"]
		var face := FACE
		if b["k"] == "force":
			face = ARMED if armed else DANGER
		_raised(r, face)
		draw_string(mono, Vector2(r.position.x + 2, r.position.y + 15),
			_fit(str(b["t"]), r.size.x - 4.0, 10),
			HORIZONTAL_ALIGNMENT_CENTER, r.size.x - 4.0, 10,
			RED if (b["k"] == "force") else INK)
