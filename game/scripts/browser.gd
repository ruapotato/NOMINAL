# browser.gd — a window onto the machine's own web.
#
# The rule this app is built to obey: it contains no page content. Every byte
# it draws came back from `links` running on the emulated machine, through the
# machine's own resolver and its own /etc/hosts. So if the player breaks name
# resolution, this window breaks in exactly the same way the terminal does,
# and for the same reason. The desktop is a view, never a second source of
# truth.
#
# The only thing it knows by itself is a bookmark list -- addresses, not pages.
# That is chrome, the same as a browser's bookmarks bar, and a bookmark to a
# host that is unreachable will fail honestly when you click it.

extends Control

var mono: Font
var machine: Object = null

# --- state ------------------------------------------------------------
var url := ""                  # "" is the bookmarks page
var addr := ""                 # what is typed in the address bar
var editing := false           # is the address bar taking keys
var lines: PackedStringArray = PackedStringArray()
var links: Array = []          # { row, col, text, url }
var history: Array = []        # urls, most recent last
var scroll := 0
var status := ""

const FS := 12                 # page font size
const ROW := 15.0              # page line height
const CHROME := 32.0           # toolbar height
const FOOT := 18.0             # status bar height
const PAD := 10.0

const PAGE_BG   := Color("#ffffff")
const TEXT      := Color("#1c1c1c")
const HEAD      := Color("#1a1a1a")
const LINK      := Color("#1a4fa0")
const GREY      := Color("#d6d3ce")   # MATE-ish chrome
const GREY_DK   := Color("#9a968f")
const GREY_TX   := Color("#2b2b2b")
const FIELD     := Color("#ffffff")
const DIM       := Color("#6b6b6b")

# Addresses only. No page bodies -- those come from the machine.
const BOOKMARKS := [
	["wiki.nomnix.org",     "NomnixOS documentation"],
	["intranet.internal",   "staff intranet"],
	["helpdesk.internal",   "the ticket queue"],
	["status.internal",     "service status board"],
	["notices.internal",    "all-staff notices"],
	["cafeteria.internal",  "this week's menu"],
	["home.internal",       "staff pages"],
	["blog.internal",       "the previous admin's notes"],
	["oldwiki.internal",    "Project HALYARD (archived)"],
	["support.internal",    "the support desk"],
	["bofh.nomnix.org",     "not for customers"],
	["nominal.local",       "this machine"],
]

var _re_url: RegEx = null      # host[/path] anywhere in a line
var _re_path: RegEx = null     # a leading /path as the first word of a line


func _ready() -> void:
	focus_mode = Control.FOCUS_ALL
	mouse_filter = Control.MOUSE_FILTER_STOP
	if mono == null:
		mono = ThemeDB.fallback_font
	_re_url = RegEx.new()
	_re_url.compile("[a-z0-9][a-z0-9-]*(\\.[a-z0-9-]+)+(/[A-Za-z0-9._~/-]*)?")
	_re_path = RegEx.new()
	_re_path.compile("^\\s+(/[a-z0-9][a-z0-9._-]*(/[a-z0-9._-]+)?)(\\s|$)")
	_home()


func take_focus() -> void:
	grab_focus()


# --- fetching ---------------------------------------------------------

func _home() -> void:
	url = ""
	addr = ""
	scroll = 0
	var out := "bookmarks\n=========\n\n"
	for b in BOOKMARKS:
		out += "  %s%s%s\n" % [b[0], " ".repeat(max(1, 24 - b[0].length())), b[1]]
	out += "\nEvery page here is fetched by running `links` on the machine, so a\n"
	out += "bookmark that will not load is the machine telling you something.\n"
	lines = out.split("\n")
	_scan_links()
	status = "%d bookmarks" % BOOKMARKS.size()
	queue_redraw()


func _fetch(u: String) -> void:
	u = u.strip_edges()
	if u == "":
		_home()
		return
	if u.begins_with("about:") or u == "home":
		_home()
		return
	url = u
	addr = u
	scroll = 0
	if machine == null:
		lines = PackedStringArray(["browser: no machine attached."])
		links = []
		status = "offline"
		queue_redraw()
		return
	# The real browser, on the real machine, through the real resolver.
	var out: String = machine.sh_on(0, "links " + u)
	lines = out.replace("\t", "    ").split("\n")
	while lines.size() > 1 and lines[lines.size() - 1] == "":
		lines.remove_at(lines.size() - 1)
	_scan_links()
	status = "%s -- %d lines" % [u, lines.size()]
	if out.find("cannot resolve") >= 0 or out.find("nothing responded") >= 0:
		status = "failed: " + u
	queue_redraw()


func _go(u: String) -> void:
	history.append(url)
	if history.size() > 64:
		history.remove_at(0)
	_fetch(u)


func _back() -> void:
	if history.is_empty():
		return
	var u: String = history.pop_back()
	_fetch(u)


func _host_of(u: String) -> String:
	var s := u
	var i := s.find("/")
	return s.substr(0, i) if i >= 0 else s


