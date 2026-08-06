# archman.gd — the archive manager.
#
# THIS OS HAS NO TAR AND NO GZIP. `help` lists what there is; there is no
# archiver in it, there are no .tar.gz files on the disk, and a window that
# opened one would be inventing a subsystem. But the *idiom* of an archive
# manager -- pick a bundle, browse its contents as a tree with sizes and
# modes, test it, extract it somewhere -- describes something this machine
# genuinely has: packages.
#
# So this is a package browser wearing File Roller's clothes, and every fact
# in it is a command:
#
#   pkg list                       the bundles
#   cat /var/lib/pkg/<n>/files      what is in one -- "<mode> <hash> <path>"
#   ls -l <dir>                    the size and mode each file has ON DISK
#   pkg verify <n>                 test archive -- what no longer matches
#   cp <src> <dst>                 extract
#
# WHAT I WANTED AND DID NOT GET, so the next person does not go looking:
#
# * `pkg files <name>` does not exist. The verbs are list, owns, verify, diff,
#   reinstall and upgrade -- I ran `pkg` with no arguments and read them off
#   the usage line. The manifest is a plain file at /var/lib/pkg/<n>/files, so
#   the tree is built by cat'ing it. That is the same file `pkg verify` reads,
#   which is why a damaged manifest shows up here as a short package rather
#   than as a clean one.
# * There is no `mkdir`. Extract therefore cannot recreate /usr/bin/... under
#   the destination; it copies each file into ONE existing directory by its
#   basename and says so, loudly, and refuses when two files would collide.
#   With mkdir this would be `mkdir -p $dst/$(dirname $f) && cp` per file.
# * There is no tar, so there is no "create archive". The button is absent
#   rather than greyed out, because a greyed button implies a missing
#   permission and this is a missing program.
#
# THE MANIFEST IS NOT THE DISK. The tree shows the shipped mode beside the
# mode on disk and marks a file MISSING when `ls -l` does not list it. That
# difference is the entire diagnostic value of the window: a package whose
# files are gone still lists them perfectly.

extends Control

var mono: Font
var machine: Object = null
var sh: Callable = Callable()

var pkgs: Array = []          # {name, version, desc}
var psel := 0
var pscroll := 0

var cur := ""                 # package whose tree is loaded
var nodes: Array = []         # {name, full, dir, parent, kids, open, depth,
                              #  size, mode, disk_mode, present, leaf}
var rows: Array = []          # indices into nodes, in display order
var tsel := 0
var tscroll := 0
var manifest_err := ""

var dest := "/tmp"
var editing := false          # typing into the destination field
var report: Array = []        # what extract or verify actually printed
var report_title := ""
var rscroll := 0

var pane := 0                 # 0 packages, 1 tree -- only used when narrow
var err := ""

const TOP := 22.0
const BOT := 30.0
const ROW_H := 14.0
const LIST_W := 168.0
const SPLIT_MIN := 430.0      # below this the two panes take turns

const CHROME := Color("#d6d3ce")
const WHITE := Color("#ffffff")
const INK := Color("#1b1b1b")
const DIM := Color("#5f6469")
const FAINT := Color("#9aa0a6")
const SEL := Color("#3465a4")
const SELTX := Color("#ffffff")
const RED := Color("#b0281a")
const GREEN := Color("#1f6b3a")
const AMBER := Color("#8a6d1f")
const GOLD := Color("#e0a338")
const GOLD_D := Color("#b07d22")


func _ready() -> void:
	focus_mode = Control.FOCUS_ALL
	mouse_filter = Control.MOUSE_FILTER_STOP
	if mono == null:
		mono = preload("res://scripts/uifont.gd").mono()
	refresh()
	set_process(true)


func take_focus() -> void:
	grab_focus()


func _process(_dt: float) -> void:
	pass


func _sh(cmd: String) -> String:
	if sh.is_valid():
		return str(sh.call(cmd))
	if machine == null:
		return ""
	return str(machine.sh_on(0, cmd))


func _has_machine() -> bool:
	return machine != null or sh.is_valid()


# ---------------------------------------------------------------- loading

