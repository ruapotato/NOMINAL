# duview.gd — where the disk went.
#
# A full disk is a real fault in this game. Nothing is corrupt, every file
# verifies, and the next write simply fails -- so `df` tells you there is a
# problem and then has nothing else to say. This window is the "and then what"
# for that: a treemap of the tree, biggest thing biggest, click to go in.
#
# THERE IS NO `du` ON THIS MACHINE. I checked: `du: command not found`. `find`
# exists but prints no sizes, and on this machine `find` is also WRONG for a
# recursive walk -- it returns only the first entry of every subdirectory it
# descends into (compare `find /usr/share/man` with `ls -l /usr/share/man`).
# So the walk is `ls -l` once per directory, which is the same parse the file
# browser uses, and it is complete.
#
# MEASURED, so the totals can be trusted: walking everything except /proc adds
# up to 537720 bytes on a fresh ticket, and `df` reports 525K used --
# 525 * 1024 = 537600, which is 537720 truncated to whole kilobytes by df.
# They agree exactly. Including /proc gives 538843, which does not: /proc is a
# mount whose contents are generated per read and are not on the disk at all
# (`df` lists it under MOUNTED ON, and the kernel's own accounting walks the
# disk tree only). /proc is therefore skipped, and the footer says so rather
# than leaving you to wonder why the numbers are 1 KB apart.
#
# The walk is spread over frames. One pass over this filesystem is 118 `ls`
# calls and about 900 ms, and a window that freezes for a second every time it
# opens is a window people stop opening.

extends Control

var mono: Font
var machine: Object = null
var sh: Callable = Callable()

var root := "/"
var here := "/"                # what the treemap is showing
var kids: Dictionary = {}      # dir path -> Array of {name, full, dir, size}
var total: Dictionary = {}     # dir path -> bytes under it
var nfiles: Dictionary = {}    # dir path -> files under it, recursively
var ndirs: Dictionary = {}     # dir path -> directories under it
var err := ""

var scanning := false
var pending: Array = []        # directories still to list
var order: Array = []          # directories in discovery order, for the sum
var n_dirs := 0
var n_files := 0
var walked := 0                # bytes of files seen so far

var df_size := ""
var df_used := ""
var df_avail := ""
var df_pct := -1
var df_notes: Array = []       # anything df said that was not a table row

var tiles: Array = []          # {item, rect} laid out by the last _draw
var sel := -1
var hover := -1
var skipped_empty := 0

const TOP := 22.0
const BOT := 34.0
const PER_FRAME := 6           # directories listed per frame while scanning

const CHROME := Color("#d6d3ce")
const INK := Color("#1b1b1b")
const DIM := Color("#5f6469")
const FAINT := Color("#9aa0a6")
const RED := Color("#b0281a")
const GREEN := Color("#1f6b3a")
const AMBER := Color("#8a6d1f")
const SEL := Color("#3465a4")

# Directories and files are told apart by colour family, not by a badge: at
# treemap sizes a badge is three pixels and unreadable, and "which of these
# blue slabs is a folder I can open" is the question the window exists to
# answer.
const DIR_HUES := [Color("#3b6ea5"), Color("#4a7fb5"), Color("#2f5c8a"),
	Color("#5a8cbf"), Color("#27506f")]
const FILE_HUES := [Color("#7f8c8d"), Color("#95a5a6"), Color("#6d7b7c"),
	Color("#8d9b9c"), Color("#5f6d6e")]


func _ready() -> void:
	focus_mode = Control.FOCUS_ALL
	mouse_filter = Control.MOUSE_FILTER_STOP
	if mono == null:
		mono = preload("res://scripts/uifont.gd").mono()
	rescan()
	set_process(true)


func take_focus() -> void:
	grab_focus()


func _sh(cmd: String) -> String:
	if sh.is_valid():
		return str(sh.call(cmd))
	if machine == null:
		return ""
	return str(machine.sh_on(0, cmd))


func _has_machine() -> bool:
	return machine != null or sh.is_valid()


# ---------------------------------------------------------------- scanning

func rescan() -> void:
	kids = {}
	total = {}
	nfiles = {}
	ndirs = {}
	order = []
	pending = []
	n_dirs = 0
	n_files = 0
	walked = 0
	sel = -1
	err = ""
	tiles = []
	if not _has_machine():
		err = "no machine attached"
		scanning = false
		queue_redraw()
		return
	_read_df()
	here = root
	pending.append(root)
	order.append(root)
	scanning = true
	queue_redraw()


