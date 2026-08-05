# svcman.gd — the service manager.
#
# `ps` answers "what is running". This answers "what is SUPPOSED to be
# running", which is a different question and usually the one that matters: a
# service can be defined and disabled, defined and running, or defined and
# dead in a loop the boot console scrolled past twenty lines ago.
#
# Every fact here comes from `svc` and `svc status <name>`, and nothing is
# kept between refreshes. If this window and the terminal ever disagree, one
# of them is lying, and it must not be this one -- a service manager that
# shows a stale "running" is how you spend an hour debugging the wrong half of
# the machine.
#
# THE DETAIL PANE IS THE POINT. The table can only say running or DEAD, and on
# a machine that boots with something quietly down that is where the trail
# used to end. `svc status` has always known the rest: how many times the
# kernel restarted it, what it exited with, WHAT IT SAID ON THE WAY DOWN, and
# whether the kernel gave up on it. Selecting a unit asks that question
# immediately, because the follow-up question is always the same one.
#
# DEAD IS RED AND SORTS FIRST. `disabled` is amber, not green and not red: it
# is not a fault by itself, and it is not innocent either -- anything ordered
# `after` a disabled unit waits forever for something that is never coming,
# and that is one of the nastier faults in this game.
#
# enable and disable REWRITE THE UNIT FILE, which is a package file. The next
# `pkg verify` will report it CHANGED, which is correct -- you changed it --
# and this window says so rather than letting it turn up later as a mystery.

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


var svcs: Array = []        # {name, state, exec, rank}
var sel := 0
var scroll := 0
var det: Dictionary = {}    # the parsed fields of `svc status <name>`
var det_raw: PackedStringArray = []   # ...and what it printed, verbatim
var det_for := ""
var status := ""
var status_bad := false
var err := ""

const TOP := 20.0
const ROW_H := 15.0
const BTN_H := 22.0
const BOT_H := 72.0   # a message line and TWO rows of buttons

const CHROME := Color("#d6d3ce")
const WHITE := Color("#ffffff")
const FACE := Color("#dedbd6")
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

# `svc status` prints "key<pad>value", and one of the keys is two words. They
# are matched as prefixes, in this order, so "last said" is recognised before
# anything tries to split the line on whitespace.
const FIELDS := ["service", "exec", "state", "restarts", "exit", "last said",
	"signal"]


func _ready() -> void:
	focus_mode = Control.FOCUS_ALL
	mouse_filter = Control.MOUSE_FILTER_STOP
	if mono == null:
		mono = preload("res://scripts/uifont.gd").mono()
	refresh()


func take_focus() -> void:
	grab_focus()


# ------------------------------------------------------------ asking again

func refresh() -> void:
	err = ""
	if machine == null:
		err = "no machine attached"
		svcs = []
		det = {}
		det_raw = []
		queue_redraw()
		return
	var want := ""
	if sel >= 0 and sel < svcs.size():
		want = str(svcs[sel]["name"])
	_read_table(_sh("svc"))
	if want != "":
		for i in range(svcs.size()):
			if svcs[i]["name"] == want:
				sel = i
				break
	sel = clampi(sel, 0, maxi(0, svcs.size() - 1))
	_read_detail()
	_clamp()
	queue_redraw()


# `svc` pads the name to 17 columns and the state to 15. Several of the states
# have SPACES IN THEM -- "not at rl3", "disabled, up" -- so this table is read
# by column position; splitting on whitespace invents a service called "not".
func _read_table(out: String) -> void:
	svcs = []
	var started := false
	for line in out.split("\n"):
		if line.begins_with("SERVICE"):
			started = true
			continue
		if not started:
			continue
		# The table ends at the first blank line. What follows is the legend
		# about what DEAD means, which is prose.
		if line.strip_edges() == "":
			break
		if line.length() < 18:
			continue
		var nm := line.substr(0, 17).strip_edges()
		var st := line.substr(17, 15).strip_edges()
		var ex := line.substr(32).strip_edges() if line.length() > 32 else ""
		if nm == "":
			continue
		svcs.append({"name": nm, "state": st, "exec": ex, "rank": _rank(st)})
	# What is broken goes to the top. `svc` prints units in directory order,
	# which is an accident of the filesystem, not an order anyone wants.
	svcs.sort_custom(func(a, b):
		if a["rank"] != b["rank"]:
			return a["rank"] < b["rank"]
		return a["name"] < b["name"])


