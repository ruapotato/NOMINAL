# phone.gd — the mobile debugger. A phone, not a trolley.
#
# It was a crash cart: a metre of steel shelving that appeared in front of your
# face when a lead went in. The owner: "your mobile debugger which should look
# mostly like a phone that plugs into a server and gives you a 2D interface
# that you look at and can operate similar to the 2D interface that
# workstations should have. Sort of like your mobile workstation."
#
# So it is a handheld: a slab you hold up at reading distance in your right
# hand, with a screen that fills most of it and a short lead out of the bottom
# into whatever you are standing at. You equip it in the inventory and it is in
# your hand; it lights up when a lead is in something and goes dark when it is
# not.
#
# WHAT IT DOES IS UNCHANGED, and deliberately so. Everything below this line
# is the crash cart's model, because the model was the honest part:
#
# This is `rcon connect` made physical. Today the player types an address at a
# desk; here they walk to the rack, pick a lead and plug it in, and the two
# leads are not the same thing:
#
#   SERIAL  goes to the console. It works on a machine that never finished
#           booting, which is the entire reason a serial lead exists: you get
#           the firmware, the boot log, and on a machine with no userland the
#           same "[no shell here ...]" block the socket console prints. Same
#           call, sh_on(), so the phone and the desktop terminal cannot
#           disagree about what a machine says.
#
#   DISPLAY gets you the graphical desktop, and ONLY from a machine that has a
#           display output and finished booting. A rack server has no display
#           connector; plugging the display lead into it gets you nothing, and
#           that is not a failure of the phone, it is what the back of a rack
#           server looks like. A passive patch panel has no ports at all.
#
# The failure cases matter as much as the success case: each one is a true
# fact about the hardware, learned by trying it, which is the only kind of
# teaching this project does.

# PLUGGING IN IS A MODE. The owner played it: "when you select a server with a
# debugger, it does show a terminal, but you can\'t actually input into it. It
# should kind of zoom in on your debugger, open the terminal, show a connection
# to the server. Actually act as an interface to tell you how to escape."
#
# So a serial lead going in does three things: the handset comes up to reading
# distance in the middle of the view, the terminal on it takes the keyboard,
# and the screen says what it is connected to and that [Esc] lets go. What you
# type goes to machine.sh_on() -- the same call the socket console makes and
# the same one game/tests/console_speaks.gd gates -- through terminal.gd, which
# is the real terminal, with real path completion on Tab.

extends Node3D

var tower: Node3D = null
var with_desktop := true

var plugged := -1            # device index, -1 = nothing
var lead := ""               # "serial" | "hdmi"
var status := "unplugged"
var lit := false             # is there a lead in something
var focused := false         # the keyboard is on the handset

var _vp: SubViewport
var _screen: MeshInstance3D
var _serial: Control
var _de: Control = null
var _term: Control = null
var _mono: Font
var _lead: MeshInstance3D = null
var _lead_col := Color("#1e6f3a")
var _prompt := ""
var _hint: Label = null
var _idle: ColorRect = null      # the standby screen, while no lead is in anything
var _hand: Array = []            # the thumb, visible only at the down pose
var _repaint := 0                # frames of viewport left to draw while dark


func _ready() -> void:
	position = POSE_DOWN
	rotation = ROT_DOWN
	_mono = preload("res://scripts/uifont.gd").mono()
	_body()
	_make_viewport()
	_show_serial()
	_say("[no lead plugged in]")
	_relight()
	# A FEW FRAMES, SO THE SCREEN IS NOT BLANK. A viewport that has never
	# rendered is a black texture, and a black texture on a handset in your hand
	# is the black rectangle nobody could name. It cannot be one frame: the
	# Controls on it have not had a layout pass yet when _ready returns, so the
	# single render came back empty. Three frames, then it stops for good and
	# the standby screen stays on the glass for the rest of the run.
	_repaint = 3


# ----------------------------------------------------------------- the handset
#
# The whole thing is 190 x 120 mm of case with a 176 x 110 mm screen in it, and
# it hangs off the camera at 380 mm -- reading distance for something you are
# holding up. That is what makes the 2D on it legible: a small screen you look
# at closely, rather than a monitor across a room.

const CASE_W := 0.200
const CASE_H := 0.130
const SCREEN_W := 0.186
const SCREEN_H := 0.116

