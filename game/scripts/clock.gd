# clock.gd — the clock, the calendar, the timer and the stopwatch.
#
# THIS ONE IS ABOUT YOUR WORKSTATION, NOT THE CUSTOMER'S MACHINE, and that
# distinction is the whole reason it does not run a command. The customer's box
# has its own clock, ntpd, and a `date` that can be hours out -- that is a
# fault, it belongs to sysmon and to `ntpd`, and if this window quietly showed
# THEIR time you would read the wrong number off the wrong machine while
# writing a change window into a ticket. So: the time here is
# Time.get_datetime_dict_from_system(), the clock of the desk you are sitting
# at, and the footer says so on every tab.
#
# The timer and the stopwatch count with the frame delta rather than by
# differencing the wall clock. A stopwatch that subtracts two system times
# jumps whenever the host clock is stepped -- which is exactly the moment
# somebody is likely to be timing something.

extends Control

var mono: Font
var machine: Object = null   # unused: see the note above. Kept for the contract.

var tab := 0                 # 0 clock, 1 calendar, 2 timer, 3 stopwatch

# calendar
var cal_y := 0               # 0 means "not initialised yet, use today"
var cal_m := 0
var cal_d := 0

# countdown
var timer_set := 300.0       # what R goes back to
var timer_left := 300.0
var timer_run := false
var timer_rang := false
var flash := 0.0

# stopwatch
var watch := 0.0
var watch_run := false
var laps: Array = []         # seconds at the moment L was pressed

var last_sec := -1

const TAB_H := 22.0
const BOT_H := 26.0
const TAB_W := 76.0
const TITLES := ["clock", "calendar", "timer", "stopwatch"]
const DAYNAMES := ["Sunday", "Monday", "Tuesday", "Wednesday", "Thursday",
	"Friday", "Saturday"]
const MONTHS := ["January", "February", "March", "April", "May", "June",
	"July", "August", "September", "October", "November", "December"]

const CHROME := Color("#d6d3ce")
const WHITE := Color("#ffffff")
const INK := Color("#1b1b1b")
const DIM := Color("#5f6469")
const FAINT := Color("#9aa0a6")
const SEL := Color("#3465a4")
const RED := Color("#b0281a")
const GREEN := Color("#1f6b3a")
const AMBER := Color("#8a6d1f")
const FACE := Color("#fbfaf8")
const RIM := Color("#6b7176")


func _ready() -> void:
	focus_mode = Control.FOCUS_ALL
	mouse_filter = Control.MOUSE_FILTER_STOP
	if mono == null:
		mono = ThemeDB.fallback_font
	var n := Time.get_datetime_dict_from_system()
	cal_y = int(n.get("year", 1970))
	cal_m = int(n.get("month", 1))
	cal_d = int(n.get("day", 1))
	set_process(true)


func take_focus() -> void:
	grab_focus()


# Redraw when something a person can see has changed: the displayed second,
# a running countdown, a running stopwatch, or the alarm flashing. Redrawing
# every frame to move a second hand once a second is sixty times the work for
# the same picture.
func _process(dt: float) -> void:
	var dirty := false
	var s := int(Time.get_datetime_dict_from_system().get("second", 0))
	if s != last_sec:
		last_sec = s
		dirty = true
	if timer_run:
		timer_left -= dt
		if timer_left <= 0.0:
			timer_left = 0.0
			timer_run = false
			timer_rang = true
			flash = 0.0
		dirty = true
	if watch_run:
		watch += dt
		dirty = true
	if timer_rang:
		flash += dt
		dirty = true
	if dirty:
		queue_redraw()


# ------------------------------------------------------------ calendar maths

func _leap(y: int) -> bool:
	return (y % 4 == 0 and y % 100 != 0) or y % 400 == 0


func _days_in(y: int, m: int) -> int:
	match m:
		1, 3, 5, 7, 8, 10, 12: return 31
		4, 6, 9, 11: return 30
		_: return 29 if _leap(y) else 28


