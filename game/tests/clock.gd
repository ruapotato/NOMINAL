extends SceneTree
# clock.gd — the station runs while you stand in it.
#
# D44: "Hitting n to go to the next day is not gonna be a game playing
# candidate. The game is gonna be played live time."
#
# WHAT THIS FILE IS FOR, AND WHAT IT IS NOT FOR. It asserts that time PASSES
# in the window without anybody typing `day` -- that _process drives the busy
# period, that a whole day completes on its own, and that the report arrives
# when one does. Whether the NUMBERS a sliced day produces are right is
# --sitecheck's business: check_clock() there runs the same thirty days two
# ways, one whole and one in slices of a hundred milliseconds, and compares
# every field of the report plus the money, the complaints and each tenancy's
# own score. Two files, two claims, neither duplicating the other.
#
# AND IT HAS ITS OWN TOWER, which is the whole reason it is a separate file.
# Starting a day is not a read: site_day_begin() moves every due tenancy in
# and sends their DHCP. Running the clock inside game/tests/tower.gd made
# eleven assertions about reaching a port fail, because desks had appeared
# between the walk and the look. A clock test has to own the world it winds.

var bad := 0

func fail(msg: String) -> void:
	print("  FAIL: " + msg)
	bad += 1

func ok(msg: String) -> void:
	print("  ok   " + msg)

func _init() -> void:
	var t = preload("res://scripts/tower.gd").new()
	get_root().add_child(t)
	await process_frame
	await process_frame
	if not t.site_up:
		fail("no session")
		print("clock: %d failures" % bad)
		quit(1)
		return

	# THE CLOCK IS OFF IN A HEADLESS RUN because a headless run has no player,
	# so a test of live time has to ask for it -- which is the right way round:
	# the window turns it on for a person, and everything else says so.
	if t.clock_running:
		fail("the clock is running in a headless process, where there is nobody to run it for")
	else:
		ok("a headless station stands still until a test asks it not to")
	t.clock_running = true

	# ---- IT MOVES, AND NOBODY PRESSED A KEY
	var day0: int = int(t.ses_state().get("day", 0))
	var p0: float = t.day_progress()
	for i in range(120):
		t._run_clock(1.0 / 60.0)
	var p1: float = t.day_progress()
	if p1 <= max(p0, 0.0):
		fail("two seconds of frames and the busy period is still at %.3f" % p1)
	else:
		ok("two seconds of frames run %.1f%% of a day, with nothing typed" % (p1 * 100.0))

	# ---- AND THE RATE IS THE ONE THE WINDOW SAYS IT IS
	#
	# DAY_SECONDS is the only knob, so it is the only thing worth checking:
	# a day should take about that many real seconds, and a test that measured
	# a hardcoded number instead would be a second opinion about the rate.
	var want: float = 2.0 / t.DAY_SECONDS
	if absf(p1 - want) > want * 0.25:
		fail("two seconds should be %.4f of a day at DAY_SECONDS=%.0f, and it was %.4f"
			% [want, t.DAY_SECONDS, p1])
	else:
		ok("and at DAY_SECONDS=%.0f that is the %.4f of a day it should be"
			% [t.DAY_SECONDS, p1])

	# ---- A WHOLE DAY COMPLETES ON ITS OWN, and says so when it does.
	#
	# Driven at a frame's worth at a time, as a window would, until the day
	# rolls over -- with a bound, because a test that could hang is not a test.
	var rolled := false
	for i in range(int(t.DAY_SECONDS * 70.0)):
		t._run_clock(1.0 / 60.0)
		if int(t.ses_state().get("day", 0)) > day0:
			rolled = true
			break
	if not rolled:
		fail("the day never ended: still day %d after a whole DAY_SECONDS of frames"
			% int(t.ses_state().get("day", 0)))
	else:
		ok("the day rolled over by itself: day %d became day %d"
			% [day0, int(t.ses_state().get("day", 0))])

	# and the window was told, rather than having to poll for it
	if not t.report_open():
		fail("a day ended and the window put no report up")
	else:
		ok("and the report went up on its own")

	# ---- AND IT STOPS WHEN THE WORLD IS NOT YOURS TO RUN
	#
	# Sitting at a desk is being somewhere else; the clock has no business
	# running the station while the player is inside a machine, and _process
	# returns before it gets there. This asserts the guard rather than the
	# effect, because the effect is a frame of _process a test cannot fake.
	t.clock_running = false
	var frozen: float = t.day_progress()
	for i in range(30):
		t._run_clock(1.0 / 60.0)
	if absf(t.day_progress() - frozen) > 0.0001:
		fail("the clock ran with clock_running false: %.4f became %.4f"
			% [frozen, t.day_progress()])
	else:
		ok("and it stands still again the moment it is told to")

	print("clock: %d failures" % bad)
	quit(1 if bad else 0)
