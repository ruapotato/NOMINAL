# wire.gd — a line into a window that is already running.
#
# The owner's reason, in his words: *"Claude playing a video game in 3D space
# by taking screenshots of the actual user interface is not a fantastic way for
# it to iterate. It should operate with commands over the port."*
#
# Half of that was already true: the 3D drives a Session with verbs instead of
# calling the model, so everything a player does in the window is a line a
# socket client could have typed. The half that was missing is this one -- a
# RUNNING window had no ear. `./build/bf --serve` plays the same game in a
# separate process with no window in it, which proves the verbs and proves
# nothing about the view.
#
# So: a TCP listener on the loopback, read a line, hand it to tower.command(),
# write back what the session said. The Session it drives is the one the window
# is drawing, so `go f3.comms` walks the player across the building you are
# watching and `cable core:1 sw2:0` leaves a real cable hanging in the tray.
# An agent plays it in text; a human watches it happen.
#
# A SOCKET RATHER THAN A PIPE because a pipe has no framing, no way to tell
# whether anybody is on the other end, and no answer channel that survives the
# reader going away. `_process` polls it on the main thread, which is where
# every one of these commands has to run anyway: they touch the scene tree.

extends Node

const DEFAULT_PORT := 7373
const TRIES := 16            # if the port is taken, walk up: two towers can run

var tower: Node3D = null
var port := 0

var _srv: TCPServer = null
var _peers: Array = []       # [{s: StreamPeerTCP, b: String}]


func _wanted_port() -> int:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--port=") :
			return int(a.split("=")[1])
	var env := OS.get_environment("NOMINAL_PORT")
	if env != "":
		return int(env)
	return DEFAULT_PORT


func _ready() -> void:
	_srv = TCPServer.new()
	var want := _wanted_port()
	for p in range(want, want + TRIES):
		if _srv.listen(p, "127.0.0.1") == OK:
			port = p
			break
	if port == 0:
		push_warning("nominal: nothing to listen on from %d upwards" % want)
		return
	# ON STDOUT, because an agent that cannot find the port cannot play.
	print("nominal: the tower takes commands on 127.0.0.1:%d" % port)


func _exit_tree() -> void:
	for c in _peers:
		c.s.disconnect_from_host()
	_peers.clear()
	if _srv:
		_srv.stop()


# THE PROMPT IS THE SESSION'S OWN, and it has to say WHICH MACHINE.
#
# This used to be derived from the room and nothing else: `f0 MDF> ` whether
# you were standing in the MDF with your hands in your pockets or sitting at a
# root shell on a server with a serial lead in it. A blind playtester typing
# `ls /` and `dmesg` down that line had no way to tell that the words were
# going to a different machine from the one they went to a minute ago -- they
# called it the single most dangerous piece of missing state in the game, and
# for a client with no screen that is exactly what it is.
#
# core/session.c's session_prompt() has always known the difference:
#
#   f0 MDF>        standing in a room, `go` and `carry` and `cable` go here
#   mgmt@core#     the crash cart's lead in an appliance's management line
#   root@files#    a real shell on the real operating system in that box
#   you@desk#      back at the workstation, where the break-fix game is
#
# So it is asked, through ses_prompt(), rather than worked out again here.
# Deriving it a second time in GDScript is how the two came to disagree in the
# first place. The room fallback below is only for a window with no session in
# it yet, which is the one case session_prompt() cannot be asked about.
func prompt() -> String:
	if tower == null:
		return "> "
	if tower.has_method("ses_prompt"):
		var p := str(tower.ses_prompt())
		if p != "":
			return p
	var r: int = int(tower.ses_state().get("room", -1))
	if r < 0 or r >= tower.rooms.size():
		return "> "
	return "d%d %s> " % [int(tower.rooms[r].floor), str(tower.rooms[r].name)]


func _send(c: Dictionary, s: String) -> void:
	if s == "":
		return
	c.s.put_data(s.to_utf8_buffer())


func _process(_dt: float) -> void:
	if _srv == null or port == 0:
		return
	while _srv.is_connection_available():
		var s := _srv.take_connection()
		var c := {"s": s, "b": ""}
		_peers.append(c)
		# THE VERBS THAT ARE THE WINDOW'S AND NOT THE GAME'S. `help` comes out
		# of core/session.c and cannot name these: there is no camera in a
		# Session and no report modal in one either. A socket client has no
		# other way to find out they exist.
		_send(c, "nominal: the tower, live in the window. `help` lists the verbs.\n")
		_send(c, "  the window also takes: `hud` for what is on the screen,\n")
		_send(c, "  `face <box>` / `face <box>:<port>` / `face <room>` to point the\n")
		_send(c, "  camera before a screenshot, and `dismiss` to close the day's\n")
		_send(c, "  report -- the panel a player at the keyboard closes with [Esc].\n")
		_send(c, prompt())
	var live: Array = []
	for c in _peers:
		var s: StreamPeerTCP = c.s
		s.poll()
		if s.get_status() != StreamPeerTCP.STATUS_CONNECTED:
			continue
		var n := s.get_available_bytes()
		if n > 0:
			var got: Array = s.get_data(n)
			if int(got[0]) == OK:
				c.b += (got[1] as PackedByteArray).get_string_from_utf8()
		while c.b.find("\n") >= 0:
			var i: int = c.b.find("\n")
			var line: String = c.b.substr(0, i).strip_edges()
			c.b = c.b.substr(i + 1)
			if line == "bye" or line == "quit":
				s.disconnect_from_host()
				break
			var said := ""
			if tower and tower.has_method("command"):
				said = str(tower.command(line))
			if said != "" and not said.ends_with("\n"):
				said += "\n"
			_send(c, said)
			_send(c, prompt())
		if s.get_status() == StreamPeerTCP.STATUS_CONNECTED:
			live.append(c)
	_peers = live
