#!/bin/sh
# check-decoys.sh — every legitimate local edit must leave a HEALTHY machine.
#
# A decoy that breaks the machine is the worst kind of fairness bug: every
# signal the game gives says the edit is somebody's deliberate work, and it is
# actually the fault. One shipped -- /etc/httpd/httpd.conf used `listen`/`root`
# where httpd wants `Listen`/`DocumentRoot` -- and survived a 20-machine health
# run, because 17 decoys drawn 2-5 at a time do not cover themselves in twenty
# tries. This covers them on purpose, one at a time.
# The count is the length of the EDITS table in image.c. It went from 17 to 27
# when the structural fault set roughly doubled: a decoy set that does not grow
# with the fault set turns `pkg verify` back into an oracle, because the one
# unfamiliar line in the output is the answer again. 27 to 37 for the third
# generation, and six of those ten are in files a NEW fault also writes --
# zbl.cfg, /etc/shells, rc.3, auditd.conf, crontab, httpd.conf -- because a
# decoy that shares no file with any fault teaches nothing.
N=${1:-37}
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