# Godot's own calendar, asked at noon. Noon rather than midnight because the
# unix conversion is UTC and a midnight timestamp in a negative offset lands on
# the previous day -- the classic off-by-one that makes a calendar disagree
# with the clock beside it for the first twelve hours of every day.
func _weekday(y: int, m: int, d: int) -> int:
	var t := Time.get_unix_time_from_datetime_dict({
		"year": y, "month": m, "day": d, "hour": 12, "minute": 0, "second": 0})
	return int(Time.get_datetime_dict_from_unix_time(int(t)).get("weekday", 0))


func _shift_month(by: int) -> void:
	var m := cal_m + by
	while m > 12:
		m -= 12
		cal_y += 1
	while m < 1:
		m += 12
		cal_y -= 1
	cal_m = m
	cal_d = mini(cal_d, _days_in(cal_y, cal_m))


func _shift_day(by: int) -> void:
	var d := cal_d + by
	while d > _days_in(cal_y, cal_m):
		d -= _days_in(cal_y, cal_m)
		_shift_month(1)
	while d < 1:
		_shift_month(-1)
		d += _days_in(cal_y, cal_m)
	cal_d = d


func _today() -> void:
	var n := Time.get_datetime_dict_from_system()
	cal_y = int(n.get("year", 1970))
	cal_m = int(n.get("month", 1))
	cal_d = int(n.get("day", 1))


# ---------------------------------------------------------------- formatting

func _two(n: int) -> String:
	return ("0" + str(n)) if n < 10 else str(n)


func _hms(t: float) -> String:
	var w := int(floor(maxf(0.0, t)))
	return "%s:%s:%s" % [_two(w / 3600), _two((w / 60) % 60), _two(w % 60)]


# Tenths, not hundredths. A stopwatch driven by the frame delta cannot
# honestly claim a hundredth of a second at sixty frames a second, and a
# digit that flickers through values it never really held is a lie with a
# decimal point in it.
func _hmst(t: float) -> String:
	return "%s.%d" % [_hms(t), int(fmod(maxf(0.0, t), 1.0) * 10.0)]


