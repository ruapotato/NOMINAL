# sysmon.gd — the system monitor.
#
# It knows nothing. Every process, every service, every block and every inode
# on this window is the output of a command you could have typed yourself:
# `ps`, `svc`, `df`, `df -i`. There is no model of the machine here, no cache,
# no "last known good" -- refresh() throws the lot away and asks again.
#
# The rule David set for the file browser applies here twice over: if this
# window and the terminal ever disagree, one of them is lying, and it must not
# be this one. A browser that is wrong gets argued with. A monitor that is
# wrong ends the investigation -- you look at a green row, believe the service
# is up, and go and break something else looking for the fault.
#
# WHAT IS DEAD SORTS TO THE TOP AND IS RED. That is the whole reason the
# window gets opened. `svc` prints its units in directory order, which is fine
# for a table you read line by line and useless for a table you glance at, so
# the default sort ranks DEAD first, then disabled, then units belonging to
# another runlevel, then everything that is fine. The healthy machine is the
# boring bottom of the list.
#
# ONE HONEST UGLINESS: reading /proc puts this window in /proc. Every poll
# leaves an exited /bin/ps, /bin/svc and /bin/df behind and they accumulate in
# the process table. They are NOT filtered out -- filtering the monitor's own
# footprint out of the monitor is exactly the lie this app exists to avoid --
# they are explained in the footer, and A stops the polling.

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


var procs: Array = []       # {pid, ppid, state, exit, ins, cmd}
var svcs: Array = []        # {name, state, exec, rank}
var disk: Dictionary = {}   # {fs, size, used, avail, pct}
var inod: Dictionary = {}   # {fs, total, used, free, pct}
var mounts: Array = []      # "none on /proc", as df printed it
var notes: Array = []       # anything df said that was not a table row
var err := ""

var tab := 0                # 0 services, 1 processes, 2 storage
var sel := 0
var scroll := 0
var sort_key := ["rank", "pid", ""]
var sort_asc := [true, true, true]
var auto := true
var age := 0.0              # seconds since the last poll
var polls := 0

const PERIOD := 5.0         # see the note about /proc: cheap is not free

const TAB_H := 22.0
const SUM_H := 30.0
const HDR_H := 15.0
const ROW_H := 14.0
const BOT_H := 30.0
const TAB_W := 86.0

const CHROME := Color("#d6d3ce")
const WHITE := Color("#ffffff")
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
const TITLES := ["services", "processes", "storage"]


func _ready() -> void:
	focus_mode = Control.FOCUS_ALL
	mouse_filter = Control.MOUSE_FILTER_STOP
	if mono == null:
		mono = preload("res://scripts/uifont.gd").mono()
	refresh()
	set_process(true)


func take_focus() -> void:
	grab_focus()


func _process(dt: float) -> void:
	age += dt
	if auto and age >= PERIOD:
		refresh()


# ------------------------------------------------------------ asking again

# The desktop calls this after every command run anywhere, and the timer calls
# it on its own. It is the whole refresh: four commands, and nothing that was
# here before survives.
func refresh() -> void:
	age = 0.0
	polls += 1
	err = ""
	if machine == null:
		err = "no machine attached"
		procs = []
		svcs = []
		disk = {}
		inod = {}
		mounts = []
		queue_redraw()
		return
	_read_procs(_sh("ps"))
	_read_svcs(_sh("svc"))
	_read_df(_sh("df"))
	_read_di(_sh("df -i"))
	_sort()
	_clamp()
	queue_redraw()


# `ps` prints "  PID  PPID STATE     EXIT  INSTRUCTIONS  COMMAND" and then one
# line per /proc entry. None of the six fields can contain a space -- the
# command is a path, not a command line -- so splitting on runs of whitespace
# is exact rather than approximate.
func _read_procs(out: String) -> void:
	procs = []
	for line in out.split("\n"):
		var t := line.strip_edges()
		if t == "" or t.begins_with("PID"):
			continue
		var f := t.split(" ", false)
		if f.size() < 6 or not f[0].is_valid_int():
			continue
		procs.append({
			"pid": int(f[0]), "ppid": int(f[1]), "state": f[2],
			"exit": int(f[3]), "ins": int(f[4]), "cmd": f[5],
		})


