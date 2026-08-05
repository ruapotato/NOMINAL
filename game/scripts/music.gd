# music.gd — the music player.
#
# THE PLAYLIST IS A DIRECTORY LISTING. Every row on this window is a line of
# `ls /usr/share/sounds` run through the machine's own shell, with the mode and
# the size the machine reported. There is no array of track names in this file
# and there must never be one: the desktop is a VIEW of the operating system,
# and a player that knows about a song the filesystem has never heard of is a
# second source of truth about a directory the player can read with `ls`.
# Delete a track and it leaves the playlist. `pkg reinstall nomnix-sounds` and
# it comes back. That is the whole point of shipping the sounds as a package.
#
# The SAMPLES, though, come from res://. A wav is a megabyte of PCM per ten
# seconds and this guest disk is modelled byte for byte -- so the disk carries
# the entry (name, mode, size, owner) and the desktop carries the audio, and a
# name with no sample behind it is shown as such rather than silently skipped.
#
# THE 26 MB TRACK IS LOADED WHEN IT IS FIRST PLAYED, never at scene load. A
# `preload` here is a compile-time load: opening a terminal would have cost
# twenty-six megabytes of resident PCM for a window the player never opened.
#
# THE VISUALISER IS NOT A DECORATION. It is fed by an AudioEffectSpectrumAnalyzer
# on the master bus and by the bus's own peak meters -- if the audio stops, the
# bars fall, because they are measurements. An animation that ignores the sound
# is a lie in the one part of an application whose entire job is to tell you
# what you are hearing.

extends Control

var mono: Font
var machine: Object = null

# --- palette: the desktop's light chrome, with one dark instrument panel -----
const CHROME  := Color("#d6d3ce")
const PANEL   := Color("#e6e3de")
const WHITE   := Color("#ffffff")
const EDGE_L  := Color("#f4f2ef")
const EDGE_D  := Color("#8e8b86")
const BORDER  := Color("#5c5c5c")
const INK     := Color("#1b1b1b")
const DIM     := Color("#5f6469")
const SEL     := Color("#3465a4")
const SELTX   := Color("#ffffff")
const RED     := Color("#b0281a")
const AMBER   := Color("#8a6d1f")
const SCOPE   := Color("#12161c")   # the same charcoal the terminal uses
const SCOPE_E := Color("#4b5157")
const BAR_LO  := Color("#4fb06a")
const BAR_HI  := Color("#e0a338")
const GRID    := Color("#242a32")

const PAD    := 6.0
const HEAD_H := 30.0
const BAR_H  := 12.0
const TRAN_H := 26.0
const BTN_W  := 28.0
const ROW_H  := 14.0
const VOL_W  := 96.0

const SOUND_DIR := "/usr/share/sounds"
const RES_DIR   := "res://sounds/"
# The one track the desktop always has, for when the machine lists nothing.
const FALLBACK  := "hamnix-demo.wav"

const BANDS := 20
const HZ_LO := 40.0
const HZ_HI := 12000.0

# tracks: {name, size, mode, res, stream, len}
var tracks: Array = []
var cur := -1                # the track loaded into the player, or -1
var sel := 0
var scroll := 0
var note := ""               # where the playlist came from, in words
var err := ""
var repeat := 1              # 0 off, 1 all, 2 one
var vol := 0.8
var muted := false

var _player: AudioStreamPlayer
var _spec: AudioEffectSpectrumAnalyzerInstance = null
var _bands: PackedFloat32Array = PackedFloat32Array()
var _lpk := 0.0
var _rpk := 0.0
var _drag := ""              # "bar" or "vol" while a slider is held


func _ready() -> void:
	focus_mode = Control.FOCUS_ALL
	mouse_filter = Control.MOUSE_FILTER_STOP
	if mono == null:
		mono = preload("res://scripts/uifont.gd").mono()
	_bands.resize(BANDS)
	_player = AudioStreamPlayer.new()
	_player.bus = "Master"
	_player.volume_db = _vol_db()
	_player.finished.connect(_on_finished)
	add_child(_player)
	_attach_analyzer()
	refresh()
	set_process(true)


