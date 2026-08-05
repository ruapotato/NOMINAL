# Booting the rescue medium must NOT close the ticket.
#
# It did, on every seed: `rcon media insert; rcon boot media; rcon power
# cycle` and the customer said "it is working again, everything is where it
# was" -- with their disk still corrupt and not even mounted. Thirty seconds,
# no diagnosis, and it made the optimal play "skip the game".
extends SceneTree

func _init() -> void:
	var st: Object = ClassDB.instantiate("NominalStation")
	var bad := 0
	for seed_no in [4823, 4824, 4831, 4845]:
		st.take_ticket(seed_no, 1)
		var addr: String = str(st.peer_addr())
		st.sh_on(0, "rcon connect " + addr)
		st.sh_on(0, "rcon media insert")
		st.sh_on(0, "rcon boot media")
		st.sh_on(0, "rcon power cycle")
		var up: bool = st.booted()
		var resc: bool = st.on_rescue()
		var well: bool = st.healthy()
		print("seed %d: booted=%s on_rescue=%s healthy=%s" % [seed_no, up, resc, well])
		if well:
			print("  FAIL: the ticket would close on a rescue boot")
			bad += 1
		if up and not resc:
			print("  FAIL: rescue medium booted but on_rescue() says otherwise")
			bad += 1
	# AND THE OTHER HALF: a machine running from its own disk, healthy, must
	# still close. A fix that makes every ticket unclosable would pass the
	# test above perfectly, which is exactly why this half exists.
	# `install` and not `take_ticket(seed, 0)`: take_ticket clamps faults up
	# to one, so asking it for an unbroken machine quietly hands you a broken
	# one. My first version of this half tested nothing and said FAIL twice,
	# which is the right way round for a test to be wrong.
	for seed_no in [4823, 4831]:
		st.install(seed_no)
		print("clean seed %d: booted=%s on_rescue=%s healthy=%s" % [
			seed_no, st.booted(), st.on_rescue(), st.healthy()])
		if not st.healthy():
			print("  FAIL: a healthy machine on its own disk did not close")
			bad += 1

	print("rescue_close: %d failures" % bad)
	quit(1 if bad else 0)
