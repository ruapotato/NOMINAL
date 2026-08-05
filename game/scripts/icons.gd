extends Control
# THE ICON SET.
#
# One painter for every application icon on the NOMINAL desktop: the launcher
# column (34px), the Applications menu (22px) and the window list (16px). The
# desktop calls exactly one function -- Icons.draw_icon(canvas, at, size, kind).
#
# Rules that make this a SET rather than twenty separate drawings:
#   * every icon is authored in normalised 0..1 coordinates and multiplied by
#     `sz`, so nothing is pinned to a pixel and 16px is not a special case;
#   * one corner treatment -- a chamfered rectangle, CHAMFER of the size, used
#     for every card, key, block and page;
#   * one border weight -- BORDER of the size, floored at one pixel so the
#     outline survives the 16px window list;
#   * one optical size -- art lives inside the same margin, so a folder and a
#     terminal look like they weigh the same in a column;
#   * one palette, below. Twenty icons that share nine colours read as a family.
#
# Shapes, not letters. A set that spells out its meaning in glyphs is a set
# that gave up.

# --- palette -----------------------------------------------------------------
# A light MATE-ish desktop: warm folder yellow, charcoal terminals with a green
# prompt, paper white ruled in grey, a blue titlebar/browser accent.
const INK      := Color("#2b2f36")  # outlines, mines, gamepad body
const CHAR     := Color("#1c1f22")  # terminal charcoal
const CHAR_EDGE:= Color("#4b5157")  # the bezel around anything charcoal
const LINE     := Color("#8b929b")  # the universal grey rule / border
const PAPER    := Color("#fbfbf7")
const WHITE     := Color("#ffffff")
const GREY     := Color("#e3e5e9")  # chrome, calculator bodies
const GREY_D   := Color("#b7bcc4")
const GREEN    := Color("#79d17a")  # the prompt green
const GREEN_D  := Color("#4e8b4f")
const AMBER    := Color("#e0a338")  # folder yellow
const AMBER_D  := Color("#b57c1e")
const ORANGE   := Color("#d97a34")
const RED      := Color("#c0453c")
const BLUE     := Color("#3c6eb4")
const BLUE_L   := Color("#7ea6d8")
const BLUE_D   := Color("#27497a")
const SKY      := Color("#8fc4e8")
const TEAL     := Color("#3f9c96")
const SLATE    := Color("#79838f")
const WOOD     := Color("#b5824c")
const WOOD_D   := Color("#8a6033")

# --- geometry ----------------------------------------------------------------
const CHAMFER := 0.10   # corner cut, as a fraction of the shape's short side
const BORDER  := 0.045  # border weight, as a fraction of the icon size

# Demo-scene state. The desktop never touches these.
var mono: Font
var demo := false


# =============================================================================
# THE ONLY ENTRY POINT THE DESKTOP NEEDS.
# Draw one icon of `kind` into `c`, top-left at `at`, `sz` pixels square.
# =============================================================================
static func draw_icon(c: CanvasItem, at: Vector2, sz: float, kind: String) -> void:
	match kind:
		"term": _term(c, at, sz)
		"chat": _chat(c, at, sz)
		"files": _files(c, at, sz)
		"notes": _notes(c, at, sz)
		"log": _log(c, at, sz)
		"manual": _manual(c, at, sz)
		"browser": _browser(c, at, sz)
		"game": _game(c, at, sz)
		"calc": _calc(c, at, sz)
		"sysmon": _sysmon(c, at, sz)
		"pkg": _pkg(c, at, sz)
		"svc": _svc(c, at, sz)
		"editor": _editor(c, at, sz)
		"snake": _snake(c, at, sz)
		"mines": _mines(c, at, sz)
		"blocks": _blocks(c, at, sz)
		"cards": _cards(c, at, sz)
		"worms": _worms(c, at, sz)
		"liquid": _liquid(c, at, sz)
		"flappy": _flappy(c, at, sz)
		"music": _music(c, at, sz)
		_: _app(c, at, sz)


# Every kind this set knows how to draw, in the order the demo shows them.
static func kinds() -> PackedStringArray:
	return PackedStringArray([
		"term", "chat", "files", "notes", "log", "manual", "browser",
		"game", "calc", "sysmon", "pkg", "svc", "editor",
		"snake", "mines", "blocks", "cards", "worms", "liquid", "flappy",
		"music", "app"])