# Where it sits relative to your eye. Down at your side when there is no lead
# in anything, and up at reading distance when there is -- which is the whole
# reason the 2D on it is legible: 186 mm of screen at 190 mm from your eye is
# two fifths of the width of the view, not a postage stamp across a room.
#
# DOWN MEANS DOWN, AND STILL HELD. It hung at (0.30, -0.33, -0.47) rolled a
# fifth of a radian, which put a black rectangle with an orange edge into the
# bottom right corner of every screenshot -- clipped by two screen edges, at
# about twenty degrees, with the words on the glass running off the side of it.
# The owner, twice, on his own playtests: "an orange-outlined black panel
# floating at an odd angle at the bottom right that I cannot identify", and
# then "a picture frame falling over rather than a device somebody is holding".
#
# Three things were wrong and none of them was the brightness:
#
#   THE ROLL. A rectangle tipped off the horizontal reads as a thing that has
#   fallen. There is no roll now: the case is level with the bottom of the
#   view, and so is the writing on the glass, which is the whole of the "the
#   label should be level with the screen" note.
#
#   THE CLIP. Two edges cut it, so you never saw an outline, and an object
#   with no silhouette cannot be named. The whole handset is in frame now,
#   sitting on the bottom edge with its own margin, tipped back the way a
#   thing held at your waist is tipped: you look down the face of it.
#
#   THE DEPTH. It was a bezel and a pane -- literally a picture frame. The
#   case has a back, shoulders and side rails now (see _body()), so the near
#   edge shows its thickness and it reads as something with a weight in it.
const POSE_DOWN := Vector3(0.208, -0.222, -0.430)
const ROT_DOWN := Vector3(-0.40, 0.13, 0.0)
# AND THE ONE IN BETWEEN IS A POSE AND NOT A PARKING SPACE. Held up with a
# lead in, it was at (0.115, -0.070, -0.230) turned three degrees: a rectangle
# almost parallel to the glass of the screen, sitting off to one side. A
# playtester: "still rolls with the yaw and is squarer to the eye than it
# should be. Legible, just not designed."
#
# Both halves of that are the same fault. A rectangle whose face is parallel
# to the view but whose CENTRE is off to the right does not project as a
# rectangle -- the perspective shears it, and a sheared rectangle reads as a
# thing that has rolled rather than a thing that is held. Three degrees of
# yaw is not enough to say "somebody has turned this in their hand" and is
# quite enough to shear it.
#
# So the pose commits: turned nine degrees so the far edge really is farther
# away and the case shows its own left side, pitched eleven so you are looking
# DOWN the face of it the way you look down a phone, and rolled three -- the
# cant a right wrist puts on a slab it is holding from the low corner. The
# shear is still there, because perspective, and now it agrees with the pose
# instead of arguing with it.
#
# IT DID NOT MOVE MUCH, and that is deliberate: 186 mm of screen at reading
# distance is the reason the console on it is readable at all, and an angle
# steep enough to be dramatic is an angle that foreshortens the text. The
# check is that every line of the console is still readable in a screenshot.
const POSE_READ := Vector3(0.124, -0.082, -0.238)
const ROT_READ := Vector3(-0.19, 0.16, -0.055)
# AND UP IN FRONT OF YOUR FACE when you are actually typing on it: square to
# the eye, centred, and close enough that 900 pixels of screen are about 900
# pixels of window. This is the "zoom in on your debugger" the owner asked for,
# and it is a pose rather than a second camera, because it is still the handset
# in your hand in the room you are standing in.
const POSE_ZOOM := Vector3(0.0, -0.012, -0.152)
const ROT_ZOOM := Vector3(0.0, 0.0, 0.0)


func _mesh(size: Vector3, pos: Vector3, col: Color) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = size
	mi.mesh = bm
	mi.position = pos
	var mat := StandardMaterial3D.new()
	mat.albedo_color = col
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mi.material_override = mat
	add_child(mi)
	return mi