func take_focus() -> void:
	grab_focus()


# THE ANALYSER IS A BUS EFFECT, AND A BUS IS GLOBAL. Two music windows would
# otherwise stack two analysers on the master bus and pay for both, so an
# existing one is reused. It is never removed: a window that is closed on this
# desktop is only hidden, and tearing an effect off a bus that another window
# is still reading from is a worse bug than one idle FFT.
func _attach_analyzer() -> void:
	var idx := -1
	for i in AudioServer.get_bus_effect_count(0):
		if AudioServer.get_bus_effect(0, i) is AudioEffectSpectrumAnalyzer:
			idx = i
			break
	if idx < 0:
		var fx := AudioEffectSpectrumAnalyzer.new()
		fx.buffer_length = 0.1
		AudioServer.add_bus_effect(0, fx)
		idx = AudioServer.get_bus_effect_count(0) - 1
	var inst: AudioEffectInstance = AudioServer.get_bus_effect_instance(0, idx)
	if inst is AudioEffectSpectrumAnalyzerInstance:
		_spec = inst as AudioEffectSpectrumAnalyzerInstance


# ------------------------------------------------------------ the playlist

# The desktop calls this after every command run anywhere, which is exactly
# what makes `rm /usr/share/sounds/hamnix-demo.wav` in a terminal empty this
# window a frame later. Nothing that was here before survives except the track
# that is actually playing, which is identified by NAME and re-found -- a
# player that stops the music because a row moved is a player nobody trusts.
func refresh() -> void:
	var playing_name := ""
	if cur >= 0 and cur < tracks.size():
		playing_name = str(tracks[cur]["name"])
	var keep: Dictionary = {}
	for t in tracks:
		keep[t["name"]] = t
	err = ""
	tracks = []

	var out := ""
	if machine == null:
		err = "no machine attached"
	else:
		out = str(machine.sh_on(0, "ls " + SOUND_DIR))
	for line in out.split("\n"):
		var s := line.strip_edges()
		if s == "":
			continue
		if s.begins_with("ls:"):
			err = s
			continue
		var e: Dictionary = _parse(line)
		if e.is_empty():
			continue
		if keep.has(e["name"]):
			# The loaded stream survives a refresh. Re-reading a 26 MB wav off
			# disk every time somebody types a command in another window is
			# not a refresh, it is a stutter.
			var old: Dictionary = keep[e["name"]]
			e["stream"] = old["stream"]
			e["len"] = old["len"]
		tracks.append(e)

	if tracks.is_empty():
		# SAY WHY THE LIST IS EMPTY. A player showing one track with no
		# explanation looks like a player with one track; the interesting case
		# is a machine whose /usr/share/sounds is gone, and that is a fault.
		note = ("%s listed nothing -- playing the desktop's own copy"
			% SOUND_DIR) if err == "" else \
			("%s -- playing the desktop's own copy" % err)
		tracks.append(_entry(FALLBACK, "", ""))
	else:
		note = "%d file(s) in %s, as `ls` prints them" % [tracks.size(), SOUND_DIR]

	cur = -1
	for i in tracks.size():
		if str(tracks[i]["name"]) == playing_name:
			cur = i
	if cur < 0 and _player != null and _player.playing:
		# The file being played has left the disk. Keep playing it -- the
		# samples are in memory and the sound does not stop when the inode
		# does -- but the list no longer claims it is there.
		pass
	sel = clampi(sel, 0, maxi(0, tracks.size() - 1))
	_clamp()
	queue_redraw()