# =============================================================================
# primitives -- normalised 0..1 coordinates, scaled by sz
# =============================================================================
static func _pt(at: Vector2, sz: float, x: float, y: float) -> Vector2:
	return Vector2(at.x + x * sz, at.y + y * sz)


static func _rc(at: Vector2, sz: float, x: float, y: float, w: float, h: float) -> Rect2:
	return Rect2(at.x + x * sz, at.y + y * sz, w * sz, h * sz)


# The one corner treatment: a rectangle with its corners cut.
static func _cham(r: Rect2) -> PackedVector2Array:
	var k: float = minf(r.size.x, r.size.y) * CHAMFER
	var x0: float = r.position.x
	var y0: float = r.position.y
	var x1: float = r.position.x + r.size.x
	var y1: float = r.position.y + r.size.y
	return PackedVector2Array([
		Vector2(x0 + k, y0), Vector2(x1 - k, y0),
		Vector2(x1, y0 + k), Vector2(x1, y1 - k),
		Vector2(x1 - k, y1), Vector2(x0 + k, y1),
		Vector2(x0, y1 - k), Vector2(x0, y0 + k)])


# A filled, chamfered, outlined shape -- the building block of the whole set.
static func _card(c: CanvasItem, r: Rect2, fill: Color, edge: Color, sz: float) -> void:
	var p: PackedVector2Array = _cham(r)
	c.draw_colored_polygon(p, fill)
	var loop: PackedVector2Array = p.duplicate()
	loop.append(p[0])
	c.draw_polyline(loop, edge, _bw(sz), true)


static func _bw(sz: float) -> float:
	return maxf(1.0, sz * BORDER)


# A flat filled chamfered shape with no outline (blocks, keys, pages).
static func _chip(c: CanvasItem, r: Rect2, fill: Color) -> void:
	c.draw_colored_polygon(_cham(r), fill)


# A horizontal rule, used for every "text line" in the set.
static func _rule(c: CanvasItem, at: Vector2, sz: float,
		x: float, y: float, w: float, col: Color, weight := 0.075) -> void:
	c.draw_line(_pt(at, sz, x, y), _pt(at, sz, x + w, y), col, sz * weight)


# The full-bleed body most icons sit on.
static func _body(at: Vector2, sz: float) -> Rect2:
	return _rc(at, sz, 0.06, 0.08, 0.88, 0.84)


# =============================================================================
# the icons
# =============================================================================

# Terminal: charcoal, green prompt chevron and a cursor bar.
static func _term(c: CanvasItem, at: Vector2, sz: float) -> void:
	_card(c, _body(at, sz), CHAR, CHAR_EDGE, sz)
	var w: float = sz * 0.085
	c.draw_line(_pt(at, sz, 0.20, 0.34), _pt(at, sz, 0.36, 0.50), GREEN, w)
	c.draw_line(_pt(at, sz, 0.36, 0.50), _pt(at, sz, 0.20, 0.66), GREEN, w)
	c.draw_rect(_rc(at, sz, 0.46, 0.60, 0.30, 0.09), GREEN)


# Chat: a blue speech bubble with a tail.
static func _chat(c: CanvasItem, at: Vector2, sz: float) -> void:
	_card(c, _rc(at, sz, 0.06, 0.12, 0.88, 0.54), BLUE, BLUE_D, sz)
	c.draw_colored_polygon(PackedVector2Array([
		_pt(at, sz, 0.24, 0.62), _pt(at, sz, 0.24, 0.92),
		_pt(at, sz, 0.48, 0.64)]), BLUE)
	for i in 3:
		c.draw_circle(_pt(at, sz, 0.28 + i * 0.22, 0.39), sz * 0.06, WHITE)


# Files: the folder, with a tab. Its own silhouette, same corner language.
static func _files(c: CanvasItem, at: Vector2, sz: float) -> void:
	_chip(c, _rc(at, sz, 0.06, 0.14, 0.40, 0.20), AMBER_D)
	_card(c, _rc(at, sz, 0.06, 0.26, 0.88, 0.60), AMBER, AMBER_D, sz)
	c.draw_rect(_rc(at, sz, 0.14, 0.34, 0.72, 0.06), Color(1, 1, 1, 0.35))


