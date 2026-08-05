extends SceneTree

class FakeMachine:
	var store := {}
	func sh_on(_n: int, cmd: String) -> String:
		if cmd.begins_with("cat "):
			return store.get(cmd.substr(4), "")
		if cmd.begins_with("echo "):
			var q := cmd.split('"')
			var path := cmd.get_slice("> ", 1)
			store[path] = q[1] + "\n"
			return ""
		return ""

func _initialize() -> void:
	var S = load("res://scripts/gworms.gd")
	var m := FakeMachine.new()
	m.store["/root/.worms"] = "3 4\n"
	for sz in [Vector2(400, 300), Vector2(900, 600), Vector2(1400, 500)]:
		var g = S.new()
		g.machine = m
		g.size = sz
		root.add_child(g)
		print("wins ", g.wins, " state ", g.state, " turn ", g.turn)
		var before := float(g.ground[100])
		# fire a lot of shells; every one must land or leave the map
		for shot_i in range(40):
			if g.state == "over":
				g._new_game()
			g.angle = randf_range(10, 170)
			g.power = randf_range(30, 100)
			g._fire()
			var guard := 0
			while g.state != "aim" and g.state != "over" and guard < 4000:
				g._process(1.0 / 60.0)
				guard += 1
			if guard >= 4000:
				push_error("shell never landed")
			g._draw()
		print("  ground[100] ", before, " -> ", float(g.ground[100]),
			" hp ", g.worms[0]["hp"], "/", g.worms[1]["hp"], " store ", m.store)
		g.queue_free()
	quit()