func _body() -> void:
	# THE SHELL, AND IT HAS A BACK TO IT. It was a 13 mm slab with a 9 mm
	# orange plate hiding behind it, which from the front is a bezel and a pane
	# of glass -- a picture frame, which is what the owner called it. A handset
	# is 26 mm of case with a rubber bumper wrapped round the SIDES of it, so
	# that is what this is: the case stands proud of the bumper front and back,
	# the bumper stands proud of the case left, right, top and bottom, and the
	# near edge of the thing shows its own thickness at any angle but square-on.
	_mesh(Vector3(CASE_W + 0.008, CASE_H + 0.008, 0.020), Vector3(0, 0, -0.010),
		Color("#a3601f"))
	_mesh(Vector3(CASE_W, CASE_H, 0.027), Vector3(0, 0, -0.011), Color("#23272c"))
	# and the grips: a rubber rail down each side, which is the part of a
	# handheld tool your fingers are actually on
	for sx in [-1.0, 1.0]:
		_mesh(Vector3(0.007, CASE_H * 0.52, 0.031),
			Vector3(sx * (CASE_W * 0.5 + 0.005), 0, -0.012), Color("#191c20"))
	# AND A HAND ON IT. The last thing anybody said about this object was that
	# it looks like a device rather than a device somebody is holding, and the
	# difference between the two in a first-person view is a thumb: it comes
	# round the near corner and lies across the bumper, in front of the glass,
	# which is the one silhouette nobody mistakes for a picture frame.
	#
	# ONLY WHILE IT IS AT YOUR SIDE. Up at reading distance the case is three
	# times the size it is down here and the same thumb is a slab across a
	# third of the view; at the zoom pose the glass IS the view and every
	# millimetre of it is console, so a thumb over the left margin is a thumb
	# over the first character of every line. It goes when the lead goes in --
	# which is also when the screen stops needing to say what the thing is.
	# See _process().
	_hand = [
		_mesh(Vector3(0.026, 0.056, 0.020),
			Vector3(-CASE_W * 0.5 + 0.003, -CASE_H * 0.5 + 0.022, 0.004),
			Color("#b07a55")),
		_mesh(Vector3(0.040, 0.030, 0.026),
			Vector3(-CASE_W * 0.5 + 0.001, -CASE_H * 0.5 - 0.006, -0.004),
			Color("#9c6a48")),
	]
	# A speaker grille and a button, so the top and the bottom of the case are
	# not the same. BEHIND THE SCREEN PLANE, not level with it: at 0.0035 they
	# sit exactly where the screen quad is and win the depth test against it,
	# and once the handset is up at your face that is two grey blocks parked
	# over the middle of the first and last lines of the console.
	_mesh(Vector3(0.040, 0.004, 0.002), Vector3(0, CASE_H * 0.5 - 0.004, 0.0026),
		Color("#15181c"))
	_mesh(Vector3(0.016, 0.016, 0.002), Vector3(0, -CASE_H * 0.5 + 0.007, 0.0026),
		Color("#3a4046"))
	# the plug on the bottom of the case. THE LEAD ITSELF IS NOT DRAWN HERE.
	#
	# It used to be: seven little green boxes stepping away from the corner of
	# the handset, in the handset\'s own space, going nowhere. The owner saw
	# exactly what that is -- "when I use a debugger on a server it shows a
	# bunch of green lines that get towards the left" -- and he was right: they
	# are not a cable, they are a staircase of blocks hanging in front of the
	# camera, and they do not touch the thing the lead is supposedly in.
	#
	# A lead goes from the handset to the socket it is in. That is a length of
	# cable between two points in the ROOM, so it is drawn in the room -- see
	# _draw_lead() -- with the same sweep and sag every other cable in the
	# building gets.
	var g = preload("res://scripts/vgeo.gd").new()
	# BEHIND THE SCREEN PLANE, for the same reason as the grille: the screen is
	# at z 0.0035 and a plug that stands proud of it is a block parked over the
	# line that tells you how to put the handset down.
	g.box(Vector3(-0.010, -CASE_H * 0.5 - 0.026, -0.016),
		Vector3(0.020, 0.028, 0.014), Color("#20242a"), false)
	add_child(g.node("plug"))


