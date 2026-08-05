# browser.gd — a window onto the machine's own web.
#
# The rule this app is built to obey: it contains no page content. Every byte
# it draws came back from `links --raw` running on the emulated machine,
# through the machine's own resolver and its own /etc/hosts. So if the player
# breaks name resolution, this window breaks in exactly the same way the
# terminal does, and for the same reason. The desktop is a view, never a
# second source of truth.
#
# The only thing it knows by itself is a bookmark list -- addresses, not
# pages. That is chrome, the same as a browser's bookmarks bar, and a bookmark
# to a host that is unreachable will fail honestly when you click it.
#
# WHAT IT RENDERS. Pages are markup, stored once in core/net_sites.c and read
# by two renderers: /usr/bin/links draws it as text at a prompt, this draws it
# with type sizes and clickable links. The subset is small on purpose --
# anything either renderer could not parse honestly would have become two
# different webs.
#
#   <h1> <h2>   headings, in larger type
#   <p>         a paragraph, wrapped to the window
#   <ul> <li>   bullets, with the wrapped text hanging under them
#   <pre>       verbatim, never wrapped: commands, logs, ASCII art
#   <hr>        a rule
#   <b> <i>     emphasis
#   <a href>    a link, clickable, resolved from the href and NOT guessed
#               from the shape of the text
#   <img>       a coloured box with the alt text in it, which is as much as
#               a machine with no image files can honestly show
#
# `links` on the terminal renders the same page, so a player with no desktop
# is never locked out of anything on this network.

extends Control

var mono: Font
var machine: Object = null

# --- state ------------------------------------------------------------
var url := ""                  # "" is the bookmarks page
var addr := ""                 # what is typed in the address bar
var editing := false           # is the address bar taking keys
var raw := ""                  # the markup, exactly as the machine sent it
var rows: Array = []           # laid-out lines, see _layout()
var history: Array = []        # urls, most recent last
var scroll := 0
var status := ""
var _laid_w := -1.0            # window width the current layout was made for
var _hits: Array = []          # { rect, url }, rebuilt every draw

const FS := 13                 # body text
const FS_PRE := 12             # verbatim text
const FS_H1 := 20
const FS_H2 := 15
const ROW := 17.0              # body line height
const CHROME := 32.0           # toolbar height
const FOOT := 18.0             # status bar height
const PAD := 12.0
const BULLET := 10.0           # where the bullet sits
const LI_IND := 24.0           # where a list item's text sits

const PAGE_BG   := Color("#ffffff")
const TEXT      := Color("#1c1c1c")
const HEAD      := Color("#101820")
const LINK      := Color("#1a4fa0")
const ITALIC    := Color("#4a4438")   # one font, so slant becomes hue
const PRE_BG    := Color("#f2f0ea")
const PRE_TX    := Color("#23303a")
const RULE      := Color("#c9c5bd")
const GREY      := Color("#d6d3ce")   # MATE-ish chrome
const GREY_DK   := Color("#9a968f")
const GREY_TX   := Color("#2b2b2b")
const FIELD     := Color("#ffffff")
const DIM       := Color("#6b6b6b")

# Addresses only. No page bodies -- those come from the machine.
const BOOKMARKS := [
	["wiki.nomnix.org",       "NomnixOS documentation"],
	["intranet.internal",     "staff intranet"],
	["helpdesk.internal",     "the ticket queue"],
	["status.internal",       "service status board"],
	["notices.internal",      "all-staff notices"],
	["blog.internal",         "the previous admin's notes"],
	["oldwiki.internal",      "Project HALYARD (archived)"],
	["support.internal",      "the support desk"],
	["coffee.internal",       "the kitchen camera"],
	["nomnix.org",            "the operating system's own site"],
	["forums.nomnix.org",     "the forums"],
	["bugs.nomnix.org",       "the bug tracker"],
	["rfc.nomnix.org",        "standards, allegedly"],
	["asciiart.nomnix.org",   "the ASCII art archive"],
	["bofh.nomnix.org",       "not for customers"],
	["www.tripodal.net",      "free homepages"],
	["altavistula.com",       "search the entire web"],
	["nominal.local",         "this machine"],
]


func _ready() -> void:
	focus_mode = Control.FOCUS_ALL
	mouse_filter = Control.MOUSE_FILTER_STOP
	if mono == null:
		mono = ThemeDB.fallback_font
	_home()


func take_focus() -> void:
	grab_focus()


# --- fetching ---------------------------------------------------------