func _fit(t: String, w: float, fs: int) -> String:
	if mono.get_string_size(t, HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x <= w:
		return t
	var s := t
	while s.length() > 1 and \
			mono.get_string_size(s + "...", HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x > w:
		s = s.substr(0, s.length() - 1)
	return s + "..."


# The biggest font size at which a string still fits a box. Everything on this
# window is one line of digits in a rectangle whose size is the user's choice,
# so this is how all four tabs survive a squeeze to 320x240.
func _fit_size(t: String, w: float, want: int, floor_px: int = 8) -> int:
	var fs := want
	while fs > floor_px and \
			mono.get_string_size(t, HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x > w:
		fs -= 1
	return fs


# ---------------------------------------------------------------- input

func _gui_input(e: InputEvent) -> void:
	if e is InputEventMouseButton and e.pressed:
		grab_focus()
		var mb := e as InputEventMouseButton
		if mb.button_index == MOUSE_BUTTON_LEFT:
			if mb.position.y < TAB_H:
				var i := int(mb.position.x / TAB_W)
				if i >= 0 and i < TITLES.size():
					tab = i
			elif tab == 1:
				var hit := _cal_hit(mb.position)
				if hit > 0:
					cal_d = hit
		accept_event()
		queue_redraw()
		return

	if not (e is InputEventKey) or not e.pressed:
		return
	var k := e as InputEventKey
	if k.keycode == KEY_TAB:
		tab = (tab + 1) % TITLES.size()
		accept_event()
		queue_redraw()
		return
	if k.keycode >= KEY_1 and k.keycode <= KEY_4:
		tab = k.keycode - KEY_1
		accept_event()
		queue_redraw()
		return
	match tab:
		1: _cal_key(k)
		2: _timer_key(k)
		3: _watch_key(k)
		_: pass
	accept_event()
	queue_redraw()


func _cal_key(k: InputEventKey) -> void:
	match k.keycode:
		KEY_LEFT: _shift_day(-1)
		KEY_RIGHT: _shift_day(1)
		KEY_UP: _shift_day(-7)
		KEY_DOWN: _shift_day(7)
		KEY_PAGEUP: _shift_month(-1)
		KEY_PAGEDOWN: _shift_month(1)
		KEY_HOME, KEY_T: _today()
		_: pass


func _timer_key(k: InputEventKey) -> void:
	match k.keycode:
		KEY_SPACE:
			# Starting a rung-out timer without resetting it would run from
			# zero and never fire again, which reads as a broken button.
			if timer_rang:
				timer_rang = false
				timer_left = timer_set
			timer_run = not timer_run
		KEY_R:
			timer_run = false
			timer_rang = false
			timer_left = timer_set
		KEY_UP: _timer_add(60.0)
		KEY_DOWN: _timer_add(-60.0)
		KEY_RIGHT: _timer_add(10.0)
		KEY_LEFT: _timer_add(-10.0)
		_: pass


# Adjusting the countdown adjusts the PRESET too, otherwise R throws away the
# number you just dialled in and puts back one you have forgotten.
func _timer_add(by: float) -> void:
	timer_rang = false
	timer_left = clampf(timer_left + by, 0.0, 359999.0)
	timer_set = timer_left


func _watch_key(k: InputEventKey) -> void:
	match k.keycode:
		KEY_SPACE: watch_run = not watch_run
		KEY_R:
			watch_run = false
			watch = 0.0
			laps = []
		KEY_L:
			if watch > 0.0:
				laps.append(watch)
		_: pass


# ---------------------------------------------------------------- drawing

func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), CHROME)
	_draw_tabs()
	var body := Rect2(0, TAB_H, size.x, maxf(20.0, size.y - TAB_H - BOT_H))
	draw_rect(body, WHITE)
	match tab:
		0: _draw_clock(body)
		1: _draw_calendar(body)
		2: _draw_timer(body)
		_: _draw_watch(body)
	_draw_foot()


func _draw_tabs() -> void:
	draw_rect(Rect2(0, 0, size.x, TAB_H), Color("#cfccc7"))
	for i in range(TITLES.size()):
		var r := Rect2(i * TAB_W, 0, TAB_W - 1.0, TAB_H)
		if r.position.x >= size.x:
			break
		if i == tab:
			draw_rect(r, WHITE)
			draw_rect(Rect2(r.position.x, TAB_H - 2.0, r.size.x, 2.0), SEL)
		draw_string(mono, Vector2(r.position.x + 6, 15),
			_fit(TITLES[i], TAB_W - 12.0, 11), HORIZONTAL_ALIGNMENT_LEFT, -1, 11,
			INK if i == tab else DIM)
	draw_line(Vector2(0, TAB_H), Vector2(size.x, TAB_H), Color("#a9a6a1"))


func _draw_foot() -> void:
	var y := size.y - BOT_H
	draw_rect(Rect2(0, y, size.x, BOT_H), Color("#e6e3de"))
	draw_line(Vector2(0, y), Vector2(size.x, y), Color("#b3b0ab"))
	var hint := ""
	match tab:
		0: hint = "this workstation's clock -- not the customer's"
		1: hint = "arrows day, PgUp/PgDn month, T today"
		2: hint = "space start/pause, R reset, up/down minutes, left/right 10s"
		_: hint = "space start/stop, L lap, R reset"
	draw_string(mono, Vector2(8, y + 12), _fit(hint, size.x - 16.0, 10),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 10, INK)
	draw_string(mono, Vector2(8, y + 22),
		_fit("Godot's system time; the machine's own clock is `date` on the machine",
			size.x - 16.0, 9), HORIZONTAL_ALIGNMENT_LEFT, -1, 9, Color("#7c8085"))


# ------------------------------------------------------------------- clock

func _draw_clock(body: Rect2) -> void:
	var n := Time.get_datetime_dict_from_system()
	var h := int(n.get("hour", 0))
	var m := int(n.get("minute", 0))
	var s := int(n.get("second", 0))

	# Wide enough for two panels: face on the left, digits on the right.
	# Narrow: face on top, digits under it. Below that, no face at all --
	# a thirty-pixel dial is decoration, and the digits are the information.
	var wide := body.size.x >= 420.0
	var face_r := Rect2()
	var text_r := Rect2()
	if wide:
		face_r = Rect2(body.position, Vector2(body.size.x * 0.45, body.size.y))
		text_r = Rect2(body.position + Vector2(face_r.size.x, 0),
			Vector2(body.size.x - face_r.size.x, body.size.y))
	else:
		var fh: float = minf(body.size.y * 0.6, body.size.x)
		face_r = Rect2(body.position, Vector2(body.size.x, fh))
		text_r = Rect2(body.position + Vector2(0, fh),
			Vector2(body.size.x, body.size.y - fh))

	if minf(face_r.size.x, face_r.size.y) >= 70.0:
		_draw_face(face_r, h, m, s)

	var digits := "%s:%s:%s" % [_two(h), _two(m), _two(s)]
	var fs := _fit_size(digits, text_r.size.x - 16.0, 40, 12)
	var dw := mono.get_string_size(digits, HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x
	var cx := text_r.position.x + (text_r.size.x - dw) / 2.0
	var cy := text_r.position.y + text_r.size.y / 2.0
	draw_string(mono, Vector2(cx, cy), digits, HORIZONTAL_ALIGNMENT_LEFT, -1,
		fs, INK)

	var wd := _weekday(int(n.get("year", 1970)), int(n.get("month", 1)),
		int(n.get("day", 1)))
	var date := "%s %d %s %d" % [DAYNAMES[wd], int(n.get("day", 1)),
		MONTHS[clampi(int(n.get("month", 1)) - 1, 0, 11)], int(n.get("year", 1970))]
	var dfs := _fit_size(date, text_r.size.x - 16.0, 13, 8)
	draw_string(mono, Vector2(text_r.position.x + 8, cy + 18),
		_fit(date, text_r.size.x - 16.0, dfs), HORIZONTAL_ALIGNMENT_CENTER,
		text_r.size.x - 16.0, dfs, DIM)

	# The 24-hour reading spelled out, because the analogue face cannot say
	# which half of the day it is and a change window written at the wrong
	# twelve hours is a genuine outage.
	draw_string(mono, Vector2(text_r.position.x + 8, cy + 32),
		"%s, %d-%s-%s" % ["morning" if h < 12 else "afternoon/evening",
			int(n.get("year", 1970)), _two(int(n.get("month", 1))),
			_two(int(n.get("day", 1)))],
		HORIZONTAL_ALIGNMENT_CENTER, text_r.size.x - 16.0, 9, FAINT)


func _draw_face(r: Rect2, h: int, m: int, s: int) -> void:
	var c := r.position + r.size / 2.0
	var rad: float = minf(r.size.x, r.size.y) / 2.0 - 8.0
	draw_circle(c, rad, FACE)
	draw_circle(c, rad, RIM, false, 1.5)
	for i in range(60):
		var a := float(i) * TAU / 60.0 - PI / 2.0
		var v := Vector2(cos(a), sin(a))
		var inner: float = rad - (7.0 if i % 5 == 0 else 3.0)
		draw_line(c + v * inner, c + v * (rad - 1.0),
			INK if i % 5 == 0 else FAINT, 2.0 if i % 5 == 0 else 1.0)
	# The hour hand moves with the minutes. A hand that jumps between hours
	# points at 3 for fifty-nine minutes and is wrong for all of them.
	var ha := (float(h % 12) + float(m) / 60.0) * TAU / 12.0 - PI / 2.0
	var ma := (float(m) + float(s) / 60.0) * TAU / 60.0 - PI / 2.0
	var sa := float(s) * TAU / 60.0 - PI / 2.0
	draw_line(c, c + Vector2(cos(ha), sin(ha)) * (rad * 0.52), INK, 3.5)
	draw_line(c, c + Vector2(cos(ma), sin(ma)) * (rad * 0.78), INK, 2.5)
	draw_line(c - Vector2(cos(sa), sin(sa)) * (rad * 0.12),
		c + Vector2(cos(sa), sin(sa)) * (rad * 0.88), RED, 1.0)
	draw_circle(c, 3.0, INK)


# ---------------------------------------------------------------- calendar

func _cal_grid(body: Rect2) -> Dictionary:
	var top := body.position.y + 40.0
	var cw := body.size.x / 7.0
	# 16 pixels held back for the line that spells the selected date out. At
	# 320x240 the six rows otherwise run underneath it and the last week of
	# the month is printed through a sentence.
	var ch: float = maxf(10.0, (body.position.y + body.size.y - top - 16.0) / 6.0)
	return {"top": top, "cw": cw, "ch": ch}


func _cal_hit(p: Vector2) -> int:
	var body := Rect2(0, TAB_H, size.x, maxf(20.0, size.y - TAB_H - BOT_H))
	var g := _cal_grid(body)
	var col := int(p.x / float(g["cw"]))
	var row := int((p.y - float(g["top"])) / float(g["ch"]))
	if col < 0 or col > 6 or row < 0 or row > 5:
		return -1
	var d: int = row * 7 + col - _weekday(cal_y, cal_m, 1) + 1
	return d if d >= 1 and d <= _days_in(cal_y, cal_m) else -1


func _draw_calendar(body: Rect2) -> void:
	var n := Time.get_datetime_dict_from_system()
	var ty := int(n.get("year", 1970))
	var tm := int(n.get("month", 1))
	var td := int(n.get("day", 1))

	var head := "%s %d" % [MONTHS[clampi(cal_m - 1, 0, 11)], cal_y]
	draw_rect(Rect2(body.position, Vector2(body.size.x, 22)), Color("#eceae7"))
	draw_string(mono, body.position + Vector2(0, 15), head,
		HORIZONTAL_ALIGNMENT_CENTER, body.size.x, 13, INK)
	draw_string(mono, body.position + Vector2(6, 15), "<",
		HORIZONTAL_ALIGNMENT_LEFT, -1, 13, DIM)
	draw_string(mono, body.position + Vector2(-8, 15), ">",
		HORIZONTAL_ALIGNMENT_RIGHT, body.size.x, 13, DIM)

	var g := _cal_grid(body)
	var cw: float = g["cw"]
	var ch: float = g["ch"]
	var top: float = g["top"]
	var initials := ["S", "M", "T", "W", "T", "F", "S"]
	for i in range(7):
		draw_string(mono, Vector2(i * cw, top - 6), initials[i],
			HORIZONTAL_ALIGNMENT_CENTER, cw, 10,
			RED if (i == 0 or i == 6) else DIM)

	var first := _weekday(cal_y, cal_m, 1)
	var days := _days_in(cal_y, cal_m)
	var fs: int = clampi(int(ch * 0.55), 8, 14)
	for d in range(1, days + 1):
		var idx := first + d - 1
		var col := idx % 7
		var row := idx / 7
		if row > 5:
			break
		var cell := Rect2(col * cw + 1.0, top + row * ch + 1.0, cw - 2.0, ch - 2.0)
		var is_today := (d == td and cal_m == tm and cal_y == ty)
		if d == cal_d:
			draw_rect(cell, Color("#dbe7f6"))
			draw_rect(cell, Color("#7aa7d8"), false, 1.0)
		if is_today:
			# Today is ringed, not filled, so it stays visible under the
			# selection -- otherwise moving the cursor onto today makes today
			# disappear, and the one date you came to check is the one you
			# cannot see.
			draw_rect(cell, RED, false, 1.5)
		draw_string(mono, Vector2(cell.position.x, cell.position.y + ch * 0.7),
			str(d), HORIZONTAL_ALIGNMENT_CENTER, cell.size.x, fs,
			RED if (col == 0 or col == 6) else INK)

	var sel_wd := _weekday(cal_y, cal_m, cal_d)
	var note := "%s %d %s %d" % [DAYNAMES[sel_wd], cal_d,
		MONTHS[clampi(cal_m - 1, 0, 11)], cal_y]
	if cal_y == ty and cal_m == tm and cal_d == td:
		note += "   (today)"
	draw_string(mono, Vector2(6, body.position.y + body.size.y - 3),
		_fit(note, body.size.x - 12.0, 10), HORIZONTAL_ALIGNMENT_LEFT, -1, 10,
		GREEN if note.ends_with("(today)") else DIM)


# ------------------------------------------------------------------- timer

func _draw_timer(body: Rect2) -> void:
	var txt := _hms(timer_left)
	var ringing := timer_rang and fmod(flash, 1.0) < 0.5
	if timer_rang:
		draw_rect(body, Color("#f9e2e0") if ringing else WHITE)

	var fs := _fit_size(txt, body.size.x - 24.0, 46, 14)
	var cy := body.position.y + body.size.y * 0.45
	draw_string(mono, Vector2(0, cy), txt, HORIZONTAL_ALIGNMENT_CENTER,
		body.size.x, fs,
		RED if (timer_rang or timer_left < 10.0) else INK)

	# A bar rather than a ring: it is readable at 320 wide, where a ring is
	# nine pixels across and tells you nothing.
	var bw := body.size.x - 24.0
	var by := cy + 12.0
	if by + 14.0 < body.position.y + body.size.y:
		var frac: float = 0.0 if timer_set <= 0.0 else clampf(timer_left / timer_set, 0.0, 1.0)
		draw_rect(Rect2(12, by, bw, 10), Color("#eceae7"))
		draw_rect(Rect2(12, by, bw * frac, 10),
			RED if frac < 0.1 else (AMBER if frac < 0.34 else SEL))
		draw_rect(Rect2(12, by, bw, 10), Color("#8e8b86"), false, 1.0)

	var state := "stopped"
	if timer_run:
		state = "running"
	elif timer_rang:
		state = "TIME -- it has finished"
	elif timer_left != timer_set:
		state = "paused"
	draw_string(mono, Vector2(12, body.position.y + body.size.y - 6),
		_fit("%s   set to %s" % [state, _hms(timer_set)], body.size.x - 24.0, 11),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 11,
		RED if timer_rang else (GREEN if timer_run else DIM))


# --------------------------------------------------------------- stopwatch

func _draw_watch(body: Rect2) -> void:
	var txt := _hmst(watch)
	var fs := _fit_size(txt, body.size.x - 24.0, 40, 12)
	var y := body.position.y + 12.0 + float(fs)
	draw_string(mono, Vector2(0, y), txt, HORIZONTAL_ALIGNMENT_CENTER,
		body.size.x, fs, GREEN if watch_run else INK)
	y += 8.0
	draw_line(Vector2(8, y), Vector2(body.size.x - 8, y), Color("#dcd9d4"))
	y += 14.0

	if laps.is_empty():
		draw_string(mono, Vector2(12, y), "no laps -- L takes one",
			HORIZONTAL_ALIGNMENT_LEFT, -1, 10, FAINT)
		return
	# Newest lap first: the one you just took is the one you want to read,
	# and a list that grows downwards off the bottom hides it.
	var i := laps.size() - 1
	var prev_shown := true
	while i >= 0 and y < body.position.y + body.size.y - 4.0:
		var t: float = laps[i]
		var split: float = t - (laps[i - 1] if i > 0 else 0.0)
		draw_string(mono, Vector2(12, y), "%2d" % (i + 1),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, DIM)
		draw_string(mono, Vector2(38, y), _hmst(t),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, INK)
		if body.size.x >= 260.0:
			draw_string(mono, Vector2(-12, y), "+" + _hmst(split),
				HORIZONTAL_ALIGNMENT_RIGHT, body.size.x, 11, DIM)
		y += 14.0
		i -= 1
		prev_shown = i < 0
	if not prev_shown:
		draw_string(mono, Vector2(12, body.position.y + body.size.y - 3),
			"%d more" % (i + 1), HORIZONTAL_ALIGNMENT_LEFT, -1, 9, FAINT)