# One `ls` line: a type character, four octal mode digits, two spaces, the
# size right-aligned in eight columns, then the name. Same format files.gd
# reads, because it is the same /bin/ls printing it.
func _parse(line: String) -> Dictionary:
	if line.length() < 8 or "dl-".find(line[0]) < 0:
		return {}
	if line[0] == "d":
		return {}
	var mode := line.substr(1, 4)
	var i := 5
	while i < line.length() and line[i] == " ":
		i += 1
	var s := i
	while i < line.length() and line[i] != " ":
		i += 1
	var sz := line.substr(s, i - s)
	var nm := line.substr(i).strip_edges()
	var arrow := nm.find(" -> ")
	if arrow >= 0:
		nm = nm.substr(0, arrow).strip_edges()
	if nm == "":
		return {}
	return _entry(nm, sz, mode)


func _entry(nm: String, sz: String, mode: String) -> Dictionary:
	var path := RES_DIR + nm
	return {
		"name": nm, "size": sz, "mode": mode,
		"res": path if ResourceLoader.exists(path) else "",
		"stream": null, "len": -1.0,
	}


# ------------------------------------------------------------ playing

# LOADED HERE AND NOWHERE ELSE. This is the only line in the file that reads a
# sample off disk, and it does not run until somebody presses play.
func _stream_of(i: int) -> AudioStream:
	if i < 0 or i >= tracks.size():
		return null
	var t: Dictionary = tracks[i]
	if t["stream"] != null:
		return t["stream"] as AudioStream
	if str(t["res"]) == "":
		return null
	var st: AudioStream = load(str(t["res"])) as AudioStream
	if st == null:
		return null
	t["stream"] = st
	t["len"] = st.get_length()
	return st


func _play(i: int, from: float = 0.0) -> void:
	if i < 0 or i >= tracks.size():
		return
	var st: AudioStream = _stream_of(i)
	if st == null:
		err = "%s: no sample behind that name" % str(tracks[i]["name"])
		queue_redraw()
		return
	err = ""
	cur = i
	sel = i
	_player.stream = st
	_player.stream_paused = false
	_player.play(from)
	_clamp()
	queue_redraw()


func _toggle() -> void:
	if cur < 0:
		_play(sel)
		return
	if not _player.playing:
		_play(cur)
		return
	_player.stream_paused = not _player.stream_paused
	queue_redraw()


func _stop() -> void:
	_player.stop()
	queue_redraw()


func _step(d: int) -> void:
	if tracks.size() == 0:
		return
	var i: int = (maxi(cur, 0) + d + tracks.size()) % tracks.size()
	_play(i)


func _on_finished() -> void:
	if repeat == 2:
		_play(cur)
		return
	if tracks.size() > 1 and (repeat == 1 or cur < tracks.size() - 1):
		var nxt := cur + 1
		if nxt >= tracks.size():
			if repeat != 1:
				return
			nxt = 0
		_play(nxt)


func _pos() -> float:
	if _player == null or not _player.playing:
		return 0.0
	return _player.get_playback_position()


func _len() -> float:
	if cur < 0 or cur >= tracks.size():
		return -1.0
	return float(tracks[cur]["len"])


func _vol_db() -> float:
	if muted or vol <= 0.0:
		return -80.0
	return linear_to_db(vol)


func _set_vol(v: float) -> void:
	vol = clampf(v, 0.0, 1.0)
	muted = false
	_player.volume_db = _vol_db()
	queue_redraw()