func _make_viewport() -> void:
	_vp = SubViewport.new()
	# 900 x 562 into 186 x 116 mm at 152 mm: a shade over one texture pixel per
	# screen pixel once it is up at your face, which is the whole reason the 2D
	# on it is legible rather than a postage stamp across a room.
	_vp.size = Vector2i(900, 562)
	# ONLY WHILE THERE IS SOMETHING ON IT. A viewport set to UPDATE_ALWAYS
	# re-renders 900 x 562 every frame whether or not anybody can see it, and a
	# handset hanging dark at your side is a handset nobody can see.
	_vp.render_target_update_mode = SubViewport.UPDATE_DISABLED
	_vp.transparent_bg = false
	_vp.disable_3d = true
	# The keyboard goes THROUGH here: the terminal below is a real Control with
	# real focus, and _unhandled_input pushes keys into this viewport when the
	# handset has the keyboard. Without local handling it has no focus stack of
	# its own and every key lands nowhere.
	_vp.handle_input_locally = true
	_vp.gui_disable_input = false
	add_child(_vp)

	_serial = ColorRect.new()
	_serial.color = Color("#0d1116")
	_serial.set_anchors_preset(Control.PRESET_FULL_RECT)
	# THE REAL TERMINAL, not a Label with a string in it. terminal.gd is the
	# one the desktop opens: same drawing, same history, same Tab completion
	# against the machine\'s own `ls`. A second, worse console on the handset
	# would be a second thing to keep true.
	_term = preload("res://scripts/terminal.gd").new()
	_term.mono = _mono
	_term.bg = Color("#0d1116")
	_term.fg = Color("#c9d4dd")
	_term.accent = Color("#6fdc96")
	_term.position = Vector2(0, 24)
	_term.size = Vector2(_vp.size) - Vector2(0, 30)
	_term.on_command = func(line: String) -> String:
		return _cmd(line)
	_term.prompt_fn = func() -> String:
		return _prompt
	_serial.add_child(_term)
	# The strip along the bottom that says what this is and how to put it down.
	# "Actually act as an interface to tell you how to escape."
	_hint = Label.new()
	_hint.add_theme_font_override("font", _mono)
	_hint.add_theme_font_size_override("font_size", 14)
	_hint.add_theme_color_override("font_color", Color("#8fa4b6"))
	# ALONG THE TOP, like a title bar. It was along the bottom, and the bottom
	# of the view at the zoom pose is where whatever you are kneeling in front
	# of pokes past the case: the line that tells you how to get out was the one
	# line reliably half covered.
	_hint.position = Vector2(10, 2.0)
	_hint.size = Vector2(float(_vp.size.x) - 20.0, 20.0)
	_serial.add_child(_hint)
	# THE STANDBY SCREEN, and it exists because of a screenshot. With no lead in
	# anything the glass showed a console with one grey line in the corner of it,
	# which at arm's length is a black rectangle: "an orange-outlined black panel
	# floating at an odd angle that I cannot identify." A thing you are holding
	# should say what it is from across the room, so it does -- big enough to
	# read at the size the handset actually occupies when it is down at your side.
	_idle = ColorRect.new()
	_idle.color = Color("#101820")
	_idle.set_anchors_preset(Control.PRESET_FULL_RECT)
	var big := Label.new()
	big.add_theme_font_override("font", _mono)
	big.add_theme_font_size_override("font_size", 92)
	big.add_theme_color_override("font_color", Color("#6fdc96"))
	big.text = "DEBUGGER"
	big.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	big.set_anchors_preset(Control.PRESET_FULL_RECT)
	big.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	big.offset_bottom = -70.0
	_idle.add_child(big)
	var sub := Label.new()
	sub.add_theme_font_override("font", _mono)
	sub.add_theme_font_size_override("font_size", 44)
	sub.add_theme_color_override("font_color", Color("#7e93a6"))
	sub.text = "no lead plugged in"
	sub.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	sub.set_anchors_preset(Control.PRESET_FULL_RECT)
	sub.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	sub.offset_top = 90.0
	_idle.add_child(sub)
	# AND WHAT IT IS FOR, which is the half of "what is that object" that a
	# name does not answer. A thing in your hand that says what it does when
	# you point it at something is a tool; a thing with a word on it is a sign.
	var how := Label.new()
	how.add_theme_font_override("font", _mono)
	how.add_theme_font_size_override("font_size", 32)
	how.add_theme_color_override("font_color", Color("#4f6272"))
	# [H] WAS THE DISPLAY LEAD AND IT IS GONE -- see the note on KEY_H in
	# tower.gd. A screenshot of the handset caught this still offering it, in
	# the corner of every picture of the MDF, months after the key stopped
	# doing anything. Point at the CONSOLE SOCKET now: the debugger is a
	# serial lead and the wide low socket is the only thing it goes into.
	how.text = "point at a console socket -- [F] the lead in"
	how.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	how.set_anchors_preset(Control.PRESET_FULL_RECT)
	how.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	how.offset_top = 200.0
	_idle.add_child(how)
	_serial.add_child(_idle)
	_vp.add_child(_serial)

	_screen = MeshInstance3D.new()
	var q := QuadMesh.new()
	q.size = Vector2(SCREEN_W, SCREEN_H)
	_screen.mesh = q
	# A QuadMesh faces +Z, and +Z on the handset is the side you are looking at.
	# Turning it round showed the back of the screen -- which is culled, so the
	# picture was a black rectangle that looked exactly like a phone that is off.
	_screen.position = Vector3(0, 0, 0.0035)
	var mat := StandardMaterial3D.new()
	mat.albedo_texture = _vp.get_texture()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR
	_screen.material_override = mat
	add_child(_screen)


