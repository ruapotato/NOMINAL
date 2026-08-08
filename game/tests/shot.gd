extends SceneTree
# Boot the ship and photograph a few rooms. Two seconds.
func _init() -> void:
	var t = load("res://scenes/ship.tscn").instantiate()
	get_root().add_child(t)
	for i in range(4):
		await process_frame
	var dir := "/tmp/shot"
	for a in OS.get_cmdline_user_args():
		dir = a
	var cam := Camera3D.new()
	get_root().add_child(cam)
	cam.current = true
	cam.fov = 75.0
	var want := ["MainEngineering", "Spine", "Bridge", "ShuttleBay", "SensorArray"]
	var n := 0
	for w in want:
		for r in t.rooms:
			if String(r.name) != w:
				continue
			var eye := Vector3(float(r.x0) + 2.0, float(r.y0) + 1.7,
				(float(r.z0) + float(r.z1)) * 0.5)
			cam.global_position = eye
			cam.look_at(Vector3(float(r.x1), float(r.y0) + 1.5,
				(float(r.z0) + float(r.z1)) * 0.5), Vector3.UP)
			await process_frame
			await process_frame
			get_root().get_texture().get_image().save_png("%s/%s_d%d.png" % [dir, w, int(r.deck)])
			n += 1
			break
	print("shot ", n)
	quit()
