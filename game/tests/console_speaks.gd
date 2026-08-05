# A console on a machine that never booted must SAY SO, in the same words the
# socket uses. It used to print nothing at all: the prompt was empty, and an
# empty prompt makes terminal.gd swallow the keystroke, so five commands in a
# row produced five silences. Silence about a file is indistinguishable from
# "I looked and it is fine".
extends SceneTree

func _init() -> void:
	var st: Object = ClassDB.instantiate("NominalStation")
	var bad := 0
	# Find a seed that actually fails to boot -- most tickets now come up
	# and are merely sick, which is the point of the up-but-sick class but
	# makes a fixed seed the wrong way to ask for a dead one.
	var found := false
	for seed_no in range(4820, 4870):
		st.take_ticket(seed_no, 1)
		if not st.booted():
			found = true
			print("dead machine: seed %d" % seed_no)
			break
	if not found:
		print("FAIL: no seed in 4820-4870 produced a machine that will not boot")
		quit(1)
		return
	st.sh_on(0, "rcon connect " + str(st.peer_addr()))
	for cmd in ["ls", "cat /etc/fstab", "stat /boot/vmnomuz", "svc"]:
		var out: String = str(st.sh_on(1, cmd))
		if out.strip_edges() == "":
			print("  FAIL: `%s` on a dead machine said nothing" % cmd)
			bad += 1
		elif out.find("no shell here") < 0:
			print("  FAIL: `%s` did not explain: %s" % [cmd, out.substr(0, 60)])
			bad += 1
		else:
			print("  ok %-22s refuses in words" % cmd)
	# blkid is the documented exception: the service processor reads the
	# drives without the machine's help, so it must ANSWER, not refuse.
	var b: String = str(st.sh_on(1, "blkid"))
	if b.find("UUID") < 0:
		print("  FAIL: blkid did not answer out-of-band: %s" % b.substr(0, 60))
		bad += 1
	else:
		print("  ok blkid                 answers out-of-band")
	# And `done` must exist as a verb of the job.
	var hb: String = str(st.handback())
	if hb.strip_edges() == "":
		print("  FAIL: handback() said nothing")
		bad += 1
	else:
		print("  ok handback              answers")
	print("console_speaks: %d failures" % bad)
	quit(1 if bad else 0)