# ------------------------------------------------------------------ the lead
#
# From the socket it is in, to the bottom of the handset in your hand. It is
# drawn in the WORLD, not on the camera, because that is where both of its ends
# are -- and it gets the same catenary and the same sweep as every other length
# of copper in the building, because it is one.
func _draw_lead() -> void:
	if _lead:
		_lead.queue_free()
		_lead = null
	if tower == null or plugged < 0 or plugged >= tower.devices.size():
		return
	var d: Dictionary = tower.devices[plugged]
	var port: Vector3 = tower._port_point(d, 0)
	var face: Vector3 = d.face
	var here: Vector3 = global_transform * Vector3(0.0, -CASE_H * 0.5 - 0.030, 0.0)
	var g = preload("res://scripts/vgeo.gd").new()
	var mid := (port + face * 0.08 + here) * 0.5
	# THE SAG IS A SAG, NOT A DIVE. It was `distance * 0.22`, which is fine at
	# the arm's length a lead is normally at and is a metre and a half below the
	# slab once you have walked six metres off with the handset still in your
	# hand -- the owner: *"when you connect a debugger to something and then
	# walk away, you can see that the cable clips below the floor."* A hanging
	# cable does not sag further the longer the room is; it goes taut. So the
	# belly is capped, and then floored at the slab both ends are standing on,
	# because copper lies ON a floor and does not go through it.
	var low: float = min(port.y, here.y)
	mid.y = low - min(port.distance_to(here) * 0.22, 0.30)
	var fh: float = float(tower.fheight) if tower.fheight > 0.0 else 3.0
	mid.y = max(mid.y, floor(low / fh) * fh + 0.02)
	tower._run_cable(g, [port, port + face * 0.05, mid, here], _lead_col, 2)
	if g.empty():
		return
	_lead = MeshInstance3D.new()
	_lead.name = "DebuggerLead"
	_lead.mesh = g.mesh()
	tower.add_child(_lead)


# It comes up to your face when a lead goes in, all the way up in front of you
# when you are typing on it, and back down to your side when the lead comes
# out -- because that is what a person does with a phone, and because a handset
# parked in the middle of the view is a handset you cannot see past.
func _process(dt: float) -> void:
	var pw := POSE_DOWN
	var rw := ROT_DOWN
	if focused:
		pw = POSE_ZOOM
		rw = ROT_ZOOM
	elif lit:
		pw = POSE_READ
		rw = ROT_READ
	var k: float = clampf(dt * 12.0, 0.0, 1.0)
	position = position.lerp(pw, k)
	rotation = rotation.lerp(rw, k)
	for h in _hand:
		if is_instance_valid(h):
			h.visible = not lit and not focused
	if _repaint > 0 and not lit and _vp:
		_repaint -= 1
		_vp.render_target_update_mode = SubViewport.UPDATE_ONCE
	if lit:
		_draw_lead()


# -------------------------------------------------------------------- plugging

func plug(dev: int, which_lead: String) -> String:
	var s := _plug(dev, which_lead)
	# The screen is only lit while a lead is in something. The handset itself
	# is in your hand whenever it is the equipped item -- see inventory.gd --
	# so what this switches is the picture and the lead hanging off it.
	lit = plugged >= 0
	if _vp:
		# ... and a few more frames on the way down, so the screen the lead left
		# behind is the standby screen rather than whatever was last on it.
		_vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS if lit \
			else SubViewport.UPDATE_DISABLED
		if not lit:
			_repaint = 3
	if not lit:
		_drop_lead()
	_relight()
	# PLUGGING IN IS ENTERING A MODE, so the keyboard comes with it. A console
	# you cannot type at is a picture of a console. The display lead is not a
	# console -- there is nothing to type at -- so it hands the keyboard back.
	if lit and lead == "serial":
		take_keyboard()
	else:
		_lower()
	return s


# ---------------------------------------------------------------- the mode
#
# The handset has the keyboard, the world does not, and the screen says both
# what it is on and how to get out. Esc is the way out -- of this, of the
# desktop, and of the bag -- and tower.gd routes it.

