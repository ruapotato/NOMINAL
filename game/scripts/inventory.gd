# inventory.gd — [Tab] or [I], and what is in your hands.
#
# "I want tab to go to your inventory, Minecraft style, with whatever you have.
# ... [and later] tab may be overload for terminal."
# You can drag in your inventory items into left and right click so that you
# can equip, for example, the spool."
#
# TAB OPENS IT, AND SO DOES I. The owner played his own game and reported
# *"Tab to get to the inventory isn't working"*, and it was not: Tab was bound
# to nothing anywhere in the 3D shell. It had been moved to [I] on the reading
# of "tab may be overload for terminal" -- but that sentence is about the
# TERMINAL, and the collision it names is real only where a shell has the
# keyboard. Standing in a corridor there is no shell, no completion, and
# nothing else Tab could mean; the crosshair in tower.gd has gone on saying
# "[Tab] spool in hand to cable it" the whole time, pointing at a key that did
# nothing. One key doing two things silently was the bug under the bug, and the
# two things were never in the same place.
#
# So Tab is handled HERE, and it steps aside for every front end that types:
# a desk, somebody else's machine, and the handset all keep it for completion.
# [I] stays because it is in the HUD and in a day's worth of muscle memory.
#
# It is _unhandled_key_input rather than _input for the same reason: _input
# runs before the focused Control gets a look, and a bag that opened on top of
# a half-typed path would be Tab stealing from the shell rather than the other
# way round.
#
# THIS INVENTS NO STATE. Everything it shows is already true somewhere else and
# is read back out every time it draws:
#
#   the spool          tower.gd's _cable_from, which is site_cable() in core
#   the two leads      phone.gd's plugged/lead, which is machine.sh_on()
#   the box            tower.gd's carrying, which is site_move() in core/site.c
#
# So it is a picture of the simulation and a pair of shortcuts into it, which
# is the only way it can be added without becoming a second rulebook that
# drifts away from the first. A blind playtest drives `carry`, `drop`, `spool`
# and `cable` over the socket and never opens this at all; nothing here is
# reachable only from here.
#
# THE ONE RULE IT ENFORCES is the one core already has: both hands are on a box
# you are carrying. core/session.c refuses a drum of cable in those words --
# "you are carrying %s. A drum of cable takes both hands too" -- and the 3D
# shell used to let you run a cable with a switch under each arm, because the
# hands were not modelled anywhere in it. Now they are, and the refusal you
# read is the one core writes.

extends Control

const TILE := 74.0
const SLOT := 112.0

const KIT := {
	"spool": {"label": "cable spool",
		"hint": ["run copper between", "two boxes"]},
	"power": {"label": "power lead",
		"hint": ["a box into a socket", "on the room's wall"]},
	"serial": {"label": "debugger: serial",
		"hint": ["a console, even on a box", "that never booted"]},
}

const COL := {
	"spool": Color("#2f6fd0"),
	"power": Color("#8a5a3e"),
	"serial": Color("#1e6f3a"),
	"box": Color("#7c828c"),
}

var tower: Node3D = null

# What is in each hand: "", "spool", "power", "serial". A box in your arms
# is not stored here -- it is read off tower.carrying, because that is where it
# really is.
# WHAT IS IN YOUR HANDS WHEN THE DAY STARTS, and it used to be the two
# debugger leads: serial in the left, display in the right. So both mouse
# buttons were a debugger and the spool -- the drum that runs the cable this
# whole game is about -- was in the bag, reachable only by opening the
# inventory and dragging it into a hand. The owner never got that far: "by
# default you should have right click be the spool. The spool does not seem to
# work." It worked. Nothing armed it, and the one key that could was the
# inventory he also could not open.
var left := "serial"
var right := "spool"

var msg := ""
var _drag := ""
var _drag_from := -1        # -1 kit, 0 left hand, 1 right hand
var _at := Vector2.ZERO
var _font: Font


func _ready() -> void:
	_font = preload("res://scripts/uifont.gd").mono()
	set_anchors_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	visible = false


# ------------------------------------------------------------------ the model

func carrying_box() -> String:
	if tower == null or tower.carrying < 0:
		return ""
	for d in tower.site_devs():
		if int(d.i) == int(tower.carrying):
			return str(d.name)
	return "the box"


# BOTH HANDS ARE ON IT. Not a rule this file made up: it is why core/site.c
# moves the box with you every time you cross a threshold, and why session.c
# will not give you a drum of cable while you are holding one.
func hand(side: int) -> String:
	if carrying_box() != "":
		return "box"
	return left if side == 0 else right