func _rank(state: String) -> int:
	if state == "DEAD":
		return 0
	# A unit that would not come back at the next boot and is running anyway
	# is not healthy, whatever the process table says: it is a machine that is
	# right until somebody reboots it, which is the half of a repair people
	# forget. It sorts with the other things worth looking at.
	if state.ends_with(", up"):
		return 1
	if state == "disabled":
		return 1
	if state.begins_with("not at"):
		return 2
	if state == "running":
		return 4
	return 3


# `svc status` on a unit that was never started says so and explains why, in
# four lines of prose. That is a real answer -- a disabled unit has nothing to
# report -- so it is kept and shown rather than swallowed as an error.
func _read_detail() -> void:
	det = {}
	det_raw = []
	det_for = ""
	if machine == null or sel < 0 or sel >= svcs.size():
		return
	det_for = str(svcs[sel]["name"])
	var out: String = _sh("svc status " + det_for)
	det_raw = out.split("\n")
	for line in det_raw:
		for f in FIELDS:
			if line.begins_with(f):
				var v := line.substr(f.length()).strip_edges()
				if v != "":
					det[f] = v
				break


func _run(cmd: String) -> void:
	if machine == null:
		return
	var out := _sh(cmd).strip_edges()
	status = out.replace("\n", "  ") if out != "" else cmd + ": (no output)"
	# RED MEANS IT DID NOT HAPPEN. Every verb reports what it actually did,
	# including refusing, and a refusal drawn in green is the same lie as a
	# service manager showing a stale "running".
	status_bad = out.begins_with("svc:")
	for good in [" started", " stopped", " reloaded", " restarted",
			" enabled -- ", " disabled -- ", "already running"]:
		if out.find(good) >= 0:
			status_bad = false
	refresh()


# ---------------------------------------------------------------- layout

func _list_w() -> float:
	return clampf(size.x * 0.45, 120.0, 240.0)


func _visible() -> int:
	return maxi(1, int((size.y - TOP - BOT_H) / ROW_H))


func _clamp() -> void:
	var vis := _visible()
	if sel < scroll:
		scroll = sel
	elif sel >= scroll + vis:
		scroll = sel - vis + 1
	scroll = clampi(scroll, 0, maxi(0, svcs.size() - vis))


# TWO ROWS, BECAUSE THEY ARE TWO DIFFERENT QUESTIONS.
#
# The top row acts on the process that is running right now; the bottom row
# only decides what happens at the next boot. This window offered the bottom
# row alone, so the only way to make a repair take effect from here was to
# reboot the machine -- which is the one act that destroys the evidence for
# the whole class of fault where a daemon is out of step with its file.
func _buttons() -> Array:
	var y2 := size.y - BTN_H - 4.0
	var y1 := y2 - BTN_H - 2.0
	var w1: float = minf(90.0, (size.x - 20.0) / 4.0)
	var w2: float = minf(120.0, (size.x - 12.0) / 2.0)
	return [
		{"t": "start", "k": "start", "r": Rect2(4, y1, w1, BTN_H)},
		{"t": "stop", "k": "stop", "r": Rect2(8 + w1, y1, w1, BTN_H)},
		{"t": "restart", "k": "restart", "r": Rect2(12 + w1 * 2, y1, w1, BTN_H)},
		{"t": "reload", "k": "reload", "r": Rect2(16 + w1 * 3, y1, w1, BTN_H)},
		{"t": "enable", "k": "enable", "r": Rect2(4, y2, w2, BTN_H)},
		{"t": "disable", "k": "disable", "r": Rect2(8 + w2, y2, w2, BTN_H)},
	]


# ---------------------------------------------------------------- input

