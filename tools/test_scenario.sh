#!/bin/sh
# Scenario tests. These go through the real socket and the real shell, because
# that is the interface the game is played through.
#
# What they protect is the salvage-and-shim loop, which is the keeper:
#
#   your air is running out -> mount a wreck -> its devices speak a foreign
#   vocabulary -> binding it straight on does NOT work -> you write a shim that
#   translates -> the air comes back
#
# The middle step is the one that matters. If binding alien hardware directly
# ever starts working, the translation layer has stopped being the game.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
BIN=build/nominal
PORT=${PORT:-7913}

WORK=$(mktemp -d)
cleanup() { kill $SERVER 2>/dev/null || true; rm -rf "$WORK" 2>/dev/null || true; }
trap cleanup EXIT
mkdir -p "$WORK/scripts"
printf 'while true:\n    sleep(50)\n' > "$WORK/scripts/idle.nom"
# the scenario tests drive the shipped scripts, so they have to be here
cp home/scripts/boot.nom home/scripts/serve.nom "$WORK/scripts/"

pass=0
fail=0
check() {
    if printf '%s' "$3" | grep -qF -- "$2"; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "scenario: FAIL $1"
        echo "          wanted: $2"
        printf '          got: %s\n' "$3" | head -14
    fi
}

"$BIN" --serve --port "$PORT" --home "$WORK" --seed 5 --quiet &
SERVER=$!
sleep 1

drive() { printf "$1" | python3 tools/play.py "$PORT" - 2>&1 || true; }

# ---- the machine comes up and describes itself ---------------------------
r=$(drive 'slots\n')
check "hardware inventory"   'cpu0'      "$r"
check "part ids shown"       'cpu-mk1'   "$r"
check "empty slots shown"    '(empty)'   "$r"

r=$(drive 'cat /etc/cpu0.conf\n')
check "config is a real file" 'duty 1.00' "$r"

r=$(drive 'cat /dev/cpu0/status\n')
check "per-card status"       'Tessel Mk1' "$r"

# ---- air is a clock ------------------------------------------------------
r=$(drive 'attach /home/scripts/idle.nom\nlaunch 5\ncat /dev/life/status\n')
check "air is falling"       'rate 0.000'  "$r"
check "and it is counted"    'ticks_left'  "$r"

# ---- the wreck speaks a foreign language ---------------------------------
# ssh is the primary verb: a Linux admin already knows it, and the session's
# whole view moves so every command they know keeps working over there.
r=$(drive 'hosts\nssh wreck-01\nls /dev\ncat /dev/thm-04\n')
check "hosts are listed"     'wreck-01'           "$r"
check "ssh moves the view"   'thm-04'             "$r"
check "banner on login"      'kel-morrin'         "$r"
check "and it is locked"     'sequence required'  "$r"

r=$(drive 'ssh wreck-01\nwrite /dev/sequence kel-morrin\ncat /dev/thm-04\nlogout\nls /dev\n')
check "foreign units"        'kPa'        "$r"
check "foreign vocabulary"   'partial'    "$r"
check "logout comes home"    'cpu0'       "$r"

# sshfs is the thing you reach for when ssh-ing to everything stops scaling.
r=$(drive 'sshfs wreck-01 /n/w\nls /n/w/dev\n')
check "sshfs mounts"         'thm-04'     "$r"

# ---- THE loop: tenants only pay while ops is running ----------------------
# This is the whole game. If income survives killing ops, the job has stopped
# being a job.
r=$(drive 'detach\nattach /home/scripts/boot.nom\nattach /home/scripts/serve.nom\nlaunch 11\nstep 400\nstation\n')
check "ops running: tenants served"  '100%'        "$r"
check "ops running: income"          'income +'    "$r"

r=$(drive 'kill 2\nps\nstep 400\nstation\n')
check "kill shows in ps"             'killed'          "$r"
check "no ops: heartbeat lapses"     'no heartbeat'    "$r"
check "no ops: nothing earns"        'income +0.00'    "$r"

r=$(drive 'restart 2\nstep 300\nstation\n')
check "restart restores service"     '100%'        "$r"

# ---- the shed order is a real decision -----------------------------------
r=$(drive 'station\n')
check "priority column"              'pri segment' "$r"
r=$(drive 'priority lab-1 1\nstation\n')
check "priority reorders"            '+OK lab-1 is now priority 1' "$r"

# ---- the OS is learnable from inside it ----------------------------------
r=$(drive 'man\nman srv\n')
check "man index"                    'srv        tenants and heartbeats' "$r"
check "man page"                     'THIS IS THE JOB' "$r"

# ---- the shell behaves like a shell --------------------------------------
r=$(drive 'cat /var/log/messages | grep reactor | tail 3\n')
check "pipeline"                     'reactor: online' "$r"
r=$(drive 'ls /srv | wc\n')
check "wc"                           'lines' "$r"

kill $SERVER 2>/dev/null || true

# ---- binding it straight on must NOT be enough ---------------------------
# The scenario is that your own scrubber has died — pull the card so the path
# is free, which is what a failed unit amounts to.
cat > "$WORK/scripts/naive.nom" <<'NOM'
write("/dev/scrub0/ctl", "disable")
mount("wreck-01", "/n/wreck")
write("/n/wreck/dev/sequence", "kel-morrin")
write("/n/wreck/dev/thm-04", "turn")
bind("/n/wreck/dev/thm-04", "/dev/scrubber")
while true:
    sleep(50)
NOM
naive=$("$BIN" --headless --home "$WORK" --script naive.nom --seed 5 --ticks 300 2>/dev/null || true)
rm -f "$WORK/scripts/naive.nom"

# ---- a shim that translates must work ------------------------------------
cat > "$WORK/scripts/shim.nom" <<'NOM'
write("/dev/scrub0/ctl", "disable")
mount("wreck-01", "/n/wreck")
write("/n/wreck/dev/sequence", "kel-morrin")
write("/n/wreck/dev/thm-04", "turn")
while true:
    d = parse(read("/n/wreck/dev/thm-04"))
    if d["mode"] == "turning":
        kpa = num(split(d["partial"])[0])
        write("/dev/scrubber", "rate " + str(kpa * 0.0028))
    else:
        write("/dev/scrubber", "rate 0.0")
    sleep(20)
NOM
shim=$("$BIN" --headless --home "$WORK" --script shim.nom --seed 5 --ticks 300 2>/dev/null || true)

o2n=$(printf '%s' "$naive" | sed -n 's/.*"o2":\([0-9]*\)\..*/\1/p')
o2s=$(printf '%s' "$shim"  | sed -n 's/.*"o2":\([0-9]*\)\..*/\1/p')
if [ "${o2n:-99}" -lt 78 ] 2>/dev/null && [ "${o2s:-0}" -ge 80 ] 2>/dev/null; then
    pass=$((pass + 1))
    echo "scenario: translation is the game — a bare bind leaves O2 at ${o2n}%, the shim holds ${o2s}%"
else
    fail=$((fail + 1))
    echo "scenario: FAIL binding alien hardware directly must NOT restore air (bind $o2n, shim $o2s)"
fi

echo "scenario: $pass passed, $fail failed"
cleanup
trap - EXIT
if [ "$fail" -eq 0 ]; then exit 0; else exit 1; fi
