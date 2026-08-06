# screens.gd — what is on a tenant's monitor, and where every pixel of it came
# from.
#
# The owner: "Those people at the desks don't seem to show a 2d interface like
# the one in the IT room. I'd like those to act a lot like our main one, but
# with whatever software the end user is using / the user sets up for them."
#
# THE CONSTRAINT THAT SHAPES THIS FILE. The workstation in the MDF shows a real
# desktop because there is a real machine behind it: one de.gd in a SubViewport
# on a Station that exists for the whole run. A booted machine costs 18 MB
# (D31) and a full tower is 176 desks, so 176 real desktops is 3.2 GB, and 176
# live SubViewports is a renderer with 176 more render targets in it. Neither
# is affordable and neither is on offer here.
#
# What is on offer is the honest half: A SCREEN SHOWS WHAT THE MODEL ALREADY
# KNOWS ABOUT THAT MACHINE. Nothing here is invented and nothing here is a
# terminal:
#
#   WHICH SOFTWARE      the tenancy's TRADE, off `service`'s own trade column.
#                       An office runs a file transfer list, a call centre a
#                       call panel, a web host its origin's request graph, a
#                       studio an ingest timeline -- because that is the work
#                       core/siteday.c really makes their desks do, in the
#                       units `service` counts it in.
#   HOW WELL IT IS      the tenancy's `done` fraction, the same number the rent
#   GOING               is paid on. Six transfers on an office screen and four
#                       of them finished is 4/6 on that row of `service`.
#   WHETHER IT IS ON    the desk's own port and address: no link is a screen
#   THE NETWORK         with nothing on it but the error, an address that
#                       never arrived is the software up and disconnected.
#                       The same two columns -- `up` and `addr` -- that decide
#                       whether the person at that desk has their hand up.
#
# AND WHAT IS A DEPICTION, said plainly because this project does not let a
# picture make a claim the machine cannot: THE LAYOUT IS A DEPICTION. There is
# no window manager running on a tenant's desk, no spreadsheet on the disk, and
# these panels are not screenshots of a program. They are the shape of that
# trade's work at the scale you can read across a room -- rows, bars, a
# waveform, a timeline -- driven by numbers that are real. Nothing on them is
# text, because text you cannot read is text that is pretending.
#
# THE ONE PLACE THERE ARE REAL PIXELS IS THE CHAIR. `sit <desk>` boots that
# machine for as long as you are in the seat, and tower.gd puts a real terminal
# on it -- the session's own shell, the same one the socket types at. One
# machine, because a person has one backside; see D31.
#
# WHAT IT COSTS. One MultiMesh per floor, two triangles per desk, one shader.
# A floor of twenty screens is one draw call and forty triangles, and the whole
# thing animates in the fragment stage off TIME, so nothing is stepped per
# frame per desk on the CPU. The buffers are per floor for the reason people.gd
# is: a MultiMesh is culled by the AABB of its buffer, and one buffer for the
# tower is a buffer the size of the tower.

extends Node3D

# The states a screen can be in, which are `service`'s columns and not moods
# kept here. tower.gd decides them; this file only draws them.
const S_NOLINK := 0     # nothing in the socket, or a run too long to carry
const S_NOADDR := 1     # link, and nothing ever answered the DHCP
const S_WORKING := 2    # link and address: this desk is doing the day's work

# The trades, in the order core/site.h names them. tower.gd maps `service`'s
# trade column onto these; an unknown word lands on office, which is the
# baseline trade rather than a guess.
const T_OFFICE := 0
const T_VOICE := 1
const T_WEBHOST := 2
const T_STUDIO := 3


# WHERE THE GLASS IS, TAKEN OFF THE DESK ITSELF.
#
# people.gd builds the desk, monitor included, and tags the glass P_SCREEN so
# its own shader can flicker it. That tag is a fact about the mesh, so this
# file finds the glass by reading the mesh back rather than by copying the six
# numbers out of _desk_mesh() -- which would be the same value living in two
# files, and the day somebody reshapes the desk the picture would hang in the
# air where the monitor used to be. Another agent is reshaping that file right
# now, which is exactly the case this defends against.
var _rect := {}          # {mid: Vector3 (desk frame), w, h} or {} if not found

