extends SceneTree
func _init() -> void:
	var m: Object = ClassDB.instantiate("NominalStation")
	m.take_ticket(1, 1)
	print(m.sh_on(0, "df").split("\n")[1])
	for i in range(4):
		m.sh_on(0, "cp /usr/share/fortunes /tmp/f%d" % i)
	print(m.sh_on(0, "ls -l /tmp"))
	print(m.sh_on(0, "df").split("\n")[1])
	quit(0)
