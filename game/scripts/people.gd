# people.gd — the desks a tenancy rents, and the person sitting at each one.
#
# The owner: "let's also add in the virtual people to actually be in their
# office at a computer desk similar to the server room, where if you felt like
# it you could go over to their desk and see what issues they're complaining
# about... basically let's make the world feel alive."
#
# NOTHING IN THIS FILE KNOWS ANYTHING. It is handed a list of seats -- a point,
# a yaw and a mood -- and it draws them. Where a desk is comes out of the same
# `_tenant_desk_slot()` that puts the tenant's computer there, which comes out
# of site_devs(); whether somebody is unhappy comes out of `service`'s own
# columns. A person cannot exist here that the model does not have a desk for,
# because there is no list of people anywhere: rebuild() is given one and
# throws the last one away.
#
# AND IT IS ONE MULTIMESH PER POSE PER FLOOR. A full tower is 176 desks; 176
# nodes with a mesh and a collider each is what the racks already cost and it
# is not what a crowd should cost. Five meshes are built once -- the desk and
# four poses -- and every person in the building is an entry in a transform
# buffer that shares them.
#
# PER FLOOR, and that part is measured rather than tidy. One buffer for the
# whole tower has one AABB the size of the tower, and a MultiMesh is culled by
# its buffer: standing in the MDF with eighty desks four floors above cost
# 12.8 ms a frame of geometry nobody could see. Split by floor, the floors you
# are not on fall out of the frustum and the same view costs 0.4 ms. It is the
# same five draw calls per floor either way.
#
# No collision on any of it, deliberately. A person is not a wall: the
# doorways, the aisles and the rack fronts have to stay walkable, and the
# cheapest way to guarantee that is geometry that has no body in it at all.
# game/tests/tower.gd checks the seats against the doorways anyway, because
# "you cannot walk into it" is not the same as "it is not standing in the way".
#
# ---------------------------------------------------------------------------
# AND THEY ARE NOT ALL THE SAME PERSON. A playtester walked a floor of these
# and said the two halves of it exactly:
#
#   "it reads brilliantly from the doorway -- you know the floor is dead
#    before you read a word... but inhabited AS A DIORAMA: identical shirts,
#    identical pose, no motion, no idle. A single desk lamp flickering or one
#    head turning would take it from populated to occupied."
#
# So: WHO SITS THERE DECIDES WHAT THEY LOOK LIKE, and nothing here rolls a
# die. core/session.c already names every desk -- `desks 1` prints "Ola
# Jelinek" beside t1d4 and prints the same name every time, because it is a
# hash of the seed and the device -- so this file hashes THAT NAME and spends
# it on hair, skin, trousers, the shirt's shade and the phase of their idle.
# Same seed, same tower, same people, same faces, with no random state
# anywhere that a screenshot could disagree with a second later. Two people
# who really are called the same thing really do look alike, which is the
# honest consequence of deriving it and not a bug.
#
# THE SHIRT'S HUE IS STILL THE MOOD, and the variation is deliberately
# clamped inside it. The one thing that already works is that a striking
# floor reads as struck from the doorway, so a person's own colour moves the
# shade and the saturation and barely moves the hue: twenty different blues
# in a working office, twenty different reds in a bad one, and no red in the
# blue room.
#
# AND IT IS STILL ONE MESH PER POSE. All of that arrives as MultiMesh custom
# data -- four floats an instance -- and is spent in a vertex shader: the
# shirt colour whole in .rgb, and hair/skin/trousers/height/phase packed into
# .a as an integer plus a fraction. No extra mesh, no extra draw call, no
# extra triangle. The mesh's UVs are not texture coordinates: they say WHICH
# PART OF A PERSON each vertex belongs to (u) and WHAT MOVES IT (v), which is
# what lets one shared mesh be recoloured and animated per instance.
#
# THE MOTION IS THE PLAYTESTER'S OWN BAR AND NOT A FRAME MORE. Heads turn --
# each person on their own phase, once every twenty seconds or so, hold, and
# back -- shoulders breathe a centimetre, a raised hand drifts, and the
# monitors flicker as a screen redraws. It is all vertex arithmetic on
# geometry that was being drawn anyway: no _process, no tweens, no nodes, no
# physics, and nothing the CPU does per frame per person.