func _glass() -> Dictionary:
	if not _rect.is_empty():
		return _rect
	var P = preload("res://scripts/people.gd")
	var m: ArrayMesh = P.new()._desk_mesh()
	if m.get_surface_count() == 0:
		return {}
	var arr: Array = m.surface_get_arrays(0)
	var vs: PackedVector3Array = arr[Mesh.ARRAY_VERTEX]
	var uv: PackedVector2Array = arr[Mesh.ARRAY_TEX_UV]
	if uv.size() != vs.size():
		return {}
	var mn := Vector3(1e9, 1e9, 1e9)
	var mx := -mn
	var got := false
	for i in range(vs.size()):
		if absf(uv[i].x - P.P_SCREEN) > 0.01:
			continue
		got = true
		mn = Vector3(min(mn.x, vs[i].x), min(mn.y, vs[i].y), min(mn.z, vs[i].z))
		mx = Vector3(max(mx.x, vs[i].x), max(mx.y, vs[i].y), max(mx.z, vs[i].z))
	if not got:
		return {}
	# The person sits at +z and the monitor faces them, so the face of the
	# glass is its highest z. Two millimetres proud of it, which is what keeps
	# this off the plane of the panel behind it instead of z-fighting it.
	_rect = {"mid": Vector3((mn.x + mx.x) * 0.5, (mn.y + mx.y) * 0.5, mx.z + 0.002),
		"w": mx.x - mn.x, "h": mx.y - mn.y}
	return _rect


