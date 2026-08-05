extends SceneTree

func _init() -> void:
	var f: Font = preload("res://scripts/uifont.gd").mono()
	var cm := preload("res://scripts/charmap.gd")
	var blocks: Array = cm.BLOCKS
	for b in blocks:
		var n := 0
		var tot := 0
		for c in range(b[0], b[1] + 1):
			tot += 1
			if f.has_char(c):
				n += 1
		print("%-24s %d/%d" % [b[2], n, tot])
	# the terminal's own cell: is it square?
	for s in [10, 11, 12, 13]:
		print("size %d: M=%f i=%f space=%f" % [s,
			f.get_string_size("M", 0, -1, s).x,
			f.get_string_size("i", 0, -1, s).x,
			f.get_string_size(" ", 0, -1, s).x])
	quit()