# ------------------------------------------------------------ geometry
#
# ONE function owns every rectangle on this window, and both the drawing and
# the hit testing ask it. Two copies of the same arithmetic is how a play
# button comes to start the track next to the one you clicked.
func _geom() -> Dictionary:
	var w: float = size.x
	var y: float = HEAD_H
	var bar := Rect2(PAD, y, maxf(20.0, w - PAD * 2.0), BAR_H)
	y += BAR_H + 8.0
	var btns: Array = []
	var bx := PAD
	for i in 5:
		btns.append(Rect2(bx, y, BTN_W - 2.0, TRAN_H - 4.0))
		bx += BTN_W
	var vw: float = minf(VOL_W, maxf(40.0, w - bx - PAD - 26.0))
	var volr := Rect2(w - PAD - vw, y + 6.0, vw, TRAN_H - 16.0)
	y += TRAN_H
	var rest: float = maxf(30.0, size.y - y - PAD)
	# The visualiser takes a share, not a fixed slab: at 240 pixels tall the
	# playlist still gets rows, and at 800 the scope does not become a poster.
	var vh: float = clampf(rest * 0.45, 34.0, 220.0)
	if rest < 90.0:
		vh = rest * 0.5
	var vis := Rect2(PAD, y, maxf(20.0, w - PAD * 2.0), vh)
	var list := Rect2(PAD, y + vh + 4.0, maxf(20.0, w - PAD * 2.0),
		maxf(ROW_H, size.y - (y + vh + 4.0) - PAD))
	return {"bar": bar, "btns": btns, "vol": volr, "vis": vis, "list": list}


func _visible_rows() -> int:
	var g: Dictionary = _geom()
	return maxi(1, int((g["list"] as Rect2).size.y / ROW_H))


func _clamp() -> void:
	var vis := _visible_rows()
	sel = clampi(sel, 0, maxi(0, tracks.size() - 1))
	if sel < scroll:
		scroll = sel
	elif sel >= scroll + vis:
		scroll = sel - vis + 1
	scroll = clampi(scroll, 0, maxi(0, tracks.size() - vis))


# ------------------------------------------------------------ input

func _gui_input(e: InputEvent) -> void:
	if e is InputEventMouseButton:
		var mb := e as InputEventMouseButton
		if mb.pressed:
			grab_focus()
		var g: Dictionary = _geom()
		if mb.button_index == MOUSE_BUTTON_WHEEL_UP and mb.pressed:
			scroll = maxi(0, scroll - 2)
			accept_event()
			queue_redraw()
			return
		if mb.button_index == MOUSE_BUTTON_WHEEL_DOWN and mb.pressed:
			scroll = clampi(scroll + 2, 0,
				maxi(0, tracks.size() - _visible_rows()))
			accept_event()
			queue_redraw()
			return
		if mb.button_index != MOUSE_BUTTON_LEFT:
			return
		if not mb.pressed:
			_drag = ""
			accept_event()
			return
		var p := mb.position
		if (g["bar"] as Rect2).grow(3.0).has_point(p):
			_drag = "bar"
			_seek_at(p.x, g["bar"] as Rect2)
			accept_event()
			return
		if (g["vol"] as Rect2).grow(4.0).has_point(p):
			_drag = "vol"
			var vr := g["vol"] as Rect2
			_set_vol((p.x - vr.position.x) / maxf(1.0, vr.size.x))
			accept_event()
			return
		var bts: Array = g["btns"]
		for i in bts.size():
			if (bts[i] as Rect2).has_point(p):
				match i:
					0: _step(-1)
					1: _toggle()
					2: _stop()
					3: _step(1)
					4:
						repeat = (repeat + 1) % 3
						queue_redraw()
				accept_event()
				return
		var lr := g["list"] as Rect2
		if lr.has_point(p):
			var i2 := scroll + int((p.y - lr.position.y) / ROW_H)
			if i2 >= 0 and i2 < tracks.size():
				sel = i2
				if mb.double_click:
					_play(i2)
				queue_redraw()
		accept_event()
		return

	if e is InputEventMouseMotion and _drag != "":
		var g2: Dictionary = _geom()
		var mm := e as InputEventMouseMotion
		if _drag == "bar":
			_seek_at(mm.position.x, g2["bar"] as Rect2)
		else:
			var vr2 := g2["vol"] as Rect2
			_set_vol((mm.position.x - vr2.position.x) / maxf(1.0, vr2.size.x))
		accept_event()
		return

	if not (e is InputEventKey) or not (e as InputEventKey).pressed:
		return
	match (e as InputEventKey).keycode:
		KEY_SPACE, KEY_P:
			_toggle()
		KEY_S:
			_stop()
		KEY_N:
			_step(1)
		KEY_B:
			_step(-1)
		KEY_L:
			repeat = (repeat + 1) % 3
		KEY_M:
			muted = not muted
			_player.volume_db = _vol_db()
		KEY_ENTER, KEY_KP_ENTER:
			_play(sel)
		KEY_UP:
			sel = maxi(0, sel - 1)
			_clamp()
		KEY_DOWN:
			sel = mini(maxi(0, tracks.size() - 1), sel + 1)
			_clamp()
		KEY_LEFT:
			_seek(_pos() - 5.0)
		KEY_RIGHT:
			_seek(_pos() + 5.0)
		KEY_EQUAL, KEY_PLUS:
			_set_vol(vol + 0.1)
		KEY_MINUS:
			_set_vol(vol - 0.1)
		KEY_R:
			refresh()
		_:
			return
	accept_event()
	queue_redraw()