func items() -> Array:
	var out: Array = []
	if carrying_box() != "":
		return out                 # your hands are full and the kit is on your belt
	for k in KIT.keys():
		if left != k and right != k:
			out.append(k)
	return out


func equip(item: String, side: int) -> String:
	var box := carrying_box()
	if box != "":
		return "you are carrying %s in both hands. A drum of cable takes both hands too: put it down first  [G]." % box
	# the same thing cannot be in both hands
	if side == 0:
		if right == item: right = left
		left = item
	else:
		if left == item: left = right
		right = item
	return ""


# What a mouse button does. `dev` is the device you are standing in front of,
# which is nearest_device()'s business, not this file's.
func use(side: int, dev: int, port := -1) -> String:
	var h := hand(side)
	match h:
		"box":
			return tower.drop_here()
		"spool":
			if dev < 0:
				return "nothing under the crosshair to put the end of the cable in."
			if port < 0:
				return "aim at a port. Every socket on the back of a box is a "  \
					+ "place a cable goes, and which one matters."
			return tower.cable_at(dev, port)
		"power":
			if dev < 0:
				return "nothing under the crosshair to plug into the wall."
			return tower.mains_at(dev)
		"serial":
			if dev < 0:
				if tower.phone and tower.phone.plugged >= 0:
					tower.phone.unplug()
					return "lead out."
				return "nothing in reach to plug into."
			# THROUGH THE TOWER, NOT STRAIGHT AT THE HANDSET. _lead_in() is
			# what knows that a console is out of band and that the RJ45 is
			# not it, and it is also what puts the lead in through the
			# session so a socket client and the window agree about what is
			# plugged in where.
			return str(tower._lead_in(dev, false))
	return "nothing in that hand."


# --------------------------------------------------------------------- the UI

func toggle() -> void:
	visible = not visible
	msg = ""
	_drag = ""
	if tower and tower.player:
		if visible:
			tower.player.capture(false)
			tower.player.velocity = Vector3.ZERO
			tower.player.set_physics_process(false)
		else:
			tower.player.set_physics_process(true)
			tower.player.capture(true)
	queue_redraw()


func _panel() -> Rect2:
	var vs := get_viewport_rect().size
	var sz := Vector2(720.0, 330.0)
	return Rect2(((vs - sz) * 0.5).round(), sz)


func _kit_rect(i: int) -> Rect2:
	var p := _panel()
	return Rect2(p.position + Vector2(26 + float(i) * (TILE + 14), 106), Vector2(TILE, TILE))


func _hand_rect(side: int) -> Rect2:
	var p := _panel()
	return Rect2(p.position + Vector2(p.size.x - 2 * SLOT - 86 + float(side) * (SLOT + 60), 92),
		Vector2(SLOT, SLOT))


func _icon(r: Rect2, item: String) -> void:
	var c: Color = COL.get(item, Color("#555b63"))
	var m := r.grow(-10.0)
	match item:
		"spool":
			# a drum on its side: two flanges and the copper between them
			draw_rect(Rect2(m.position, Vector2(m.size.x * 0.18, m.size.y)), c.darkened(0.35))
			draw_rect(Rect2(m.position + Vector2(m.size.x * 0.82, 0),
				Vector2(m.size.x * 0.18, m.size.y)), c.darkened(0.35))
			draw_rect(Rect2(m.position + Vector2(m.size.x * 0.18, m.size.y * 0.18),
				Vector2(m.size.x * 0.64, m.size.y * 0.64)), c)
		"box":
			draw_rect(m, c)
			draw_rect(Rect2(m.position + Vector2(0, m.size.y * 0.42),
				Vector2(m.size.x, m.size.y * 0.16)), c.lightened(0.3))
		_:
			# the handset, with its lead coming out of the bottom in the colour
			# of whichever lead this is
			var body := Rect2(m.position + Vector2(m.size.x * 0.24, 0),
				Vector2(m.size.x * 0.52, m.size.y * 0.74))
			draw_rect(body, Color("#23272c"))
			draw_rect(body.grow(-4.0), Color("#0d1116"))
			draw_rect(Rect2(m.position + Vector2(m.size.x * 0.44, m.size.y * 0.74),
				Vector2(m.size.x * 0.12, m.size.y * 0.26)), c)