# --- link discovery ---------------------------------------------------
#
# `links` prints plain text, so there is no markup to trust. What there IS,
# reliably, is the way these pages are written: an address appears as itself
# (wiki.nomnix.org/boot) and an index entry appears as an indented path as the
# first word of its line (  /boot   how this system boots). Both are worth
# making clickable; anything else is left as text.

# A dotted word is only an address if it ends in a domain this network uses,
# or if it is entirely numeric -- otherwise zbl.cfg and boot.log.1 turn blue
# and the page starts lying about what you can click.
const TLDS := ["internal", "local", "org", "com", "net"]

func _looks_like_site(t: String) -> bool:
	var host := _host_of(t)
	var parts := host.split(".")
	if parts.size() < 2:
		return false
	var numeric := true
	for p in parts:
		if not p.is_valid_int():
			numeric = false
			break
	if numeric:
		return parts.size() == 4
	return TLDS.has(parts[parts.size() - 1])


func _scan_links() -> void:
	links = []
	var host := _host_of(url)
	for r in range(lines.size()):
		var line: String = lines[r]
		if line == "":
			continue
		for m in _re_url.search_all(line):
			var t: String = m.get_string()
			while t.length() > 0 and ".,:;)".find(t[t.length() - 1]) >= 0:
				t = t.substr(0, t.length() - 1)
			if not _looks_like_site(t):
				continue
			# a filesystem path that happens to contain a dot is not a site
			if m.get_start() > 0 and "/.-_".find(line[m.get_start() - 1]) >= 0:
				continue
			links.append({"row": r, "col": m.get_start(), "text": t, "url": t})
		if host != "":
			var p := _re_path.search(line)
			if p:
				var path: String = p.get_string(1)
				links.append({"row": r, "col": p.get_start(1),
					"text": path, "url": host + path})


func _link_at(pos: Vector2) -> Dictionary:
	var r := int((pos.y - CHROME - 4.0) / ROW) + scroll
	for l in links:
		if int(l["row"]) != r:
			continue
		var line: String = lines[int(l["row"])]
		var x0: float = PAD + mono.get_string_size(line.substr(0, int(l["col"])),
			HORIZONTAL_ALIGNMENT_LEFT, -1, FS).x
		var w: float = mono.get_string_size(String(l["text"]),
			HORIZONTAL_ALIGNMENT_LEFT, -1, FS).x
		if pos.x >= x0 and pos.x <= x0 + w:
			return l
	return {}


# --- chrome geometry --------------------------------------------------

func _r_back() -> Rect2:  return Rect2(6, 6, 44, 20)
func _r_home() -> Rect2:  return Rect2(54, 6, 46, 20)
func _r_addr() -> Rect2:  return Rect2(104, 6, max(80.0, size.x - 104 - 50), 20)
func _r_go()   -> Rect2:  return Rect2(size.x - 42, 6, 36, 20)


# --- input ------------------------------------------------------------

func _gui_input(e: InputEvent) -> void:
	if e is InputEventMouseButton and e.pressed:
		grab_focus()
		var mb := e as InputEventMouseButton
		if mb.button_index == MOUSE_BUTTON_WHEEL_UP:
			scroll = max(0, scroll - 3); queue_redraw(); return
		if mb.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			scroll = min(max(0, lines.size() - 2), scroll + 3); queue_redraw(); return
		if mb.button_index != MOUSE_BUTTON_LEFT:
			return
		if _r_back().has_point(mb.position):
			editing = false; _back(); return
		if _r_home().has_point(mb.position):
			editing = false; history.append(url); _home(); return
		if _r_go().has_point(mb.position):
			editing = false; _go(addr); return
		if _r_addr().has_point(mb.position):
			editing = true; queue_redraw(); return
		editing = false
		var l := _link_at(mb.position)
		if not l.is_empty():
			_go(String(l["url"]))
		else:
			queue_redraw()
		return

	if not (e is InputEventKey) or not e.pressed:
		return
	var k := e as InputEventKey

	if editing:
		accept_event()
		match k.keycode:
			KEY_ENTER, KEY_KP_ENTER:
				editing = false
				_go(addr)
			KEY_ESCAPE:
				editing = false
				addr = url
			KEY_BACKSPACE:
				if addr.length() > 0:
					addr = addr.substr(0, addr.length() - 1)
			_:
				if k.unicode >= 32 and k.unicode < 127:
					addr += char(k.unicode)
		queue_redraw()
		return

	accept_event()
	match k.keycode:
		KEY_L:
			if k.ctrl_pressed:
				editing = true
				addr = url
			else:
				return
		KEY_ENTER, KEY_KP_ENTER:
			editing = true
			addr = url
		KEY_BACKSPACE:
			_back()
		KEY_LEFT:
			if k.alt_pressed:
				_back()
		KEY_F5:
			if url != "":
				_fetch(url)
			else:
				_home()
		KEY_HOME:
			scroll = 0
		KEY_END:
			scroll = max(0, lines.size() - _rows())
		KEY_DOWN:
			scroll = min(max(0, lines.size() - 2), scroll + 2)
		KEY_UP:
			scroll = max(0, scroll - 2)
		KEY_PAGEDOWN:
			scroll = min(max(0, lines.size() - 2), scroll + _rows() - 2)
		KEY_PAGEUP:
			scroll = max(0, scroll - (_rows() - 2))
		KEY_ESCAPE:
			pass
	queue_redraw()