# Notes: ruled paper and a pencil.
static func _notes(c: CanvasItem, at: Vector2, sz: float) -> void:
	_card(c, _rc(at, sz, 0.14, 0.08, 0.72, 0.84), PAPER, LINE, sz)
	for i in 3:
		_rule(c, at, sz, 0.24, 0.24 + i * 0.16, 0.52, LINE, 0.085)
	# The pencil is what tells notes apart from every other sheet of paper.
	c.draw_line(_pt(at, sz, 0.52, 0.88), _pt(at, sz, 0.86, 0.58), AMBER, sz * 0.15)
	c.draw_circle(_pt(at, sz, 0.87, 0.56), sz * 0.075, INK)


# Log viewer: charcoal, a stream of coloured lines. One green, one amber,
# one red -- a log is a thing with severities in it.
static func _log(c: CanvasItem, at: Vector2, sz: float) -> void:
	_card(c, _body(at, sz), CHAR, CHAR_EDGE, sz)
	_rule(c, at, sz, 0.17, 0.27, 0.52, GREEN, 0.06)
	_rule(c, at, sz, 0.17, 0.44, 0.66, LINE, 0.06)
	_rule(c, at, sz, 0.17, 0.61, 0.40, AMBER, 0.06)
	_rule(c, at, sz, 0.17, 0.78, 0.58, RED, 0.06)


# Manual: a bound book -- blue cover, paper block, spine.
static func _manual(c: CanvasItem, at: Vector2, sz: float) -> void:
	_card(c, _rc(at, sz, 0.08, 0.08, 0.84, 0.84), BLUE, BLUE_D, sz)
	_chip(c, _rc(at, sz, 0.30, 0.16, 0.54, 0.68), PAPER)
	c.draw_rect(_rc(at, sz, 0.22, 0.10, 0.08, 0.80), BLUE_D)
	_rule(c, at, sz, 0.38, 0.36, 0.38, LINE, 0.06)
	_rule(c, at, sz, 0.38, 0.52, 0.38, LINE, 0.06)
	_rule(c, at, sz, 0.38, 0.68, 0.24, LINE, 0.06)


# Browser: chrome with a blue bar and a globe.
static func _browser(c: CanvasItem, at: Vector2, sz: float) -> void:
	_card(c, _body(at, sz), WHITE, LINE, sz)
	c.draw_rect(_rc(at, sz, 0.06, 0.08, 0.88, 0.20), BLUE)
	c.draw_rect(_rc(at, sz, 0.30, 0.13, 0.58, 0.10), BLUE_L)
	var ctr: Vector2 = _pt(at, sz, 0.50, 0.62)
	var rad: float = sz * 0.23
	c.draw_circle(ctr, rad, BLUE_L)
	c.draw_line(ctr - Vector2(rad, 0), ctr + Vector2(rad, 0), BLUE, sz * 0.055)
	c.draw_line(ctr - Vector2(0, rad), ctr + Vector2(0, rad), BLUE, sz * 0.055)
	c.draw_circle(ctr, rad, BLUE, false, _bw(sz))


# Game: a gamepad -- d-pad and two buttons.
static func _game(c: CanvasItem, at: Vector2, sz: float) -> void:
	_card(c, _rc(at, sz, 0.04, 0.20, 0.92, 0.60), INK, CHAR_EDGE, sz)
	c.draw_rect(_rc(at, sz, 0.11, 0.44, 0.30, 0.12), PAPER)
	c.draw_rect(_rc(at, sz, 0.20, 0.35, 0.12, 0.30), PAPER)
	c.draw_circle(_pt(at, sz, 0.68, 0.40), sz * 0.10, RED)
	c.draw_circle(_pt(at, sz, 0.83, 0.60), sz * 0.10, GREEN)


# Calculator: dark display over a keypad with one accent column.
static func _calc(c: CanvasItem, at: Vector2, sz: float) -> void:
	_card(c, _body(at, sz), GREY, LINE, sz)
	_chip(c, _rc(at, sz, 0.15, 0.16, 0.70, 0.20), CHAR)
	c.draw_rect(_rc(at, sz, 0.56, 0.23, 0.22, 0.06), GREEN)
	# Two rows of fat keys read at 16px where a 3x3 grid turns to noise.
	for row in 2:
		for col in 3:
			var accent: bool = col == 2 and row == 1
			_chip(c, _rc(at, sz, 0.15 + col * 0.245, 0.46 + row * 0.24, 0.19, 0.18),
				ORANGE if accent else SLATE)