extends Node3D

# The moods, which are not invented here either -- see tower.gd's _seats().
const M_WORKING := 0        # this desk has an address: somebody is doing a day's work
const M_WAITING := 1        # no address on it, and the tenancy is not striking yet
const M_WAITING_BAD := 2    # no address, and it has cost them days
const M_SLUMPED := 3        # addressed, and the tenancy is striking anyway
const NMOOD := 4

# SKIN, HAIR AND TROUSERS ARE NOT CONSTANTS ANY MORE: they are four tones,
# four tones and three, chosen per person in the shader from the hash of their
# name. Anything wearing one of them is built WHITE so that the vertex colour
# carries the face's shading alone and the shader multiplies the person into
# it. The numbers themselves are in CROWD_SHADER, which is the only thing that
# can use them.
const WHITE := Color(1, 1, 1)
const SHOE := Color("#23262b")
const CHAIR := Color("#3a4048")
const CHAIR_DK := Color("#2b2f35")
# Blue is a day's work. Amber is the colour the door beacon already uses for a
# tenancy waiting on you, and red the one it uses when they have gone without.
const SHIRT := [Color("#5f7f9e"), Color("#d9a23c"), Color("#c2543c"), Color("#c2543c")]

const DESK_TOP := Color("#9c8f79")
const DESK_LEG := Color("#5b6068")
const MON_SHELL := Color("#1b1e22")
const MON_GLASS := Color("#12333f")

const H := 0.72             # the desk top

# Where the tenant's computer goes, in the desk's own frame: under the top, on
# the left, its back to the wall side so the patch lead rises BEHIND the desk
# instead of through it. tower.gd puts the device box here; the numbers live in
# this file because they are part of the same piece of furniture.
const BOX_X0 := -0.38
const BOX_X1 := 0.02
const BOX_Z0 := -0.42
const BOX_Z1 := -0.02
const BOX_H := 0.42

const SEAT_X := 0.12        # the chair, and everything above it, sits right of the box

# ------------------------------------------------- what a vertex belongs to
#
# UV.x: which part, and therefore whose colour it takes. A part that is
# tinted per person is built in WHITE, so its vertex colour carries only the
# face's shading and the shader multiplies the person's own colour into it.
const P_FIXED := 0.0        # the chair, the shoes, the desk: everybody's is the same
const P_SHIRT := 1.0
const P_SKIN := 2.0
const P_HAIR := 3.0
const P_TROUSER := 4.0
const P_SCREEN := 5.0       # the desk's monitor glass, which flickers
# UV.y: what moves it.
const G_STILL := 0.0
const G_HEAD := 1.0         # turns about the neck, and breathes
const G_ARM := 2.0          # the raised hand, which drifts

# Where the head turns about: the neck, in the person's own frame.
const NECK_Z := 0.36

