#!/bin/sh
# check-decoys.sh — every legitimate local edit must leave a HEALTHY machine.
#
# A decoy that breaks the machine is the worst kind of fairness bug: every
# signal the game gives says the edit is somebody's deliberate work, and it is
# actually the fault. One shipped -- /etc/httpd/httpd.conf used `listen`/`root`
# where httpd wants `Listen`/`DocumentRoot` -- and survived a 20-machine health
# run, because 17 decoys drawn 2-5 at a time do not cover themselves in twenty
# tries. This covers them on purpose, one at a time.
N=${1:-17}
fail=0
i=0
while [ "$i" -lt "$N" ]; do
    out=$(NOM_FORCE_EDIT=$i ./build/bf --health 2>&1 | tail -1)
    case "$out" in
        *"20/20"*) ;;
        *) echo "decoy $i BREAKS THE MACHINE: $out"; fail=1 ;;
    esac
    i=$((i + 1))
done
[ "$fail" -eq 0 ] && echo "$N/$N decoys leave a healthy machine"
exit $fail
