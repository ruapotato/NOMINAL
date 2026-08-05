# launch.gd — which game starts.
#
# THE BUILDING IS THE GAME NOW, so it is the main scene. It used to be behind
# `-- --tower=<seed>`, which meant that starting the game the ordinary way --
# double-clicking it, or pressing play in the editor, which passes no user
# args -- showed the old 2D desktop and nothing else. The owner reported
# exactly that: "not seeing a three D interface, still just shows the two D."
# A feature reachable only by a command-line flag nobody typed is a feature
# nobody has.
#
# `-- --desk` still gives the bare desktop, because three gates drive it and
# because it is genuinely useful to open the machine without walking to it.
# `-- --tower=<seed>` still picks a seed.
#
# An autoload rather than a branch inside de.gd, because de.gd is the one
# scene three gates already depend on and it should not grow a second job.

extends Node


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--desk"):
			call_deferred("_to_desk")
			return
		if a.begins_with("--tower") and a.find("=") >= 0:
			call_deferred("_reseed", int(a.split("=")[1]))
			return


func _to_desk() -> void:
	var scene: PackedScene = load("res://scenes/de.tscn")
	var d: Node = scene.instantiate()
	var tree := get_tree()
	if tree.current_scene:
		tree.current_scene.queue_free()
	tree.root.add_child(d)
	tree.current_scene = d


func _reseed(s: int) -> void:
	var tree := get_tree()
	if tree.current_scene and tree.current_scene.has_method("build"):
		tree.current_scene.call("build", s)


func _to_tower(s: int) -> void:
	print("nominal: the building, seed %d" % s)
	var scene: PackedScene = load("res://scenes/tower.tscn")
	var t: Node = scene.instantiate()
	t.seed_no = s
	var tree := get_tree()
	if tree.current_scene:
		tree.current_scene.queue_free()
	tree.root.add_child(t)
	tree.current_scene = t
