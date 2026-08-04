#!/usr/bin/env python3
"""Persistent session against the NOMINAL bench.

The original client opened a connection, ran its arguments, and hung up -- and
hanging up destroys the ticket, so you could never look at output and THEN
decide what to do next. A blind playtester called that "not playtesting, it is
guessing", wrote their own FIFO-backed client, and said to ship it instead.
They were right. This is that.

One connection stays alive across invocations, owned by a relay process. You
send commands to it and read back whatever the machine said.

    nomplay.py new [port]     take a fresh ticket (drops any current one)
    nomplay.py 'cmd' ...      run commands on the CURRENT ticket
    nomplay.py show           reprint the whole transcript so far
    nomplay.py end            hang up

Everything goes to the same machine until you call `new` or `end`.
"""
import os, socket, sys, time, signal

D = os.environ.get('NOMPLAY_DIR', '/tmp/nomplay')
FIFO, LOG, PID = D + '/in', D + '/out', D + '/pid'
PROMPT = b'rescue# '


def relay(port):
    """Owns the socket. Reads commands from the FIFO, appends everything the
    server says to the log. Lives until killed or the server goes away."""
    s = socket.create_connection(('127.0.0.1', port))
    s.settimeout(0.4)

    def drain():
        """Read until the prompt comes back.

        Breaking on the FIRST timeout after any data at all split slow replies
        across two commands: a `dmesg` that took a moment to arrive showed up
        underneath the NEXT command's marker, which is exactly the kind of
        confusion a playtest client must not add. Wait for the prompt, and
        only fall back to silence after a decent pause."""
        out = b''
        deadline = time.time() + 40      # a model reply can take ~5s
        quiet = 0.0
        while time.time() < deadline:
            try:
                c = s.recv(65536)
                if not c:
                    break
                out += c
                quiet = 0.0
                if out.endswith(PROMPT):
                    break
            except socket.timeout:
                if out:
                    quiet += 0.4
                    if quiet >= 2.0:
                        break
        return out

    with open(LOG, 'ab', buffering=0) as log:
        log.write(drain())
        while True:
            with open(FIFO) as f:        # blocks until someone writes
                for line in f:
                    line = line.rstrip('\n')
                    if line == '\x00QUIT':
                        s.close()
                        return
                    s.sendall((line + '\n').encode())
                    log.write(('\n$ ' + line + '\n').encode())
                    log.write(drain())


def alive():
    try:
        os.kill(int(open(PID).read()), 0)
        return True
    except Exception:
        return False


def start(port):
    stop()
    os.makedirs(D, exist_ok=True)
    for p in (FIFO, LOG):
        if os.path.exists(p):
            os.remove(p)
    os.mkfifo(FIFO)
    open(LOG, 'wb').close()
    if os.fork() == 0:
        os.setsid()
        try:
            relay(port)
        except Exception:
            pass
        os._exit(0)
    # the child writes nothing; record its pid from the parent's view
    time.sleep(0.2)
    open(PID, 'w').write(str(os.getpgid(0) and _child_pid()))
    time.sleep(2.5)                      # let the ticket banner arrive


def _child_pid():
    """The forked relay is our only child; find it."""
    out = os.popen('pgrep -f "nomplay.py" -P %d' % os.getpid()).read().split()
    return int(out[0]) if out else 0


def stop():
    if os.path.exists(PID) and alive():
        try:
            with open(FIFO, 'w') as f:
                f.write('\x00QUIT\n')
            time.sleep(0.3)
        except Exception:
            pass
        try:
            os.kill(int(open(PID).read()), signal.SIGKILL)
        except Exception:
            pass


def send(cmds):
    if not (os.path.exists(PID) and alive()):
        print('no session -- run: nomplay.py new')
        sys.exit(1)
    before = os.path.getsize(LOG)
    with open(FIFO, 'w') as f:
        for c in cmds:
            f.write(c + '\n')
            f.flush()
    last, still = before, 0
    while still < 6:                     # wait for the log to go quiet
        time.sleep(0.6)
        now = os.path.getsize(LOG)
        if now == last:
            still += 1
        else:
            still, last = 0, now
    with open(LOG, 'rb') as f:
        f.seek(before)
        sys.stdout.write(f.read().decode(errors='replace'))


if __name__ == '__main__':
    a = sys.argv[1:]
    if not a:
        print(__doc__)
    elif a[0] == 'new':
        start(int(a[1]) if len(a) > 1 else 7777)
        sys.stdout.write(open(LOG, 'rb').read().decode(errors='replace'))
    elif a[0] == 'end':
        stop()
        print('session ended')
    elif a[0] == 'show':
        sys.stdout.write(open(LOG, 'rb').read().decode(errors='replace'))
    else:
        send(a)