func take_keyboard() -> void:
	if focused or plugged < 0 or lead != "serial":
		return
	focused = true
	if tower and tower.player:
		tower.player.capture(false)
		tower.player.velocity = Vector3.ZERO
		tower.player.set_physics_process(false)
	if tower and tower.hud:
		tower.hud.visible = false
	if tower and tower.reticle:
		tower.reticle.visible = false
	if _term:
		_term.call_deferred("take_focus")
	_relabel()


# PUTTING IT DOWN IS TAKING THE LEAD OUT. The owner, three times over one
# session: *"When you press esc, while connected to a server, the debugger
# cable should disconnect"*, *"the debugger stays attached until you hit right
# click"*, and -- looking at the copper still hanging off a box he had walked
# away from -- *"when you hit escape on a debugger, it should disconnect."*
#
# It lowered the handset and left the lead in, which is a state with no way out
# on the keyboard at all: the world had the keys back, the HUD still said
# "serial console on core", and [U] is not a key anybody guesses. Escape is the
# key this project uses for "I am done with this thing" everywhere else, and a
# lead in your hand is a thing you are done with.
#
# AND IT GOES THROUGH THE SESSION. `plugged <dev> <hdmi>` in ses_state() is
# what tower.gd's _reconcile_phone() makes the prop agree with, so a handset
# that unplugged itself while core still believed a lead was in would be the
# exact disagreement reconcile was built to end. `unplug` is core's own verb
# for this and it is the one that runs.
func let_go() -> String:
	if not focused:
		return ""
	_lower()
	return detach()


# The pose half on its own: the handset comes down, the world gets the keys
# back. Used when a display lead goes in (there is nothing to type at, so the
# keyboard goes back, and the lead stays in) and by unplug() itself.
func _lower() -> void:
	if not focused:
		return
	focused = false
	if tower and tower.player:
		tower.player.set_physics_process(true)
		tower.player.capture(true)
	if tower and tower.hud:
		tower.hud.visible = true
	if tower and tower.reticle:
		tower.reticle.visible = true
	_relabel()


# Take the lead out, saying so to core FIRST. If the session has a lead in
# something, `unplug` is what ends it -- the same word a blind playtester types
# down the wire -- and the prop follows. If the session has nothing plugged
# (the lead went in with [F], which is a key this window has and the session
# does not know about) there is nothing to tell it and only the prop to clear.
func detach() -> String:
	var was := _where()
	if plugged < 0:
		return ""
	if tower and tower.has_method("site") and tower.has_method("ses_state"):
		var pl: Variant = tower.ses_state().get("plugged", -1)
		var sdev := -1
		if pl is Array and (pl as Array).size() >= 1:
			sdev = int((pl as Array)[0])
		else:
			sdev = int(pl)
		if sdev >= 0:
			tower.site("unplug")
	unplug()
	return "you take the lead out of %s." % was


func _where() -> String:
	if tower == null or plugged < 0 or plugged >= tower.devices.size():
		return "nothing"
	return str(tower.devices[plugged].name)


# The line along the bottom of the screen: what this is connected to, and the
# key that gets you out of it. Both facts, on the screen they are about.
func _relabel() -> void:
	if _hint == null:
		return
	if plugged < 0:
		_hint.text = "no lead in anything"
		return
	if focused:
		_hint.text = "connected to %s over %s   [Esc] take the lead out   [Tab] completes" \
			% [_where(), "the serial lead" if lead == "serial" else "the display lead"]
	else:
		_hint.text = "lead in %s   [F] pick the handset back up   [Esc] take the lead out" % _where()


func _drop_lead() -> void:
	if _lead:
		_lead.queue_free()
		_lead = null