func _seek_at(x: float, r: Rect2) -> void:
	var l := _len()
	if l <= 0.0:
		# Nothing is loaded yet: a click on the bar is a request to hear the
		# selected track from there, which is what the click meant.
		_play(sel)
		l = _len()
		if l <= 0.0:
			return
	_seek(clampf((x - r.position.x) / maxf(1.0, r.size.x), 0.0, 1.0) * l)


func _seek(t: float) -> void:
	var l := _len()
	if l <= 0.0:
		return
	var at := clampf(t, 0.0, maxf(0.0, l - 0.05))
	if _player.playing:
		_player.seek(at)
	else:
		_play(cur if cur >= 0 else sel, at)
	queue_redraw()


# ------------------------------------------------------------ the meters

func _process(_dt: float) -> void:
	# THE ONLY SOURCE OF THESE NUMBERS IS THE AUDIO BUS. get_bus_peak_volume_*
	# is what the mixer actually pushed out last frame, and the analyser is an
	# FFT of the same signal. When the music stops they go to silence on their
	# own, with no code here to tell them to.
	var l := db_to_linear(AudioServer.get_bus_peak_volume_left_db(0, 0))
	var r := db_to_linear(AudioServer.get_bus_peak_volume_right_db(0, 0))
	_lpk = maxf(l, _lpk * 0.90)
	_rpk = maxf(r, _rpk * 0.90)
	var lo := HZ_LO
	for i in BANDS:
		var hi: float = HZ_LO * pow(HZ_HI / HZ_LO, float(i + 1) / float(BANDS))
		var v := 0.0
		if _spec != null:
			var m: Vector2 = _spec.get_magnitude_for_frequency_range(lo, hi,
				AudioEffectSpectrumAnalyzerInstance.MAGNITUDE_MAX)
			# An FFT bin is a linear magnitude and hearing is not linear, so
			# it is read in dB over a 60 dB window -- otherwise everything but
			# the kick drum sits flat on the floor of the display.
			v = clampf((linear_to_db(maxf(m.x, m.y)) + 60.0) / 60.0, 0.0, 1.0)
		else:
			# No analyser on this bus (some drivers refuse to give one back).
			# The peak meter is still real audio, so the bars ride it with a
			# fixed spectral tilt rather than pretending to be a spectrum.
			v = clampf(maxf(_lpk, _rpk) * (1.0 - 0.5 * float(i) / BANDS), 0.0, 1.0)
		# Fall, do not snap. A bar that drops to zero between two frames of a
		# quiet passage reads as a broken meter.
		_bands[i] = maxf(v, _bands[i] * 0.86)
		lo = hi
	if _player != null and _player.playing:
		queue_redraw()
	elif _lpk > 0.001 or _rpk > 0.001:
		queue_redraw()