func _gui_input(e: InputEvent) -> void:
	if e is InputEventMouseButton and e.pressed:
		grab_focus()
		var mb := e as InputEventMouseButton
		if mb.button_index == MOUSE_BUTTON_WHEEL_UP:
			scroll = maxi(0, scroll - 3)
			queue_redraw()
			return
		if mb.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			scroll = clampi(scroll + 3, 0, maxi(0, svcs.size() - _visible()))
			queue_redraw()
			return
		if mb.button_index != MOUSE_BUTTON_LEFT:
			return
		for b in _buttons():
			if (b["r"] as Rect2).has_point(mb.position):
				if sel >= 0 and sel < svcs.size():
					_run("svc %s %s" % [b["k"], str(svcs[sel]["name"])])
				return
		if mb.position.x < _list_w() and mb.position.y >= TOP \
				and mb.position.y < size.y - BOT_H:
			var i := scroll + int((mb.position.y - TOP) / ROW_H)
			if i >= 0 and i < svcs.size() and i != sel:
				sel = i
				status = ""
				_read_detail()
				queue_redraw()
		return

	if not (e is InputEventKey) or not e.pressed:
		return
	var k := e as InputEventKey
	match k.keycode:
		KEY_UP:
			sel = maxi(0, sel - 1); _move()
		KEY_DOWN:
			sel = mini(maxi(0, svcs.size() - 1), sel + 1); _move()
		KEY_PAGEUP:
			sel = maxi(0, sel - _visible()); _move()
		KEY_PAGEDOWN:
			sel = mini(maxi(0, svcs.size() - 1), sel + _visible()); _move()
		KEY_HOME:
			sel = 0; _move()
		KEY_END:
			sel = maxi(0, svcs.size() - 1); _move()
		KEY_R, KEY_F5:
			refresh()
		KEY_E:
			if sel >= 0 and sel < svcs.size():
				_run("svc enable " + str(svcs[sel]["name"]))
		KEY_D:
			if sel >= 0 and sel < svcs.size():
				_run("svc disable " + str(svcs[sel]["name"]))
		KEY_S:
			if sel >= 0 and sel < svcs.size():
				_run("svc start " + str(svcs[sel]["name"]))
		KEY_X:
			if sel >= 0 and sel < svcs.size():
				_run("svc stop " + str(svcs[sel]["name"]))
		KEY_T:
			if sel >= 0 and sel < svcs.size():
				_run("svc restart " + str(svcs[sel]["name"]))
		KEY_L:
			if sel >= 0 and sel < svcs.size():
				_run("svc reload " + str(svcs[sel]["name"]))
		_:
			return
	accept_event()
	queue_redraw()


func _move() -> void:
	status = ""
	_clamp()
	_read_detail()


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


func _colour(state: String) -> Color:
	if state == "DEAD":
		return RED
	# Running now, gone at the next boot. Amber, like `disabled`, and for the
	# same reason: not a fault by itself, not innocent either.
	if state.ends_with(", up"):
		return AMBER
	if state == "disabled":
		return AMBER
	if state == "running":
		return GREEN
	return DIM


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), CHROME)
	var lw := _list_w()

	draw_rect(Rect2(0, 0, size.x, TOP), Color("#cfccc7"))
	var dead := 0
	for s in svcs:
		if s["state"] == "DEAD":
			dead += 1
	draw_string(mono, Vector2(6, 14), "services",
		HORIZONTAL_ALIGNMENT_LEFT, -1, 11, INK)
	draw_string(mono, Vector2(66, 14),
		("%d DEAD" % dead) if dead > 0 else "none dead",
		HORIZONTAL_ALIGNMENT_LEFT, -1, 10, RED if dead > 0 else GREEN)
	draw_string(mono, Vector2(lw + 6, 14), _fit(det_for, size.x - lw - 100.0, 11),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 11, INK)
	draw_string(mono, Vector2(size.x - 118, 14), "R re-reads   E/D toggle",
		HORIZONTAL_ALIGNMENT_RIGHT, 112, 9, DIM)
	draw_line(Vector2(0, TOP), Vector2(size.x, TOP), Color("#a9a6a1"))

	if err != "":
		draw_string(mono, Vector2(8, TOP + 20), err,
			HORIZONTAL_ALIGNMENT_LEFT, -1, 12, RED)
		return

	_draw_list(lw)
	_draw_detail(lw)
	_draw_foot()


func _draw_list(lw: float) -> void:
	var h := size.y - TOP - BOT_H
	draw_rect(Rect2(0, TOP, lw, h), WHITE)
	draw_line(Vector2(lw, TOP), Vector2(lw, TOP + h), Color("#b3b0ab"))
	var vis := _visible()
	for i in range(scroll, mini(svcs.size(), scroll + vis)):
		var s: Dictionary = svcs[i]
		var y := TOP + (i - scroll) * ROW_H
		var nc := INK
		var sc := _colour(str(s["state"]))
		if i == sel:
			draw_rect(Rect2(0, y, lw, ROW_H), SEL)
			nc = SELTX
			sc = SELTX
		# A square of state colour before the name, so the shape of the list
		# reads at a glance even on the selected row where the text is white.
		draw_rect(Rect2(4, y + 4, 7, 7), _colour(str(s["state"])))
		draw_string(mono, Vector2(15, y + 12), _fit(str(s["name"]), lw - 84.0, 11),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, nc)
		draw_string(mono, Vector2(lw - 68, y + 12), _fit(str(s["state"]), 64.0, 10),
			HORIZONTAL_ALIGNMENT_RIGHT, 64, 10, sc)
	if svcs.is_empty():
		draw_string(mono, Vector2(6, TOP + 14), "`svc` printed no units",
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, DIM)


