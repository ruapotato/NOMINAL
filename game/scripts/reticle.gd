# reticle.gd — the dot in the middle of the screen, and what it is on.
#
# The owner, having played it: "The center of the screen is missing a dot so
# it's precisely hit anything. I was able to hit E on a computer, but it was
# really hard to know when I was actually hitting E on the right thing... Let's
# also add in next to the dot what you're interacting with. So it would show
# you that you're interacting with the computer or a particular port or a
# server."
#
# So there are two things here and they are different things. The DOT says
# where the ray goes. The LABEL beside it says what the ray landed on and which
# key does what to it. Neither invents anything: tower.aim() does the ray and
# tower.aim_text() names the thing, out of the site model's own names.
#
# The dot has three states, because "targetable" is the fact the player is
# missing: a small dim ring when there is nothing there, a bright ring with a
# filled centre when the ray is on something, and the accent colour when what
# it is on is a port and there is a spool in your hands -- the one moment when
# a click does something irreversible to the building.

extends Control

const DOT := 2.0
const RING := 7.0

var what := ""              # "core port 6"
var hint := ""              # "[LMB] plug in"
var hot := false            # the ray is really on it
var live := false           # ... and clicking would run cable

var _font: Font


func _ready() -> void:
	_font = preload("res://scripts/uifont.gd").mono()
	set_anchors_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE


func show_target(w: String, h: String, on: bool, cabling: bool) -> void:
	if w == what and h == hint and on == hot and cabling == live:
		return
	what = w
	hint = h
	hot = on
	live = cabling
	queue_redraw()


func _draw() -> void:
	var c := (get_viewport_rect().size * 0.5).round()
	var col := Color("#e8eef4")
	if live:
		col = Color("#7fe08a")
	elif not hot:
		col = Color(0.85, 0.89, 0.94, 0.45)
	# The ring, then the dot inside it. A ring alone is a doughnut you cannot
	# aim with; a dot alone disappears against a rack. Both, and the exact
	# centre is the pixel the ray goes through.
	draw_arc(c, RING, 0.0, TAU, 24, Color(0, 0, 0, 0.55), 3.0, true)
	draw_arc(c, RING, 0.0, TAU, 24, col, 1.0, true)
	if hot:
		draw_circle(c, DOT + 1.0, Color(0, 0, 0, 0.6))
		draw_circle(c, DOT, col)
	else:
		draw_circle(c, 1.0, col)
	if what == "":
		return
	# and the name of the thing, beside the dot rather than under it, so it
	# never sits on top of what you are aiming at
	var at := c + Vector2(RING + 12.0, 5.0)
	draw_string(_font, at + Vector2(1, 1), what, HORIZONTAL_ALIGNMENT_LEFT, -1, 15,
		Color(0, 0, 0, 0.8))
	draw_string(_font, at, what, HORIZONTAL_ALIGNMENT_LEFT, -1, 15,
		Color("#f2f6fa") if hot else Color(0.95, 0.97, 1.0, 0.6))
	if hint != "":
		var w2 := _font.get_string_size(what, HORIZONTAL_ALIGNMENT_LEFT, -1, 15).x
		var at2 := at + Vector2(w2 + 14.0, 0.0)
		draw_string(_font, at2 + Vector2(1, 1), hint, HORIZONTAL_ALIGNMENT_LEFT, -1, 15,
			Color(0, 0, 0, 0.8))
		draw_string(_font, at2, hint, HORIZONTAL_ALIGNMENT_LEFT, -1, 15,
			Color("#7fe08a") if live else Color("#a8c8e8"))