# ------------------------------------------------------------ drawing

func _mmss(t: float) -> String:
	if t < 0.0:
		return "--:--"
	var s := int(t)
	return "%d:%02d" % [s / 60, s % 60]


func _fit(t: String, w: float, fs: int) -> String:
	if mono.get_string_size(t, HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x <= w:
		return t
	var s := t
	while s.length() > 1 and \
			mono.get_string_size(s + "...", HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x > w:
		s = s.substr(0, s.length() - 1)
	return s + "..."


func _raised(r: Rect2, face: Color) -> void:
	draw_rect(r, face)
	draw_line(r.position + Vector2(0.5, 0.5),
		r.position + Vector2(r.size.x - 0.5, 0.5), EDGE_L)
	draw_line(r.position + Vector2(0.5, 0.5),
		r.position + Vector2(0.5, r.size.y - 0.5), EDGE_L)
	draw_line(r.position + Vector2(0.5, r.size.y - 0.5),
		r.position + Vector2(r.size.x - 0.5, r.size.y - 0.5), EDGE_D)
	draw_line(r.position + Vector2(r.size.x - 0.5, 0.5),
		r.position + Vector2(r.size.x - 0.5, r.size.y - 0.5), EDGE_D)
	draw_rect(r, BORDER, false, 1.0)


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), CHROME)
	var g: Dictionary = _geom()
	_draw_head()
	_draw_bar(g["bar"] as Rect2)
	_draw_transport(g["btns"] as Array, g["vol"] as Rect2)
	_draw_scope(g["vis"] as Rect2)
	_draw_list(g["list"] as Rect2)


func _draw_head() -> void:
	draw_rect(Rect2(0, 0, size.x, HEAD_H), PANEL)
	draw_line(Vector2(0, HEAD_H - 1), Vector2(size.x, HEAD_H - 1), Color("#b3b0ab"))
	var title := "nothing loaded"
	if cur >= 0 and cur < tracks.size():
		title = str(tracks[cur]["name"])
	elif tracks.size() > 0:
		title = str(tracks[sel]["name"])
	var state := "stopped"
	if _player != null and _player.playing:
		state = "paused" if _player.stream_paused else "playing"
	var times := "%s / %s" % [_mmss(_pos() if state != "stopped" else 0.0),
		_mmss(_len())]
	var tw: float = mono.get_string_size(times, HORIZONTAL_ALIGNMENT_LEFT, -1, 12).x
	draw_string(mono, Vector2(PAD, 14), _fit(title, size.x - tw - 18.0, 12),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 12, INK)
	draw_string(mono, Vector2(size.x - PAD - tw, 14), times,
		HORIZONTAL_ALIGNMENT_LEFT, -1, 12, INK if state == "playing" else DIM)
	var sub := note if err == "" else err
	draw_string(mono, Vector2(PAD, 26), _fit("%s  --  %s" % [state, sub],
		size.x - PAD * 2.0, 9), HORIZONTAL_ALIGNMENT_LEFT, -1, 9,
		RED if err != "" else DIM)


func _draw_bar(r: Rect2) -> void:
	draw_rect(r, Color("#eceae7"))
	var l := _len()
	if l > 0.0:
		var f: float = clampf(_pos() / l, 0.0, 1.0)
		draw_rect(Rect2(r.position, Vector2(r.size.x * f, r.size.y)), SEL)
		# The handle is what says "this is draggable" without a tooltip.
		var hx: float = r.position.x + r.size.x * f
		draw_rect(Rect2(hx - 2.0, r.position.y - 2.0, 4.0, r.size.y + 4.0), INK)
	draw_rect(r, EDGE_D, false, 1.0)