# System monitor: charcoal, a green plot over a baseline.
static func _sysmon(c: CanvasItem, at: Vector2, sz: float) -> void:
	_card(c, _body(at, sz), CHAR, CHAR_EDGE, sz)
	c.draw_line(_pt(at, sz, 0.14, 0.78), _pt(at, sz, 0.86, 0.78), CHAR_EDGE, sz * 0.05)
	var pts := PackedVector2Array()
	var ys: Array = [0.66, 0.36, 0.56, 0.24, 0.48, 0.30]
	for i in ys.size():
		pts.append(_pt(at, sz, 0.15 + i * 0.14, ys[i]))
	c.draw_polyline(pts, GREEN, sz * 0.075, true)


# Package: a taped carton.
static func _pkg(c: CanvasItem, at: Vector2, sz: float) -> void:
	# A carton: lighter lid, darker body, one strip of tape down the middle.
	_card(c, _rc(at, sz, 0.06, 0.30, 0.88, 0.58), WOOD, WOOD_D, sz)
	_card(c, _rc(at, sz, 0.02, 0.14, 0.96, 0.22), Color("#cf9a5f"), WOOD_D, sz)
	c.draw_rect(_rc(at, sz, 0.42, 0.36, 0.16, 0.52), Color("#e3c193"))
	c.draw_rect(_rc(at, sz, 0.42, 0.14, 0.16, 0.22), Color("#e3c193"))


# Service: a gear with a running-status dot.
static func _svc(c: CanvasItem, at: Vector2, sz: float) -> void:
	var ctr: Vector2 = _pt(at, sz, 0.48, 0.48)
	var ro: float = sz * 0.44
	var ri: float = sz * 0.33
	var pts := PackedVector2Array()
	var n := 48
	for i in n:
		var seg: int = int(i / 3.0)
		var rad: float = ro if seg % 2 == 0 else ri
		var a: float = TAU * float(i) / float(n)
		pts.append(ctr + Vector2(cos(a), sin(a)) * rad)
	c.draw_colored_polygon(pts, SLATE)
	var loop: PackedVector2Array = pts.duplicate()
	loop.append(pts[0])
	c.draw_polyline(loop, INK, _bw(sz) * 0.8, true)
	c.draw_circle(ctr, sz * 0.14, GREY)
	c.draw_circle(ctr, sz * 0.14, INK, false, _bw(sz) * 0.8)
	c.draw_circle(_pt(at, sz, 0.84, 0.84), sz * 0.13, GREEN)
	c.draw_circle(_pt(at, sz, 0.84, 0.84), sz * 0.13, GREEN_D, false, _bw(sz) * 0.8)


# Editor: paper with indented, syntax-coloured lines and a caret.
static func _editor(c: CanvasItem, at: Vector2, sz: float) -> void:
	_card(c, _rc(at, sz, 0.10, 0.08, 0.80, 0.84), PAPER, LINE, sz)
	_rule(c, at, sz, 0.19, 0.26, 0.32, BLUE, 0.09)
	_rule(c, at, sz, 0.28, 0.44, 0.38, GREEN_D, 0.09)
	_rule(c, at, sz, 0.28, 0.62, 0.24, ORANGE, 0.09)
	c.draw_rect(_rc(at, sz, 0.58, 0.55, 0.08, 0.16), INK)
	_rule(c, at, sz, 0.19, 0.80, 0.42, LINE, 0.09)


# Snake: a segmented body on a dark field, with an apple.
static func _snake(c: CanvasItem, at: Vector2, sz: float) -> void:
	_card(c, _body(at, sz), Color("#1f3b28"), GREEN_D, sz)
	var seg: Array = [
		Vector2(0.16, 0.62), Vector2(0.34, 0.62), Vector2(0.52, 0.62),
		Vector2(0.52, 0.44), Vector2(0.52, 0.26)]
	for i in seg.size():
		var v: Vector2 = seg[i]
		_chip(c, _rc(at, sz, v.x, v.y, 0.17, 0.17),
			GREEN if i == seg.size() - 1 else GREEN_D)
	c.draw_circle(_pt(at, sz, 0.78, 0.34), sz * 0.10, RED)