# One shader for the crowd and the furniture both. It is UNSHADED like
# everything else in this building, so ALBEDO is the vertex colour with the
# person's own colour multiplied into it -- and the whole of the idle is here,
# in the vertex stage, because that is the only place a shared mesh can move
# differently for each of the people using it.
const CROWD_SHADER := """
shader_type spatial;
render_mode unshaded, cull_back, shadows_disabled, specular_disabled;

uniform float head_amp = 0.0;      // radians of head turn, by mood
uniform float arm_amp = 0.0;       // radians of drift in a raised arm
uniform float breathe = 0.0;       // metres the shoulders rise and fall
uniform float screen_amp = 0.0;    // how much a monitor flickers
uniform float pivot_x = 0.12;
uniform float pivot_z = 0.36;

varying vec3 vcol;

void vertex() {
	float part = UV.x;
	float grp = UV.y;
	// .a is an integer of packed choices plus a fraction of phase.
	float packed = INSTANCE_CUSTOM.a;
	float idx = floor(packed);
	float phase = packed - idx;
	float hair_i = mod(idx, 4.0);
	float skin_i = mod(floor(idx / 4.0), 4.0);
	float tall_i = mod(floor(idx / 16.0), 5.0);
	float trou_i = mod(floor(idx / 80.0), 3.0);

	vec3 tint = vec3(1.0);
	if (part > 0.5 && part < 1.5) {
		tint = INSTANCE_CUSTOM.rgb;                       // the shirt
	} else if (part > 1.5 && part < 2.5) {
		tint = vec3(0.776, 0.604, 0.463) * (0.62 + 0.17 * skin_i);
	} else if (part > 2.5 && part < 3.5) {
		tint = mix(vec3(0.10, 0.08, 0.07), vec3(0.62, 0.55, 0.47),
			hair_i / 3.0);
	} else if (part > 3.5 && part < 4.5) {
		if (trou_i < 0.5) {
			tint = vec3(0.228, 0.251, 0.282);             // charcoal
		} else if (trou_i < 1.5) {
			tint = vec3(0.180, 0.212, 0.310);             // navy
		} else {
			tint = vec3(0.310, 0.263, 0.208);             // brown
		}
	} else if (part > 4.5) {
		// A SCREEN IS NEVER QUITE STILL. A slow breathe, and every so often
		// the short dip of a redraw -- on this desk's own phase, so the room
		// blinks here and there instead of in time with itself.
		float u = fract(TIME * 0.071 + phase * 1.7);
		float blip = 1.0 - 0.30 * smoothstep(0.0, 0.015, u)
			* (1.0 - smoothstep(0.015, 0.07, u));
		tint = vec3(1.0 + screen_amp * (0.5 * sin(TIME * 0.8 + phase * 6.283)
			+ (blip - 1.0) * 3.0));
	}
	vcol = COLOR.rgb * tint;

	if (grp > 0.5 && grp < 1.5) {
		// A HEAD TURNS, ONCE IN A WHILE. Away over a second, held for four,
		// and back: on a twenty-second cycle offset per person, so at any
		// moment one or two heads in a room of twenty are moving and the
		// rest are not.
		float u = fract(TIME * 0.05 + phase);
		float turn = smoothstep(0.0, 0.06, u) - smoothstep(0.26, 0.34, u);
		float dir = fract(phase * 8.0) < 0.5 ? -1.0 : 1.0;
		float a = head_amp * turn * dir;
		float ca = cos(a);
		float sa = sin(a);
		vec2 d = VERTEX.xz - vec2(pivot_x, pivot_z);
		VERTEX.xz = vec2(pivot_x, pivot_z)
			+ vec2(ca * d.x + sa * d.y, ca * d.y - sa * d.x);
		VERTEX.y += (tall_i - 2.0) * 0.007
			+ breathe * sin(TIME * 0.7 + phase * 6.283);
	} else if (grp > 1.5) {
		// A RAISED HAND DOES NOT HANG THERE PERFECTLY STILL.
		float a = arm_amp * sin(TIME * 0.55 + phase * 6.283);
		float ca = cos(a);
		float sa = sin(a);
		vec2 d = VERTEX.xy - vec2(pivot_x, 0.90);
		VERTEX.xy = vec2(pivot_x, 0.90)
			+ vec2(ca * d.x - sa * d.y, sa * d.x + ca * d.y);
	}
}

void fragment() {
	ALBEDO = vcol;
}
"""

# HOW MUCH EACH MOOD MOVES, and it is not the same amount. A working floor is
# busy; a floor that cannot work is waiting, and the hand that says so drifts;
# a floor that has been failed for days is bent over its desks and has stopped
# looking up. The idle carries the same signal the colour does rather than
# arguing with it.
const IDLE := [
	{"head": 0.30, "arm": 0.0, "breathe": 0.004},    # M_WORKING
	{"head": 0.16, "arm": 0.055, "breathe": 0.005},  # M_WAITING
	{"head": 0.12, "arm": 0.045, "breathe": 0.005},  # M_WAITING_BAD
	{"head": 0.0, "arm": 0.0, "breathe": 0.008},     # M_SLUMPED
]