func refresh() -> void:
	pkgs = []
	err = ""
	if not _has_machine():
		err = "no machine attached"
		nodes = []
		rows = []
		queue_redraw()
		return
	# `pkg list` pads the name to 18 columns and then prints the contents of
	# /var/lib/pkg/<n>/version, which is "<version>  <description>". Splitting
	# the whole line on whitespace would make "the base layout and system
	# identity" into six columns; the name is taken by position and only the
	# first token of the remainder is the version.
	for line in _sh("pkg list").split("\n"):
		if line.strip_edges() == "":
			continue
		if line.begins_with("pkg:") or line.begins_with("usage:"):
			err = line.strip_edges()
			continue
		var nm := line.substr(0, 18).strip_edges() if line.length() > 18 else line.strip_edges()
		var rest := line.substr(18).strip_edges() if line.length() > 18 else ""
		var ver := rest
		var desc := ""
		var sp := rest.find(" ")
		if sp > 0:
			ver = rest.substr(0, sp)
			desc = rest.substr(sp).strip_edges()
		if nm == "":
			continue
		pkgs.append({"name": nm, "version": ver, "desc": desc})
	psel = clampi(psel, 0, maxi(0, pkgs.size() - 1))
	if not pkgs.is_empty():
		load_pkg(str(pkgs[psel]["name"]))
	queue_redraw()


func load_pkg(name: String) -> void:
	cur = name
	nodes = []
	rows = []
	manifest_err = ""
	tsel = 0
	tscroll = 0
	if not _has_machine():
		return
	var out := _sh("cat /var/lib/pkg/%s/files" % name)
	var paths: Array = []
	for line in out.split("\n"):
		var t := line.strip_edges()
		if t == "":
			continue
		if t.begins_with("cat:"):
			# A package with no manifest is not an empty package. Say what the
			# machine said; `pkg verify` reports the same damage.
			manifest_err = t
			continue
		var f := t.split(" ", false)
		if f.size() < 3:
			continue
		paths.append({"mode": f[0], "hash": f[1], "path": f[2]})
	if paths.is_empty() and manifest_err == "":
		manifest_err = "/var/lib/pkg/%s/files listed nothing" % name

	_build_tree(paths)
	_stat_tree()
	_sum_sizes()
	_reflow()
	queue_redraw()


func _build_tree(paths: Array) -> void:
	nodes = [{"name": "/", "full": "/", "dir": true, "parent": -1, "kids": [],
		"open": true, "depth": 0, "size": 0, "mode": "", "disk_mode": "",
		"present": true, "leaf": false}]
	for p in paths:
		var comps: PackedStringArray = str(p["path"]).split("/", false)
		var at := 0
		for i in range(comps.size()):
			var last := i == comps.size() - 1
			var found := -1
			for kidx in nodes[at]["kids"]:
				if nodes[kidx]["name"] == comps[i]:
					found = kidx
					break
			if found < 0:
				var full: String = nodes[at]["full"]
				full = (full if full.ends_with("/") else full + "/") + comps[i]
				nodes.append({"name": comps[i], "full": full, "dir": not last,
					"parent": at, "kids": [], "open": true,
					"depth": int(nodes[at]["depth"]) + 1, "size": 0,
					"mode": str(p["mode"]) if last else "", "disk_mode": "",
					"present": false, "leaf": last})
				found = nodes.size() - 1
				nodes[at]["kids"].append(found)
			at = found


# One `ls -l` per directory that the manifest mentions, not one `stat` per
# file. A package can list forty files in five directories; that is five
# commands instead of forty, and `ls -l` answers size, mode and existence at
# the same time. Existence is the one that matters: a name the manifest has
# and the listing does not is a DELETED file, which is the commonest fault on
# this machine and the reason to open this window at all.
func _stat_tree() -> void:
	var by_dir: Dictionary = {}
	for i in range(nodes.size()):
		if not nodes[i]["leaf"]:
			continue
		var d: String = str(nodes[i]["full"]).get_base_dir()
		if d == "":
			d = "/"
		if not by_dir.has(d):
			by_dir[d] = []
		by_dir[d].append(i)
	for d in by_dir.keys():
		var seen: Dictionary = {}
		for line in _sh("ls -l " + str(d)).split("\n"):
			if line.length() < 8 or "dl-".find(line[0]) < 0:
				continue
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
			var nm := line.substr(i).replace("(DANGLING)", "").strip_edges()
			var arrow := nm.find(" -> ")
			if arrow >= 0:
				nm = nm.substr(0, arrow).strip_edges()
			if nm != "":
				seen[nm] = {"size": int(sz), "mode": mode}
		for idx in by_dir[d]:
			var nm2: String = nodes[idx]["name"]
			if seen.has(nm2):
				nodes[idx]["present"] = true
				nodes[idx]["size"] = int(seen[nm2]["size"])
				nodes[idx]["disk_mode"] = str(seen[nm2]["mode"])