# Minesweeper: the mine, spiked, on a grey tile.
static func _mines(c: CanvasItem, at: Vector2, sz: float) -> void:
	_card(c, _body(at, sz), GREY, LINE, sz)
	var ctr: Vector2 = _pt(at, sz, 0.50, 0.52)
	var w: float = sz * 0.075
	var spikes: Array = [Vector2(1, 0), Vector2(0, 1), Vector2(0.7, 0.7), Vector2(-0.7, 0.7)]
	for s: Vector2 in spikes:
		c.draw_line(ctr - s * sz * 0.36, ctr + s * sz * 0.36, INK, w)
	c.draw_circle(ctr, sz * 0.23, INK)
	c.draw_circle(ctr - Vector2(sz * 0.08, sz * 0.08), sz * 0.055, GREY)


# Blocks: two falling tetrominoes on a charcoal well.
static func _blocks(c: CanvasItem, at: Vector2, sz: float) -> void:
	_card(c, _body(at, sz), CHAR, CHAR_EDGE, sz)
	var b := 0.21
	for i in 3:
		_chip(c, _rc(at, sz, 0.16 + i * 0.23, 0.20, b, b), RED)
	_chip(c, _rc(at, sz, 0.39, 0.43, b, b), RED)
	_chip(c, _rc(at, sz, 0.16, 0.66, b, b), BLUE_L)
	_chip(c, _rc(at, sz, 0.39, 0.66, b, b), BLUE_L)


# Cards: two overlapping playing cards with a red pip.
static func _cards(c: CanvasItem, at: Vector2, sz: float) -> void:
	_card(c, _rc(at, sz, 0.06, 0.10, 0.50, 0.68), GREY, LINE, sz)
	_card(c, _rc(at, sz, 0.34, 0.22, 0.56, 0.70), WHITE, LINE, sz)
	var ctr: Vector2 = _pt(at, sz, 0.62, 0.57)
	var d: float = sz * 0.22
	c.draw_colored_polygon(PackedVector2Array([
		ctr + Vector2(0, -d), ctr + Vector2(d * 0.75, 0),
		ctr + Vector2(0, d), ctr + Vector2(-d * 0.75, 0)]), RED)


# Worms: sky over turf, a worm, and a lobbed shot.
static func _worms(c: CanvasItem, at: Vector2, sz: float) -> void:
	_card(c, _body(at, sz), SKY, LINE, sz)
	c.draw_rect(_rc(at, sz, 0.06, 0.62, 0.88, 0.30), Color("#8a6b3f"))
	c.draw_rect(_rc(at, sz, 0.06, 0.62, 0.88, 0.10), Color("#6f9c3f"))
	c.draw_circle(_pt(at, sz, 0.28, 0.50), sz * 0.17, Color("#e08a92"))
	c.draw_circle(_pt(at, sz, 0.34, 0.46), sz * 0.05, INK)
	for i in 2:
		c.draw_circle(_pt(at, sz, 0.58 + i * 0.20, 0.36 - i * 0.12), sz * 0.07, INK)


# Liquid: a beaker, part full, with bubbles.
static func _liquid(c: CanvasItem, at: Vector2, sz: float) -> void:
	var glass: Rect2 = _rc(at, sz, 0.24, 0.14, 0.52, 0.78)
	_card(c, glass, Color("#eaf4f6"), LINE, sz)
	c.draw_colored_polygon(_cham(_rc(at, sz, 0.24, 0.48, 0.52, 0.44)), TEAL)
	c.draw_rect(_rc(at, sz, 0.24, 0.48, 0.52, 0.06), Color("#69c2bb"))
	c.draw_circle(_pt(at, sz, 0.40, 0.68), sz * 0.055, Color(1, 1, 1, 0.7))
	c.draw_circle(_pt(at, sz, 0.58, 0.78), sz * 0.04, Color(1, 1, 1, 0.7))
	c.draw_rect(_rc(at, sz, 0.16, 0.10, 0.68, 0.09), GREY_D)
	c.draw_rect(_rc(at, sz, 0.16, 0.10, 0.68, 0.09), LINE, false, _bw(sz) * 0.7)


# Flappy: a bird and a pipe gap.
static func _flappy(c: CanvasItem, at: Vector2, sz: float) -> void:
	_card(c, _body(at, sz), SKY, LINE, sz)
	c.draw_rect(_rc(at, sz, 0.62, 0.08, 0.22, 0.24), Color("#4e9b45"))
	c.draw_rect(_rc(at, sz, 0.58, 0.28, 0.30, 0.09), Color("#3d7a36"))
	c.draw_rect(_rc(at, sz, 0.62, 0.66, 0.22, 0.26), Color("#4e9b45"))
	c.draw_rect(_rc(at, sz, 0.58, 0.57, 0.30, 0.09), Color("#3d7a36"))
	c.draw_circle(_pt(at, sz, 0.33, 0.48), sz * 0.16, Color("#f0c419"))
	c.draw_colored_polygon(PackedVector2Array([
		_pt(at, sz, 0.46, 0.44), _pt(at, sz, 0.60, 0.49),
		_pt(at, sz, 0.46, 0.54)]), ORANGE)
	c.draw_circle(_pt(at, sz, 0.36, 0.43), sz * 0.04, INK)