# `svc` pads the name to 17 columns and the state to 11, and one of the states
# it prints is "not at rl3" -- WITH SPACES IN IT. Splitting this table on
# whitespace turns that unit into a three-column row with a service called
# "not" in it, so the columns are read by position, which is what they are.
func _read_svcs(out: String) -> void:
	svcs = []
	var started := false
	for line in out.split("\n"):
		if line.begins_with("SERVICE"):
			started = true
			continue
		if not started:
			continue
		# The table ends at the first blank line; after it comes the legend
		# about what DEAD means, which is prose and not a unit.
		if line.strip_edges() == "":
			break
		if line.length() < 18:
			continue
		var nm := line.substr(0, 17).strip_edges()
		var st := line.substr(17, 11).strip_edges()
		var ex := line.substr(28).strip_edges() if line.length() > 28 else ""
		if nm == "":
			continue
		svcs.append({"name": nm, "state": st, "exec": ex, "rank": _rank(st)})


# The sort order of a monitor is an opinion about what matters. This one: a
# unit that is enabled and not running is a fault; a unit somebody switched
# off may be the fault, since anything ordered after it waits forever; a unit
# belonging to another runlevel is not a fault at all; running is fine.
func _rank(state: String) -> int:
	if state == "DEAD":
		return 0
	if state == "disabled":
		return 1
	if state.begins_with("not at"):
		return 2
	if state == "running":
		return 4
	return 3


# `df` prints the space table, a blank line, then the mount table. Both rows
# are plain whitespace columns. Anything else it says -- the paragraph about
# having no free inodes, for instance -- is kept verbatim and shown, because
# df only says those things when something is wrong.
func _read_df(out: String) -> void:
	disk = {}
	mounts = []
	notes = []
	var section := 0
	for line in out.split("\n"):
		var t := line.strip_edges()
		if t == "":
			continue
		if t.begins_with("FILESYSTEM"):
			section = 2 if t.find("MOUNTED ON") >= 0 else 1
			continue
		if section == 2:
			mounts.append(t)
			continue
		var f := t.split(" ", false)
		if section == 1 and f.size() >= 5 and f[4].ends_with("%"):
			disk = {"fs": f[0], "size": f[1], "used": f[2], "avail": f[3],
				"pct": int(f[4].replace("%", ""))}
		else:
			notes.append(t)


func _read_di(out: String) -> void:
	inod = {}
	for line in out.split("\n"):
		var t := line.strip_edges()
		if t == "" or t.begins_with("FILESYSTEM"):
			continue
		var f := t.split(" ", false)
		if f.size() >= 5 and f[4].ends_with("%") and f[1].is_valid_int():
			inod = {"fs": f[0], "total": int(f[1]), "used": int(f[2]),
				"free": int(f[3]), "pct": int(f[4].replace("%", ""))}
		else:
			notes.append(t)


func _sort() -> void:
	var key: String = sort_key[tab]
	var asc: bool = sort_asc[tab]
	if key == "":
		return
	var cmp := func(a: Dictionary, b: Dictionary) -> bool:
		var x = a.get(key, "")
		var y = b.get(key, "")
		if x == y:
			# A stable second key, so re-sorting a column does not shuffle the
			# rows that tie -- a list that reorders under you cannot be read.
			x = a.get("name", a.get("pid", 0))
			y = b.get("name", b.get("pid", 0))
			return x < y
		return x < y if asc else y < x
	if tab == 0:
		svcs.sort_custom(cmp)
	elif tab == 1:
		procs.sort_custom(cmp)


func _rows() -> Array:
	if tab == 0:
		return svcs
	if tab == 1:
		return procs
	return []


func _cols() -> Array:
	if tab == 0:
		var ew: float = maxf(60.0, size.x - 210.0)
		return [
			{"t": "SERVICE", "k": "name", "x": 4.0, "w": 110.0},
			{"t": "STATE", "k": "rank", "x": 116.0, "w": 84.0},
			{"t": "EXEC", "k": "exec", "x": 202.0, "w": ew},
		]
	if tab == 1:
		var wide := size.x >= 430.0
		var x := 4.0
		var c: Array = []
		c.append({"t": "PID", "k": "pid", "x": x, "w": 36.0}); x += 40.0
		c.append({"t": "PPID", "k": "ppid", "x": x, "w": 36.0}); x += 40.0
		c.append({"t": "STATE", "k": "state", "x": x, "w": 54.0}); x += 58.0
		c.append({"t": "EXIT", "k": "exit", "x": x, "w": 30.0}); x += 34.0
		if wide:
			c.append({"t": "INSTRUCTIONS", "k": "ins", "x": x, "w": 84.0})
			x += 88.0
		c.append({"t": "COMMAND", "k": "cmd", "x": x, "w": maxf(60.0, size.x - x - 6.0)})
		return c
	return []


