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


func _ready() -> void:
	position = POSE_DOWN
	rotation = ROT_DOWN
	_mono = preload("res://scripts/uifont.gd").mono()
	_body()
	_make_viewport()
	_show_serial()
	_say("[no lead plugged in]")
	_relight()


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
const POSE_DOWN := Vector3(0.30, -0.30, -0.46)
const ROT_DOWN := Vector3(-0.55, 0.30, 0.12)
const POSE_READ := Vector3(0.115, -0.070, -0.230)
const ROT_READ := Vector3(-0.10, 0.06, 0.0)
# AND UP IN FRONT OF YOUR FACE when you are actually typing on it: square to
# the eye, centred, and close enough that 900 pixels of screen are about 900
# pixels of window. This is the "zoom in on your debugger" the owner asked for,
# and it is a pose rather than a second camera, because it is still the handset
# in your hand in the room you are standing in.
const POSE_ZOOM := Vector3(0.0, -0.012, -0.152)
const ROT_ZOOM := Vector3(0.0, 0.0, 0.0)


func _mesh(size: Vector3, pos: Vector3, col: Color) -> void:
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


func _body() -> void:
	# the case, and a rubber bumper round it, because a thing carried round a
	# building in a pocket has a rubber bumper round it
	_mesh(Vector3(CASE_W, CASE_H, 0.013), Vector3(0, 0, -0.004), Color("#23272c"))
	_mesh(Vector3(CASE_W + 0.008, CASE_H + 0.008, 0.009), Vector3(0, 0, -0.006),
		Color("#c8792c"))
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
	mid.y = min(port.y, here.y) - port.distance_to(here) * 0.22
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
		_vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS if lit \
			else SubViewport.UPDATE_DISABLED
	if not lit:
		_drop_lead()
	_relight()
	# PLUGGING IN IS ENTERING A MODE, so the keyboard comes with it. A console
	# you cannot type at is a picture of a console. The display lead is not a
	# console -- there is nothing to type at -- so it hands the keyboard back.
	if lit and lead == "serial":
		take_keyboard()
	else:
		let_go()
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


func let_go() -> String:
	if not focused:
		return ""
	focused = false
	if tower and tower.player:
		tower.player.set_physics_process(true)
		tower.player.capture(true)
	if tower and tower.hud:
		tower.hud.visible = true
	if tower and tower.reticle:
		tower.reticle.visible = true
	_relabel()
	return "you lower the handset. The lead is still in %s." % _where()


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
		_hint.text = "connected to %s over %s   [Esc] put the handset down   [Tab] completes" \
			% [_where(), "the serial lead" if lead == "serial" else "the display lead"]
	else:
		_hint.text = "lead in %s   [F] pick the handset back up   [U] unplug" % _where()


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
		let_go()
	plugged = -1
	lead = ""
	status = "unplugged"
	lit = false
	if _vp:
		_vp.render_target_update_mode = SubViewport.UPDATE_DISABLED
	_drop_lead()
	_relight()
	_show_serial()
	if _term:
		_term.clear()
	_say("[no lead plugged in]")
	_relabel()


# Does this device actually paint a picture right now? The workstation is up
# by construction; the customer's machine is up only when it booted.
func _has_picture(d: Dictionary) -> bool:
	if tower == null or tower.machine == null:
		return false
	if d.which == 0:
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
	if not focused or _vp == null:
		return
	if not (event is InputEventKey):
		return
	if (event as InputEventKey).keycode == KEY_ESCAPE:
		return
	_vp.push_input(event, true)
	get_viewport().set_input_as_handled()
