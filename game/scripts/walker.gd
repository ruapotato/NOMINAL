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

const SPEED := 3.6
const RUN := 6.0
const GRAVITY := 12.0
const EYE := 1.62
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
	var cs := CollisionShape3D.new()
	var cap := CapsuleShape3D.new()
	cap.height = 1.75
	cap.radius = 0.28
	cs.shape = cap
	cs.position = Vector3(0, 0.875, 0)
	add_child(cs)

	cam = Camera3D.new()
	cam.position = Vector3(0, EYE, 0)
	cam.fov = 75.0
	cam.near = 0.05
	cam.far = 400.0
	add_child(cam)
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
			velocity.y = 4.2
	move_and_slide()