func _draw_detail(lw: float) -> void:
	var x := lw + 1.0
	var w := size.x - x
	var h := size.y - TOP - BOT_H
	draw_rect(Rect2(x, TOP, w, h), WHITE)
	if sel < 0 or sel >= svcs.size():
		return
	var s: Dictionary = svcs[sel]
	var y := TOP + 14.0

	# The table's own answer first: it is the one thing that is true right now
	# rather than true of the last time the unit was started.
	y = _row(x, y, w, "state", str(s["state"]), _colour(str(s["state"])))
	y = _row(x, y, w, "exec", str(s["exec"]), INK)

	if det.is_empty():
		# Not an error. A unit that was never started this boot has nothing to
		# report, and `svc status` says exactly that in four useful lines.
		y += 4.0
		for line in det_raw:
			var t := str(line).strip_edges()
			if t == "":
				continue
			if y > size.y - BOT_H - 4.0:
				break
			draw_string(mono, Vector2(x + 4, y), _fit(t, w - 8.0, 10),
				HORIZONTAL_ALIGNMENT_LEFT, -1, 10, DIM)
			y += 13.0
		return

	if det.has("restarts"):
		var n := int(str(det["restarts"]))
		y = _row(x, y, w, "restarts", str(det["restarts"]),
			RED if n > 0 else DIM)
	if det.has("exit"):
		y = _row(x, y, w, "exit status", str(det["exit"]),
			RED if str(det["exit"]) != "0" else DIM)
	if det.has("signal"):
		y = _row(x, y, w, "signal", str(det["signal"]), AMBER)
	if det.has("state") and str(det["state"]) != str(s["state"]):
		# The kernel's own words about it -- "gave up after repeated failures"
		# is not something the table can say.
		y = _row(x, y, w, "kernel says", str(det["state"]), RED)

	if det.has("last said"):
		y += 4.0
		draw_string(mono, Vector2(x + 4, y), "what it said as it died",
			HORIZONTAL_ALIGNMENT_LEFT, -1, 9, DIM)
		y += 12.0
		# Wrapped, not truncated. This one line is the diagnosis more often
		# than anything else on the window -- "cannot write -- is the disk
		# full?" is the whole ticket -- so it never gets an ellipsis.
		for part in _wrap(str(det["last said"]), w - 12.0, 11):
			if y > size.y - BOT_H - 4.0:
				break
			draw_string(mono, Vector2(x + 6, y), part,
				HORIZONTAL_ALIGNMENT_LEFT, -1, 11, RED)
			y += 13.0
		y += 4.0
		if y < size.y - BOT_H - 12.0:
			draw_string(mono, Vector2(x + 4, y),
				_fit("the rest is in the boot log: dmesg -f " + det_for, w - 8.0, 9),
				HORIZONTAL_ALIGNMENT_LEFT, -1, 9, DIM)


func _row(x: float, y: float, w: float, k: String, v: String, col: Color) -> float:
	draw_string(mono, Vector2(x + 4, y), k, HORIZONTAL_ALIGNMENT_LEFT, -1, 10, DIM)
	draw_string(mono, Vector2(x + 78, y), _fit(v, w - 84.0, 11),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 11, col)
	return y + 15.0


func _wrap(t: String, w: float, fs: int) -> PackedStringArray:
	var lines := PackedStringArray()
	var cur := ""
	for word in t.split(" ", false):
		var try_it := word if cur == "" else cur + " " + word
		if mono.get_string_size(try_it, HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x > w \
				and cur != "":
			lines.append(cur)
			cur = word
		else:
			cur = try_it
	if cur != "":
		lines.append(cur)
	return lines


func _draw_foot() -> void:
	var y := size.y - BOT_H
	draw_rect(Rect2(0, y, size.x, BOT_H), Color("#e6e3de"))
	draw_line(Vector2(0, y), Vector2(size.x, y), Color("#b3b0ab"))
	var msg := status
	var col := RED if status_bad else GREEN
	if msg == "":
		# Said before you press it, not after. enable/disable edit a file that
		# a package owns, and finding that out from `pkg verify` an hour later
		# is how a repair turns into a second fault.
		msg = "top row acts now; enable/disable rewrite the unit file for the NEXT boot"
		col = DIM
	draw_string(mono, Vector2(6, y + 12), _fit(msg, size.x - 12.0, 9),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 9, col)

	for b in _buttons():
		var r: Rect2 = b["r"]
		_raised(r, FACE)
		draw_string(mono, Vector2(r.position.x + 2, r.position.y + 15),
			str(b["t"]), HORIZONTAL_ALIGNMENT_CENTER, r.size.x - 4.0, 11, INK)