# The bookmarks page is written here, in the same markup, because it is the
# one page that is genuinely about this window and not about the network. It
# contains addresses and nothing else: click one and the machine is asked.
func _home() -> void:
	url = ""
	addr = ""
	scroll = 0
	var m := "<h1>bookmarks</h1><ul>"
	for b in BOOKMARKS:
		m += "<li><a href=\"%s\">%s</a> -- %s</li>" % [b[0], b[0], b[1]]
	m += "</ul><p>Every page here is fetched by running <b>links</b> on the "
	m += "machine, so a bookmark that will not load is the machine telling "
	m += "you something. The same pages read fine at a prompt: "
	m += "<b>links wiki.nomnix.org</b>.</p>"
	raw = m
	_relayout()
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
		raw = "<p>browser: no machine attached.</p>"
		_relayout()
		status = "offline"
		queue_redraw()
		return
	# The real browser, on the real machine, through the real resolver.
	# --raw hands back the markup; the rendering below is this window's only
	# contribution, and it is a rendering, not a source.
	raw = machine.sh_on(0, "links --raw " + u)
	_relayout()
	status = "%s -- %d lines" % [u, rows.size()]
	if raw.find("cannot resolve") >= 0 or raw.find("nothing responded") >= 0:
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
	var i := u.find("/")
	return u.substr(0, i) if i >= 0 else u


# An href of "/faq" means this host, exactly as it does at the prompt, where
# `links` prints the same link resolved. Nothing here guesses: a link exists
# because the page said <a href>, and it goes where the href says.
func _resolve(href: String) -> String:
	if href.begins_with("/"):
		return _host_of(url) + href
	return href


# --- layout -----------------------------------------------------------
#
# One pass over the markup, exactly the passes `links` makes, producing rows.
# A row is { kind, h, items } where an item is a drawn run:
#   { t, x, fs, col, bold, under, url }
# Wrapping is done in pixels here and in columns there, which is the only
# difference between the two renderers.

var _items: Array = []
var _x := 0.0
var _ind := 0.0
var _cont := 0.0
var _fs := FS
var _col := TEXT
var _lh := ROW
var _bold := false
var _ital := false
var _href := ""
var _open := false
var _blank := false
var _wrap := 400.0


func _relayout() -> void:
	_laid_w = size.x
	_wrap = max(160.0, size.x - PAD * 2.0)
	rows = []
	_items = []
	_x = 0.0; _ind = 0.0; _cont = 0.0
	_fs = FS; _col = TEXT; _lh = ROW
	_bold = false; _ital = false; _href = ""
	_open = false; _blank = false
	_parse(raw)
	scroll = min(scroll, max(0, rows.size() - 1))


func _line_end() -> void:
	if not _open:
		return
	rows.append({"kind": "text", "h": _lh, "items": _items})
	_items = []
	_open = false


# The blank line between blocks is owed, not spent, until a block actually
# draws something -- so an empty <p> leaves no hole and no page starts with a
# gap at the top.
func _blank_now() -> void:
	if _blank and not rows.is_empty():
		rows.append({"kind": "gap", "h": _lh * 0.55, "items": []})
	_blank = false