var _mesh: Array = []       # the five meshes, built once and shared
var _mm := {}               # floor -> [MultiMeshInstance3D, one per mesh]


func _ready() -> void:
	_make()


func _make() -> void:
	if not _mesh.is_empty():
		return
	_mesh.append(_desk_mesh())
	for m in range(NMOOD):
		_mesh.append(_person_mesh(m))


func _floor_row(f: int) -> Array:
	if _mm.has(f):
		return _mm[f]
	var row: Array = []
	for i in range(_mesh.size()):
		var mmi := MultiMeshInstance3D.new()
		mmi.name = "f%d_%s" % [f, "desks" if i == 0 else "mood%d" % (i - 1)]
		var mm := MultiMesh.new()
		mm.transform_format = MultiMesh.TRANSFORM_3D
		# WHO EACH OF THEM IS, four floats at a time. It has to be asked for
		# before there are any instances to put it on.
		mm.use_custom_data = true
		# AND A WHITE IN THE COLOUR SLOT, WHICH IS NOT DECORATION. The
		# compatibility renderer multiplies an instance colour into the vertex
		# colour whether the buffer holds one or not: with `use_colors` off it
		# multiplies by what is in the slot, which is nothing, and a floor of
		# people comes out as black silhouettes. Photographed, that.
		mm.use_colors = true
		mm.mesh = _mesh[i]
		mm.instance_count = 0
		mmi.multimesh = mm
		add_child(mmi)
		row.append(mmi)
	_mm[f] = row
	return row


# THE ONE ENTRY POINT. `seats` is [{pos, yaw, mood, floor, who}, ...], and
# after this call the world holds exactly those people and no others. `who` is
# the name core prints for that desk; a seat without one falls back to the
# desk's own name, so a person always looks like something and always looks
# like the SAME something.
func rebuild(seats: Array) -> void:
	_make()
	var want := {}
	for s in seats:
		var f: int = int(s.get("floor", 0))
		var m: int = clampi(int(s.get("mood", 0)), 0, NMOOD - 1)
		var t := Transform3D(Basis(Vector3.UP, float(s.get("yaw", 0.0))),
			s.get("pos", Vector3.ZERO))
		var look := appearance(str(s.get("who", "")), m)
		if not want.has(f):
			var lists: Array = []
			for i in range(_mesh.size()):
				lists.append([])
			want[f] = lists
		want[f][0].append([t, look])
		want[f][1 + m].append([t, look])
	for f in _mm.keys():
		if not want.has(f):
			for mmi in _mm[f]:
				_fill(mmi, [])
	for f in want.keys():
		var row := _floor_row(f)
		for i in range(_mesh.size()):
			_fill(row[i], want[f][i])


func _fill(mmi: MultiMeshInstance3D, xs: Array) -> void:
	var mm: MultiMesh = mmi.multimesh
	mm.instance_count = xs.size()
	for i in range(xs.size()):
		mm.set_instance_transform(i, xs[i][0])
		mm.set_instance_color(i, Color(1, 1, 1))
		mm.set_instance_custom_data(i, xs[i][1])
	mmi.visible = xs.size() > 0