# Music: charcoal, like every other window onto something the machine is
# doing, with two beamed quavers in amber. A note is a shape, not a letter --
# and amber is the one accent the charcoal icons had not spent yet, so this
# reads apart from the terminal's green chevron and the monitor's green plot
# at sixteen pixels, which is where icons go to die.
static func _music(c: CanvasItem, at: Vector2, sz: float) -> void:
	_card(c, _body(at, sz), CHAR, CHAR_EDGE, sz)
	var w: float = sz * 0.075
	c.draw_circle(_pt(at, sz, 0.33, 0.72), sz * 0.115, AMBER)
	c.draw_circle(_pt(at, sz, 0.67, 0.63), sz * 0.115, AMBER)
	c.draw_line(_pt(at, sz, 0.435, 0.72), _pt(at, sz, 0.435, 0.32), AMBER, w)
	c.draw_line(_pt(at, sz, 0.775, 0.63), _pt(at, sz, 0.775, 0.23), AMBER, w)
	c.draw_colored_polygon(PackedVector2Array([
		_pt(at, sz, 0.40, 0.32), _pt(at, sz, 0.81, 0.23),
		_pt(at, sz, 0.81, 0.36), _pt(at, sz, 0.40, 0.45)]), AMBER)


# The fallback. An unknown application still gets a deliberate mark: the same
# card, the same corners, a slate lozenge. Never a grey box with an asterisk.
static func _app(c: CanvasItem, at: Vector2, sz: float) -> void:
	_card(c, _body(at, sz), GREY, LINE, sz)
	var ctr: Vector2 = _pt(at, sz, 0.50, 0.50)
	var d: float = sz * 0.30
	c.draw_colored_polygon(PackedVector2Array([
		ctr + Vector2(0, -d), ctr + Vector2(d, 0),
		ctr + Vector2(0, d), ctr + Vector2(-d, 0)]), SLATE)
	c.draw_colored_polygon(PackedVector2Array([
		ctr + Vector2(0, -d * 0.45), ctr + Vector2(d * 0.45, 0),
		ctr + Vector2(0, d * 0.45), ctr + Vector2(-d * 0.45, 0)]), GREY)


# =============================================================================
# self-test: run this script's scene with demo = true and the whole set is
# drawn at 34, 22 and 16 px with labels, so it can be judged as a set.
# =============================================================================
func _ready() -> void:
	if mono == null:
		mono = ThemeDB.fallback_font
	if demo:
		set_anchors_preset(Control.PRESET_FULL_RECT)
		queue_redraw()


func _draw() -> void:
	if not demo:
		return
	var all: PackedStringArray = kinds()
	draw_rect(Rect2(Vector2.ZERO, size), Color("#cfd3d8"))
	var cols := 6
	var cw: float = 144.0
	var ch: float = 150.0
	var ox: float = 18.0
	var oy: float = 22.0
	for i in all.size():
		var col: int = i % cols
		var row: int = int(i / float(cols))
		var x: float = ox + col * cw
		var y: float = oy + row * ch
		draw_rect(Rect2(x - 6, y - 6, cw - 10, ch - 12), Color("#e7e9ec"))
		draw_icon(self, Vector2(x, y + 6), 34, all[i])
		draw_icon(self, Vector2(x + 46, y + 18), 22, all[i])
		draw_icon(self, Vector2(x + 78, y + 24), 16, all[i])
		# The 16px row again on the dark panel colour, which is where the
		# window list actually lives.
		draw_rect(Rect2(x - 2, y + 52, 118, 26), Color("#3b4148"))
		for j in 4:
			draw_icon(self, Vector2(x + 3 + j * 28, y + 57), 16, all[i])
		if mono != null:
			draw_string(mono, Vector2(x, y + 100), all[i],
				HORIZONTAL_ALIGNMENT_LEFT, -1, 13, Color("#22262b"))
