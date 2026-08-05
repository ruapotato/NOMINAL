# calc.gd — the calculator.
#
# The one app on this desktop that does not touch the machine. It is here
# because a desktop without a calculator is a demo of a desktop: you convert
# blocks to kilobytes, you work out what 305 of 703 inodes is, and if the
# panel cannot do that you go and find something that can and the illusion is
# over. It still carries `machine`, because every app on this desktop is
# constructed the same way and an app that needs a special case is a trap for
# whoever adds the next one.
#
# IT SHOWS THE EXPRESSION, NOT A REGISTER. A four-function calculator that
# keeps a hidden accumulator makes you trust your memory of what you pressed;
# here the whole sum is on screen and `2+3*4` is 14, because that is what it
# means. Precedence is done by an actual parser -- numbers, unary minus,
# parentheses, then * and / , then + and - -- so nothing has to be entered in
# a special order to come out right.
#
# PERCENT IS THE CALCULATOR MEANING, NOT THE PROGRAMMER ONE. `200+10%` is 220
# and `200*10%` is 20, which is what anyone reaching for the key expects: a
# percent after + or - is a percent OF what came before it, and anywhere else
# it is simply a hundredth. Both readings are visible in the expression line,
# so the answer can be checked rather than believed.

extends Control

var mono: Font
var machine: Object = null      # unused here; the contract is the contract

var expr := ""                  # exactly what was typed, character for character
var out := "0"                  # the last answer, or the complaint
var err := false
var ans := 0.0                  # the answer, as a number, for chaining
var has_ans := false
var hist: Array = []            # {"e": expression, "v": formatted answer}

# Parser scratch. Members rather than arguments because the percent rule needs
# one bit of information to travel back out of _term(), and threading a second
# return value through three functions to carry a boolean is worse.
var _tk: Array = []
var _at := 0
var _fail := false
var _pct := false               # the term just parsed was a bare percent

# Column, row, and how many columns wide. Written out rather than derived from
# a grid of strings so that "=" can be four wide without a sentinel value.
const KEYS := [
	{"t": "C",  "c": 0, "r": 0, "w": 1}, {"t": "<-", "c": 1, "r": 0, "w": 1},
	{"t": "(",  "c": 2, "r": 0, "w": 1}, {"t": ")",  "c": 3, "r": 0, "w": 1},
	{"t": "7",  "c": 0, "r": 1, "w": 1}, {"t": "8",  "c": 1, "r": 1, "w": 1},
	{"t": "9",  "c": 2, "r": 1, "w": 1}, {"t": "/",  "c": 3, "r": 1, "w": 1},
	{"t": "4",  "c": 0, "r": 2, "w": 1}, {"t": "5",  "c": 1, "r": 2, "w": 1},
	{"t": "6",  "c": 2, "r": 2, "w": 1}, {"t": "*",  "c": 3, "r": 2, "w": 1},
	{"t": "1",  "c": 0, "r": 3, "w": 1}, {"t": "2",  "c": 1, "r": 3, "w": 1},
	{"t": "3",  "c": 2, "r": 3, "w": 1}, {"t": "-",  "c": 3, "r": 3, "w": 1},
	{"t": "0",  "c": 0, "r": 4, "w": 1}, {"t": ".",  "c": 1, "r": 4, "w": 1},
	{"t": "+/-","c": 2, "r": 4, "w": 1}, {"t": "+",  "c": 3, "r": 4, "w": 1},
	{"t": "%",  "c": 0, "r": 5, "w": 1}, {"t": "=",  "c": 1, "r": 5, "w": 3},
]

const CHROME := Color("#d6d3ce")
const FACE := Color("#dedbd6")
const WHITE := Color("#ffffff")
const EDGE_L := Color("#f4f2ef")
const EDGE_D := Color("#8e8b86")
const BORDER := Color("#5c5c5c")
const INK := Color("#1b1b1b")
const DIM := Color("#5f6469")
const SEL := Color("#3465a4")
const RED := Color("#b0281a")
const HIST_W := 132.0
const DISP_H := 50.0
const PAD := 5.0


func _ready() -> void:
	focus_mode = Control.FOCUS_ALL
	mouse_filter = Control.MOUSE_FILTER_STOP
	if mono == null:
		mono = preload("res://scripts/uifont.gd").mono()


func take_focus() -> void:
	grab_focus()