func _draw_transport(btns: Array, volr: Rect2) -> void:
	for i in btns.size():
		var b := btns[i] as Rect2
		var on: bool = (i == 1 and _player != null and _player.playing
			and not _player.stream_paused) or (i == 4 and repeat != 0)
		_raised(b, WHITE if on else CHROME)
		var c := b.position + b.size * 0.5
		var s: float = minf(b.size.x, b.size.y) * 0.30
		match i:
			0:
				_tri(c + Vector2(s * 0.4, 0), -s)
				draw_rect(Rect2(c.x - s * 1.0, c.y - s, 2.0, s * 2.0), INK)
			1:
				if _player != null and _player.playing and not _player.stream_paused:
					draw_rect(Rect2(c.x - s * 0.7, c.y - s, s * 0.55, s * 2.0), INK)
					draw_rect(Rect2(c.x + s * 0.15, c.y - s, s * 0.55, s * 2.0), INK)
				else:
					_tri(c + Vector2(-s * 0.3, 0), s)
			2:
				draw_rect(Rect2(c.x - s * 0.8, c.y - s * 0.8, s * 1.6, s * 1.6), INK)
			3:
				_tri(c + Vector2(-s * 0.4, 0), s)
				draw_rect(Rect2(c.x + s * 0.8, c.y - s, 2.0, s * 2.0), INK)
			4:
				# Repeat: a loop of two arrows. A second mark means "this one
				# track", which is the state a symbol has to carry because the
				# button cannot hold a word at 26 pixels.
				var col: Color = INK if repeat != 0 else DIM
				draw_rect(Rect2(c.x - s, c.y - s * 0.9, s * 2.0, 2.0), col)
				draw_rect(Rect2(c.x - s, c.y + s * 0.7, s * 2.0, 2.0), col)
				draw_rect(Rect2(c.x - s, c.y - s * 0.9, 2.0, s * 1.8), col)
				draw_rect(Rect2(c.x + s - 2.0, c.y - s * 0.9, 2.0, s * 1.8), col)
				# Two arrowheads, or the loop is a rectangle and the rectangle
				# next to it is the stop button.
				var a: float = maxf(2.0, s * 0.5)
				draw_colored_polygon(PackedVector2Array([
					Vector2(c.x + s * 0.2, c.y - s * 0.9 - a * 0.7),
					Vector2(c.x + s * 0.2 + a, c.y - s * 0.9 + 1.0),
					Vector2(c.x + s * 0.2, c.y - s * 0.9 + a * 0.7 + 1.0)]), col)
				draw_colored_polygon(PackedVector2Array([
					Vector2(c.x - s * 0.2, c.y + s * 0.7 - a * 0.7 + 1.0),
					Vector2(c.x - s * 0.2 - a, c.y + s * 0.7 + 1.0),
					Vector2(c.x - s * 0.2, c.y + s * 0.7 + a * 0.7 + 2.0)]), col)
				if repeat == 2:
					draw_circle(c, maxf(1.5, s * 0.35), col)

	# Volume. A slider, drawn from the same rect the drag reads.
	draw_string(mono, Vector2(volr.position.x - 22.0, volr.position.y + 9.0),
		"vol", HORIZONTAL_ALIGNMENT_LEFT, -1, 9, DIM)
	draw_rect(volr, Color("#eceae7"))
	var v: float = 0.0 if muted else vol
	draw_rect(Rect2(volr.position, Vector2(volr.size.x * v, volr.size.y)),
		AMBER if muted else SEL)
	draw_rect(volr, EDGE_D, false, 1.0)
	draw_rect(Rect2(volr.position.x + volr.size.x * v - 2.0,
		volr.position.y - 2.0, 4.0, volr.size.y + 4.0), INK)


func _tri(c: Vector2, s: float) -> void:
	draw_colored_polygon(PackedVector2Array([
		c + Vector2(0, -absf(s)), c + Vector2(s, 0), c + Vector2(0, absf(s))]), INK)