# Same parse as sysmon's: the space table, then a blank line, then the mount
# table. Anything df says that is not a row is kept verbatim -- df only starts
# writing prose when something is wrong, and on a full disk that prose is the
# diagnosis.
func _read_df() -> void:
	df_size = ""
	df_used = ""
	df_avail = ""
	df_pct = -1
	df_notes = []
	var section := 0
	for line in _sh("df").split("\n"):
		var t := line.strip_edges()
		if t == "":
			continue
		if t.begins_with("FILESYSTEM"):
			section = 2 if t.find("MOUNTED ON") >= 0 else 1
			continue
		if section == 2:
			continue
		var f := t.split(" ", false)
		if section == 1 and f.size() >= 5 and f[4].ends_with("%"):
			df_size = f[1]
			df_used = f[2]
			df_avail = f[3]
			df_pct = int(f[4].replace("%", ""))
		else:
			df_notes.append(t)


func _process(_dt: float) -> void:
	if not scanning:
		return
	var did := 0
	while did < PER_FRAME and not pending.is_empty():
		_list(str(pending.pop_front()))
		did += 1
	if pending.is_empty():
		_sum()
		scanning = false
	queue_redraw()


func _list(dir: String) -> void:
	n_dirs += 1
	var out: Array = []
	for line in _sh("ls -l " + dir).split("\n"):
		var t := line.strip_edges()
		if t == "":
			continue
		if t.begins_with("ls:"):
			# A directory that cannot be listed is not zero bytes. Recording
			# the refusal keeps it out of the totals and out of the treemap,
			# instead of drawing a convincing empty square over it.
			df_notes.append(t)
			continue
		var e := _parse(line, dir)
		if e.is_empty():
			continue
		# /proc is a mount, not disk. See the header for the measurement.
		if e["full"] == "/proc":
			continue
		out.append(e)
		if e["dir"]:
			pending.append(e["full"])
			order.append(e["full"])
		else:
			n_files += 1
			walked += int(e["size"])
	kids[dir] = out


func _parse(line: String, dir: String) -> Dictionary:
	if line.length() < 8 or "dl-".find(line[0]) < 0:
		return {}
	var t := line[0]
	var mode := line.substr(1, 4)
	var i := 5
	while i < line.length() and line[i] == " ":
		i += 1
	var s := i
	while i < line.length() and line[i] != " ":
		i += 1
	var sz := line.substr(s, i - s)
	while i < line.length() and line[i] == " ":
		i += 1
	var rest := line.substr(i).replace("(DANGLING)", "").strip_edges()
	var arrow := rest.find(" -> ")
	if arrow >= 0:
		rest = rest.substr(0, arrow)
	var nm := rest.strip_edges()
	if nm == "":
		return {}
	var full := (dir if dir.ends_with("/") else dir + "/") + nm
	# A symlink is NOT descended into. `ls` gives its size as the length of
	# its target, which is what it occupies, and following it would count the
	# target twice -- or loop forever on /usr/lib -> /lib.
	return {"name": nm, "full": full, "dir": t == "d", "size": int(sz),
		"mode": mode, "type": t}


# Deepest first. Directories were discovered breadth first, so walking `order`
# backwards guarantees every child is totalled before its parent.
func _sum() -> void:
	for i in range(order.size() - 1, -1, -1):
		var d: String = str(order[i])
		var t := 0
		var fc := 0
		var dc := 0
		var list: Array = kids.get(d, [])
		for j in range(list.size()):
			var e: Dictionary = list[j]
			if e["dir"]:
				var sub: int = int(total.get(e["full"], 0))
				e["size"] = sub
				list[j] = e
				t += sub
				# Counted per directory, not globally. The footer used to show
				# the whole disk's file count beside whatever directory you had
				# clicked into, which reads as a claim about that directory.
				fc += int(nfiles.get(e["full"], 0))
				dc += 1 + int(ndirs.get(e["full"], 0))
			else:
				t += int(e["size"])
				fc += 1
		total[d] = t
		nfiles[d] = fc
		ndirs[d] = dc


# ---------------------------------------------------------------- treemap