func _visible() -> int:
	return maxi(1, int((size.y - TAB_H - SUM_H - HDR_H - BOT_H) / ROW_H))


func _clamp() -> void:
	var n := _rows().size()
	sel = clampi(sel, 0, maxi(0, n - 1))
	var vis := _visible()
	if sel < scroll:
		scroll = sel
	elif sel >= scroll + vis:
		scroll = sel - vis + 1
	scroll = clampi(scroll, 0, maxi(0, n - vis))


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
			scroll += 3
			_clamp_scroll()
			queue_redraw()
			return
		if mb.button_index != MOUSE_BUTTON_LEFT:
			return
		var p := mb.position
		if p.y < TAB_H:
			var i := int(p.x / TAB_W)
			if i >= 0 and i < TITLES.size():
				tab = i
				sel = 0
				scroll = 0
				_sort()
				queue_redraw()
			return
		var hy := TAB_H + SUM_H
		if p.y >= hy and p.y < hy + HDR_H and tab != 2:
			for c in _cols():
				if p.x >= c["x"] - 2.0 and p.x < c["x"] + c["w"]:
					if sort_key[tab] == c["k"]:
						sort_asc[tab] = not sort_asc[tab]
					else:
						sort_key[tab] = c["k"]
						sort_asc[tab] = true
					_sort()
					queue_redraw()
					return
			return
		if p.y >= hy + HDR_H and p.y < size.y - BOT_H:
			var i2 := scroll + int((p.y - hy - HDR_H) / ROW_H)
			if i2 >= 0 and i2 < _rows().size():
				sel = i2
				queue_redraw()
		return

	if not (e is InputEventKey) or not e.pressed:
		return
	var k := e as InputEventKey
	match k.keycode:
		KEY_R:
			refresh()
		KEY_A:
			auto = not auto
			age = 0.0
		KEY_1:
			tab = 0; sel = 0; scroll = 0; _sort()
		KEY_2:
			tab = 1; sel = 0; scroll = 0; _sort()
		KEY_3:
			tab = 2; sel = 0; scroll = 0
		KEY_TAB:
			tab = (tab + 1) % 3; sel = 0; scroll = 0; _sort()
		KEY_UP:
			sel = maxi(0, sel - 1); _clamp()
		KEY_DOWN:
			sel = mini(maxi(0, _rows().size() - 1), sel + 1); _clamp()
		KEY_PAGEUP:
			sel = maxi(0, sel - _visible()); _clamp()
		KEY_PAGEDOWN:
			sel = mini(maxi(0, _rows().size() - 1), sel + _visible()); _clamp()
		KEY_HOME:
			sel = 0; _clamp()
		KEY_END:
			sel = maxi(0, _rows().size() - 1); _clamp()
		_:
			return
	accept_event()
	queue_redraw()


func _clamp_scroll() -> void:
	scroll = clampi(scroll, 0, maxi(0, _rows().size() - _visible()))


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


func _n_dead() -> int:
	var n := 0
	for s in svcs:
		if s["state"] == "DEAD":
			n += 1
	return n


func _n_failed() -> int:
	var n := 0
	for p in procs:
		if p["state"] != "running" and p["exit"] != 0:
			n += 1
	return n


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), CHROME)
	_draw_tabs()
	_draw_summary()
	if err != "":
		draw_string(mono, Vector2(8, TAB_H + SUM_H + 20), err,
			HORIZONTAL_ALIGNMENT_LEFT, -1, 12, RED)
		return
	if tab == 2:
		_draw_storage()
	else:
		_draw_table()
	_draw_foot()


func _draw_tabs() -> void:
	draw_rect(Rect2(0, 0, size.x, TAB_H), Color("#cfccc7"))
	for i in range(TITLES.size()):
		var r := Rect2(i * TAB_W, 0, TAB_W - 1.0, TAB_H)
		if i == tab:
			draw_rect(r, WHITE)
			draw_rect(Rect2(r.position.x, TAB_H - 2.0, r.size.x, 2.0), SEL)
		draw_string(mono, Vector2(r.position.x + 8, 15), TITLES[i],
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, INK if i == tab else DIM)
	var hint := "R refresh   A auto %s" % ("on" if auto else "OFF")
	draw_string(mono, Vector2(size.x - 168, 15), hint,
		HORIZONTAL_ALIGNMENT_RIGHT, 160, 10, DIM if auto else AMBER)
	draw_line(Vector2(0, TAB_H), Vector2(size.x, TAB_H), Color("#a9a6a1"))