func _rows() -> int:
	return max(1, int((size.y - CHROME - FOOT - 6.0) / ROW))


# --- drawing ----------------------------------------------------------

func _button(r: Rect2, label: String, on: bool) -> void:
	draw_rect(r, Color("#e8e5e0") if on else Color("#dedbd6"))
	draw_rect(r, GREY_DK, false)
	var c := GREY_TX if on else DIM
	draw_string(mono, Vector2(r.position.x, r.position.y + 14), label,
		HORIZONTAL_ALIGNMENT_CENTER, r.size.x, 11, c)


func _draw() -> void:
	# toolbar
	draw_rect(Rect2(0, 0, size.x, CHROME), GREY)
	draw_line(Vector2(0, CHROME - 1), Vector2(size.x, CHROME - 1), GREY_DK)
	_button(_r_back(), "< back", not history.is_empty())
	_button(_r_home(), "home", true)
	_button(_r_go(), "go", true)

	var ar := _r_addr()
	draw_rect(ar, FIELD)
	draw_rect(ar, Color("#3a6ea5") if editing else GREY_DK, false)
	var shown := addr if editing or addr != "" else "bookmarks"
	var avail := ar.size.x - 10.0
	while shown.length() > 4 and mono.get_string_size(shown,
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11).x > avail:
		shown = shown.substr(1)
	draw_string(mono, Vector2(ar.position.x + 5, ar.position.y + 14), shown,
		HORIZONTAL_ALIGNMENT_LEFT, -1, 11,
		GREY_TX if (editing or addr != "") else DIM)
	if editing:
		var cx: float = ar.position.x + 5 + mono.get_string_size(shown,
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11).x + 1
		cx = min(cx, ar.position.x + ar.size.x - 3)
		draw_line(Vector2(cx, ar.position.y + 3),
			Vector2(cx, ar.position.y + 17), GREY_TX)

	# page
	var top := CHROME
	var bot: float = max(top + ROW, size.y - FOOT)
	draw_rect(Rect2(0, top, size.x, bot - top), PAGE_BG)

	var rows := _rows()
	var first: int = clampi(scroll, 0, max(0, lines.size() - 1))
	var y := top + 4.0 + ROW - 4.0
	for i in range(first, min(lines.size(), first + rows)):
		var line: String = lines[i]
		var col := TEXT
		if line.length() > 3 and line.strip_edges() != "" \
				and (line.begins_with("=") or line.begins_with("-")) \
				and line.strip_edges().replace("=", "").replace("-", "") == "":
			col = DIM
		elif i == 0 or (line != "" and not line.begins_with(" ") \
				and line == line.to_upper() and line.length() > 6):
			col = HEAD
		draw_string(mono, Vector2(PAD, y), line,
			HORIZONTAL_ALIGNMENT_LEFT, -1, FS, col)
		y += ROW

	# links, drawn over the text in blue and underlined
	for l in links:
		var r: int = int(l["row"])
		if r < first or r >= first + rows:
			continue
		var line2: String = lines[r]
		var x0: float = PAD + mono.get_string_size(line2.substr(0, int(l["col"])),
			HORIZONTAL_ALIGNMENT_LEFT, -1, FS).x
		var ly: float = top + 4.0 + (r - first) * ROW + ROW - 4.0
		var txt: String = String(l["text"])
		draw_rect(Rect2(x0, ly - ROW + 4.0,
			mono.get_string_size(txt, HORIZONTAL_ALIGNMENT_LEFT, -1, FS).x,
			ROW - 1.0), PAGE_BG)
		draw_string(mono, Vector2(x0, ly), txt,
			HORIZONTAL_ALIGNMENT_LEFT, -1, FS, LINK)
		draw_line(Vector2(x0, ly + 2),
			Vector2(x0 + mono.get_string_size(txt,
				HORIZONTAL_ALIGNMENT_LEFT, -1, FS).x, ly + 2), LINK)

	# status bar
	draw_rect(Rect2(0, bot, size.x, size.y - bot), GREY)
	draw_line(Vector2(0, bot), Vector2(size.x, bot), GREY_DK)
	draw_string(mono, Vector2(6, bot + 13), status,
		HORIZONTAL_ALIGNMENT_LEFT, -1, 10, GREY_TX)
	if lines.size() > rows:
		draw_string(mono, Vector2(size.x - 210, bot + 13),
			"%d/%d  wheel or arrows, backspace goes back"
				% [first + 1, lines.size()],
			HORIZONTAL_ALIGNMENT_LEFT, -1, 10, DIM)