func _sum_sizes() -> void:
	# Bottom up. Children always have a higher index than their parent because
	# the tree is built by appending, so one backwards pass is enough.
	for i in range(nodes.size() - 1, 0, -1):
		var p: int = nodes[i]["parent"]
		if p >= 0:
			nodes[p]["size"] = int(nodes[p]["size"]) + int(nodes[i]["size"])


func _reflow() -> void:
	rows = []
	_walk(0)
	tsel = clampi(tsel, 0, maxi(0, rows.size() - 1))


func _walk(i: int) -> void:
	rows.append(i)
	if not nodes[i]["open"]:
		return
	for k in nodes[i]["kids"]:
		_walk(k)


func _n_missing() -> int:
	var n := 0
	for nd in nodes:
		if nd["leaf"] and not nd["present"]:
			n += 1
	return n


func _leaves() -> Array:
	var out: Array = []
	for nd in nodes:
		if nd["leaf"]:
			out.append(nd)
	return out


# ---------------------------------------------------------------- actions

# "Test archive". `pkg verify` hashes every installed file against the
# manifest and prints one line per finding, so the output is shown raw --
# summarising it would drop the sentence about the manifest itself being
# damaged, which is the finding you must not miss.
func test_pkg() -> void:
	if cur == "" or not _has_machine():
		return
	report_title = "pkg verify " + cur
	report = []
	var out := _sh("pkg verify " + cur)
	for line in out.split("\n"):
		if line.strip_edges() != "":
			report.append(line)
	if report.is_empty():
		report.append("(no output: every file matches what was shipped)")
	rscroll = 0
	queue_redraw()


# Extract. Flat, by basename, because there is no mkdir -- see the header.
# It runs one `cp` per file and keeps every line cp printed, including the
# refusals, because "extracted 7 files" over the top of four failures is how
# you end up debugging a directory that is missing half its contents.
func extract() -> void:
	if cur == "" or not _has_machine():
		return
	report_title = "extract %s to %s" % [cur, dest]
	report = []
	var leaves := _leaves()
	if leaves.is_empty():
		report.append("nothing to extract: the manifest listed no files")
		queue_redraw()
		return
	var taken: Dictionary = {}
	var ok := 0
	var bad := 0
	for nd in leaves:
		var base: String = str(nd["full"]).get_file()
		if taken.has(base):
			report.append("SKIP  %s -- %s already taken by %s (no mkdir on this machine,"
				% [nd["full"], base, taken[base]])
			report.append("      so the tree cannot be recreated and two files cannot share a name)")
			bad += 1
			continue
		if not nd["present"]:
			report.append("SKIP  %s -- not on the disk to copy" % nd["full"])
			bad += 1
			continue
		taken[base] = nd["full"]
		var dst := (dest if dest.ends_with("/") else dest + "/") + base
		var out := _sh("cp %s %s" % [nd["full"], dst]).strip_edges()
		if out == "":
			report.append("  ok  %s -> %s" % [nd["full"], dst])
			ok += 1
		else:
			# cp's own words: "cp: /tmp/no/motd: cannot write".
			report.append("FAIL  " + out)
			bad += 1
	report.append("")
	report.append("%d copied, %d not copied, into %s" % [ok, bad, dest])
	report.append("flat: every file is now a basename in one directory, not a tree")
	rscroll = 0
	queue_redraw()


# ---------------------------------------------------------------- input

func _split() -> bool:
	return size.x >= SPLIT_MIN


func _list_rect() -> Rect2:
	if _split():
		return Rect2(0, TOP, LIST_W, maxf(20.0, size.y - TOP - BOT))
	return Rect2(0, TOP, size.x, maxf(20.0, size.y - TOP - BOT))