# --------------------------------------------------- WHO THAT PERSON IS
#
# A name in, four floats out, and the same four floats for the same name for
# ever. There is no seeded RNG here on purpose: an RNG has state, the state
# depends on the order rebuild() happened to walk the seats in, and the day
# somebody adds a tenancy the whole building changes its clothes. A hash of
# the name cannot do that.
#
# .rgb is the shirt, which is the MOOD's colour with this person's own shade
# of it. .a is hair, skin, trousers and height packed as an integer, plus the
# phase of their idle as the fraction -- 240 + 0.98 at the very most, which a
# 32-bit float carries with room to spare.
static func appearance(who: String, mood: int) -> Color:
	var h := name_hash(who)
	var hair := (h >> 3) & 3
	var skin := ((h >> 7) & 255) % 4
	var tall := ((h >> 13) & 255) % 5
	var trou := ((h >> 19) & 255) % 3
	var phase := float((h >> 5) & 1023) / 1024.0
	var idx := hair + 4 * skin + 16 * tall + 80 * trou
	var base: Color = SHIRT[clampi(mood, 0, SHIRT.size() - 1)]
	# HOW FAR THE HUE MAY MOVE, and it is not much. Amber and red are two
	# tenths of a turn apart and they are the doorway's warning; blue has the
	# room to itself and gets a little more, so a working office is twenty
	# shirts between slate and teal and never anything warm.
	var spread := 0.055 if mood == M_WORKING else 0.016
	var jh := (float((h >> 11) & 63) / 63.0 - 0.5) * 2.0 * spread
	var js := float((h >> 17) & 63) / 63.0
	var jv := float((h >> 23) & 63) / 63.0
	var c := Color.from_hsv(fposmod(base.h + jh, 1.0),
		clampf(base.s * (0.80 + 0.36 * js), 0.0, 1.0),
		clampf(base.v * (0.84 + 0.28 * jv), 0.0, 1.0))
	return Color(c.r, c.g, c.b, float(idx) + phase * 0.98)


# FNV-1a, because it is four lines and every bit of it is spread. Any decent
# hash would do; what matters is that it is a pure function of the name.
static func name_hash(s: String) -> int:
	var h := 2166136261
	if s == "":
		s = "nobody"
	for b in s.to_utf8_buffer():
		h = ((h ^ int(b)) * 16777619) & 0xffffffff
	return h


# How many people are drawn, by mood -- what a test reads instead of counting
# nodes, because there are no nodes to count.
func counts() -> Array:
	var out: Array = []
	for m in range(NMOOD):
		var n := 0
		for f in _mm.keys():
			n += int((_mm[f][1 + m] as MultiMeshInstance3D).multimesh.instance_count)
		out.append(n)
	return out


func total() -> int:
	var n := 0
	for f in _mm.keys():
		n += int((_mm[f][0] as MultiMeshInstance3D).multimesh.instance_count)
	return n


# How many instance buffers are live, which is the draw-call count the crowd
# adds when every floor of the tower is in the frustum at once.
func buffers() -> int:
	var n := 0
	for f in _mm.keys():
		for mmi in _mm[f]:
			if (mmi as MultiMeshInstance3D).visible:
				n += 1
	return n


# ---------------------------------------------------------------- the desk
#
# A metre of desk in the middle of a square metre of floor, the same silhouette
# as the workstation in the MDF at the scale a hot desk really is: a top, two
# gable ends, a monitor with the screen towards the chair, a keyboard and a
# mouse. The tenant's computer stands under it, which is why the left half is
# clear.
func _desk_mesh() -> ArrayMesh:
	var g = preload("res://scripts/vgeo.gd").new()
	g.tagging = true
	_box(g, -0.47, 0.47, -0.40, 0.14, H - 0.04, H, DESK_TOP)
	for x0 in [-0.47, 0.41]:
		_box(g, x0, x0 + 0.06, -0.36, 0.10, 0.0, H - 0.04, DESK_LEG)
	# monitor: base, stem, shell, and the glass a shade proud of it
	var mx := SEAT_X
	_box(g, mx - 0.11, mx + 0.11, -0.36, -0.22, H, H + 0.02, MON_SHELL)
	_box(g, mx - 0.03, mx + 0.03, -0.32, -0.27, H + 0.02, H + 0.18, MON_SHELL)
	_box(g, mx - 0.23, mx + 0.23, -0.33, -0.28, H + 0.18, H + 0.50, MON_SHELL)
	# THE ONE THING IN THE ROOM THAT IS NOT STILL. The glass is tagged, so
	# each desk's screen breathes and blinks on its own phase -- the
	# playtester's "single desk lamp flickering", except that a monitor is
	# what an office really has and it is already drawn.
	_box(g, mx - 0.21, mx + 0.21, -0.279, -0.273, H + 0.20, H + 0.48, MON_GLASS,
		P_SCREEN)
	_box(g, mx + 0.18, mx + 0.20, -0.277, -0.271, H + 0.185, H + 0.195, Color("#7fe08a"))
	_box(g, mx - 0.17, mx + 0.17, -0.10, 0.04, H, H + 0.022, Color("#d5d2c8"))
	_box(g, mx + 0.23, mx + 0.30, -0.08, 0.03, H, H + 0.03, Color("#c8c5bc"))
	var m := g.mesh()
	_dress(m, {"screen_amp": 0.35})
	return m