# Nothing here can go stale -- there is no machine to disagree with -- but the
# desktop calls this on every app after every command and an app that does not
# answer is an app somebody has to remember is different.
func refresh() -> void:
	queue_redraw()


# ------------------------------------------------------------------ the sum

# The expression, as tokens. A number is a run of digits and at most the dots
# that are in it; everything else is one character. Bad characters cannot get
# in from the keypad, but they can from the keyboard, so they are rejected
# here rather than silently dropped later.
func _lex(s: String) -> bool:
	_tk = []
	var i := 0
	while i < s.length():
		var c := s[i]
		if c == " ":
			i += 1
			continue
		if c.is_valid_int() or c == ".":
			var st := i
			while i < s.length() and (s[i].is_valid_int() or s[i] == "."):
				i += 1
			var num := s.substr(st, i - st)
			if not num.is_valid_float():
				return false
			_tk.append({"k": "n", "v": num.to_float()})
			continue
		if "+-*/()%".find(c) < 0:
			return false
		_tk.append({"k": c, "v": 0.0})
		i += 1
	return true


func _peek() -> String:
	if _at >= _tk.size():
		return ""
	return str(_tk[_at]["k"])


func _factor() -> float:
	_pct = false
	var k := _peek()
	if k == "-":
		_at += 1
		return -_factor()
	if k == "+":
		_at += 1
		return _factor()
	if k == "(":
		_at += 1
		var v := _sum()
		if _peek() != ")":
			_fail = true
			return 0.0
		_at += 1
		return v
	if k != "n":
		_fail = true
		return 0.0
	var n: float = _tk[_at]["v"]
	_at += 1
	# A percent sign binds to the number it follows and nothing else.
	if _peek() == "%":
		_at += 1
		_pct = true
		n = n / 100.0
	return n


func _term() -> float:
	var v := _factor()
	var lone := _pct          # was the whole term just "<number>%"?
	while true:
		var k := _peek()
		if k != "*" and k != "/":
			break
		_at += 1
		var r := _factor()
		lone = false
		if k == "*":
			v *= r
		else:
			if r == 0.0:
				_fail = true
				return 0.0
			v /= r
	_pct = lone
	return v


func _sum() -> float:
	var v := _term()
	while true:
		var k := _peek()
		if k != "+" and k != "-":
			break
		_at += 1
		var r := _term()
		# THE PERCENT RULE. `200+10%` reads as "add ten percent", so the right
		# operand is a fraction OF the running total. `200*10%` does not --
		# there is nothing for it to be a percent of but itself.
		if _pct:
			r = v * r
		if k == "+":
			v += r
		else:
			v -= r
	_pct = false
	return v


func _eval(s: String) -> Array:
	if s.strip_edges() == "":
		return [false, 0.0]
	if not _lex(s):
		return [false, 0.0]
	_at = 0
	_fail = false
	_pct = false
	var v := _sum()
	if _fail or _at != _tk.size():
		return [false, 0.0]
	if is_nan(v) or is_inf(v):
		return [false, 0.0]
	return [true, v]


# A number a person would have written. Integers print as integers -- "48"
# rather than "48.000000" -- and everything else keeps ten significant digits
# and loses its trailing zeros, because 0.30000000000000004 is a true answer
# that reads as a broken program.
func _fmt(v: float) -> String:
	if absf(v) < 1e15 and absf(v - round(v)) < 1e-9:
		return "%d" % int(round(v))
	var s := "%.10f" % v
	while s.ends_with("0"):
		s = s.substr(0, s.length() - 1)
	if s.ends_with("."):
		s = s.substr(0, s.length() - 1)
	return s


# ---------------------------------------------------------------- the keys

func _press(t: String) -> void:
	match t:
		"C":
			expr = ""
			out = "0"
			err = false
			has_ans = false
		"<-":
			if expr != "":
				expr = expr.substr(0, expr.length() - 1)
				_live()
		"=":
			_equals()
		"+/-":
			_flip()
		_:
			# After "=", a digit starts a new sum and an operator continues the
			# old one. That is what every calculator does and what the hand
			# expects: 12+3= then *2= is 30.
			if expr == "" and has_ans and "+-*/%".find(t) >= 0:
				expr = _fmt(ans)
			expr += t
			_live()
	queue_redraw()