func _draw() -> void:
	if not visible:
		return
	var vs := get_viewport_rect().size
	draw_rect(Rect2(Vector2.ZERO, vs), Color(0, 0, 0, 0.55))
	var p := _panel()
	draw_rect(p, Color("#181c21"))
	draw_rect(p, Color("#39414a"), false, 2.0)
	draw_string(_font, p.position + Vector2(26, 40), "INVENTORY",
		HORIZONTAL_ALIGNMENT_LEFT, -1, 22, Color("#e8eef4"))
	draw_string(_font, p.position + Vector2(26, 68),
		"drag a thing into a hand.  [Tab] or [Esc] closes.", HORIZONTAL_ALIGNMENT_LEFT, -1, 14,
		Color("#8d97a1"))

	var box := carrying_box()
	var kit := items()
	for i in range(kit.size()):
		var r := _kit_rect(i)
		draw_rect(r, Color("#22272d"))
		draw_rect(r, Color("#454e58"), false, 1.0)
		_icon(r, kit[i])
		draw_string(_font, r.position + Vector2(0, r.size.y + 16),
			str(KIT[kit[i]].label), HORIZONTAL_ALIGNMENT_LEFT, 160, 11,
			Color("#a9b3bd"))
	if box != "":
		draw_string(_font, _kit_rect(0).position + Vector2(0, 24),
			"your hands are full.", HORIZONTAL_ALIGNMENT_LEFT, -1, 15, Color("#d8a56a"))

	for side in [0, 1]:
		var r := _hand_rect(side)
		var h := hand(side)
		draw_rect(r, Color("#20252b"))
		draw_rect(r, Color("#5a656f"), false, 2.0)
		if h != "":
			_icon(r, h)
		draw_string(_font, r.position + Vector2(0, -10),
			"LEFT CLICK" if side == 0 else "RIGHT CLICK", HORIZONTAL_ALIGNMENT_CENTER,
			SLOT, 13, Color("#cbd4dd"))
		var what: String = box if h == "box" else (str(KIT[h].label) if h != "" else "empty")
		draw_string(_font, r.position + Vector2(-10, r.size.y + 18), what,
			HORIZONTAL_ALIGNMENT_CENTER, SLOT + 20, 12, Color("#a9b3bd"))
		if h != "" and h != "box":
			var hint: Array = KIT[h].hint
			for li in range(hint.size()):
				draw_string(_font, r.position + Vector2(-26, r.size.y + 36 + li * 13),
					str(hint[li]), HORIZONTAL_ALIGNMENT_CENTER, SLOT + 52, 10,
					Color("#77828c"))

	if msg != "":
		draw_string(_font, p.position + Vector2(26, p.size.y - 22), msg,
			HORIZONTAL_ALIGNMENT_LEFT, p.size.x - 52, 13, Color("#e0a45a"))

	if _drag != "":
		var d := Rect2(_at - Vector2(TILE, TILE) * 0.5, Vector2(TILE, TILE))
		draw_rect(d, Color(0, 0, 0, 0.4))
		_icon(d, _drag)


func _unhandled_key_input(e: InputEvent) -> void:
	if not (e is InputEventKey) or not e.pressed or e.is_echo():
		return
	if (e as InputEventKey).keycode != KEY_TAB:
		return
	# WHOEVER IS TYPING KEEPS IT. terminal.gd completes paths against the
	# machine's own `ls`, and that is a better use of the key than this is
	# while there is a prompt on the screen.
	if not visible and tower != null:
		if tower.has_method("desk_open") and tower.desk_open():
			return
		if tower.has_method("seat_open") and tower.seat_open():
			return
		if tower.phone != null and bool(tower.phone.focused):
			return
	toggle()
	get_viewport().set_input_as_handled()


func _input(e: InputEvent) -> void:
	if not visible:
		return
	if e is InputEventMouseMotion:
		_at = (e as InputEventMouseMotion).position
		if _drag != "":
			queue_redraw()
		return
	if not (e is InputEventMouseButton):
		return
	var mb: InputEventMouseButton = e
	if mb.button_index != MOUSE_BUTTON_LEFT:
		return
	_at = mb.position
	get_viewport().set_input_as_handled()
	if mb.pressed:
		msg = ""
		var kit := items()
		for i in range(kit.size()):
			if _kit_rect(i).has_point(_at):
				_drag = kit[i]
				_drag_from = -1
				queue_redraw()
				return
		for side in [0, 1]:
			if _hand_rect(side).has_point(_at):
				var h := hand(side)
				if h == "box":
					msg = "you are carrying %s in both hands. Put it down first  [G]." % carrying_box()
				elif h != "":
					_drag = h
					_drag_from = side
				queue_redraw()
				return
		return
	# released
	if _drag == "":
		return
	var item := _drag
	_drag = ""
	for side in [0, 1]:
		if _hand_rect(side).has_point(_at):
			msg = equip(item, side)
			queue_redraw()
			return
	# dropped back in the kit: that hand is empty now
	if _drag_from == 0: left = ""
	elif _drag_from == 1: right = ""
	queue_redraw()