# The instrument panel. Bars are the spectrum, the two thin meters down the
# right are the bus's own left and right peaks.
func _draw_scope(r: Rect2) -> void:
	draw_rect(r, SCOPE)
	draw_rect(r, SCOPE_E, false, 1.0)
	var mw: float = 10.0
	var inner := Rect2(r.position + Vector2(2, 2),
		Vector2(maxf(8.0, r.size.x - 4.0 - mw), r.size.y - 4.0))
	# Two rules, so a bar that is not moving is visibly not moving.
	for k in 2:
		var y: float = inner.position.y + inner.size.y * (0.33 + 0.34 * k)
		draw_line(Vector2(inner.position.x, y),
			Vector2(inner.position.x + inner.size.x, y), GRID, 1.0)
	var bw: float = inner.size.x / float(BANDS)
	for i in BANDS:
		var h: float = inner.size.y * _bands[i]
		if h < 1.0:
			continue
		var x: float = inner.position.x + float(i) * bw
		draw_rect(Rect2(x + 1.0, inner.position.y + inner.size.y - h,
			maxf(1.0, bw - 2.0), h), BAR_LO.lerp(BAR_HI, _bands[i]))
	var mx: float = r.position.x + r.size.x - mw
	for ch in 2:
		var pk: float = _lpk if ch == 0 else _rpk
		var mr := Rect2(mx + float(ch) * 5.0, r.position.y + 2.0, 3.0, r.size.y - 4.0)
		draw_rect(mr, GRID)
		var mh: float = mr.size.y * clampf(pk, 0.0, 1.0)
		draw_rect(Rect2(mr.position.x, mr.position.y + mr.size.y - mh,
			mr.size.x, mh), BAR_HI if pk > 0.9 else BAR_LO)
	if _spec == null:
		draw_string(mono, r.position + Vector2(6, 12), "peak meter only",
			HORIZONTAL_ALIGNMENT_LEFT, -1, 9, Color("#6d7680"))


func _draw_list(r: Rect2) -> void:
	draw_rect(r, WHITE)
	draw_rect(r, EDGE_D, false, 1.0)
	var vis := _visible_rows()
	for i in range(scroll, mini(tracks.size(), scroll + vis)):
		var t: Dictionary = tracks[i]
		var y: float = r.position.y + float(i - scroll) * ROW_H
		if y + ROW_H > r.position.y + r.size.y + 1.0:
			break
		var picked: bool = i == sel
		if picked:
			draw_rect(Rect2(r.position.x + 1.0, y, r.size.x - 2.0, ROW_H), SEL)
		elif i % 2 == 1:
			draw_rect(Rect2(r.position.x + 1.0, y, r.size.x - 2.0, ROW_H),
				Color("#f5f4f2"))
		var col: Color = SELTX if picked else INK
		if str(t["res"]) == "" and not picked:
			col = DIM
		var mark := "  "
		if i == cur:
			mark = "> " if (_player != null and _player.playing
				and not _player.stream_paused) else "= "
		var sz := str(t["size"])
		var right := ""
		if str(t["res"]) == "":
			right = "no sample"
		elif float(t["len"]) > 0.0:
			right = _mmss(float(t["len"]))
		elif sz != "":
			right = sz + " b"
		var rw: float = mono.get_string_size(right, HORIZONTAL_ALIGNMENT_LEFT,
			-1, 10).x
		draw_string(mono, Vector2(r.position.x + 5.0, y + 11.0),
			_fit(mark + str(t["name"]), r.size.x - rw - 16.0, 11),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 11, col)
		draw_string(mono, Vector2(r.position.x + r.size.x - 5.0 - rw, y + 11.0),
			right, HORIZONTAL_ALIGNMENT_LEFT, -1, 10,
			SELTX if picked else (RED if str(t["res"]) == "" else DIM))
	if tracks.is_empty():
		draw_string(mono, r.position + Vector2(6, 14),
			"no sounds installed", HORIZONTAL_ALIGNMENT_LEFT, -1, 11, DIM)
