# chat.gd — three people, one window.
#
# David: "The chat app needs to be a real chat app with the ability to talk to
# not just the customer, but a coworker and potentially a manager so you can
# ask for advice... It should resemble Pidgin."
#
# So: a contact list down the left, a conversation on the right, and a line you
# type into at the bottom. Each contact keeps its own transcript, because they
# are separate conversations with separate people who know separate things:
#
#   the customer  what a non-technical person sitting at the machine can see.
#                 Their name and manner are drawn per ticket and stay bound to
#                 each other, so the same name is always the same person.
#   Ben           a technician at the next desk. He has NOT seen this machine
#                 and knows only what you tell him.
#   Json       the engineer who wrote the runbook. Knows the architecture
#                 of the whole system and nothing about your particular fault.
#
# All three are the same 3B model with different briefs. The difference
# between them is entirely in what they have been told, which is the point.

extends Control

var mono: Font
var machine: Object = null
var ink := Color("#c9d3e0")
var dim := Color("#78849a")
var bg := Color("#1b212c")

const NAMES := ["customer", "Ben", "Json"]
const ROLES := ["on the phone", "next desk", "wrote the runbook"]
const LIST_W := 132.0
const LINE_H := 15

var who := 0
var logs := [[], [], []]     # arrays of [speaker, text]
var cust_name := "the customer"
var cur := ""
var caret := 0
var thinking := false
var blink := 0.0
var scroll := 0        # lines up from the bottom


func _ready() -> void:
	focus_mode = Control.FOCUS_ALL
	mouse_filter = Control.MOUSE_FILTER_STOP
	if mono == null:
		mono = ThemeDB.fallback_font
	set_process(true)
	logs[2].append(["Json", "Morning. Shout if you get stuck -- I know how the box is put together, even if I cannot see yours."])
	logs[1].append(["Ben", "Alright? I have not seen your machine, so tell me what it is doing."])


func take_focus() -> void:
	grab_focus()


# The first thing they say, before you have asked anything -- the message
# that was waiting in the notification.
func seed_first(text: String) -> void:
	logs[0].append([cust_name, text])
	queue_redraw()


func reset(name: String) -> void:
	cust_name = name
	logs[0] = [["", "%s is on the line. They are not technical, and they are the only pair of hands in the room." % name]]
	who = 0
	queue_redraw()


func _process(dt: float) -> void:
	_collect()
	blink += dt
	if blink > 0.5:
		blink -= 0.5
		queue_redraw()


# Ask contact `w`. Called by the desktop and by the terminal's ask/sam/boss.
#
# ON A WORKER THREAD, because the model takes seconds on a cpu and doing it on
# the main thread freezes the whole desktop -- the clock stops, the cursor
# stops blinking, and the game looks crashed every time you say hello. The
# reply is collected in _process. One call at a time: the llama context is not
# reentrant, and a support call is a conversation, not a broadcast.
var _thread: Thread = null
var _pending_who := 0

func post(w: int, text: String) -> void:
	if text.strip_edges() == "":
		return
	if thinking:
		logs[who].append(["", "(still waiting for a reply -- one at a time)"])
		queue_redraw()
		return
	who = clampi(w, 0, 2)
	logs[who].append(["you", text])
	scroll = 0
	thinking = true
	_pending_who = who
	queue_redraw()

	_thread = Thread.new()
	var target := who
	_thread.start(func() -> String:
		if target == 0:
			return machine.ask(text)
		return machine.colleague("coworker" if target == 1 else "manager", text))


func _collect() -> void:
	if _thread == null or _thread.is_alive():
		return
	var reply: String = _thread.wait_to_finish()
	_thread = null
	thinking = false

	reply = reply.strip_edges()
	# The engine frames replies as "  \"...\"" or "  Ben: \"...\"". In a chat
	# window the speaker is already a label, so strip it.
	reply = reply.trim_prefix("Ben:").trim_prefix("Json:").strip_edges()
	reply = reply.trim_prefix("\"").trim_suffix("\"")
	if reply == "":
		reply = "(no reply)"
	logs[_pending_who].append([cust_name if _pending_who == 0 else NAMES[_pending_who], reply])
	queue_redraw()


