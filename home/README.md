# Your home directory

You are the sysadmin of a station. Read `man intro` at the prompt — the
documentation lives inside the machine, not out here.

    scripts/    every *.nom here can be attached and run
    examples/   worked lessons, not run automatically
    logs/

## The job, in one paragraph

Tenants rent segments and pay you every tick — but only while something is
actually looking after them, and that something is your scripts. Write
`/srv/<tenant>/heartbeat` regularly and they pay. Stop, and within 140 ticks
they pay nothing; stay below their SLA long enough and they give notice and
leave, and take the rent with them.

Everything else follows: you need power to run the computer that runs the
scripts, power costs money, and more tenants need more than you have.

## What is already here

    boot.nom     brings the reactor up and feeds the cards. runs once.
    serve.nom    ops. walks /srv every ten ticks and writes each heartbeat.

`serve.nom` is fine with two tenants. It is your problem when there are ten,
because every script shares one instruction pool and the tenants want their
share of it too.

## First twenty minutes

    station                     who is aboard and what they are paying
    ls /srv                     one directory per tenant
    cat /srv/lab-1/status       what it needs, and how stale its heartbeat is
    ps                          what is running and what it is waiting on
    man srv                     the rules, in full

    kill 2                      stop ops and watch the income stop
    restart 2                   and start it again

## When it stops working

The station sheds by priority: when it cannot serve everyone, the top of the
list is served in full and the bottom gets nothing. That is deliberate — being
told exactly who you are failing beats everything being slightly broken.

    station                     the list is the shed order
    priority hab-2 1            move somebody up
    trace cpu0                  what a device depends on, and where it breaks
    catalog / order / install   buy your way out
    man power                   the whole story

## Things that will bite you

1. **Priming the reactor runs off the battery.** Ask the bus for anything
   during those twenty ticks and the prime aborts.
2. **`/dev/sensor/bearing` blocks.** An unpowered bench has nothing to say, and
   saying nothing is not the same as saying zero.
3. **A cold bench is a confident liar.** It is misaligned, not noisy, so
   averaging more readings returns the same wrong answer. It is warmed by the
   flight computer's waste heat.
4. **Your own code is a thermal load.** A loop that never sleeps cooks the bay
   it runs in and gets throttled for it. `sleep()` is cooling.