func _tree_rect() -> Rect2:
	if _split():
		return Rect2(LIST_W + 1.0, TOP, size.x - LIST_W - 1.0,
			maxf(20.0, size.y - TOP - BOT))
	return Rect2(0, TOP, size.x, maxf(20.0, size.y - TOP - BOT))


func _vis(r: Rect2) -> int:
	return maxi(1, int(r.size.y / ROW_H))


func _gui_input(e: InputEvent) -> void:
	if e is InputEventMouseButton and e.pressed:
		grab_focus()
		var mb := e as InputEventMouseButton
		if not report.is_empty():
			if mb.button_index == MOUSE_BUTTON_WHEEL_UP:
				rscroll = maxi(0, rscroll - 3)
			elif mb.button_index == MOUSE_BUTTON_WHEEL_DOWN:
				rscroll = mini(maxi(0, report.size() - 1), rscroll + 3)
			else:
				report = []
			accept_event()
			queue_redraw()
			return
		var lr := _list_rect()
		var tr := _tree_rect()
		if mb.button_index == MOUSE_BUTTON_WHEEL_UP or mb.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			var d := -3 if mb.button_index == MOUSE_BUTTON_WHEEL_UP else 3
			if _split() and mb.position.x < LIST_W:
				pscroll = clampi(pscroll + d, 0, maxi(0, pkgs.size() - _vis(lr)))
			elif pane == 0 and not _split():
				pscroll = clampi(pscroll + d, 0, maxi(0, pkgs.size() - _vis(lr)))
			else:
				tscroll = clampi(tscroll + d, 0, maxi(0, rows.size() - _vis(tr)))
			accept_event()
			queue_redraw()
			return
		if mb.button_index != MOUSE_BUTTON_LEFT:
			return
		if mb.position.y < TOP:
			accept_event()
			return
		var in_list := (_split() and mb.position.x < LIST_W) or (not _split() and pane == 0)
		if in_list:
			var i := pscroll + int((mb.position.y - lr.position.y) / ROW_H)
			if i >= 0 and i < pkgs.size():
				psel = i
				pane = 0
				load_pkg(str(pkgs[i]["name"]))
				if mb.double_click and not _split():
					pane = 1
		else:
			var j := tscroll + int((mb.position.y - tr.position.y) / ROW_H)
			if j >= 0 and j < rows.size():
				tsel = j
				if mb.double_click:
					_toggle()
		accept_event()
		queue_redraw()
		return

	if not (e is InputEventKey) or not e.pressed:
		return
	var k := e as InputEventKey

	if not report.is_empty():
		if k.keycode == KEY_ESCAPE or k.keycode == KEY_ENTER or k.keycode == KEY_KP_ENTER:
			report = []
		elif k.keycode == KEY_UP:
			rscroll = maxi(0, rscroll - 1)
		elif k.keycode == KEY_DOWN:
			rscroll = mini(maxi(0, report.size() - 1), rscroll + 1)
		accept_event()
		queue_redraw()
		return

	if editing:
		_edit_key(k)
		accept_event()
		queue_redraw()
		return

	# Which pane the keys drive. Split or not, that is `pane` -- when both are
	# on screen TAB moves the keyboard between them and the selected row in
	# each keeps its highlight, so it is always visible which one will move.
	var use_list := pane == 0
	match k.keycode:
		KEY_TAB:
			pane = 1 - pane
		KEY_UP:
			if use_list:
				psel = maxi(0, psel - 1)
				if not pkgs.is_empty():
					load_pkg(str(pkgs[psel]["name"]))
			else:
				tsel = maxi(0, tsel - 1)
		KEY_DOWN:
			if use_list:
				psel = mini(maxi(0, pkgs.size() - 1), psel + 1)
				if not pkgs.is_empty():
					load_pkg(str(pkgs[psel]["name"]))
			else:
				tsel = mini(maxi(0, rows.size() - 1), tsel + 1)
		KEY_LEFT:
			if not use_list:
				_collapse()
		KEY_RIGHT:
			if not use_list:
				_expand()
		KEY_ENTER, KEY_KP_ENTER:
			if use_list:
				pane = 1
			else:
				_toggle()
		KEY_X:
			extract()
		KEY_V:
			test_pkg()
		KEY_D:
			editing = true
		KEY_R, KEY_F5:
			refresh()
		_:
			return
	_clamp()
	accept_event()
	queue_redraw()