# One shader, and the whole of the picture is in it. UNSHADED, like everything
# else in this building: a monitor is a light source, not a lit surface.
#
# INSTANCE_CUSTOM carries the four numbers that make one desk's screen
# different from the next one's -- .r the trade, .g the state, .b how much of
# that tenancy's work is getting done, .a the phase this desk animates on --
# and it is read in the vertex stage because that is where a spatial shader can
# see it, and handed to the fragment stage as a varying.
const SCREEN_SHADER := """
shader_type spatial;
render_mode unshaded, cull_disabled, shadows_disabled, specular_disabled,
	depth_draw_opaque;

varying vec4 vc;

void vertex() {
	vc = INSTANCE_CUSTOM;
}

// A filled rectangle: 1 inside, 0 outside.
float rect(vec2 p, vec2 lo, vec2 hi) {
	vec2 s = step(lo, p) * step(p, hi);
	return s.x * s.y;
}

// A cheap hash, for bar heights that differ from bar to bar and do not move
// unless something moves them.
float h11(float x) {
	return fract(sin(x * 78.233) * 43758.5453);
}

void fragment() {
	float trade = floor(vc.r + 0.5);
	float state = floor(vc.g + 0.5);
	float done = clamp(vc.b, 0.0, 1.0);
	float phase = vc.a;
	// UV runs 0..1 across the quad with v=0 at the top, which is how a screen
	// is read: the title bar is at the top of the picture and at v just above
	// zero.
	vec2 p = UV;
	vec3 col = vec3(0.043, 0.070, 0.094);      // the desktop behind everything
	float t = TIME + phase * 37.0;

	// ---- the title bar, whose colour is the trade's own
	vec3 accent = vec3(0.37, 0.55, 0.72);                      // office
	if (trade > 0.5 && trade < 1.5) accent = vec3(0.36, 0.68, 0.50);   // voice
	else if (trade > 1.5 && trade < 2.5) accent = vec3(0.78, 0.63, 0.28); // web
	else if (trade > 2.5) accent = vec3(0.62, 0.42, 0.72);             // studio
	col = mix(col, accent * 0.55, rect(p, vec2(0.0, 0.0), vec2(1.0, 0.11)));
	// the one open window's title, as a block of it, and the two little
	// buttons every window in every desktop has had for thirty years
	col = mix(col, accent, rect(p, vec2(0.03, 0.035), vec2(0.34, 0.075)));
	col = mix(col, vec3(0.55), rect(p, vec2(0.90, 0.04), vec2(0.94, 0.07)));
	col = mix(col, vec3(0.55), rect(p, vec2(0.95, 0.04), vec2(0.99, 0.07)));

	if (trade < 0.5) {
		// OFFICE: the transfer list. Six rows, one per file on the go, and
		// the ones that finished are the ones that finished -- `done` is the
		// fraction of the tenancy's transfers that landed inside the busy
		// period, which is the number the rent is paid on.
		for (int i = 0; i < 6; i++) {
			float y = 0.17 + float(i) * 0.125;
			float r = rect(p, vec2(0.05, y), vec2(0.30, y + 0.075));
			col = mix(col, vec3(0.30, 0.34, 0.40), r);
			float fin = step(float(i) + 1.0, done * 6.0);
			// the one still going has a bar that fills, on this desk's phase
			float part = fract(t * 0.23 + float(i) * 0.31);
			float w = mix(part, 1.0, fin);
			float bar = rect(p, vec2(0.34, y), vec2(0.34 + 0.60 * w, y + 0.075));
			col = mix(col, mix(vec3(0.35, 0.55, 0.78), vec3(0.36, 0.72, 0.47), fin), bar);
			col = mix(col, vec3(0.15, 0.18, 0.22),
				rect(p, vec2(0.34 + 0.60 * w, y), vec2(0.94, y + 0.075)));
		}
	} else if (trade < 1.5) {
		// VOICE: four calls down the left, and the audio on the right. A call
		// is 172 bytes every 20 ms and what ruins it is concealment -- an
		// audio frame with no sound to play -- so the wave has holes in it in
		// exactly the proportion the tenancy's calls are failing.
		for (int i = 0; i < 4; i++) {
			float y = 0.20 + float(i) * 0.19;
			float lit = step(float(i) + 1.0, done * 4.0 + 0.001);
			col = mix(col, vec3(0.22, 0.26, 0.31), rect(p, vec2(0.05, y), vec2(0.36, y + 0.12)));
			col = mix(col, mix(vec3(0.72, 0.30, 0.26), vec3(0.36, 0.72, 0.47), lit),
				rect(p, vec2(0.07, y + 0.035), vec2(0.11, y + 0.085)));
		}
		// the trace: a line through the right two thirds, and a gap in it
		// wherever this instant's audio was concealed
		float u = (p.x - 0.42) / 0.52;
		float amp = 0.16 * (0.25 + 0.75 * done);
		float w = 0.53 + amp * sin(u * 26.0 + t * 3.1) * sin(u * 7.0 + t * 0.7);
		float hole = step(h11(floor(u * 22.0) + floor(t * 6.0) * 13.0), 1.0 - done);
		float line = rect(p, vec2(0.42, w - 0.018), vec2(0.94, w + 0.018)) * (1.0 - hole);
		col = mix(col, vec3(0.09, 0.12, 0.15), rect(p, vec2(0.42, 0.20), vec2(0.94, 0.86)));
		col = mix(col, vec3(0.40, 0.85, 0.55), line);
	} else if (trade < 2.5) {
		// WEB HOST: their origin's own graph. Visitors arrive FROM the
		// handoff INWARDS and what they are buying is that it answers at all,
		// so the bars are what got served and the block top right is up or
		// down -- their lease's own distinction.
		for (int i = 0; i < 11; i++) {
			float x = 0.05 + float(i) * 0.082;
			float hh = (0.15 + 0.60 * h11(float(i) + floor(t * 0.5) * 7.0)) * done;
			col = mix(col, vec3(0.78, 0.63, 0.28),
				rect(p, vec2(x, 0.88 - hh), vec2(x + 0.055, 0.88)));
		}
		col = mix(col, vec3(0.20, 0.23, 0.27), rect(p, vec2(0.05, 0.885), vec2(0.94, 0.90)));
		float up = step(0.95, done);
		col = mix(col, mix(vec3(0.72, 0.30, 0.26), vec3(0.36, 0.72, 0.47), up),
			rect(p, vec2(0.80, 0.15), vec2(0.94, 0.26)));
	} else {
		// STUDIO: the ingest, and the project they are cutting. Two sustained
		// uploads per suite for the whole busy period, all or nothing -- so
		// the clips are either running or they are not, and the playhead only
		// moves when the stream is going out.
		col = mix(col, vec3(0.10, 0.09, 0.13), rect(p, vec2(0.05, 0.15), vec2(0.55, 0.48)));
		col = mix(col, vec3(0.30 + 0.12 * sin(t * 0.4), 0.26, 0.34),
			rect(p, vec2(0.07, 0.17), vec2(0.53, 0.46)));
		for (int i = 0; i < 3; i++) {
			float y = 0.56 + float(i) * 0.13;
			col = mix(col, vec3(0.14, 0.13, 0.17), rect(p, vec2(0.05, y), vec2(0.94, y + 0.10)));
			float x0 = 0.06 + h11(float(i) * 3.0) * 0.25;
			float x1 = x0 + 0.20 + 0.45 * done;
			col = mix(col, vec3(0.55, 0.40, 0.68), rect(p, vec2(x0, y + 0.012), vec2(x1, y + 0.088)));
		}
		float head = 0.06 + fract(t * 0.11 * done) * 0.86;
		col = mix(col, vec3(0.90, 0.86, 0.94), rect(p, vec2(head, 0.54), vec2(head + 0.008, 0.90)));
		// what the two streams are doing, top right of the preview
		float on = step(0.5, done);
		for (int i = 0; i < 2; i++) {
			col = mix(col, mix(vec3(0.72, 0.30, 0.26), vec3(0.36, 0.72, 0.47), on),
				rect(p, vec2(0.60, 0.17 + float(i) * 0.10), vec2(0.94, 0.24 + float(i) * 0.10)));
		}
	}

	// ---- AND THEN THE NETWORK, WHICH IS THE PART THE PLAYER CAN DO SOMETHING
	// ABOUT. A desk with no link has nothing to show: the software is up and
	// there is no wire in it, so the screen is its own error and reads as one
	// from the doorway. A desk with a link and no address shows the work
	// greyed out behind the same bar in the colour the door beacon uses for a
	// tenancy waiting on you.
	if (state < 1.5) {
		float dim = state < 0.5 ? 0.10 : 0.34;
		col *= dim;
		vec3 bar = state < 0.5 ? vec3(0.76, 0.28, 0.24) : vec3(0.85, 0.63, 0.22);
		float band = rect(p, vec2(0.10, 0.44), vec2(0.90, 0.58));
		col = mix(col, bar, band);
		// the cross in the middle of it: a link that is not there, drawn the
		// one way that is legible at four metres
		col = mix(col, vec3(0.06, 0.07, 0.09),
			rect(p, vec2(0.12, 0.485), vec2(0.30, 0.535)));
		col = mix(col, vec3(0.06, 0.07, 0.09),
			rect(p, vec2(0.34, 0.485), vec2(0.60, 0.535)) * (state < 0.5 ? 1.0 : 0.0));
	}

	// A SCREEN IS NEVER QUITE STILL, and it is the same breathe and the same
	// redraw dip people.gd gives the glass behind this, on the same kind of
	// per-desk phase, so a room blinks here and there rather than in time with
	// itself.
	float u2 = fract(TIME * 0.071 + phase * 1.7);
	float blip = 1.0 - 0.22 * smoothstep(0.0, 0.015, u2) * (1.0 - smoothstep(0.015, 0.07, u2));
	ALBEDO = col * blip * (1.0 + 0.03 * sin(TIME * 0.8 + phase * 6.283));
}
"""

