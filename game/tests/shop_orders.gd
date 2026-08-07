# THE SHOP, AND THE TWO THINGS IT MUST NEVER DO.
#
# It must never be a second way of getting a box into the building: an order
# placed in the browser has to go through session_line()'s `order`, spend the
# money core says it spends, and leave the box in goods in like any other
# delivery. And it must never fail silently: the supplier is on the internet,
# the internet is reachable only when the network is, and a player who has
# just broken theirs needs to be told what could not be reached and shown what
# their own machine thinks its address, resolver and route are.
#
# Both are checked against the real Station -- the same session a socket
# client drives -- because a browser that ordered from a mock would prove
# nothing about the game.
extends SceneTree

var bad := 0

func fail(s: String) -> void:
	print("  FAIL: " + s)
	bad += 1

func ok(s: String) -> void:
	print("  ok   " + s)


func money_of(m: Object) -> int:
	for line in String(m.ses_state()).split("\n"):
		if line.begins_with("money "):
			return int(line.substr(6))
	return -1


func _init() -> void:
	if not ClassDB.class_exists("NominalStation"):
		print("  FAIL: the GDExtension did not load -- run `make gdext`")
		quit(1)
		return
	var machine: Object = ClassDB.instantiate("NominalStation")
	# The workstation the browser runs on is created with the ticket, exactly
	# as de.gd creates it: `links` has to have a machine to run on.
	machine.take_ticket(4823, 1)
	machine.ses_start(7008, 60000)

	var b: Control = preload("res://scripts/browser.gd").new()
	b.machine = machine
	b.size = Vector2(900, 620)
	root.add_child(b)
	await process_frame

	# ---- 1. A HOST THAT IS NOT THERE IS A PAGE, NOT A ONE-LINE SHRUG.
	b._go("nowhere.example")
	var page := String(b.raw)
	if page.find("cannot reach nowhere.example") < 0:
		fail("the failure page does not name what it could not reach")
	else:
		ok("a failed fetch names the host")
	for want in ["/etc/resolv.conf", "ip addr", "ip route"]:
		if page.find(want) < 0:
			fail("the failure page does not show `%s`" % want)
		else:
			ok("the failure page shows `%s` and its answer" % want)
	if String(b.status).find("failed") < 0:
		fail("the status bar does not say the fetch failed")
	else:
		ok("the status bar says which fetch failed")
	if b.rows.size() < 8:
		fail("the failure page is %d rows -- nothing to read" % b.rows.size())
	else:
		ok("the failure page has something on it (%d rows)" % b.rows.size())

	# ---- 2. AN ORDER LINK ASKS BEFORE IT SPENDS. There is no `sell` in this
	#         game: a mis-click would be money with no undo.
	var before := money_of(machine)
	b._go("order:switch8")
	if String(b.status) != "confirm: order switch8":
		fail("an order link did not ask to confirm (status: %s)" % b.status)
	else:
		ok("an order link asks to confirm")
	if money_of(machine) != before:
		fail("the confirm step spent money")
	else:
		ok("nothing has been spent yet")
	if String(b.raw).find("orderyes:switch8") < 0:
		fail("the confirm page offers no way to say yes")
	else:
		ok("the confirm page offers yes and no")

	# ---- 3. AND CONFIRMING GOES THROUGH THE VERB.
	b._go("orderyes:switch8")
	var receipt := String(b.raw)
	var after := money_of(machine)
	if after >= before:
		fail("ordering spent nothing: %d then %d" % [before, after])
	else:
		ok("the money left the account (%d -> %d)" % [before, after])
	if receipt.find("paid") < 0 or receipt.find("switch8") < 0:
		fail("the receipt is not core's own answer: %s" % receipt.substr(0, 120))
	else:
		ok("the receipt is the sentence core printed")

	# THE BOX IS REALLY IN GOODS IN, asked of the session rather than of the
	# receipt -- a shop that printed a receipt and delivered nothing would
	# pass every check above.
	var goods := String(machine.ses_cmd("go goods")) + String(machine.ses_cmd("look"))
	if goods.find("switch8") < 0:
		fail("nothing turned up in goods in:\n%s" % goods)
	else:
		ok("the box is in goods in, on the ground floor")

	# ---- 4. AND THE PAGE IT WAS ORDERED FROM IS THE CATALOGUE, not a list
	#         this window keeps. The bookmarks are addresses only.
	var bm := ""
	for entry in b.BOOKMARKS:
		bm += String(entry[0]) + " "
	if bm.find("halbert.co.uk") < 0:
		fail("the browser has no bookmark for the supplier")
	else:
		ok("the supplier is a bookmark (an address, not a page)")

	# ---- 5. AND THE SHOP IS ON THE END OF A CABLE. D41.
	#
	# The browser runs on the player's own workstation, which since D41 is a
	# box standing in the MDF with one socket on the back of it and its lead
	# in the ISP's handoff. So this is the whole claim, performed: the
	# catalogue loads, the lead comes out, the catalogue is gone -- not
	# because a flag was set but because the machine the browser is on is not
	# on a network any more -- and putting the lead back brings it back.
	# There is always a move.
	b._go("halbert.co.uk/catalogue")
	if String(b.raw).find("switch8") < 0:
		fail("the catalogue does not load on day one: %s" % String(b.raw).substr(0, 120))
	else:
		ok("the catalogue loads on the machine in the MDF")
	# link 0 is the lead the building came with, workstation to uplink:0
	machine.ses_cmd("uncable 0")
	b._go("halbert.co.uk/catalogue")
	var gone := String(b.raw)
	if gone.find("cannot reach halbert.co.uk") < 0:
		fail("the lead came out and the shop still loaded")
	else:
		ok("pull the workstation's lead and the shop is unreachable")
	if gone.find("NO-CARRIER") < 0:
		fail("the failure page does not show the machine's own card is down:\n%s"
			% gone.substr(0, 400))
	else:
		ok("and the page shows `ip addr` on that box saying NO-CARRIER")
	# AND THE WAY BACK, which is legs and copper and nothing else.
	machine.ses_cmd("spool cat5e")
	machine.ses_cmd("go mdf")
	machine.ses_cmd("cable ws:0 uplink:0")
	b._go("halbert.co.uk/catalogue")
	if String(b.raw).find("switch8") < 0:
		fail("re-cabled the workstation to the handoff and the shop stayed dead")
	else:
		ok("cable it back to the handoff and the shop is there again")

	print("shop_orders: %d failures" % bad)
	quit(1 if bad else 0)