# Squarified treemap. Slice-and-dice is three lines shorter and produces
# splinters one pixel wide that cannot be clicked or labelled; this keeps the
# tiles near square, which is the only reason a treemap is readable at all.
func _layout(items: Array, area: Rect2) -> Array:
	var out: Array = []
	var sum := 0.0
	for it in items:
		sum += float(it["size"])
	if sum <= 0.0 or area.size.x < 2.0 or area.size.y < 2.0:
		return out
	var scale := (area.size.x * area.size.y) / sum
	var rest: Array = items.duplicate()
	var r := area
	while not rest.is_empty() and r.size.x >= 1.0 and r.size.y >= 1.0:
		var side: float = minf(r.size.x, r.size.y)
		var row: Array = []
		var areas: Array = []
		var row_sum := 0.0
		var best := INF
		while not rest.is_empty():
			var a := float(rest[0]["size"]) * scale
			if a <= 0.0:
				rest.pop_front()
				continue
			var w := _worst(areas, a, row_sum + a, side)
			if row.is_empty() or w <= best:
				areas.append(a)
				row.append(rest.pop_front())
				row_sum += a
				best = w
			else:
				break
		if row.is_empty():
			break
		var thick: float = row_sum / side
		var along := 0.0
		for i in range(row.size()):
			var len_i: float = areas[i] / thick if thick > 0.0 else 0.0
			var tile: Rect2
			if r.size.x >= r.size.y:
				tile = Rect2(r.position.x, r.position.y + along, thick, len_i)
			else:
				tile = Rect2(r.position.x + along, r.position.y, len_i, thick)
			out.append({"item": row[i], "rect": tile})
			along += len_i
		if r.size.x >= r.size.y:
			r = Rect2(r.position.x + thick, r.position.y,
				maxf(0.0, r.size.x - thick), r.size.y)
		else:
			r = Rect2(r.position.x, r.position.y + thick, r.size.x,
				maxf(0.0, r.size.y - thick))
	return out


func _worst(areas: Array, extra: float, sum: float, side: float) -> float:
	var mn := extra
	var mx := extra
	for a in areas:
		mn = minf(mn, float(a))
		mx = maxf(mx, float(a))
	if mn <= 0.0 or sum <= 0.0 or side <= 0.0:
		return INF
	var s2 := sum * sum
	var side2 := side * side
	return maxf(side2 * mx / s2, s2 / (side2 * mn))


func _here_items() -> Array:
	var list: Array = kids.get(here, [])
	var out: Array = []
	skipped_empty = 0
	for e in list:
		if int(e["size"]) > 0:
			out.append(e)
		else:
			skipped_empty += 1
	out.sort_custom(func(a, b): return int(a["size"]) > int(b["size"]))
	return out


func _map_rect() -> Rect2:
	return Rect2(4, TOP + 2, maxf(8.0, size.x - 8.0),
		maxf(8.0, size.y - TOP - BOT - 4.0))


# ---------------------------------------------------------------- navigation

func enter(p: String) -> void:
	if kids.has(p):
		here = p
		sel = -1
		queue_redraw()


func up() -> void:
	if here == root:
		return
	var b := here.get_base_dir()
	here = b if b != "" else "/"
	sel = -1
	queue_redraw()


func _gui_input(e: InputEvent) -> void:
	if e is InputEventMouseMotion:
		var p := (e as InputEventMouseMotion).position
		var h := -1
		for i in range(tiles.size()):
			if (tiles[i]["rect"] as Rect2).has_point(p):
				h = i
				break
		if h != hover:
			hover = h
			queue_redraw()
		accept_event()
		return

	if e is InputEventMouseButton and e.pressed:
		grab_focus()
		var mb := e as InputEventMouseButton
		if mb.button_index == MOUSE_BUTTON_RIGHT:
			up()
			accept_event()
			return
		if mb.button_index != MOUSE_BUTTON_LEFT:
			return
		# The breadcrumb is a row of clickable components; clicking one jumps
		# straight there, which is what a breadcrumb is for.
		if mb.position.y < TOP:
			var jump := _crumb_hit(mb.position.x)
			if jump != "":
				enter(jump)
			accept_event()
			return
		for i in range(tiles.size()):
			if (tiles[i]["rect"] as Rect2).has_point(mb.position):
				sel = i
				var it: Dictionary = tiles[i]["item"]
				if it["dir"]:
					enter(str(it["full"]))
				break
		accept_event()
		queue_redraw()
		return

	if not (e is InputEventKey) or not e.pressed:
		return
	var k := e as InputEventKey
	match k.keycode:
		KEY_BACKSPACE, KEY_LEFT:
			up()
		KEY_HOME:
			here = root
			sel = -1
		KEY_R, KEY_F5:
			rescan()
		KEY_RIGHT, KEY_DOWN:
			sel = mini(maxi(0, tiles.size() - 1), sel + 1)
		KEY_UP:
			sel = maxi(0, sel - 1)
		KEY_ENTER, KEY_KP_ENTER:
			if sel >= 0 and sel < tiles.size():
				var it: Dictionary = tiles[sel]["item"]
				if it["dir"]:
					enter(str(it["full"]))
		_:
			return
	accept_event()
	queue_redraw()


