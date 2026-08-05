# Throwaway: exercise the six new apps against a real machine, at real sizes,
# and take screenshots. Deleted when done.
extends SceneTree

var machine: Object
var shots := "user://shots"

func _init() -> void:
	machine = ClassDB.instantiate("NominalStation")
	machine.take_ticket(1, 1)
	DirAccess.make_dir_recursive_absolute(shots)
	await process_frame
	await _clock()
	await _imgview()
	await _archman()
	await _duview()
	await _charmap()
	await _search()
	print("shots in ", ProjectSettings.globalize_path(shots))
	quit(0)


func _mk(script: String) -> Control:
	var c: Control = load("res://scripts/%s.gd" % script).new()
	c.mono = ThemeDB.fallback_font
	c.machine = machine
	c.position = Vector2.ZERO
	root.add_child(c)
	return c


func _shot(c: Control, name: String, w: int, h: int) -> void:
	DisplayServer.window_set_size(Vector2i(w, h))
	c.size = Vector2(w, h)
	c.queue_redraw()
	await process_frame
	await process_frame
	await process_frame
	var img := root.get_texture().get_image()
	img.save_png("%s/%s.png" % [shots, name])


func _clock() -> void:
	var c := _mk("clock")
	await process_frame
	print("--- clock: cal %d-%d-%d  wd=%d  days=%d" % [c.cal_y, c.cal_m, c.cal_d,
		c._weekday(c.cal_y, c.cal_m, 1), c._days_in(c.cal_y, c.cal_m)])
	print("    leap 2024=%s 2025=%s 2100=%s" % [c._leap(2024), c._leap(2025), c._leap(2100)])
	c._shift_month(-8)
	print("    after -8 months: %d-%d-%d" % [c.cal_y, c.cal_m, c.cal_d])
	c._today()
	c.timer_left = 65.0
	c.timer_set = 300.0
	c.watch = 91.4
	c.laps = [12.0, 40.5, 91.4]
	for t in range(4):
		c.tab = t
		await _shot(c, "clock%d" % t, 640, 420)
	c.tab = 0
	await _shot(c, "clock_small", 320, 240)
	c.tab = 1
	await _shot(c, "clock_cal_small", 320, 240)
	c.queue_free()
	await process_frame


func _imgview() -> void:
	var v := _mk("imgview")
	await process_frame
	print("--- imgview: dir=%s entries=%d idx=%d lines=%d err=%s" % [
		v.dir, v.entries.size(), v.idx, v.lines.size(), v.err])
	await _shot(v, "img_pictures", 640, 420)
	v.open_path("/home/nomowner/notes.txt")
	print("    notes.txt: %d lines, zoom %.2f, err=%s" % [v.lines.size(), v.zoom, v.err])
	await _shot(v, "img_notes", 640, 420)
	v.one_to_one()
	await _shot(v, "img_notes_1to1", 320, 240)
	v.open_path("/bin/ls")
	print("    /bin/ls -> err=%s" % v.err)
	await _shot(v, "img_binary", 640, 300)
	v.open_path("/etc")
	print("    /etc dir -> entries=%d first=%s err=%s" % [v.entries.size(), v.path, v.err])
	v._step(3)
	print("    after 3 steps: %s (%d lines)" % [v.path, v.lines.size()])
	v.fit()
	await _shot(v, "img_etc", 640, 420)
	v.queue_free()
	await process_frame


func _archman() -> void:
	var a := _mk("archman")
	await process_frame
	print("--- archman: %d packages, cur=%s nodes=%d rows=%d missing=%d" % [
		a.pkgs.size(), a.cur, a.nodes.size(), a.rows.size(), a._n_missing()])
	await _shot(a, "arch_wide", 700, 420)
	for i in range(a.pkgs.size()):
		if a.pkgs[i]["name"] == "nomfun":
			a.psel = i
			a.load_pkg("nomfun")
	print("    nomfun: %d nodes, %d rows, total %d bytes" % [a.nodes.size(),
		a.rows.size(), a.nodes[0]["size"]])
	await _shot(a, "arch_nomfun", 700, 420)
	a.test_pkg()
	print("    verify report: %d lines, first=%s" % [a.report.size(),
		a.report[0] if not a.report.is_empty() else ""])
	await _shot(a, "arch_verify", 700, 420)
	a.report = []
	a.extract()
	print("    extract report:")
	for l in a.report:
		print("      ", l)
	await _shot(a, "arch_extract", 700, 420)
	a.report = []
	# a package with a large tree, to check the indent and the split
	a.load_pkg("nomsh")
	print("    nomsh: %d rows deep tree" % a.rows.size())
	await _shot(a, "arch_narrow", 320, 240)
	a.queue_free()
	await process_frame