# --------------------------------------------------------------- the person
#
# Seated, facing the screen, in the same vertex-coloured boxes everything else
# in this building is made of. The three poses are the same body with different
# arms and a different neck, so a room of them reads as one workforce.
# AND EVERY BOX OF IT SAYS WHAT IT IS. The `part` argument is what colour it
# takes -- a shirt, skin, hair or trousers is the person's own and is built in
# white so the shader can multiply it -- and `grp` is what moves it.
func _person_mesh(mood: int) -> ArrayMesh:
	var g = preload("res://scripts/vgeo.gd").new()
	g.tagging = true
	var s := SEAT_X
	# the chair, which is the same chair whatever kind of day they are having
	_box(g, s - 0.22, s + 0.22, 0.20, 0.62, 0.42, 0.48, CHAIR)
	_box(g, s - 0.21, s + 0.21, 0.56, 0.60, 0.48, 0.98, CHAIR)
	_box(g, s - 0.04, s + 0.04, 0.37, 0.45, 0.06, 0.42, CHAIR_DK)
	_box(g, s - 0.24, s + 0.24, 0.39, 0.43, 0.02, 0.06, CHAIR_DK)
	_box(g, s - 0.02, s + 0.02, 0.17, 0.65, 0.02, 0.06, CHAIR_DK)
	# legs: thighs along the seat, shins down to the floor, shoes on the end
	_box(g, s - 0.16, s + 0.16, 0.12, 0.44, 0.46, 0.56, WHITE, P_TROUSER)
	for x0 in [s - 0.15, s + 0.03]:
		_box(g, x0, x0 + 0.12, 0.12, 0.24, 0.06, 0.50, WHITE, P_TROUSER)
		_box(g, x0, x0 + 0.12, 0.02, 0.18, 0.0, 0.07, SHOE)

	if mood == M_SLUMPED:
		# HEAD IN HANDS, ELBOWS ON THE DESK. Nothing floats above this office
		# and no icon says so: the room is bent over its desks, and that is
		# what a bad week looks like from the doorway.
		_box(g, s - 0.17, s + 0.17, 0.16, 0.44, 0.46, 0.90, WHITE, P_SHIRT)
		_box(g, s - 0.05, s + 0.05, 0.10, 0.20, 0.86, 0.94, WHITE, P_SKIN, G_HEAD)
		_box(g, s - 0.10, s + 0.10, -0.02, 0.16, 0.88, 1.08, WHITE, P_SKIN, G_HEAD)
		_box(g, s - 0.105, s + 0.105, -0.025, 0.165, 1.03, 1.11, WHITE, P_HAIR, G_HEAD)
		for x0 in [s - 0.26, s + 0.16]:
			# upper arm forward along the desk, forearm up to the head
			_box(g, x0, x0 + 0.10, 0.10, 0.40, H + 0.02, H + 0.10, WHITE, P_SHIRT)
			_box(g, x0 + 0.01, x0 + 0.09, 0.06, 0.16, H + 0.02, 0.98, WHITE, P_SHIRT)
			_box(g, x0 + 0.01, x0 + 0.09, 0.02, 0.14, 0.98, 1.10, WHITE, P_SKIN)
		return _dressed(g, mood)

	# upright: torso, neck, head, hair. The neck goes with the head, so a head
	# that turns or sits a centimetre higher takes its neck with it and never
	# opens a gap at the collar.
	_box(g, s - 0.17, s + 0.17, 0.24, 0.46, 0.46, 1.02, WHITE, P_SHIRT)
	_box(g, s - 0.05, s + 0.05, 0.32, 0.40, 1.00, 1.09, WHITE, P_SKIN, G_HEAD)
	_box(g, s - 0.10, s + 0.10, 0.26, 0.45, 1.09, 1.31, WHITE, P_SKIN, G_HEAD)
	_box(g, s - 0.105, s + 0.105, 0.255, 0.455, 1.25, 1.33, WHITE, P_HAIR, G_HEAD)
	# left arm on the keyboard in every upright pose
	_box(g, s - 0.25, s + 0.25, 0.30, 0.42, 0.86, 0.98, WHITE, P_SHIRT)  # shoulders
	_box(g, s - 0.25, s - 0.17, 0.30, 0.42, 0.74, 0.90, WHITE, P_SHIRT)
	_box(g, s - 0.24, s - 0.16, -0.04, 0.34, H + 0.02, H + 0.10, WHITE, P_SHIRT)
	_box(g, s - 0.23, s - 0.15, -0.10, -0.02, H + 0.02, H + 0.09, WHITE, P_SKIN)
	if mood == M_WORKING:
		_box(g, s + 0.17, s + 0.25, 0.30, 0.42, 0.74, 0.90, WHITE, P_SHIRT)
		_box(g, s + 0.16, s + 0.24, -0.04, 0.34, H + 0.02, H + 0.10, WHITE, P_SHIRT)
		_box(g, s + 0.15, s + 0.23, -0.10, -0.02, H + 0.02, H + 0.09, WHITE, P_SKIN)
		return _dressed(g, mood)
	# A HAND UP. The one gesture that is legible across a room and in a
	# screenshot, and it means exactly what it means: this desk cannot work
	# and somebody is waiting for the IT department.
	_box(g, s + 0.17, s + 0.25, 0.30, 0.42, 0.78, 1.00, WHITE, P_SHIRT, G_ARM)
	_box(g, s + 0.17, s + 0.25, 0.30, 0.40, 1.00, 1.58, WHITE, P_SHIRT, G_ARM)
	_box(g, s + 0.16, s + 0.26, 0.29, 0.41, 1.58, 1.76, WHITE, P_SKIN, G_ARM)
	return _dressed(g, mood)