func _edit_key(k: InputEventKey) -> void:
	if k.keycode == KEY_ENTER or k.keycode == KEY_KP_ENTER or k.keycode == KEY_ESCAPE:
		editing = false
		if dest == "":
			dest = "/tmp"
		return
	if k.keycode == KEY_BACKSPACE:
		dest = dest.substr(0, maxi(0, dest.length() - 1))
		return
	var c := k.unicode
	if c >= 32 and c < 127:
		dest += String.chr(c)


func _toggle() -> void:
	if tsel < 0 or tsel >= rows.size():
		return
	var i: int = rows[tsel]
	if nodes[i]["leaf"]:
		return
	nodes[i]["open"] = not nodes[i]["open"]
	var was: int = rows[tsel]
	_reflow()
	for j in range(rows.size()):
		if rows[j] == was:
			tsel = j
			break


func _expand() -> void:
	if tsel < 0 or tsel >= rows.size():
		return
	var i: int = rows[tsel]
	if not nodes[i]["leaf"] and not nodes[i]["open"]:
		_toggle()
	elif tsel + 1 < rows.size():
		tsel += 1


func _collapse() -> void:
	if tsel < 0 or tsel >= rows.size():
		return
	var i: int = rows[tsel]
	if not nodes[i]["leaf"] and nodes[i]["open"]:
		_toggle()
		return
	var p: int = nodes[i]["parent"]
	if p >= 0:
		for j in range(rows.size()):
			if rows[j] == p:
				tsel = j
				break


func _clamp() -> void:
	var lv := _vis(_list_rect())
	var tv := _vis(_tree_rect())
	psel = clampi(psel, 0, maxi(0, pkgs.size() - 1))
	tsel = clampi(tsel, 0, maxi(0, rows.size() - 1))
	if psel < pscroll:
		pscroll = psel
	elif psel >= pscroll + lv:
		pscroll = psel - lv + 1
	pscroll = clampi(pscroll, 0, maxi(0, pkgs.size() - lv))
	if tsel < tscroll:
		tscroll = tsel
	elif tsel >= tscroll + tv:
		tscroll = tsel - tv + 1
	tscroll = clampi(tscroll, 0, maxi(0, rows.size() - tv))


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
	return "%.1f M" % (float(n) / 1048576.0)


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), CHROME)
	_draw_top()
	if err != "":
		draw_string(mono, Vector2(10, TOP + 20), err,
			HORIZONTAL_ALIGNMENT_LEFT, -1, 12, RED)
		_draw_foot()
		return
	if not report.is_empty():
		_draw_report()
		_draw_foot()
		return
	if _split():
		_draw_list(_list_rect())
		_draw_tree(_tree_rect())
	elif pane == 0:
		_draw_list(_list_rect())
	else:
		_draw_tree(_tree_rect())
	_draw_foot()


func _draw_top() -> void:
	draw_rect(Rect2(0, 0, size.x, TOP), Color("#e4e4e4"))
	draw_line(Vector2(0, TOP), Vector2(size.x, TOP), Color("#b9bfc6"))
	var head := "%d packages" % pkgs.size()
	if cur != "":
		head = "%s   %d files" % [cur, _leaves().size()]
		var miss := _n_missing()
		if miss > 0:
			head += "   %d MISSING" % miss
	draw_string(mono, Vector2(8, 15), _fit(head, size.x - 150.0, 11),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 11,
		RED if _n_missing() > 0 else INK)
	var d := "to: " + dest
	if editing:
		d += "_"
	draw_string(mono, Vector2(-8, 15), _fit(d, 140.0, 10),
		HORIZONTAL_ALIGNMENT_RIGHT, size.x, 10, SEL if editing else DIM)


