# Throwaway: open every app the desktop advertises and make sure it draws.
extends SceneTree

func _init() -> void:
	var de: Control = preload("res://scripts/de.gd").new()
	de.size = Vector2(1280, 800)
	root.add_child(de)
	await process_frame
	await process_frame

	var apps: Array = de.LAUNCHERS
	print("desktop advertises %d apps" % apps.size())
	var bad := 0
	for spec in apps:
		var exec: String = spec[1]
		de._launch(exec)
		await process_frame
		var kind: String = de.EXEC_MAP.get(exec, exec)
		var want: String = str(de.TITLES.get(kind, "?"))
		if want == "?":
			print("  NO TITLE for exec=%s (kind=%s)" % [exec, kind])
			bad += 1
			continue
		if de._find(want) == null:
			print("  DID NOT OPEN: %s -> %s" % [exec, want])
			bad += 1
		else:
			print("  ok %-12s %s" % [exec, want])
	# Resize small: every window must survive a squeeze without erroring.
	de.size = Vector2(640, 480)
	await process_frame
	await process_frame
	print("smoke: %d failures" % bad)
	quit(1 if bad else 0)
