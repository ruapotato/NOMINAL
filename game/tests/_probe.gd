extends SceneTree

func _init() -> void:
	var de: Control = preload("res://scripts/de.gd").new()
	de.size = Vector2(1280, 800)
	root.add_child(de)
	await process_frame
	await process_frame
	var m: Object = de.machine
	print("booted=", m.booted(), " cust=", de.cust, " addr=", de.addr)
	for c in ["pkg list", "pkg", "pkg verify base", "svc", "df"]:
		print("=== ", c, " (workstation) ===")
		print(m.sh_on(0, c))
	de._launch("pkgman")
	await process_frame
	var w: Control = de._find("package manager")
	print("pkgman window: ", w)
	if w:
		var a: Control = w.get_meta("content")
		print("pkgs=", a.pkgs.size(), " findings=", a.findings.size(), " vnote=", a.vnote, " err=", a.err)
		print("title=", w.get_meta("title"))
	quit()
