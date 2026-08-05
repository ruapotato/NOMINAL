# THE DESKTOP KEEPS WHAT IS YOURS, AND EVERY CONTROL ON IT DOES SOMETHING.
#
# Four things a blind playtester found in one two-hour session, all of them in
# the panel or in what the panel does to your windows. They are checked here
# because none of them is visible from a headless smoke test that only asks
# whether a window opened.
extends SceneTree

var bad := 0

func fail(m: String) -> void:
	print("  FAIL: ", m)
	bad += 1


func click(de: Control, at: Vector2) -> void:
	var ev := InputEventMouseButton.new()
	ev.button_index = MOUSE_BUTTON_LEFT
	ev.pressed = true
	ev.position = at
	ev.global_position = at
	de._input(ev)


func _init() -> void:
	var de: Control = preload("res://scripts/de.gd").new()
	de.size = Vector2(1280, 800)
	root.add_child(de)
	await process_frame
	await process_frame

	# 1. THE OVERFLOW BUTTON. With enough windows open the panel shows `+N`,
	#    and clicking it used to be ignored -- the window behind it was
	#    reachable only by launching the app again.
	for a in ["term", "chat", "notes", "files", "browser", "manual", "clock",
			"gsolitaire", "log", "sysmon", "pkgman", "svcman", "search",
			"calc", "duview", "charmap"]:
		de._launch(a)
		await process_frame
	var lay: Dictionary = de._tab_layout()
	if lay["hidden"].is_empty():
		fail("sixteen windows and nothing overflowed -- this test proves nothing")
	else:
		var mr: Rect2 = lay["more"]
		de._panel_input(_press(mr.position + mr.size * 0.5))
		if not de.winlist_open:
			fail("+%d did not open the window list" % lay["hidden"].size())
		var wr: Rect2 = de._winlist_rect()
		var want: Control = de._winlist_hit(wr.position + Vector2(20, 14))
		if want == null:
			fail("the window list has no rows under its own first row")
		else:
			click(de, wr.position + Vector2(20, 14))
			if de.focused != want.get_meta("content"):
				fail("clicking a row did not raise %s" % want.get_meta("title"))
			print("  +N raised: ", want.get_meta("title"))
		if de.winlist_open:
			fail("the window list stayed open after a choice")

	# 2. THE TOAST IS A BUTTON. It says "click to open Chat".
	for w in de.windows.duplicate():
		if str(w.get_meta("title")) == "chat":
			de.windows.erase(w)
			w.queue_free()
	de.chat = null
	de._alert_msg = "somebody: are you there?"
	await process_frame
	var tr: Rect2 = de._toast_screen()
	click(de, tr.position + tr.size * 0.5)
	await process_frame
	if de._find("chat") == null:
		fail("clicking `click to open Chat` did not open Chat")
	else:
		print("  toast opened: ", de._find("chat").get_meta("title"))

	# 3. A NEW CALL IS NOT A NEW DESK. Everything that is yours survives the
	#    ticket closing; the customer's console does not, because it is a view
	#    of a machine nothing is attached to any more.
	de._launch("term")
	await process_frame
	var notes: Control = de._find("notes")
	if notes == null:
		fail("no notes window to lose")
	var before: int = de.windows.size()
	var was_cust: String = de.cust
	# The console belongs to the ticket. Build one the way the desktop does.
	de._open_terminal(1, "console - %s (%s)" % [de.addr, de.cust],
		Rect2(40, 40, 400, 200))
	await process_frame
	de._new_ticket()
	await process_frame
	if de.cust == was_cust:
		print("  (same customer name on the next ticket -- fine, it is a name)")
	if de.windows.size() < before:
		fail("closing the ticket destroyed %d window(s)" %
			(before - de.windows.size()))
	if not is_instance_valid(notes) or not de.windows.has(notes):
		fail("the notes window did not survive the ticket")
	else:
		print("  survived the new ticket: %d windows" % de.windows.size())
	if de._find("console - ") != null:
		fail("the previous ticket's console is still on screen")

	# 4. THE PANEL NAMES YOUR OWN MACHINE. It read `node-4824` -- the
	#    customer's -- on the workstation whose /etc/hostname says node-1.
	var st: String = de._status_text()
	print("  panel says: ", st)
	var real: String = str(de.machine.sh_on(0, "cat /etc/hostname")).strip_edges()
	if st.find(real) < 0:
		fail("the panel does not name this workstation (%s)" % real)
	if st.find("you:") < 0:
		fail("the panel names a machine without saying whose it is")
	if st.find("shift") < 0:
		fail("the panel clock does not say which clock it is")

	print("desk_holds: %d failures" % bad)
	quit(1 if bad else 0)


func _press(at: Vector2) -> InputEventMouseButton:
	var ev := InputEventMouseButton.new()
	ev.button_index = MOUSE_BUTTON_LEFT
	ev.pressed = true
	ev.position = at
	ev.global_position = at
	return ev