# The strip that is true no matter which tab is showing. If one thing on this
# window is going to be glanced at and acted on, it is the dead count.
func _draw_summary() -> void:
	var y := TAB_H
	draw_rect(Rect2(0, y, size.x, SUM_H), Color("#e6e3de"))
	var dead := _n_dead()
	var run := 0
	for s in svcs:
		if s["state"] == "running":
			run += 1
	draw_string(mono, Vector2(8, y + 13),
		"%d services, %d running" % [svcs.size(), run],
		HORIZONTAL_ALIGNMENT_LEFT, -1, 11, INK)
	if dead > 0:
		draw_string(mono, Vector2(170, y + 13), "%d DEAD" % dead,
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, RED)
	else:
		draw_string(mono, Vector2(170, y + 13), "nothing dead",
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, GREEN)
	var bits: Array = ["%d processes" % procs.size()]
	if _n_failed() > 0:
		bits.append("%d exited nonzero" % _n_failed())
	if disk.has("pct"):
		bits.append("disk %d%%" % disk["pct"])
	if inod.has("pct"):
		bits.append("inodes %d%%" % inod["pct"])
	var warn: bool = (disk.get("pct", 0) >= 90 or inod.get("pct", 0) >= 90
		or _n_failed() > 0)
	draw_string(mono, Vector2(8, y + 25),
		_fit("   ".join(PackedStringArray(bits)), size.x - 120.0, 10),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 10, RED if warn else DIM)
	draw_string(mono, Vector2(size.x - 108, y + 25),
		"read %ds ago" % int(age), HORIZONTAL_ALIGNMENT_RIGHT, 100, 10, DIM)
	draw_line(Vector2(0, y + SUM_H), Vector2(size.x, y + SUM_H), Color("#b3b0ab"))


func _draw_table() -> void:
	var hy := TAB_H + SUM_H
	var body := Rect2(0, hy + HDR_H, size.x, size.y - hy - HDR_H - BOT_H)
	draw_rect(body, WHITE)

	draw_rect(Rect2(0, hy, size.x, HDR_H), Color("#dcd9d4"))
	for c in _cols():
		var t := str(c["t"])
		if sort_key[tab] == c["k"]:
			t += " ^" if sort_asc[tab] else " v"
		draw_string(mono, Vector2(c["x"], hy + 11), _fit(t, c["w"], 10),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 10, INK)
	draw_line(Vector2(0, hy + HDR_H), Vector2(size.x, hy + HDR_H), Color("#b3b0ab"))

	var rows := _rows()
	var vis := _visible()
	for i in range(scroll, mini(rows.size(), scroll + vis)):
		var row: Dictionary = rows[i]
		var y := body.position.y + (i - scroll) * ROW_H
		var picked := i == sel
		if picked:
			draw_rect(Rect2(0, y, size.x, ROW_H), SEL)
		elif i % 2 == 1:
			draw_rect(Rect2(0, y, size.x, ROW_H), Color("#f5f4f2"))
		var base := y + 11.0
		for c in _cols():
			var k := str(c["k"])
			var txt := ""
			var col := INK
			if tab == 0:
				match k:
					"name": txt = str(row["name"])
					"rank":
						txt = str(row["state"])
						col = _svc_colour(str(row["state"]))
					"exec": txt = str(row["exec"])
			else:
				match k:
					"pid": txt = str(row["pid"])
					"ppid": txt = str(row["ppid"])
					"state":
						txt = str(row["state"])
						col = GREEN if row["state"] == "running" else DIM
					"exit":
						txt = str(row["exit"])
						col = RED if row["exit"] != 0 else DIM
					"ins": txt = str(row["ins"])
					"cmd":
						txt = str(row["cmd"])
						# A process that ended badly is the row you came for,
						# so the name of it is red too -- the exit column
						# alone is four pixels of red in a grey table.
						if row["state"] != "running" and row["exit"] != 0:
							col = RED
			if picked:
				col = SELTX
			draw_string(mono, Vector2(c["x"], base), _fit(txt, c["w"], 11),
				HORIZONTAL_ALIGNMENT_LEFT, -1, 11, col)
	if rows.is_empty():
		draw_string(mono, Vector2(8, body.position.y + 16),
			"nothing to show -- the command printed no rows",
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, DIM)


