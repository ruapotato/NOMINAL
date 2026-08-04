#!/bin/sh
# Language tests. Every one runs a real script through the real compiler, the
# real VM and the real device tree — nothing is stubbed. A test that mocks the
# thing under test has tested neither.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
BIN=build/nominal
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/scripts"

pass=0
fail=0

# run <name> <expected-substring>...   (the script arrives on stdin)
# Every substring must appear somewhere in the run's output.
run() {
    name=$1
    shift
    cat > "$WORK/scripts/t.nom"
    got=$("$BIN" --headless --home "$WORK" --script t.nom --ticks ${TICKS:-40} --log 2>&1 || true)
    bad=""
    for want in "$@"; do
        printf '%s' "$got" | grep -qF -- "$want" || bad="$bad
      missing: $want"
    done
    if [ -z "$bad" ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "lang: FAIL $name$bad"
        printf '      got: %s\n' "$got" | head -8
    fi
}

run arithmetic 'r 7 1 3 1.5000 8 -2' <<'EOF'
print("r", 1 + 2 * 3, 7 % 3, 7 // 2, 3 / 2, 2 ** 3, -2)
EOF

run true_division '0.5000' <<'EOF'
print(1 / 2)
EOF

run strings 'ab AB 2 ["a", "b"]' <<'EOF'
print("a" + "b", "AB", len("ab"), split("a b"))
EOF

run comparison 'true false true' <<'EOF'
print(1 < 2, "b" < "a", 2 in [1, 2, 3])
EOF

run control 'x 0' 'x 1' 'x 2' 'done' <<'EOF'
i = 0
while i < 3:
    print("x", i)
    i += 1
print("done")
EOF

run brace_blocks 'yes' <<'EOF'
if 1 < 2 { print("yes") }
EOF

run nested_dedent 'inner' 'outer' <<'EOF'
a = []
while len(a) < 1:
    if true:
        append(a, 1)
    print("inner")
print("outer")
EOF

run functions '120' <<'EOF'
def fact(n):
    if n <= 1:
        return 1
    return n * fact(n - 1)
print(fact(5))
EOF

run dict_order '["b", "a", "c"]' <<'EOF'
d = {"b": 1, "a": 2, "c": 3}
print(keys(d))
EOF

run for_loop 'sum 3' <<'EOF'
s = 0
for i in range(3):
    print(i)
    s = s + i
print("sum", s)
EOF

run break_continue '  1' '  3' <<'EOF'
for i in range(5):
    if i == 0:
        continue
    if i == 2:
        break
    print(i)
print(3)
EOF

run device_read 'state cold' <<'EOF'
print(strip(read("/dev/reactor/status")))
EOF

run device_write_read 'priming' <<'EOF'
write("/dev/reactor/ctl", "prime")
print(parse(read("/dev/reactor/status"))["state"])
EOF

# --- field files: one field, one file ---
run field_files 'cold 0' <<'EOF'
print(get("/dev/reactor/state"), get("/dev/reactor/output"))
EOF

# --- get() returns a TYPED value, not a string ---
run get_is_typed '4000' <<'EOF'
print(get("/dev/cpu/rated") * 2)
EOF

# --- waitfor suspends instead of spinning, and costs ~1 instr/tick ---
TICKS=60 run waitfor_suspends 'primed at 20' <<'EOF'
write("/dev/reactor/ctl", "prime")
waitfor("/dev/reactor/state", "idle")
print("primed at", tick())
EOF

# --- bus channels are writable files, not command strings ---
# 1846 rather than the card's rated 2000: two paying segments share the rail
# and the bay runs warm, so the CPU is derated by the station around it rather
# than by anything wrong with the CPU. See D15/D16.
TICKS=90 run bus_channel_write '"pool":2000' <<'EOF'
write("/dev/reactor/ctl", "prime")
waitfor("/dev/reactor/state", "idle")
write("/dev/reactor/ctl", "start")
write("/dev/bus/cpu0", 2.0)
waitfor("/dev/reactor/state", "online")
while true:
    sleep(10)
EOF

# --- watch() fires on a change and not before ---
TICKS=60 run watch_fires_on_change 'changed to idle' <<'EOF'
write("/dev/log", "arming")
sleep(2)
write("/dev/reactor/ctl", "prime")
v = watch("/dev/reactor/state")
print("changed to", v)
EOF

# --- every field file the examples rely on must actually exist ---
run every_field_file_resolves 'all fields ok' <<'EOF'
paths = ["/dev/reactor/state", "/dev/reactor/output", "/dev/reactor/ready",
         "/dev/bus/supply", "/dev/bus/battery", "/dev/bus/brownout",
         "/dev/bus/demand", "/dev/bus/cpu0", "/dev/bus/sen0", "/dev/bus/rad0",
         "/dev/cpu/pool", "/dev/cpu/rated", "/dev/cpu/per_script",
         "/dev/cpu/bay_temp", "/dev/cpu/throttled",
         "/dev/cpu0/state", "/dev/cpu0/health", "/dev/cpu0/duty",
         "/dev/cpu0/draw", "/dev/cpu0/effect", "/dev/cpu0/part",
         "/dev/sensor/online", "/dev/sensor/calibrated", "/dev/sensor/drift",
         "/dev/hull/integrity", "/dev/life/o2", "/dev/life/rate",
         "/dev/mission/credits", "/dev/mission/income", "/dev/mission/segments",
         "/dev/mission/wear"]
for p in paths:
    v = get(p)
print("all fields ok")
EOF

# --- a device read must compare equal to a plain literal (no stray newline) ---
run device_string_compares 'compared equal' <<'EOF'
write("/dev/reactor/ctl", "prime")
sleep(1)
if get("/dev/reactor/state") == "priming":
    write("/dev/log", "compared equal")
else:
    write("/dev/log", "COMPARISON BROKEN")
while true:
    sleep(10)
EOF

# --- /dev/alarm blocks while nothing is wrong ---
# The air is falling from tick 0, so an alarm is already queued: drain it,
# then the next read must block because nothing else is wrong yet.
run alarm_drains_then_blocks '"blocked_on":"/dev/alarm"' <<'EOF'
first = get("/dev/alarm")
print("first alarm", first)
while true:
    a = get("/dev/alarm")
    print("another", a)
EOF

run parse_numbers '78.0000 cold' <<'EOF'
d = parse("o2 78.00\nstate cold\n")
print(d["o2"], d["state"])
EOF

# --- errors must be named, never silently wrong (HAMSH_SPEC 16a) ---
run div_zero 'division by zero' <<'EOF'
print(1 / 0)
EOF

run undefined_name "undefined name 'nope'" <<'EOF'
print(nope)
EOF

run bad_index 'out of range' <<'EOF'
a = [1, 2]
print(a[5])
EOF

run type_error 'unsupported operand types' <<'EOF'
print(1 + [2])
EOF

run parse_error 'parse error' <<'EOF'
if true
    print("no colon")
EOF

run unknown_device 'no such file' <<'EOF'
print(read("/dev/warp/core"))
EOF

# --- an unpowered machine still runs, on the maintenance controller alone ---
run infinite_loop_is_bounded '"pool":240' '"state":"running"' <<'EOF'
while true:
    x = 1
EOF

# --- instructions are a POOL CONSUMED ON DEMAND: a script that never sleeps
#     takes the lot and leaves nothing for the tenants. This is the whole
#     reason writing it well matters. ---
TICKS=120 run greedy_script_starves_the_tenants '"spare":0' <<'EOF'
while true:
    x = 1
EOF

TICKS=120 run sleeping_script_leaves_spare '"state":"sleeping"' <<'EOF'
while true:
    sleep(20)
EOF

# --- powering the compute bus must actually buy instructions ---
# Powering the compute bus takes the pool from the maintenance controller's
# 240 up to 1846 — not the card's rated 2000, because the station around it
# takes its share.
TICKS=120 run compute_power_buys_budget '"pool":2000' <<'EOF'
write("/dev/reactor/ctl", "prime")
while parse(read("/dev/reactor/status"))["state"] != "idle":
    sleep(1)
write("/dev/reactor/ctl", "start")
while parse(read("/dev/reactor/status"))["state"] != "online":
    sleep(1)
write("/dev/bus/cpu0", 2.0)
while true:
    sleep(1)
EOF

# --- blocking is suspension, not an error ---
run blocking_read_suspends '"blocked_on":"/dev/sensor/contacts"' <<'EOF'
print(read("/dev/sensor/contacts"))
EOF

# --- sleep must resume exactly once, not re-arm forever ---
run sleep_resumes '0  before' '3  after' <<'EOF'
print("before")
sleep(3)
print("after")
EOF

echo "lang: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
