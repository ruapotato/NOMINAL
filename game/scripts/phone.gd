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

extends Node3D

var tower: Node3D = null
var with_desktop := true

var plugged := -1            # device index, -1 = nothing
var lead := ""               # "serial" | "hdmi"
var status := "unplugged"
var lit := false             # is there a lead in something

var _vp: SubViewport
var _screen: MeshInstance3D
var _serial: Control
var _de: Control = null
var _lines: PackedStringArray = PackedStringArray()
var _input := ""
var _label: Label
var _mono: Font
var _lead: Node3D = null
var _lead_col := Color("#1e6f3a")


func _ready() -> void:
	position = POSE_DOWN
	rotation = ROT_DOWN
	_mono = preload("res://scripts/uifont.gd").mono()
	_body()
	_make_viewport()
	_show_serial()
	_say("[no lead plugged in]")
	_relight()
	visible = false


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
const POSE_READ := Vector3(0.085, -0.045, -0.190)
const ROT_READ := Vector3(-0.10, 0.06, 0.0)


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
	# a speaker grille and a button, so the top and the bottom are not the same
	_mesh(Vector3(0.040, 0.004, 0.002), Vector3(0, CASE_H * 0.5 - 0.005, 0.0035),
		Color("#15181c"))
	_mesh(Vector3(0.016, 0.016, 0.002), Vector3(0, -CASE_H * 0.5 + 0.008, 0.0035),
		Color("#3a4046"))
	# the lead, out of the bottom corner and away towards whatever it is in
	_lead = Node3D.new()
	add_child(_lead)
	var g = preload("res://scripts/vgeo.gd").new()
	g.box(Vector3(-0.010, -CASE_H * 0.5 - 0.030, -0.008),
		Vector3(0.020, 0.032, 0.014), Color("#20242a"), false)
	for i in range(7):
		var t := float(i) / 6.0
		g.box(Vector3(-0.004 - t * 0.16, -CASE_H * 0.5 - 0.030 - t * 0.10,
				-0.006 - t * 0.30),
			Vector3(0.008, 0.008, 0.055), _lead_col, false)
	_lead.add_child(g.node("lead"))
	_lead.visible = false


func _make_viewport() -> void:
	_vp = SubViewport.new()
	# 900 x 562 into 186 x 116 mm at 190 mm: very nearly one texture pixel per
	# screen pixel at 1080p, which is the point of holding it up to your face.
	_vp.size = Vector2i(900, 562)
	_vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	_vp.transparent_bg = false
	_vp.disable_3d = true
	add_child(_vp)

	_serial = ColorRect.new()
	_serial.color = Color("#0d1116")
	_serial.set_anchors_preset(Control.PRESET_FULL_RECT)
	_label = Label.new()
	_label.position = Vector2(10, 8)
	_label.size = Vector2(884, 548)
	_label.add_theme_font_override("font", _mono)
	_label.add_theme_font_size_override("font_size", 15)
	_label.autowrap_mode = TextServer.AUTOWRAP_OFF
	_label.add_theme_color_override("font_color", Color("#c9d4dd"))
	_serial.add_child(_label)
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


# It comes up to your face when a lead goes in and drops back to your side when
# it comes out, because that is what a person does with a phone and because a
# handset parked in the middle of the view is a handset you cannot see past.
func _process(dt: float) -> void:
	var pw := POSE_READ if lit else POSE_DOWN
	var rw := ROT_READ if lit else ROT_DOWN
	var k: float = clampf(dt * 12.0, 0.0, 1.0)
	position = position.lerp(pw, k)
	rotation = rotation.lerp(rw, k)


# -------------------------------------------------------------------- plugging

func plug(dev: int, which_lead: String) -> String:
	var s := _plug(dev, which_lead)
	# The screen is only lit while a lead is in something. The handset itself
	# is in your hand whenever it is the equipped item -- see inventory.gd --
	# so what this switches is the picture and the lead hanging off it.
	lit = plugged >= 0
	if _lead:
		_lead.visible = lit
	_relight()
	return s


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
		_lines = PackedStringArray()
		_say("serial lead -> %s" % d.name)
		_say("")
		_say(_console_banner(d))
		status = "serial console on %s" % d.name
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
			return status
		plugged = dev
		lead = "hdmi"
		_lead_col = Color("#20304f")
		_show_desktop(d)
		status = "display on %s" % d.name
		return status
	status = "no such lead: %s" % which_lead
	return status


func unplug() -> void:
	plugged = -1
	lead = ""
	status = "unplugged"
	lit = false
	if _lead:
		_lead.visible = false
	_relight()
	_lines = PackedStringArray()
	_show_serial()
	_say("[no lead plugged in]")


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
	var d: Dictionary = tower.devices[plugged]
	# The managed boxes answer through site_cmd(), which is the same shell a
	# blind playtester drives over a pipe. One implementation, two front ends.
	var out: String = tower.site(line) if d.which == -2 \
		else str(tower.machine.sh_on(d.which, line))
	_say("# " + line)
	for l in out.split("\n"):
		_say(l)
	return out


func _say(s: String) -> void:
	_lines.append(s)
	while _lines.size() > 30:
		_lines.remove_at(0)
	if _label:
		_label.text = "\n".join(_lines)


func screen_text() -> String:
	if lead == "hdmi":
		return "[graphical display]" if _de != null else "[no signal]"
	return "\n".join(_lines)


# A handset with no lead in it is a handset with a dark screen. That is one
# fact, shown once, rather than a black rectangle that could equally be a
# rendering fault.
func _relight() -> void:
	if _serial == null:
		return
	if lit:
		_serial.color = Color("#0d1116")
	else:
		_serial.color = Color("#05070a")
	if _label:
		_label.add_theme_color_override("font_color",
			Color("#c9d4dd") if lit else Color("#3a444d"))


func _show_serial() -> void:
	if _de:
		_de.queue_free()
		_de = null
	if _serial:
		_serial.visible = true
		_serial.color = Color("#0d1116")


func _show_nosignal(_d: Dictionary) -> void:
	_show_serial()
	_lines = PackedStringArray()
	_say("")   # a monitor with nothing on it says nothing


func _show_desktop(_d: Dictionary) -> void:
	if not with_desktop:
		_show_serial()
		_lines = PackedStringArray()
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

func _unhandled_input(event: InputEvent) -> void:
	if plugged < 0 or lead != "serial":
		return
	if not (event is InputEventKey) or not event.pressed:
		return
	var k: InputEventKey = event
	if k.keycode == KEY_ENTER or k.keycode == KEY_KP_ENTER:
		var line := _input
		_input = ""
		if line.strip_edges() != "":
			type_line(line)
		get_viewport().set_input_as_handled()
	elif k.keycode == KEY_BACKSPACE:
		_input = _input.substr(0, max(0, _input.length() - 1))
		get_viewport().set_input_as_handled()
	elif k.unicode >= 32:
		_input += String.chr(k.unicode)
		get_viewport().set_input_as_handled()
	if _label:
		_label.text = "\n".join(_lines) + "\n# " + _input + "_"
