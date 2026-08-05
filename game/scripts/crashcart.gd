# crashcart.gd — the crash cart you carry, and plug into things.
#
# You have it on you: an inventory holds more than a real person, and a trolley
# shoved through doorways and into lifts is physics work that teaches nothing.
# It comes up in front of you when a lead is plugged in and goes away when it
# is not.
#
# This is `rcon connect` made physical. Today the player types an address at a
# desk; here they walk to the rack, pick a lead and plug it in, and the two
# leads are not the same thing:
#
#   SERIAL  goes to the console. It works on a machine that never finished
#           booting, which is the entire reason a serial lead exists: you get
#           the firmware, the boot log, and on a machine with no userland the
#           same "[no shell here ...]" block the socket console prints. Same
#           call, sh_on(), so the cart and the desktop terminal cannot
#           disagree about what a machine says.
#
#   HDMI    gets you the graphical desktop, and ONLY from a machine that has a
#           display output and finished booting. A rack server has no display
#           connector; plugging HDMI into it gets you nothing, and that is not
#           a failure of the cart, it is what the back of a rack server looks
#           like. A passive patch panel has no ports at all.
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

var _vp: SubViewport
var _screen: MeshInstance3D
var _serial: Control
var _de: Control = null
var _lines: PackedStringArray = PackedStringArray()
var _input := ""
var _label: Label
var _mono: Font


func _ready() -> void:
	_mono = preload("res://scripts/uifont.gd").mono()
	_body()
	_make_viewport()
	_show_serial()
	visible = false


# ------------------------------------------------------------------ the cart

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
	_mesh(Vector3(0.62, 0.06, 0.46), Vector3(0, 0.10, 0), Color("#4a4f57"))   # lower shelf
	_mesh(Vector3(0.62, 0.06, 0.46), Vector3(0, 0.78, 0), Color("#5a606a"))   # worktop
	for sx in [-0.26, 0.26]:
		for sz in [-0.18, 0.18]:
			_mesh(Vector3(0.05, 0.78, 0.05), Vector3(sx, 0.39, sz), Color("#31353b"))
	_mesh(Vector3(0.40, 0.02, 0.16), Vector3(0, 0.82, 0.12), Color("#d5d2c8"))  # keyboard
	_mesh(Vector3(0.56, 0.36, 0.03), Vector3(0, 1.05, -0.14), Color("#1b1e22")) # bezel
	# the two leads, coiled on the shelf
	_mesh(Vector3(0.14, 0.05, 0.14), Vector3(-0.18, 0.16, 0), Color("#1e6f3a"))  # serial
	_mesh(Vector3(0.14, 0.05, 0.14), Vector3(0.18, 0.16, 0), Color("#20304f"))   # hdmi


func _make_viewport() -> void:
	_vp = SubViewport.new()
	_vp.size = Vector2i(1024, 640)
	_vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	_vp.transparent_bg = false
	_vp.disable_3d = true
	add_child(_vp)

	_serial = ColorRect.new()
	_serial.color = Color("#0d1116")
	_serial.set_anchors_preset(Control.PRESET_FULL_RECT)
	_label = Label.new()
	_label.position = Vector2(10, 8)
	_label.size = Vector2(1004, 624)
	_label.add_theme_font_override("font", _mono)
	_label.add_theme_font_size_override("font_size", 15)
	_label.add_theme_color_override("font_color", Color("#c9d4dd"))
	_serial.add_child(_label)
	_vp.add_child(_serial)

	_screen = MeshInstance3D.new()
	var q := QuadMesh.new()
	q.size = Vector2(0.52, 0.325)
	_screen.mesh = q
	# A QuadMesh faces +Z, and +Z on the cart is the side you stand at. Turning
	# it round showed the back of the screen -- which is culled, so the picture
	# was a black rectangle that looked exactly like a monitor that is off.
	_screen.position = Vector3(0, 1.05, -0.122)
	var mat := StandardMaterial3D.new()
	mat.albedo_texture = _vp.get_texture()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR
	_screen.material_override = mat
	add_child(_screen)


# -------------------------------------------------------------------- plugging

func plug(dev: int, which_lead: String) -> String:
	var s := _plug(dev, which_lead)
	# The cart is only in your hands while a lead is in something.
	visible = plugged >= 0
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
		_show_desktop(d)
		status = "display on %s" % d.name
		return status
	status = "no such lead: %s" % which_lead
	return status


func unplug() -> void:
	plugged = -1
	lead = ""
	status = "unplugged"
	visible = false
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
	var out: String = str(tower.machine.sh_on(d.which, line))
	_say("# " + line)
	for l in out.split("\n"):
		_say(l)
	return out


func _say(s: String) -> void:
	_lines.append(s)
	while _lines.size() > 38:
		_lines.remove_at(0)
	if _label:
		_label.text = "\n".join(_lines)


func screen_text() -> String:
	if lead == "hdmi":
		return "[graphical display]" if _de != null else "[no signal]"
	return "\n".join(_lines)


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