func _dressed(g, mood: int) -> ArrayMesh:
	var m: ArrayMesh = g.mesh()
	var idle: Dictionary = IDLE[clampi(mood, 0, IDLE.size() - 1)]
	_dress(m, {"head_amp": idle.head, "arm_amp": idle.arm,
		"breathe": idle.breathe})
	return m


# The crowd shader over the top of vgeo's plain material, with this mesh's own
# idle in it. One material per mesh, five in the building, whatever a floor of
# people costs.
func _dress(m: ArrayMesh, uni: Dictionary) -> void:
	var sh := Shader.new()
	sh.code = CROWD_SHADER
	var mat := ShaderMaterial.new()
	mat.shader = sh
	mat.set_shader_parameter("pivot_x", SEAT_X)
	mat.set_shader_parameter("pivot_z", NECK_Z)
	for k in uni.keys():
		mat.set_shader_parameter(str(k), float(uni[k]))
	for i in range(m.get_surface_count()):
		m.surface_set_material(i, mat)


func _box(g, x0: float, x1: float, z0: float, z1: float, y0: float, y1: float,
		col: Color, part := P_FIXED, grp := G_STILL) -> void:
	g.tag = Vector2(part, grp)
	g.box(Vector3(x0, y0, z0), Vector3(x1 - x0, y1 - y0, z1 - z0), col, false)