func _duview() -> void:
	var d := _mk("duview")
	await process_frame
	var guard := 0
	while d.scanning and guard < 600:
		await process_frame
		guard += 1
	print("--- duview: scanned %d dirs, %d files, %d bytes, %d frames" % [
		d.n_dirs, d.n_files, d.walked, guard])
	print("    total / = %d   df says %s used of %s (%d%%)" % [
		d.total.get("/", 0), d.df_used, d.df_size, d.df_pct])
	print("    walked K = %d, df K = %d" % [int(d.total.get("/", 0)) / 1024,
		int(str(d.df_used).replace("K", ""))])
	await _shot(d, "du_root", 700, 460)
	d.enter("/usr")
	await _shot(d, "du_usr", 700, 460)
	d.enter("/usr/bin")
	print("    /usr/bin = %d bytes over %d entries" % [d.total.get("/usr/bin", 0),
		(d.kids.get("/usr/bin", []) as Array).size()])
	await _shot(d, "du_usrbin", 700, 460)
	await _shot(d, "du_small", 320, 240)
	d.up()
	d.up()
	print("    after two ups: here=%s" % d.here)
	d.queue_free()
	await process_frame


func _charmap() -> void:
	var m := _mk("charmap")
	await process_frame
	print("--- charmap: %d blocks" % m.BLOCKS.size())
	for i in range(m.BLOCKS.size()):
		var b: Array = m.BLOCKS[i]
		print("    %-24s %d/%d" % [b[2], m._covered(i), int(b[1]) - int(b[0]) + 1])
	await _shot(m, "char_ascii", 640, 420)
	m.block = 5
	m._load_block()
	await _shot(m, "char_cyrillic", 640, 420)
	m.block = 11
	m._load_block()
	print("    box drawing covered: %d" % m._covered(11))
	await _shot(m, "char_box", 640, 420)
	m.block = 1
	m._load_block()
	m.sel = 5
	m._append()
	m.sel = 9
	m._append()
	print("    buffer after two appends: %s" % m.buffer)
	m.searching = true
	m.query = "U+00E9"
	m._run_search()
	print("    search U+00E9 -> %d hits, first %d" % [m.hits.size(),
		int(m.hits[0]) if not m.hits.is_empty() else -1])
	m.query = "greek"
	m._run_search()
	print("    search 'greek' -> %d hits" % m.hits.size())
	m.query = "zzz"
	m._run_search()
	print("    search 'zzz' -> %d hits" % m.hits.size())
	await _shot(m, "char_search", 640, 420)
	m.searching = false
	m.hits = []
	m.block = 0
	m._load_block()
	await _shot(m, "char_small", 320, 240)
	m.queue_free()
	await process_frame


func _search() -> void:
	var s := _mk("search")
	await process_frame
	s.path = "/etc"
	s.pattern = "*.conf"
	s.run()
	print("--- search: cmd=%s -> %d results, note=%s" % [s.cmd, s.results.size(), s.note])
	for r in s.results:
		print("      ", r["text"])
	await _shot(s, "search_conf", 640, 420)
	s.path = "/nope"
	s.pattern = ""
	s.run()
	print("    /nope: %d results, first=%s err=%s" % [s.results.size(),
		s.results[0]["text"] if not s.results.is_empty() else "",
		s.results[0]["err"] if not s.results.is_empty() else false])
	await _shot(s, "search_err", 640, 300)
	s.path = "/"
	s.pattern = "*.so*"
	s.kind = 1
	s.run()
	print("    / *.so* -type f: cmd=%s -> %d" % [s.cmd, s.results.size()])
	for r in s.results:
		print("      ", r["text"])
	s.pattern = "it's"
	s.run()
	print("    quote refusal: cmd='%s' note=%s" % [s.cmd, s.note])
	await _shot(s, "search_refuse", 640, 300)
	s.pattern = "*"
	s.kind = 2
	s.path = "/var"
	s.run()
	print("    /var -type d: %d" % s.results.size())
	await _shot(s, "search_small", 320, 240)
	s.queue_free()
	await process_frame