func _draw_list(r: Rect2) -> void:
	draw_rect(r, WHITE)
	if _split():
		draw_line(Vector2(r.size.x, r.position.y),
			Vector2(r.size.x, r.position.y + r.size.y), Color("#b3b0ab"))
	var vis := _vis(r)
	for i in range(pscroll, mini(pkgs.size(), pscroll + vis)):
		var y := r.position.y + (i - pscroll) * ROW_H
		var picked := i == psel
		if picked:
			draw_rect(Rect2(r.position.x, y, r.size.x, ROW_H), SEL)
		_box(Vector2(r.position.x + 4, y + 3), picked)
		var nm: String = pkgs[i]["name"]
		draw_string(mono, Vector2(r.position.x + 18, y + 11),
			_fit(nm, r.size.x - 62.0, 11), HORIZONTAL_ALIGNMENT_LEFT, -1, 11,
			SELTX if picked else INK)
		draw_string(mono, Vector2(r.position.x, y + 11),
			_fit(str(pkgs[i]["version"]), 42.0, 9), HORIZONTAL_ALIGNMENT_RIGHT,
			r.size.x - 4.0, 9, SELTX if picked else FAINT)
	if pkgs.is_empty():
		draw_string(mono, Vector2(r.position.x + 8, r.position.y + 16),
			"`pkg list` printed nothing", HORIZONTAL_ALIGNMENT_LEFT, -1, 10, DIM)


# A little crate, drawn not loaded. It is the one thing on this window that is
# decoration, and it is eight rectangles.
func _box(at: Vector2, picked: bool) -> void:
	var body := Color("#e8d3a4") if not picked else Color("#f2e6cd")
	draw_rect(Rect2(at.x, at.y, 10, 8), body)
	draw_rect(Rect2(at.x, at.y, 10, 8), GOLD_D, false, 1.0)
	draw_line(Vector2(at.x, at.y + 3), Vector2(at.x + 10, at.y + 3), GOLD_D)
	draw_rect(Rect2(at.x + 4, at.y, 2, 8), GOLD)


func _draw_tree(r: Rect2) -> void:
	draw_rect(r, WHITE)
	if manifest_err != "":
		draw_string(mono, Vector2(r.position.x + 8, r.position.y + 18),
			_fit(manifest_err, r.size.x - 16.0, 11), HORIZONTAL_ALIGNMENT_LEFT,
			-1, 11, RED)
		draw_string(mono, Vector2(r.position.x + 8, r.position.y + 32),
			_fit("the manifest is a file on this disk and can be damaged too -- `pkg verify` agrees",
				r.size.x - 16.0, 9), HORIZONTAL_ALIGNMENT_LEFT, -1, 9, DIM)
		if rows.is_empty():
			return
	var vis := _vis(r)
	var wide := r.size.x >= 300.0
	for j in range(tscroll, mini(rows.size(), tscroll + vis)):
		var i: int = rows[j]
		var nd: Dictionary = nodes[i]
		var y := r.position.y + (j - tscroll) * ROW_H
		var picked := j == tsel
		if picked:
			draw_rect(Rect2(r.position.x, y, r.size.x, ROW_H), SEL)
		elif j % 2 == 1:
			draw_rect(Rect2(r.position.x, y, r.size.x, ROW_H), Color("#f5f4f2"))
		var x := r.position.x + 4.0 + float(nd["depth"]) * 10.0
		var col := INK
		if not nd["leaf"]:
			draw_string(mono, Vector2(x, y + 11), "-" if nd["open"] else "+",
				HORIZONTAL_ALIGNMENT_LEFT, -1, 11, SELTX if picked else DIM)
			col = Color("#1b4f8f")
		elif not nd["present"]:
			col = RED
		x += 10.0
		var nm: String = nd["name"]
		if nd["leaf"] and not nd["present"]:
			nm += "   MISSING"
		var right := 0.0
		if wide:
			right = 118.0
		draw_string(mono, Vector2(x, y + 11),
			_fit(nm, maxf(30.0, r.size.x - (x - r.position.x) - right - 6.0), 11),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, SELTX if picked else col)
		if not wide:
			continue
		# Shipped mode, then the mode on disk when they differ. A file that is
		# present with the wrong mode is invisible to `ls` unless you know what
		# it was supposed to be, and this column is what it was supposed to be.
		var modes := str(nd["mode"])
		if nd["leaf"] and nd["present"] and str(nd["disk_mode"]) != str(nd["mode"]):
			modes = "%s->%s" % [nd["mode"], nd["disk_mode"]]
		draw_string(mono, Vector2(r.position.x + r.size.x - 116.0, y + 11),
			_fit(modes, 56.0, 10), HORIZONTAL_ALIGNMENT_LEFT, -1, 10,
			SELTX if picked else (AMBER if modes.find("->") >= 0 else FAINT))
		draw_string(mono, Vector2(0, y + 11), _bytes(int(nd["size"])),
			HORIZONTAL_ALIGNMENT_RIGHT, r.position.x + r.size.x - 6.0, 10,
			SELTX if picked else DIM)
	if rows.is_empty() and manifest_err == "":
		draw_string(mono, Vector2(r.position.x + 8, r.position.y + 18),
			"no package selected", HORIZONTAL_ALIGNMENT_LEFT, -1, 10, DIM)