func _gui_input(e: InputEvent) -> void:
	if e is InputEventMouseButton and e.pressed and e.button_index == MOUSE_BUTTON_WHEEL_UP:
		scroll += 3; queue_redraw(); return
	if e is InputEventMouseButton and e.pressed and e.button_index == MOUSE_BUTTON_WHEEL_DOWN:
		scroll = max(0, scroll - 3); queue_redraw(); return
	if e is InputEventMouseButton and e.pressed and e.button_index == MOUSE_BUTTON_LEFT:
		grab_focus()
		if e.position.x < LIST_W:
			var i := int((e.position.y - 6) / 34)
			if i >= 0 and i < 3:
				who = i
				queue_redraw()
		return

	if not (e is InputEventKey) or not e.pressed:
		return
	var k := e as InputEventKey
	accept_event()
	match k.keycode:
		KEY_ENTER, KEY_KP_ENTER:
			var t := cur
			cur = ""; caret = 0
			post(who, t)
			return
		KEY_BACKSPACE:
			if caret > 0:
				cur = cur.substr(0, caret - 1) + cur.substr(caret)
				caret -= 1
			queue_redraw(); return
		KEY_LEFT:
			caret = max(0, caret - 1); queue_redraw(); return
		KEY_RIGHT:
			caret = min(cur.length(), caret + 1); queue_redraw(); return
		KEY_TAB:
			who = (who + 1) % 3; queue_redraw(); return
	var ch := char(k.unicode)
	if k.unicode >= 32 and ch != "":
		cur = cur.insert(caret, ch)
		caret += 1
		queue_redraw()


func _wrap(s: String, w: float, sz: int) -> PackedStringArray:
	var out: PackedStringArray = []
	var line := ""
	for word in s.split(" "):
		var t := word if line == "" else line + " " + word
		if mono.get_string_size(t, HORIZONTAL_ALIGNMENT_LEFT, -1, sz).x > w and line != "":
			out.append(line)
			line = word
		else:
			line = t
	if line != "":
		out.append(line)
	return out


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), bg)

	# --- contacts ---
	draw_rect(Rect2(0, 0, LIST_W, size.y), Color("#151a23"))
	draw_line(Vector2(LIST_W, 0), Vector2(LIST_W, size.y), Color("#2b3444"))
	for i in range(3):
		var y := 6.0 + i * 34
		var nm: String = cust_name if i == 0 else NAMES[i]
		if i == who:
			draw_rect(Rect2(2, y, LIST_W - 4, 32), Color("#2f6fb5"))
		draw_string(mono, Vector2(10, y + 14), nm,
			HORIZONTAL_ALIGNMENT_LEFT, -1, 12, ink if i == who else Color("#a9b4c4"))
		draw_string(mono, Vector2(10, y + 27), ROLES[i],
			HORIZONTAL_ALIGNMENT_LEFT, -1, 10, Color("#dbe6f5") if i == who else dim)

	# --- conversation ---
	var x := LIST_W + 10
	var w: float = size.x - x - 18.0
	var rows: PackedStringArray = []
	var cols: PackedColorArray = []
	for entry in logs[who]:
		var speaker: String = entry[0]
		var text: String = entry[1]
		if speaker == "":
			for l in _wrap(text, w, 11):
				rows.append(l); cols.append(dim)
			rows.append(""); cols.append(dim)
			continue
		var head := "you" if speaker == "you" else speaker
		rows.append(head + ":")
		cols.append(Color("#6fa8e8") if speaker == "you" else Color("#8fd6a4"))
		for l in _wrap(text, w - 20, 12):
			rows.append("  " + l); cols.append(ink)
		rows.append(""); cols.append(dim)
	if thinking:
		var dots := ".".repeat(1 + int(blink * 6) % 3)
		rows.append("  %s is typing%s" % [
			cust_name if who == 0 else NAMES[who], dots])
		cols.append(Color("#d3b06a"))

	var visible := int((size.y - 34) / LINE_H)
	scroll = clampi(scroll, 0, max(0, rows.size() - visible))
	var first: int = max(0, rows.size() - visible - scroll)
	var y2 := 16.0
	for i in range(first, min(rows.size(), first + visible)):
		draw_string(mono, Vector2(x, y2), rows[i],
			HORIZONTAL_ALIGNMENT_LEFT, -1, 12, cols[i])
		y2 += LINE_H

	# --- what you are typing ---
	var iy := size.y - 8
	draw_line(Vector2(x, iy - 17), Vector2(size.x - 8, iy - 17), Color("#2b3444"))
	var lead := "to %s: " % (cust_name if who == 0 else NAMES[who])
	draw_string(mono, Vector2(x, iy), lead,
		HORIZONTAL_ALIGNMENT_LEFT, -1, 12, dim)
	var lw := mono.get_string_size(lead, HORIZONTAL_ALIGNMENT_LEFT, -1, 12).x
	draw_string(mono, Vector2(x + lw, iy), cur,
		HORIZONTAL_ALIGNMENT_LEFT, -1, 12, ink)
	if has_focus() and blink < 0.25:
		var cw := mono.get_string_size(cur.substr(0, caret),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 12).x
		draw_rect(Rect2(x + lw + cw, iy - 11, 7, 13), ink)