# While you type, show the answer if there is one and say nothing if there is
# not. A half-typed sum is not an error -- "12+" is a person mid-thought --
# so the result line simply holds still rather than flashing a complaint.
func _live() -> void:
	var r := _eval(expr)
	if r[0]:
		out = _fmt(r[1])
		err = false
	elif expr.strip_edges() == "":
		out = "0"
		err = false


func _equals() -> void:
	if expr.strip_edges() == "":
		return
	var r := _eval(expr)
	if not r[0]:
		out = "that is not a sum I can finish"
		err = true
		return
	out = _fmt(r[1])
	err = false
	ans = r[1]
	has_ans = true
	hist.append({"e": expr, "v": out})
	while hist.size() > 24:
		hist.remove_at(0)
	expr = ""


# Flip the sign of the number being typed, by putting a minus in front of it
# or taking one away. "3--2" is legal -- the parser has unary minus -- so this
# never has to rewrite the sum to keep it valid.
func _flip() -> void:
	if expr == "":
		if has_ans:
			expr = _fmt(-ans)
			_live()
		else:
			expr = "-"
		return
	var i := expr.length()
	while i > 0 and (expr[i - 1].is_valid_int() or expr[i - 1] == "."):
		i -= 1
	if i > 0 and expr[i - 1] == "-":
		expr = expr.substr(0, i - 1) + expr.substr(i)
	else:
		expr = expr.substr(0, i) + "-" + expr.substr(i)
	_live()


# ---------------------------------------------------------------- layout

func _hist_w() -> float:
	# Below about 400 pixels the history costs more than it gives, so it goes
	# and the last answer stays on the display line where it always is.
	return HIST_W if size.x >= 400.0 else 0.0


func _pad_rect() -> Rect2:
	var hw := _hist_w()
	return Rect2(PAD, DISP_H + PAD, size.x - hw - PAD * 2.0,
		size.y - DISP_H - PAD * 2.0)


func _keys() -> Array:
	var a := _pad_rect()
	var out_keys: Array = []
	if a.size.x < 40.0 or a.size.y < 40.0:
		return out_keys
	var cw := a.size.x / 4.0
	var ch := a.size.y / 6.0
	for k in KEYS:
		var r := Rect2(a.position.x + k["c"] * cw + 1.0,
			a.position.y + k["r"] * ch + 1.0,
			cw * k["w"] - 2.0, ch - 2.0)
		out_keys.append({"t": k["t"], "r": r})
	return out_keys


func _hist_rows() -> Array:
	var hw := _hist_w()
	var rows: Array = []
	if hw <= 0.0:
		return rows
	var x := size.x - hw + 2.0
	var y := DISP_H + PAD
	var n := int((size.y - y - PAD) / 26.0)
	var first: int = maxi(0, hist.size() - n)
	for i in range(first, hist.size()):
		rows.append({"i": i, "r": Rect2(x, y, hw - PAD - 2.0, 24.0)})
		y += 26.0
	return rows


# ---------------------------------------------------------------- input

func _gui_input(e: InputEvent) -> void:
	if e is InputEventMouseButton and e.pressed:
		grab_focus()
		var mb := e as InputEventMouseButton
		if mb.button_index != MOUSE_BUTTON_LEFT:
			return
		for k in _keys():
			if (k["r"] as Rect2).has_point(mb.position):
				_press(str(k["t"]))
				return
		# A history line is a value you already worked out. Clicking it puts
		# that value into the sum, which is the whole reason to keep them.
		for h in _hist_rows():
			if (h["r"] as Rect2).has_point(mb.position):
				var v: String = hist[h["i"]]["v"]
				if not v.begins_with("that"):
					expr += v
					_live()
					queue_redraw()
				return
		return

	if not (e is InputEventKey) or not e.pressed:
		return
	var k2 := e as InputEventKey
	var t := ""
	match k2.keycode:
		KEY_0, KEY_KP_0: t = "0"
		KEY_1, KEY_KP_1: t = "1"
		KEY_2, KEY_KP_2: t = "2"
		KEY_3, KEY_KP_3: t = "3"
		KEY_4, KEY_KP_4: t = "4"
		KEY_5, KEY_KP_5: t = "5"
		KEY_6, KEY_KP_6: t = "6"
		KEY_7, KEY_KP_7: t = "7"
		KEY_8, KEY_KP_8: t = "8"
		KEY_9, KEY_KP_9: t = "9"
		KEY_PERIOD, KEY_KP_PERIOD, KEY_COMMA: t = "."
		KEY_PLUS, KEY_KP_ADD: t = "+"
		KEY_MINUS, KEY_KP_SUBTRACT: t = "-"
		KEY_ASTERISK, KEY_KP_MULTIPLY: t = "*"
		KEY_SLASH, KEY_KP_DIVIDE: t = "/"
		KEY_PARENLEFT: t = "("
		KEY_PARENRIGHT: t = ")"
		KEY_PERCENT: t = "%"
		KEY_EQUAL, KEY_ENTER, KEY_KP_ENTER: t = "="
		KEY_BACKSPACE: t = "<-"
		KEY_ESCAPE, KEY_DELETE: t = "C"
		KEY_N, KEY_F9: t = "+/-"
		_:
			# The shifted keys arrive as the unshifted keycode on some layouts,
			# so the typed character is the last word on what was meant.
			var u := k2.unicode
			if u > 0:
				var ch := String.chr(u)
				if "0123456789.+-*/()%=".find(ch) >= 0:
					t = ch
			if t == "":
				return
	accept_event()
	_press(t)


