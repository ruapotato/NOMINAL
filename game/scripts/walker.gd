# walker.gd — a person, in the building.
#
# 1.75 m tall, eye at 1.62, and it walks: no flying, no clipping, no noclip
# debug key. If the geometry does not let you get from the lobby to a comms
# cupboard on foot then the geometry is wrong, and a camera that ignores walls
# would hide exactly that.
#
# The input is read directly off the keyboard rather than through named
# actions, because the project has no input map and adding one changes a file
# the 2D desktop also loads.

extends CharacterBody3D

# TUNABLE FROM THE EDITOR, not from this file.
#
# David: "Maybe the player mesh is too big?... Seems like something that could
# just be a scene file. Allow me to edit it."
#
# He was right to ask. These were consts, so the only way to change how the
# player moves was to edit GDScript -- and the reason he could not jump was one
# of them: JUMP was 4.2 m/s against a GRAVITY of 12, which is an apex of
# v^2/2g = 0.73 m. A knee-high hop, which inside a 4.75 m deck reads as not
# leaving the ground at all. I scaled the ship fourfold chasing that number.
#
# They are exported now and live in scenes/player.tscn, so the capsule, the
# eye height, the speed and the jump are all things to try rather than things
# to ask for.
@export var SPEED := 3.6
@export var RUN := 6.0
@export var GRAVITY := 12.0
@export var JUMP := 6.5          # apex = JUMP^2 / (2*GRAVITY) metres
@export var EYE := 1.62
@export var CAP_HEIGHT := 1.75
@export var CAP_RADIUS := 0.28
@export var FOV := 75.0
const MOUSE := 0.0022

var cam: Camera3D
var yaw := 0.0
var pitch := 0.0
var look_free := false      # true = mouse captured

# Headless drive: a test sets these instead of touching a keyboard.
var drive := Vector2.ZERO   # x = strafe, y = forward
var drive_active := false

# THE LADDER, WHICH IS NOT A SLOPE.
#
# The riser got a ladder because the owner asked for one -- "with a ladder so
# you can actually climb up and down" -- and the first attempt drew it the way
# the stairs are drawn: rungs you can see, and an invisible incline behind
# them that a capsule really walks up. That works at 30 degrees and cannot
# work here. A ladder is 81 degrees in this building and floor_max_angle is
# 50; the body slid straight back down it, and the physics test said so:
# "walked at the riser ladder from y = 0.43 and got to y = -0.00".
#
# So a ladder is a MODE, the way it is in every game that has one. While the
# body is in the volume of a ladder, gravity is off and forward means up.
# tower.gd owns where the ladders are -- it is the thing that drew them -- and
# this only asks.
const CLIMB := 2.2          # metres a second, and a ladder is slower than a walk
var tower: Node3D = null
var on_ladder := false


func _ready() -> void:
	# BUILT HERE ONLY IF THE SCENE DID NOT BRING ONE. scenes/player.tscn has a
	# capsule and a camera in it that can be dragged about in the editor; this
	# is the fallback for the tests and tools that instantiate the script on
	# its own.
	var cs: CollisionShape3D = get_node_or_null("Collider")
	if cs == null:
		cs = CollisionShape3D.new()
		cs.name = "Collider"
		var cap := CapsuleShape3D.new()
		cap.height = CAP_HEIGHT
		cap.radius = CAP_RADIUS
		cs.shape = cap
		cs.position = Vector3(0, CAP_HEIGHT * 0.5, 0)
		add_child(cs)

	cam = get_node_or_null("Camera3D")
	if cam == null:
		cam = Camera3D.new()
		cam.name = "Camera3D"
		add_child(cam)
	cam.position = Vector3(0, EYE, 0)
	cam.fov = FOV
	cam.near = 0.05
	cam.far = 4000.0
	cam.current = true
	floor_max_angle = deg_to_rad(50.0)
	floor_snap_length = 0.4


func capture(on: bool) -> void:
	look_free = on
	Input.set_mouse_mode(Input.MOUSE_MODE_CAPTURED if on else Input.MOUSE_MODE_VISIBLE)


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and look_free:
		yaw -= event.relative.x * MOUSE
		pitch = clamp(pitch - event.relative.y * MOUSE, -1.45, 1.45)
		rotation.y = yaw
		cam.rotation.x = pitch
	elif event is InputEventMouseButton and event.pressed and not look_free:
		capture(true)
	elif event is InputEventKey and event.pressed and event.keycode == KEY_ESCAPE:
		capture(false)


func look_at_yaw(a: float) -> void:
	yaw = a
	rotation.y = a


func _physics_process(delta: float) -> void:
	var input := drive
	if not drive_active:
		input = Vector2(
			(1.0 if Input.is_key_pressed(KEY_D) else 0.0) - (1.0 if Input.is_key_pressed(KEY_A) else 0.0),
			(1.0 if Input.is_key_pressed(KEY_W) else 0.0) - (1.0 if Input.is_key_pressed(KEY_S) else 0.0))
	var speed := RUN if (not drive_active and Input.is_key_pressed(KEY_SHIFT)) else SPEED
	var dir := (transform.basis * Vector3(input.x, 0, -input.y))
	dir.y = 0
	if dir.length() > 0.001:
		dir = dir.normalized()
	on_ladder = tower != null and tower.has_method("on_ladder") \
		and tower.on_ladder(global_position)
	# ONLY WHILE YOU ARE PULLING YOURSELF UP IT. Standing in a riser is
	# standing in a room -- the slab is under you and gravity is normal. It is
	# holding forward against the rungs that lifts you, and letting go puts
	# you back on the floor rather than leaving you hanging in the shaft.
	if on_ladder and absf(input.y) > 0.01:
		velocity.x = dir.x * speed * 0.4
		velocity.z = dir.z * speed * 0.4
		velocity.y = input.y * CLIMB
		move_and_slide()
		return
	velocity.x = dir.x * speed
	velocity.z = dir.z * speed
	if not is_on_floor():
		velocity.y -= GRAVITY * delta
	else:
		velocity.y = -0.1
		if not drive_active and Input.is_key_pressed(KEY_SPACE):
			velocity.y = JUMP
	move_and_slide()
