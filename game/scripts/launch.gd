# launch.gd — which game starts.
#
# The 2D desktop is still the default and still the main scene: nothing about
# `./Godot --path game` changes. `-- --tower=<seed>` swaps in the building
# instead, so the 3D shell can be built and looked at without the desktop
# having to know it exists.
#
# An autoload rather than a branch inside de.gd, because de.gd is the one
# scene three gates already depend on and it should not grow a second job.

extends Node


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--tower"):
			var s := 200
			if a.find("=") >= 0:
				s = int(a.split("=")[1])
			call_deferred("_to_tower", s)
			return


func _to_tower(s: int) -> void:
	var scene: PackedScene = load("res://scenes/tower.tscn")
	var t: Node = scene.instantiate()
	t.seed_no = s
	var tree := get_tree()
	if tree.current_scene:
		tree.current_scene.queue_free()
	tree.root.add_child(t)
	tree.current_scene = t