# ---------------------------------------------------------------- drawing

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


func _fit(t: String, w: float, fs: int) -> String:
	if mono.get_string_size(t, HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x <= w:
		return t
	var s := t
	while s.length() > 1 and \
			mono.get_string_size(s + "...", HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x > w:
		s = s.substr(0, s.length() - 1)
	return s + "..."


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), CHROME)

	# The display: what you typed above, what it comes to below. Both, always,
	# because the answer alone is unverifiable.
	var dw := size.x - PAD * 2.0
	var disp := Rect2(PAD, PAD, dw, DISP_H - PAD * 2.0)
	draw_rect(disp, WHITE)
	draw_rect(disp, Color("#8e8b86"), false, 1.0)
	var shown := expr if expr != "" else ("ans " + _fmt(ans) if has_ans else "")
	draw_string(mono, Vector2(disp.position.x + 6, disp.position.y + 14),
		_fit(shown, dw - 12.0, 12), HORIZONTAL_ALIGNMENT_LEFT, -1, 12, DIM)
	draw_string(mono, Vector2(disp.position.x + 6, disp.position.y + 33),
		_fit(out, dw - 12.0, 18), HORIZONTAL_ALIGNMENT_RIGHT, dw - 12.0, 18,
		RED if err else INK)

	for k in _keys():
		var t := str(k["t"])
		var face := FACE
		if t == "=":
			face = Color("#c3d6ee")
		elif "0123456789.".find(t) < 0:
			face = Color("#d2cfca")
		if t == "C":
			face = Color("#e7cdc6")
		var r: Rect2 = k["r"]
		_raised(r, face)
		draw_string(mono, Vector2(r.position.x, r.position.y + r.size.y * 0.5 + 5.0),
			t, HORIZONTAL_ALIGNMENT_CENTER, r.size.x, 14, INK)

	var hw := _hist_w()
	if hw > 0.0:
		var hx := size.x - hw + PAD
		draw_rect(Rect2(size.x - hw, DISP_H, hw, size.y - DISP_H), Color("#eceae7"))
		draw_line(Vector2(size.x - hw, DISP_H), Vector2(size.x - hw, size.y),
			Color("#b3b0ab"))
		if hist.is_empty():
			draw_string(mono, Vector2(hx, DISP_H + 20), "no sums yet",
				HORIZONTAL_ALIGNMENT_LEFT, -1, 10, Color("#8b8f94"))
		for h in _hist_rows():
			var r2: Rect2 = h["r"]
			var it: Dictionary = hist[h["i"]]
			draw_string(mono, Vector2(r2.position.x + 2, r2.position.y + 10),
				_fit(str(it["e"]), r2.size.x - 4.0, 10),
				HORIZONTAL_ALIGNMENT_LEFT, -1, 10, DIM)
			draw_string(mono, Vector2(r2.position.x + 2, r2.position.y + 22),
				_fit(str(it["v"]), r2.size.x - 4.0, 12),
				HORIZONTAL_ALIGNMENT_RIGHT, r2.size.x - 4.0, 12, SEL)
			draw_line(Vector2(r2.position.x, r2.position.y + r2.size.y + 1),
				Vector2(r2.position.x + r2.size.x, r2.position.y + r2.size.y + 1),
				Color("#dcd9d5"))