func _crumbs() -> Array:
	var out: Array = [{"label": "/", "path": "/"}]
	var at := "/"
	for c in here.split("/", false):
		at = (at if at.ends_with("/") else at + "/") + c
		out.append({"label": c, "path": at})
	return out


func _crumb_hit(x: float) -> String:
	var cx := 8.0
	for c in _crumbs():
		var w := mono.get_string_size(str(c["label"]), HORIZONTAL_ALIGNMENT_LEFT,
			-1, 11).x + 10.0
		if x >= cx and x < cx + w:
			return str(c["path"])
		cx += w
	return ""


# ---------------------------------------------------------------- drawing

func _fit(t: String, w: float, fs: int) -> String:
	if mono.get_string_size(t, HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x <= w:
		return t
	var s := t
	while s.length() > 1 and \
			mono.get_string_size(s + "...", HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x > w:
		s = s.substr(0, s.length() - 1)
	return s + "..."


func _bytes(n: int) -> String:
	if n < 1024:
		return "%d B" % n
	if n < 1024 * 1024:
		return "%.1f K" % (float(n) / 1024.0)
	return "%.2f M" % (float(n) / 1048576.0)


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), CHROME)
	_draw_crumbs()
	var m := _map_rect()
	draw_rect(m, Color("#f4f3f1"))

	if err != "":
		draw_string(mono, Vector2(10, m.position.y + 20), err,
			HORIZONTAL_ALIGNMENT_LEFT, -1, 12, RED)
		_draw_foot()
		return

	if scanning:
		_draw_progress(m)
		_draw_foot()
		return

	tiles = _layout(_here_items(), m)
	for i in range(tiles.size()):
		_draw_tile(i)
	if tiles.is_empty():
		var msg := "nothing here has any bytes in it"
		if not kids.has(here):
			msg = "this directory was never listed"
		draw_string(mono, Vector2(m.position.x + 8, m.position.y + 20), msg,
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, DIM)
	draw_rect(m, Color("#9aa0a6"), false, 1.0)
	_draw_foot()


func _draw_progress(m: Rect2) -> void:
	draw_string(mono, Vector2(m.position.x + 10, m.position.y + 24),
		"scanning: %d directories listed, %d files, %s so far" % [
			n_dirs, n_files, _bytes(walked)],
		HORIZONTAL_ALIGNMENT_LEFT, -1, 11, INK)
	draw_string(mono, Vector2(m.position.x + 10, m.position.y + 40),
		_fit("%d directories still queued -- one `ls -l` each, a few per frame"
			% pending.size(), m.size.x - 20.0, 10),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 10, DIM)
	var bw := m.size.x - 20.0
	var seen := float(n_dirs)
	var frac: float = seen / maxf(1.0, seen + float(pending.size()))
	draw_rect(Rect2(m.position.x + 10, m.position.y + 50, bw, 8), Color("#e2e0dc"))
	draw_rect(Rect2(m.position.x + 10, m.position.y + 50, bw * frac, 8), SEL)