func _plug(dev: int, which_lead: String) -> String:
	if tower == null or dev < 0 or dev >= tower.devices.size():
		status = "there is nothing there to plug into."
		return status
	var d: Dictionary = tower.devices[dev]
	if which_lead == "serial":
		if not d.serial:
			# The truth about a patch panel: it is copper and a label. There
			# is nothing running in it to have a console.
			status = "%s: no console port. It is passive -- there is nothing in it running." % d.name
			return status
		plugged = dev
		lead = "serial"
		_lead_col = Color("#1e6f3a")
		_show_serial()
		if _term:
			_term.clear()
		_prompt = _prompt_for(d)
		_say("serial lead -> %s" % d.name)
		_say("")
		_say(_console_banner(d))
		_say("")
		status = "serial console on %s" % d.name
		_relabel()
		return status
	if which_lead == "hdmi":
		if not d.hdmi:
			status = "%s: no display output on the back of it. Use the serial lead." % d.name
			return status
		if not _has_picture(d):
			# A machine that has not finished booting is not painting a
			# screen yet, so an honest monitor shows nothing at all.
			status = "%s: no signal. It has a display output and it is not driving it." % d.name
			plugged = dev
			lead = "hdmi"
			_show_nosignal(d)
			_relabel()
			return status
		plugged = dev
		lead = "hdmi"
		_lead_col = Color("#20304f")
		_show_desktop(d)
		status = "display on %s" % d.name
		_relabel()
		return status
	status = "no such lead: %s" % which_lead
	return status


# WHAT THE PROMPT SAYS IS WHAT THE LINE IS. A managed switch answers on its
# management line, a machine answers as root on its console, and the prompt is
# never empty -- terminal.gd swallows keystrokes at an empty prompt, which is
# how a console once ate five commands in a row and said nothing.
func _prompt_for(d: Dictionary) -> String:
	if int(d.which) == -2:
		return "%s> " % str(d.name)
	return "root@%s# " % str(d.name).replace(" ", "-")


func unplug() -> void:
	if focused:
		_lower()
	plugged = -1
	lead = ""
	status = "unplugged"
	lit = false
	if _vp:
		_vp.render_target_update_mode = SubViewport.UPDATE_DISABLED
		# AND A FEW FRAMES ON THE WAY DOWN, the same three plug() takes when it
		# goes dark. Without them the viewport stops updating on the frame the
		# lead comes out and the glass keeps the console that was on it: after
		# [Esc] the handset hung at your side still showing core's port table,
		# which says a lead is in when there is not one.
		_repaint = 3
	_drop_lead()
	_relight()
	_show_serial()
	if _term:
		_term.clear()
	_say("[no lead plugged in]")
	_relabel()
	# AND THE RECONCILER IS TOLD THE LEAD IS OUT.
	#
	# tower.gd's _reconcile_phone() remembers the device and lead it last made
	# the prop agree with, and returns early when the session still names the
	# same pair -- which is right, because plugging the same box in twice is
	# not an event. Its `dev < 0` branch calls this function and does NOT clear
	# that memory, so after ANY unplug the pair it remembers is a lead that is
	# no longer in anything: `plug core` / `unplug` / `plug core` down the
	# socket leaves the handset dark while core believes a lead is in. That is
	# the exact disagreement reconcile exists to prevent, and it is reachable
	# without this file -- but Escape now takes that path every single time, so
	# it is cleared from here rather than left for the next player to find.
	#
	# THE REAL FIX IS TWO LINES IN tower.gd's `if dev < 0:` branch and it is
	# reported; this comes out when it lands.
	if tower != null and "_phone_dev" in tower:
		tower._phone_dev = -1
		tower._phone_lead = ""


# Does this device actually paint a picture right now? The workstation paints
# one while the site says it has power; the customer's machine only when it
# booted.
func _has_picture(d: Dictionary) -> bool:
	if tower == null or tower.machine == null:
		return false
	if d.which == 0:
		# THE PLAYER'S OWN WORKSTATION, and since D41 it is a box in a room
		# with a plug in the wall rather than a machine that is up by
		# construction. A display lead into a computer with no power in it
		# shows what a display lead into any dead computer shows.
		if tower.has_method("_ws_live"):
			return bool(tower._ws_live())
		return true
	if d.which == 1:
		return bool(tower.machine.booted())
	return false


func _console_banner(d: Dictionary) -> String:
	var m: Object = tower.machine
	if d.which == -2:
		# A MANAGED BOX. A switch and a router are not machines with a disk in
		# this game; they are entries in the site model with real ports, a real
		# forwarding table and real frames going through them. So the lead gets
		# you the management line, and what it prints is that box's own state
		# out of core/netstack.c -- ports, addresses, routes, the MAC table --
		# not a paragraph somebody wrote about switches.
		var s: String = tower.site("show " + d.name)
		return ("management line on %s\n\n" % d.name) + s + \
			"\nthis line takes one operation at a time. `help` lists them."
	if d.which == 1:
		# The REAL boot log off the real boot chain. It is a pure function of
		# what is on that disk, so reading it costs nothing and cannot lie.
		var log: String = str(m.boot())
		if not m.booted():
			log += "\n[boot stopped at %s: %s]" % [str(m.boot_stage()), str(m.boot_reason())]
		return log
	var host: String = str(m.sh_on(0, "cat /etc/hostname")).strip_edges()
	return "console on %s\nlogin as root -- already signed in on this line" % host