var _mesh: QuadMesh = null
var _mat: ShaderMaterial = null
var _mm := {}                    # floor -> MultiMeshInstance3D
var _hidden := {}                # desk device name -> true, while it is live


func _make() -> void:
	if _mesh != null:
		return
	var r := _glass()
	_mesh = QuadMesh.new()
	_mesh.size = Vector2(float(r.get("w", 0.42)), float(r.get("h", 0.28)))
	var sh := Shader.new()
	sh.code = SCREEN_SHADER
	_mat = ShaderMaterial.new()
	_mat.shader = sh
	_mesh.material = _mat


func _row(f: int) -> MultiMeshInstance3D:
	if _mm.has(f):
		return _mm[f]
	var mmi := MultiMeshInstance3D.new()
	mmi.name = "f%d_screens" % f
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.use_custom_data = true
	# WHITE IN THE COLOUR SLOT. The compatibility renderer multiplies the
	# instance colour into the fragment whether the buffer holds one or not;
	# people.gd photographed a floor of black silhouettes learning that.
	mm.use_colors = true
	mm.mesh = _mesh
	mm.instance_count = 0
	mmi.multimesh = mm
	add_child(mmi)
	_mm[f] = mmi
	return mmi


# THE ONE ENTRY POINT. `screens` is
# [{pos, yaw, floor, trade, state, done, who, dev}, ...] -- one per desk device
# the model says is there, exactly the list people.gd is given, and after this
# call the building holds those screens and no others.
#
# `hide` is the desk that is currently SAT AT, if any: its glass is a live
# viewport put there by tower.gd, and a depiction painted over the top of the
# real thing would be the worst lie in the file.
func rebuild(screens: Array, hide := "") -> void:
	_make()
	var r := _glass()
	if r.is_empty():
		return
	var mid: Vector3 = r.mid
	var want := {}
	_counts = [0, 0, 0]
	for s in screens:
		if hide != "" and str(s.get("dev", "")) == hide:
			continue
		var f: int = int(s.get("floor", 0))
		var yaw: float = float(s.get("yaw", 0.0))
		# The glass is given in the desk's own frame, so it is turned with the
		# desk. The quad faces +z, which is the way the monitor faces: at the
		# person in the chair.
		var t := Transform3D(Basis(Vector3.UP, yaw),
			Vector3(s.get("pos", Vector3.ZERO)) + _rot_xz(mid, yaw))
		if not want.has(f):
			want[f] = []
		want[f].append([t, _look(s)])
		_counts[clampi(int(s.get("state", S_NOLINK)), 0, 2)] += 1
	for f in _mm.keys():
		if not want.has(f):
			_fill(_mm[f], [])
	for f in want.keys():
		_fill(_row(f), want[f])