func _draw_tile(i: int) -> void:
	var it: Dictionary = tiles[i]["item"]
	var r: Rect2 = tiles[i]["rect"]
	if r.size.x < 1.0 or r.size.y < 1.0:
		return
	var pal: Array = DIR_HUES if it["dir"] else FILE_HUES
	var col: Color = pal[abs(str(it["name"]).hash()) % pal.size()]
	if i == hover:
		col = col.lightened(0.16)
	draw_rect(r, col)
	if i == sel:
		draw_rect(r, Color("#ffffff"), false, 2.0)
	else:
		draw_rect(r, Color("#f4f3f1"), false, 1.0)
	# Only label a tile that can hold a label. A truncated name on a
	# fifteen-pixel square is a smear that looks like corruption.
	if r.size.x < 34.0 or r.size.y < 14.0:
		return
	draw_string(mono, r.position + Vector2(4, 12),
		_fit(str(it["name"]), r.size.x - 8.0, 11), HORIZONTAL_ALIGNMENT_LEFT,
		-1, 11, Color("#ffffff"))
	if r.size.y >= 27.0:
		draw_string(mono, r.position + Vector2(4, 24),
			_fit(_bytes(int(it["size"])), r.size.x - 8.0, 10),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 10, Color("#e6eef6"))


func _draw_crumbs() -> void:
	draw_rect(Rect2(0, 0, size.x, TOP), Color("#e4e4e4"))
	draw_line(Vector2(0, TOP), Vector2(size.x, TOP), Color("#b9bfc6"))
	var cx := 8.0
	var cs := _crumbs()
	for i in range(cs.size()):
		var label: String = str(cs[i]["label"])
		var w := mono.get_string_size(label, HORIZONTAL_ALIGNMENT_LEFT, -1, 11).x + 10.0
		if cx + w > size.x - 60.0:
			draw_string(mono, Vector2(cx, 15), "...", HORIZONTAL_ALIGNMENT_LEFT,
				-1, 11, DIM)
			break
		var last := i == cs.size() - 1
		draw_string(mono, Vector2(cx, 15), label, HORIZONTAL_ALIGNMENT_LEFT, -1,
			11, INK if last else SEL)
		cx += w
		if not last:
			draw_string(mono, Vector2(cx - 8, 15), ">", HORIZONTAL_ALIGNMENT_LEFT,
				-1, 9, FAINT)
	draw_string(mono, Vector2(-8, 15), _bytes(int(total.get(here, 0))),
		HORIZONTAL_ALIGNMENT_RIGHT, size.x, 10, DIM)


func _draw_foot() -> void:
	var y := size.y - BOT
	draw_rect(Rect2(0, y, size.x, BOT), Color("#e6e3de"))
	draw_line(Vector2(0, y), Vector2(size.x, y), Color("#b3b0ab"))

	var msg := ""
	var pick := hover if hover >= 0 else sel
	if pick >= 0 and pick < tiles.size():
		var it: Dictionary = tiles[pick]["item"]
		var share: float = 0.0
		var t: int = int(total.get(here, 0))
		if t > 0:
			share = float(it["size"]) * 100.0 / float(t)
		msg = "%s   %s   %.1f%% of %s   mode %s" % [it["full"],
			_bytes(int(it["size"])), share, here, it["mode"]]
	elif scanning:
		msg = "scanning..."
	else:
		msg = "%s   %s in %d files, %d directories" % [here,
			_bytes(int(total.get(here, 0))), int(nfiles.get(here, 0)),
			int(ndirs.get(here, 0))]
		if skipped_empty > 0:
			msg += "   (%d empty entries have no tile)" % skipped_empty
	draw_string(mono, Vector2(8, y + 12), _fit(msg, size.x - 16.0, 10),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 10, INK)

	# The reconciliation line. A treemap that quietly disagrees with `df` is
	# worse than no treemap: you would go hunting for the missing megabyte in
	# the picture instead of in the filesystem.
	var line2 := ""
	var col := DIM
	if df_pct >= 0:
		var walked_k := int(total.get(root, 0)) / 1024
		line2 = "df: %s used of %s (%d%%), %s free   |   walked %d K, /proc excluded" % [
			df_used, df_size, df_pct, df_avail, walked_k]
		var said := int(df_used.replace("K", ""))
		if not scanning and abs(said - walked_k) > 1:
			line2 += "   -- DISAGREES with df by %d K" % (said - walked_k)
			col = RED
		elif df_pct >= 90:
			col = RED
	else:
		line2 = "df printed no space table"
		col = RED
	if not df_notes.is_empty():
		line2 = str(df_notes[0])
		col = RED
	draw_string(mono, Vector2(8, y + 24), _fit(line2, size.x - 16.0, 9),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 9, col)