func type_line(line: String) -> String:
	if plugged < 0 or lead != "serial":
		return ""
	if _term:
		_term.write(_prompt + line)
	var out := _cmd(line)
	if _term:
		_term.write(out)
	return out


# ONE MACHINE, TWO FRONT ENDS. sh_on() is the same call the socket console
# makes and the same one game/tests/console_speaks.gd gates: a box that never
# finished booting refuses in words here exactly as it does there, because it
# is the same words from the same function. The managed boxes answer through
# site_cmd(), which is the shell a blind playtester drives over a pipe.
func _cmd(line: String) -> String:
	if plugged < 0 or plugged >= tower.devices.size():
		return ""
	var d: Dictionary = tower.devices[plugged]
	if int(d.which) == -2:
		return tower.site(line)
	return str(tower.machine.sh_on(int(d.which), line))


func _say(s: String) -> void:
	if _term:
		_term.write(s + "\n")


func screen_text() -> String:
	if lead == "hdmi":
		return "[graphical display]" if _de != null else "[no signal]"
	if _term == null:
		return ""
	return "\n".join(_term.lines)


# A handset with no lead in it is a handset with a dark screen. That is one
# fact, shown once, rather than a black rectangle that could equally be a
# rendering fault.
func _relight() -> void:
	if _serial == null:
		return
	_serial.color = Color("#0d1116") if lit else Color("#05070a")
	if _term:
		_term.bg = Color("#0d1116") if lit else Color("#05070a")
		_term.fg = Color("#c9d4dd") if lit else Color("#3a444d")
		_term.queue_redraw()
	# The standby screen is the whole screen when there is no lead in anything,
	# and gone the moment there is one: what is on the glass then is the console.
	if _idle:
		_idle.visible = not lit


func _show_serial() -> void:
	if _de:
		_de.queue_free()
		_de = null
	if _serial:
		_serial.visible = true
		_serial.color = Color("#0d1116")


func _show_nosignal(_d: Dictionary) -> void:
	_show_serial()
	if _term:
		_term.clear()
	_say("")   # a monitor with nothing on it says nothing


func _show_desktop(_d: Dictionary) -> void:
	if not with_desktop:
		_show_serial()
		if _term:
			_term.clear()
		_say("[display attached -- desktop not built in this run]")
		return
	if _de != null:
		return
	_serial.visible = false
	# The SAME desktop the 2D game runs, on the SAME machine object the serial
	# lead talks to. Two front ends onto one Station, which is the rule.
	_de = preload("res://scripts/de.gd").new()
	_de.machine = tower.machine
	# _new_ticket() increments before it installs, so this lands on exactly the
	# ticket already in the rack: the desktop must not silently swap the
	# machine the serial lead is looking at.
	_de.seed_no = tower.seed_no - 1
	_de.set_anchors_preset(Control.PRESET_FULL_RECT)
	_de.size = Vector2(_vp.size)
	_vp.add_child(_de)


# ---------------------------------------------------------------- keyboard
#
# While the handset has the keyboard, every key goes into its viewport and the
# world sees none of them: you are typing at a shell, and `w` is a letter.
# Escape is the exception and tower.gd takes it first -- see its _input().
func _unhandled_input(event: InputEvent) -> void:
	if not (event is InputEventKey):
		return
	var k: int = (event as InputEventKey).keycode
	# ESCAPE WITH THE HANDSET ALREADY DOWN. tower.gd's _input() takes Escape
	# while the handset has the keyboard and calls let_go(); with the keyboard
	# already back in the world it falls through to here, and it has to mean
	# the same thing in both places. A display lead never takes the keyboard at
	# all, so this is the ONLY way Escape reaches a display lead -- and a lead
	# left in was the state the owner could not get out of: *"the debugger
	# stays attached until you hit right click."*
	if k == KEY_ESCAPE:
		if focused:
			return                  # tower.gd's _input has already had it
		if not event.pressed or event.is_echo() or plugged < 0:
			return
		var said := detach()
		if said != "":
			print(said)
		get_viewport().set_input_as_handled()
		return
	if not focused or _vp == null:
		return
	_vp.push_input(event, true)
	get_viewport().set_input_as_handled()
