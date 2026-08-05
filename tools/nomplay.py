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
# A COMMAND FILE, NOT A FIFO.
#
# The first version used a named pipe, and opening a pipe for writing BLOCKS
# until something opens the read end -- so when the relay died, every later
# invocation hung forever, ignoring its own timeout. That cost more of my time
# than any bug in the game. Appending to a file cannot deadlock.
CMDS, LOG, PID = D + '/cmds', D + '/out', D + '/pid'
# The prompt says which machine you are on, so there is more than one.
PROMPTS = (b'you@desk# ', b'root@node# ', b'rescue# ')


def _at_prompt(buf):
    return any(buf.endswith(p) for p in PROMPTS)


def relay(port):
    """Owns the socket. Reads commands from the command file, appends everything the
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
                if _at_prompt(out):
                    break
            except socket.timeout:
                if out:
                    quiet += 0.4
                    if quiet >= 2.0:
                        break
        return out

    with open(LOG, 'ab', buffering=0) as log:
        log.write(drain())
        pos = 0
        while True:
            try:
                with open(CMDS, 'r') as f:
                    f.seek(pos)
                    fresh = f.read()
                    pos = f.tell()
            except FileNotFoundError:
                fresh = ''
            if not fresh:
                time.sleep(0.15)
                continue
            for line in fresh.splitlines():
                if line == '\x00QUIT':
                    s.close()
                    return
                s.sendall((line + '\n').encode())
                log.write(('\n$ ' + line + '\n').encode())
                log.write(drain())


def alive():
    try:
        pid = int(open(PID).read())
        if pid <= 1:
            return False            # never signal 0: that is the whole group
        os.kill(pid, 0)
        return True
    except Exception:
        return False


def start(port):
    stop()
    os.makedirs(D, exist_ok=True)
    for p in (CMDS, LOG):
        if os.path.exists(p):
            os.remove(p)
    open(CMDS, 'w').close()
    open(LOG, 'wb').close()
    # fork() HANDS THE PARENT THE CHILD'S PID. Looking it up with pgrep
    # instead could return nothing, and 0 written to the pid file made stop()
    # call os.kill(0, SIGKILL) -- which signals the whole PROCESS GROUP, so
    # the client killed its own caller and anything else sharing the group.
    # That is a foot-gun I built for no reason; fork already told me.
    pid = os.fork()
    if pid == 0:
        os.setsid()
        # DETACH THE STANDARD STREAMS. The relay inherited stdout, so the
        # shell that started it waited for that pipe to close -- forever --
        # and every invocation looked like a hang in the client when the
        # client had already finished. A daemon closes its streams; I skipped
        # the one step that makes it a daemon.
        devnull = os.open(os.devnull, os.O_RDWR)
        os.dup2(devnull, 0)
        os.dup2(devnull, 1)
        os.dup2(devnull, 2)
        try:
            relay(port)
        except Exception:
            pass
        os._exit(0)
    open(PID, 'w').write(str(pid))
    time.sleep(2.5)                      # let the ticket banner arrive


def stop():
    if os.path.exists(PID) and alive():
        try:
            with open(CMDS, 'a') as f:
                f.write('\x00QUIT\n')
            time.sleep(0.4)
        except Exception:
            pass
        try:
            pid = int(open(PID).read())
            if pid > 1:
                os.kill(pid, signal.SIGKILL)
        except Exception:
            pass


def send(cmds):
    if not (os.path.exists(PID) and alive()):
        print('no session -- run: nomplay.py new')
        sys.exit(1)
    before = os.path.getsize(LOG)
    with open(CMDS, 'a') as f:
        for c in cmds:
            f.write(c + '\n')
        f.flush()
    last, still = before, 0
    deadline = time.time() + 180         # a wedged relay must never hang us
    while still < 6 and time.time() < deadline:
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