func _draw_report() -> void:
	var r := Rect2(0, TOP, size.x, maxf(20.0, size.y - TOP - BOT))
	draw_rect(r, Color("#fbfbf9"))
	draw_string(mono, Vector2(8, r.position.y + 13), _fit(report_title, size.x - 16.0, 11),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 11, INK)
	draw_line(Vector2(0, r.position.y + 18), Vector2(size.x, r.position.y + 18),
		Color("#dcd9d4"))
	var vis := maxi(1, int((r.size.y - 20.0) / ROW_H))
	for i in range(rscroll, mini(report.size(), rscroll + vis)):
		var line: String = str(report[i])
		var col := INK
		# Every word `pkg verify` can put in its status column. It grew
		# TRUNCATED (a file that is the shipped bytes and then stops, which is
		# what an interrupted write leaves) and got MODE back after a bug had
		# been printing that column blank -- and this list knew neither, so
		# the archive manager listed those findings without marking them. A
		# view that quietly downgrades a finding is worse than one that shows
		# nothing, because the player reads the absence as good news.
		if line.begins_with("FAIL") or line.find("MISSING") >= 0 \
				or line.find("CHANGED") >= 0 or line.find("TRUNCATED") >= 0 \
				or line.find("MODE") >= 0:
			col = RED
		elif line.begins_with("  ok"):
			col = GREEN
		elif line.begins_with("SKIP"):
			col = AMBER
		draw_string(mono, Vector2(8, r.position.y + 32.0 + (i - rscroll) * ROW_H),
			_fit(line, size.x - 16.0, 10), HORIZONTAL_ALIGNMENT_LEFT, -1, 10, col)


func _draw_foot() -> void:
	var y := size.y - BOT
	draw_rect(Rect2(0, y, size.x, BOT), Color("#e6e3de"))
	draw_line(Vector2(0, y), Vector2(size.x, y), Color("#b3b0ab"))
	var msg := ""
	if not report.is_empty():
		msg = "%d lines -- any key closes this" % report.size()
	elif editing:
		msg = "typing the destination directory; enter accepts. There is no mkdir: it must exist"
	elif tsel >= 0 and tsel < rows.size():
		var nd: Dictionary = nodes[rows[tsel]]
		msg = str(nd["full"])
		if nd["leaf"]:
			msg += "   shipped %s" % nd["mode"]
			if nd["present"]:
				msg += "   on disk %s, %d bytes" % [nd["disk_mode"], nd["size"]]
			else:
				msg += "   NOT ON THE DISK"
		else:
			msg += "   %s in %d files" % [_bytes(int(nd["size"])), _count_leaves(rows[tsel])]
	elif psel >= 0 and psel < pkgs.size():
		msg = "%s %s -- %s" % [pkgs[psel]["name"], pkgs[psel]["version"], pkgs[psel]["desc"]]
	draw_string(mono, Vector2(8, y + 12), _fit(msg, size.x - 16.0, 10),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 10, INK)
	draw_string(mono, Vector2(8, y + 23),
		_fit("X extract   V test (pkg verify)   D destination   tab pane   -- no tar here, these are packages",
			size.x - 16.0, 9), HORIZONTAL_ALIGNMENT_LEFT, -1, 9, Color("#7c8085"))


func _count_leaves(i: int) -> int:
	if nodes[i]["leaf"]:
		return 1
	var n := 0
	for k in nodes[i]["kids"]:
		n += _count_leaves(k)
	return n
