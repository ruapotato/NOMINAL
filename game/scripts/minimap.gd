# minimap.gd — the plan of the floor you are standing on.
#
# The owner, after walking his own building: "It would be better if that window
# just said the floor you were on and the room you were on like it does now,
# but also included a mini-map instead of that confusing blurb of seemingly
# nonsense." The blurb went in the commit before this one. This is the other
# half of the sentence.
#
# NOTHING IN THIS FILE KNOWS ANYTHING, which is the same rule people.gd is
# written under. It is handed tower.map_rows() -- one entry per room on this
# floor, in metres, with a flag for the one you are standing in and the socket
# count off the site model -- and it draws that. It does not know what a riser
# is, it cannot say a room exists that the generator did not make, and if a
# room moves the map moves with it because there is no second copy of where
# anything is.
#
# WHY POWER IS ON IT. He asked for the map and for power in one breath: "have
# a way to view the mini map for the entire area and request or order
# additional power". It was a count of sockets on each room's wall, and there
# is no wall any more -- "per room outlets will go away, all things will be
# powered by the new conduit power system". What is marked now is where power
# can COME from: a source standing in that room -- the core, or a strip you
# have fed -- with a way out still free. That is the thing you want to know
# before you carry a switch up two decks, and it is the tree's own number.
#
# It is drawn, not built out of nodes: a floor is twenty-odd rectangles and a
# dot, and twenty Controls that move every frame is what the HUD text pool was
# rewritten to stop doing.

extends Control

const PAD := 8.0            # inside the frame, in pixels
const W := 190.0            # the panel, in pixels
const H := 150.0

# THE ROOM KINDS COME FROM THE TOWER, NOT FROM A COPY OF THEM HERE. The first
# draft of this file wrote the numbers out again -- K_CORRIDOR := 1, K_RISER
# := 9 -- and every one of them was wrong, because they are 0 and 5. Nothing
# would have crashed: the map would just have painted the riser in the
# corridor's colour for ever. Same defect as every other one this project
# keeps finding, and the cure is the same: ask the thing that knows.
var tower: Node3D = null

var rows: Array = []
var floor_no := 0
var here := Vector2.ZERO     # the body, in metres
var facing := 0.0            # yaw, radians

var _font: Font
var _sig := ""


func _ready() -> void:
	_font = preload("res://scripts/uifont.gd").mono()
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	custom_minimum_size = Vector2(W, H)
	size = Vector2(W, H)


# Called every frame by tower.gd and redrawn only when something moved enough
# to see. A map that repaints sixty times a second while you stand still costs
# more than the floor it is a picture of.
func show_floor(f: int, r: Array, at: Vector2, yaw: float) -> void:
	var sig := "%d:%d:%d:%d:%d" % [f, r.size(), int(at.x * 2.0), int(at.y * 2.0),
		int(yaw * 8.0)]
	if sig == _sig:
		return
	_sig = sig
	floor_no = f
	rows = r
	here = at
	facing = yaw
	queue_redraw()


# The needle, as a unit vector in map space, so a test can ask which way it
# points without reading pixels. One expression, used by _draw() and by the
# assertion in game/tests/tower.gd.
func needle() -> Vector2:
	return Vector2(-sin(facing), -cos(facing))


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, Vector2(W, H)), Color(0.04, 0.06, 0.08, 0.72))
	draw_rect(Rect2(Vector2.ZERO, Vector2(W, H)), Color(0.42, 0.48, 0.55, 0.55), false, 1.0)
	if rows.is_empty():
		return
	# THE PLATE, MEASURED OFF THE ROOMS THEMSELVES. The building generator is
	# free to make any footprint it likes and this has to fit whatever it made,
	# so the extent comes from the rooms rather than from a constant.
	var mn := Vector2(INF, INF)
	var mx := Vector2(-INF, -INF)
	for m in rows:
		mn.x = min(mn.x, float(m.x0)); mn.y = min(mn.y, float(m.y0))
		mx.x = max(mx.x, float(m.x1)); mx.y = max(mx.y, float(m.y1))
	var span := mx - mn
	if span.x <= 0.0 or span.y <= 0.0:
		return
	# ONE SCALE FOR BOTH AXES. Stretching a plan to fill a panel makes a square
	# room oblong and a corridor look like a room, and the whole use of this
	# thing is recognising the shape you are standing in.
	var k: float = min((W - PAD * 2.0) / span.x, (H - PAD * 2.0 - 12.0) / span.y)
	var org := Vector2(PAD + ((W - PAD * 2.0) - span.x * k) * 0.5,
		PAD + 12.0 + ((H - PAD * 2.0 - 12.0) - span.y * k) * 0.5)

	for m in rows:
		var a: Vector2 = org + (Vector2(float(m.x0), float(m.y0)) - mn) * k
		var b: Vector2 = org + (Vector2(float(m.x1), float(m.y1)) - mn) * k
		var rc := Rect2(a, b - a)
		draw_rect(rc, _fill(int(m.kind), bool(m.here)))
		draw_rect(rc, Color(0.55, 0.62, 0.70, 0.75), false, 1.0)
		# A ROOM YOU CAN PLUG SOMETHING IN IS THE ONE YOU NEED TO KNOW ABOUT
		# BEFORE YOU CARRY A SWITCH INTO IT, so that is what gets the mark: a
		# dot per free way out of a source standing there, up to four. A room
		# with none is unmarked, which is most of them. The number is the
		# model's, not this file's.
		if int(m.outlets) > 0 and rc.size.x > 9.0 and rc.size.y > 7.0:
			var n: int = min(int(m.free), 4)
			for j in range(n):
				draw_rect(Rect2(a + Vector2(2.0 + float(j) * 3.0, rc.size.y - 4.0),
					Vector2(2.0, 2.0)), Color("#8fd6a0"))


	# YOU, and which way you are looking. A dot alone on a plan is not enough
	# to turn towards a door with.
	var me: Vector2 = org + (here - mn) * k
	# WHICH WAY THE NEEDLE POINTS, and it pointed backwards.
	#
	# David: "A mini map is a circle with a line, but the line points behind
	# you, not in front of you." He is right and it is a sign: a body's
	# forward in this engine is `basis * Vector3(0, 0, -1)`, which in world
	# x/z is (-sin(yaw), -cos(yaw)), and this drew (+sin, +cos) -- the exact
	# negation. The map's y is world z with no flip, so the two have to agree.
	var dir := needle()
	draw_line(me, me + dir * 9.0, Color("#ffd479"), 1.5)
	draw_circle(me, 2.6, Color("#ffd479"))

	if _font:
		draw_string(_font, Vector2(PAD, PAD + 8.0), "DECK %d" % floor_no,
			HORIZONTAL_ALIGNMENT_LEFT, -1, 10, Color(0.78, 0.84, 0.90))


func _fill(kind: int, mine: bool) -> Color:
	if mine:
		return Color("#3c5a74")
	if tower == null:
		return Color(0.18, 0.19, 0.21, 0.9)
	if kind == tower.K_CORRIDOR: return Color(0.13, 0.15, 0.18, 0.9)
	if kind == tower.K_LIFT:     return Color(0.30, 0.24, 0.12, 0.9)
	if kind == tower.K_STAIR:    return Color(0.16, 0.28, 0.20, 0.9)
	if kind == tower.K_RISER:    return Color(0.26, 0.18, 0.26, 0.9)
	if kind == tower.K_COMMS or kind == tower.K_MDF:
		return Color(0.18, 0.22, 0.30, 0.9)
	return Color(0.18, 0.19, 0.21, 0.9)