func _svc_colour(state: String) -> Color:
	if state == "DEAD":
		return RED
	if state == "disabled":
		return AMBER
	if state == "running":
		return GREEN
	return DIM


# A filesystem runs out of bytes and inodes independently, and the second one
# is invisible to everything but `df -i`: every file intact, every write
# refused. Both bars are always drawn, side by side, so the empty one cannot
# be the one you forgot to look at.
func _draw_storage() -> void:
	var y := TAB_H + SUM_H
	draw_rect(Rect2(0, y, size.x, size.y - y - BOT_H), WHITE)
	y += 16.0
	var w := size.x - 20.0
	y = _bar(y, "bytes", disk.get("pct", -1),
		("%s of %s used, %s free on %s" % [disk.get("used", "?"),
			disk.get("size", "?"), disk.get("avail", "?"), disk.get("fs", "?")])
		if disk.has("pct") else "df printed no space table", w)
	y = _bar(y, "inodes", inod.get("pct", -1),
		("%d of %d used, %d free" % [inod.get("used", 0), inod.get("total", 0),
			inod.get("free", 0)])
		if inod.has("pct") else "df -i printed no table", w)

	for n in notes:
		draw_string(mono, Vector2(10, y + 10), _fit(str(n), w, 11),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, RED)
		y += 14.0
	y += 6.0
	draw_string(mono, Vector2(10, y + 10), "MOUNTED",
		HORIZONTAL_ALIGNMENT_LEFT, -1, 10, DIM)
	y += 16.0
	for m in mounts:
		if y > size.y - BOT_H - 4.0:
			break
		draw_string(mono, Vector2(10, y + 10), _fit(str(m), w, 11),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, INK)
		y += 14.0


func _bar(y: float, label: String, pct: int, note: String, w: float) -> float:
	draw_string(mono, Vector2(10, y + 10), label,
		HORIZONTAL_ALIGNMENT_LEFT, -1, 11, INK)
	if pct >= 0:
		draw_string(mono, Vector2(10, y + 10), "%d%%" % pct,
			HORIZONTAL_ALIGNMENT_RIGHT, w, 11,
			RED if pct >= 90 else INK)
	y += 15.0
	var r := Rect2(10, y, w, 12)
	draw_rect(r, Color("#eceae7"))
	if pct >= 0:
		var f: float = clampf(float(pct) / 100.0, 0.0, 1.0)
		draw_rect(Rect2(10, y, w * f, 12),
			RED if pct >= 90 else (AMBER if pct >= 75 else SEL))
	draw_rect(r, Color("#8e8b86"), false, 1.0)
	y += 15.0
	draw_string(mono, Vector2(10, y + 9), _fit(note, w, 10),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 10, DIM)
	return y + 18.0


func _draw_foot() -> void:
	var y := size.y - BOT_H
	draw_rect(Rect2(0, y, size.x, BOT_H), Color("#e6e3de"))
	draw_line(Vector2(0, y), Vector2(size.x, y), Color("#b3b0ab"))
	var rows := _rows()
	var msg := ""
	if tab == 0 and sel >= 0 and sel < rows.size():
		var s: Dictionary = rows[sel]
		msg = "%s   %s   %s" % [s["name"], s["state"], s["exec"]]
		if s["state"] == "DEAD":
			msg += "   -- `svc status %s` says why" % s["name"]
	elif tab == 1 and sel >= 0 and sel < rows.size():
		var p: Dictionary = rows[sel]
		msg = "pid %d, parent %d, %s, exit %d, %d instructions   %s" % [
			p["pid"], p["ppid"], p["state"], p["exit"], p["ins"], p["cmd"]]
	elif tab == 2:
		msg = "df and df -i, exactly as the shell prints them"
	draw_string(mono, Vector2(8, y + 12), _fit(msg, size.x - 16.0, 10),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 10, INK)

	var note := "everything here is `ps`, `svc`, `df`, `df -i` -- nothing is remembered"
	if tab == 1:
		# Told, not hidden. The monitor is a process too.
		note = "the exited /bin/ps, /bin/svc and /bin/df rows are this window polling; A stops it"
	draw_string(mono, Vector2(8, y + 25), _fit(note, size.x - 16.0, 9),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 9, Color("#7c8085"))