# The four numbers a screen is drawn from, and every one of them is read off
# the model by tower.gd before it gets here. The phase is a hash of who sits
# there -- the same name people.gd dresses them from -- so a room does not
# animate in lockstep and the same tower always blinks the same way.
func _look(s: Dictionary) -> Color:
	var P = preload("res://scripts/people.gd")
	var ph := float(P.name_hash(str(s.get("who", s.get("dev", "")))) & 1023) / 1024.0
	return Color(float(int(s.get("trade", T_OFFICE))),
		float(int(s.get("state", S_NOLINK))),
		clampf(float(s.get("done", 0.0)), 0.0, 1.0), ph)


func _fill(mmi: MultiMeshInstance3D, xs: Array) -> void:
	var mm: MultiMesh = mmi.multimesh
	mm.instance_count = xs.size()
	for i in range(xs.size()):
		mm.set_instance_transform(i, xs[i][0])
		mm.set_instance_color(i, Color(1, 1, 1))
		mm.set_instance_custom_data(i, xs[i][1])
	mmi.visible = xs.size() > 0


# The same right-angle-safe rotation tower.gd uses to put a point in a desk's
# own frame into the building's.
static func _rot_xz(v: Vector3, yaw: float) -> Vector3:
	var s := sin(yaw)
	var c := cos(yaw)
	return Vector3(snappedf(v.x * c + v.z * s, 0.001), v.y,
		snappedf(-v.x * s + v.z * c, 0.001))


# How many screens are lit, by state -- what a test reads, because there are no
# nodes to count.
#
# TALLIED WHEN THEY ARE BUILT, not read back out of the instance buffer.
# get_instance_custom_data() comes back as zeros under the dummy renderer a
# --headless gate runs on, so a test that read the buffer was told every screen
# in the building was showing the link error and believed it. What is counted
# here is what rebuild() was given, which is the thing the check is about.
var _counts := [0, 0, 0]

func counts() -> Array:
	return _counts.duplicate()


func total() -> int:
	var n := 0
	for f in _mm.keys():
		n += int((_mm[f] as MultiMeshInstance3D).multimesh.instance_count)
	return n


func buffers() -> int:
	var n := 0
	for f in _mm.keys():
		if (_mm[f] as MultiMeshInstance3D).visible:
			n += 1
	return n


# Where the glass is in the desk's own frame, for tower.gd's live viewport and
# for the test that checks the two agree.
func glass_rect() -> Dictionary:
	_make()
	return _glass()