func _wof(t: String, fs: int) -> float:
	return mono.get_string_size(t, HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x


func _word(t: String) -> void:
	if t == "":
		return
	_blank_now()
	var w := _wof(t, _fs)
	var sp := _wof(" ", _fs) if (_open and _x > _ind) else 0.0
	if _open and _x > _ind and _x + sp + w > _wrap:
		_line_end()
		_x = _cont
		sp = 0.0
	if not _open:
		_open = true
		if _x < _ind:
			_x = _ind
	_items.append({"t": t, "x": _x + sp, "fs": _fs, "col": _col,
		"bold": _bold, "under": _href != "", "url": _resolve(_href) if _href != "" else ""})
	_x += sp + w


func _block_end() -> void:
	_line_end()
	_blank = true
	_x = 0.0


func _body_style() -> void:
	_fs = FS; _col = TEXT; _lh = ROW; _bold = false; _ital = false
	_ind = 0.0; _cont = 0.0; _x = 0.0


func _ent(s: String) -> String:
	if s.find("&") < 0:
		return s
	return s.replace("&lt;", "<").replace("&gt;", ">") \
		.replace("&quot;", "\"").replace("&amp;", "&")


func _attr(tag: String, name: String) -> String:
	var i := tag.findn(name + "=")
	if i < 0:
		return ""
	var j := i + name.length() + 1
	if j >= tag.length():
		return ""
	var q := tag[j]
	if q == "\"" or q == "'":
		var e := tag.find(q, j + 1)
		return _ent(tag.substr(j + 1, (e if e >= 0 else tag.length()) - j - 1))
	var e2 := tag.find(" ", j)
	return _ent(tag.substr(j, (e2 if e2 >= 0 else tag.length()) - j))


func _tag_is(tag: String, name: String) -> bool:
	if not tag.begins_with(name):
		return false
	if tag.length() == name.length():
		return true
	var c := tag[name.length()]
	return c == " " or c == "/"


func _parse(src: String) -> void:
	var i := 0
	var n := src.length()
	while i < n:
		if src[i] == "<":
			var e := src.find(">", i)
			if e < 0:
				break
			var tag := src.substr(i + 1, e - i - 1)
			i = e + 1
			if _tag_is(tag, "pre"):
				var close := src.find("</pre>", i)
				if close < 0:
					close = n
				_do_pre(src.substr(i, close - i))
				i = min(n, close + 6)
				continue
			_do_tag(tag)
			continue
		var t := src.find("<", i)
		if t < 0:
			t = n
		_do_text(src.substr(i, t - i))
		i = t


func _do_text(s: String) -> void:
	# Newlines in the source are how a C string literal is kept readable, not
	# line breaks in the page: whitespace of every kind just ends a word.
	var flat := s.replace("\n", " ").replace("\r", " ").replace("\t", " ")
	for w in flat.split(" ", false):
		var t := w.strip_edges()
		if t != "":
			_word(_ent(t))


# The colour text goes back to when a link or an italic run closes -- which
# depends on the block, since a link inside a heading must return to heading
# colour and not to body colour.
func _base_col() -> Color:
	if _fs == FS_H1 or _fs == FS_H2:
		return HEAD
	return ITALIC if _ital else TEXT


func _do_tag(tag: String) -> void:
	if _tag_is(tag, "h1") or _tag_is(tag, "h2"):
		_block_end()
		var one := _tag_is(tag, "h1")
		_fs = FS_H1 if one else FS_H2
		_lh = 26.0 if one else 21.0
		_col = HEAD
		_bold = true
		_ind = 0.0; _cont = 0.0; _x = 0.0
	elif _tag_is(tag, "/h1") or _tag_is(tag, "/h2"):
		_block_end()
		_body_style()
	elif _tag_is(tag, "p") or _tag_is(tag, "/p"):
		_block_end()
		_body_style()
	elif _tag_is(tag, "ul") or _tag_is(tag, "/ul"):
		_block_end()
		_body_style()
	elif _tag_is(tag, "li"):
		# A list is one block: no owed blank between items, or every list on
		# the network would be double spaced.
		_line_end()
		_blank_now()
		_body_style()
		_ind = LI_IND; _cont = LI_IND; _x = LI_IND
		_open = true
		_items.append({"t": "•", "x": BULLET, "fs": FS, "col": TEXT,
			"bold": false, "under": false, "url": ""})
	elif _tag_is(tag, "/li"):
		_line_end()
		_body_style()
	elif _tag_is(tag, "hr"):
		_block_end()
		_blank_now()
		rows.append({"kind": "hr", "h": 11.0, "items": []})
		_blank = true
	elif _tag_is(tag, "img"):
		# Nothing on this machine has an image file in it, and inventing one
		# in the desktop would be inventing content. A box with the alt text
		# is the honest amount of picture available.
		_block_end()
		_blank_now()
		var alt := _attr(tag, "alt")
		if alt == "":
			alt = _attr(tag, "src")
		rows.append({"kind": "img", "h": 46.0, "items": [], "alt": alt,
			"src": _attr(tag, "src")})
		_blank = true
	elif _tag_is(tag, "a"):
		_href = _attr(tag, "href")
		_col = LINK
	elif _tag_is(tag, "/a"):
		_href = ""
		_col = _base_col()
	elif _tag_is(tag, "b"):
		_bold = true
	elif _tag_is(tag, "/b"):
		_bold = _fs == FS_H1 or _fs == FS_H2
	elif _tag_is(tag, "i"):
		_ital = true
		if _href == "":
			_col = _base_col()
	elif _tag_is(tag, "/i"):
		_ital = false
		if _href == "":
			_col = _base_col()


func _do_pre(body: String) -> void:
	_block_end()
	_blank_now()
	var lines := body.split("\n")
	var first := 0
	if lines.size() > 0 and lines[0].strip_edges() == "":
		first = 1        # the newline right after <pre> is not a blank line
	for k in range(first, lines.size()):
		var t: String = _ent(lines[k]).replace("\t", "    ")
		rows.append({"kind": "pre", "h": 15.0, "items": [
			{"t": t, "x": PAD, "fs": FS_PRE, "col": PRE_TX,
			 "bold": false, "under": false, "url": ""}]})
	_blank = true
	_body_style()


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
			scroll = min(max(0, rows.size() - 2), scroll + 3); queue_redraw(); return
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
		for h in _hits:
			if (h["rect"] as Rect2).has_point(mb.position):
				_go(String(h["url"]))
				return
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
			scroll = max(0, rows.size() - _rows())
		KEY_DOWN:
			scroll = min(max(0, rows.size() - 2), scroll + 2)
		KEY_UP:
			scroll = max(0, scroll - 2)
		KEY_PAGEDOWN:
			scroll = min(max(0, rows.size() - 2), scroll + _rows() - 2)
		KEY_PAGEUP:
			scroll = max(0, scroll - (_rows() - 2))
		KEY_ESCAPE:
			pass
	queue_redraw()


# How many rows fit, counted for real: rows are not all the same height once
# a page has headings and pictures in it.
func _rows() -> int:
	var avail: float = max(1.0, size.y - CHROME - FOOT - 8.0)
	var n := 0
	var y := 0.0
	for i in range(scroll, rows.size()):
		y += float(rows[i]["h"])
		if y > avail:
			break
		n += 1
	return max(1, n)


# --- drawing ----------------------------------------------------------

func _button(r: Rect2, label: String, on: bool) -> void:
	draw_rect(r, Color("#e8e5e0") if on else Color("#dedbd6"))
	draw_rect(r, GREY_DK, false)
	var c := GREY_TX if on else DIM
	draw_string(mono, Vector2(r.position.x, r.position.y + 14), label,
		HORIZONTAL_ALIGNMENT_CENTER, r.size.x, 11, c)


func _draw() -> void:
	if size.x != _laid_w:
		_relayout()

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

	_hits = []
	var y := top + 4.0
	var first: int = clampi(scroll, 0, max(0, rows.size() - 1))
	var shown_rows := 0
	for i in range(first, rows.size()):
		var r: Dictionary = rows[i]
		var h: float = float(r["h"])
		if y + h > bot:
			break
		shown_rows += 1
		match String(r["kind"]):
			"hr":
				draw_line(Vector2(PAD, y + h * 0.5),
					Vector2(size.x - PAD, y + h * 0.5), RULE)
			"img":
				# A stable colour per image, so the same picture is the same
				# box every visit and a page keeps its own look.
				var src := String(r.get("src", ""))
				var hue: float = float(abs(src.hash()) % 360) / 360.0
				var box := Rect2(PAD, y + 2.0, min(size.x - PAD * 2.0, 420.0), h - 6.0)
				draw_rect(box, Color.from_hsv(hue, 0.30, 0.86))
				draw_rect(box, Color.from_hsv(hue, 0.35, 0.55), false)
				var alt := String(r.get("alt", ""))
				var maxw := box.size.x - 12.0
				while alt.length() > 3 and _wof(alt, 11) > maxw:
					alt = alt.substr(0, alt.length() - 2)
				draw_string(mono, Vector2(box.position.x + 6, box.position.y + 17),
					alt, HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color("#22262b"))
				draw_string(mono, Vector2(box.position.x + 6, box.position.y + 31),
					"[image]", HORIZONTAL_ALIGNMENT_LEFT, -1, 10, Color("#4a4f55"))
			"pre":
				draw_rect(Rect2(PAD * 0.5, y, size.x - PAD, h), PRE_BG)
				_draw_items(r["items"], y, h)
			"gap":
				pass
			_:
				_draw_items(r["items"], y, h)
		y += h

	# status bar
	draw_rect(Rect2(0, bot, size.x, size.y - bot), GREY)
	draw_line(Vector2(0, bot), Vector2(size.x, bot), GREY_DK)
	draw_string(mono, Vector2(6, bot + 13), status,
		HORIZONTAL_ALIGNMENT_LEFT, -1, 10, GREY_TX)
	if rows.size() > shown_rows:
		draw_string(mono, Vector2(size.x - 230, bot + 13),
			"%d/%d  wheel or arrows, backspace goes back"
				% [first + 1, rows.size()],
			HORIZONTAL_ALIGNMENT_LEFT, -1, 10, DIM)


func _draw_items(items: Array, y: float, h: float) -> void:
	for it in items:
		var t: String = String(it["t"])
		if t == "":
			continue
		var fs: int = int(it["fs"])
		var x: float = PAD + float(it["x"])
		var base := y + h - 5.0
		var col: Color = it["col"]
		draw_string(mono, Vector2(x, base), t,
			HORIZONTAL_ALIGNMENT_LEFT, -1, fs, col)
		if bool(it["bold"]):
			# One font in the whole desktop, so weight is faked the way early
			# terminals faked it: draw it again, a hair to the right.
			draw_string(mono, Vector2(x + 0.7, base), t,
				HORIZONTAL_ALIGNMENT_LEFT, -1, fs, col)
		var w := _wof(t, fs)
		if bool(it["under"]):
			draw_line(Vector2(x, base + 2.0), Vector2(x + w, base + 2.0), col)
		var u := String(it["url"])
		if u != "":
			_hits.append({"rect": Rect2(x, y, w, h), "url": u})
