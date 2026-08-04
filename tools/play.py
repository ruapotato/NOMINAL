#!/usr/bin/env python3
"""play.py — drive a NOMINAL station over the socket.

This is a client, not part of the game. It exists so a transcript of a session
can be replayed and pasted into a report. Anything it does, you can do by hand
with telnet; the protocol is line-based and dot-terminated on purpose.

  tools/play.py 7777 cmd.txt      run the commands in cmd.txt
  tools/play.py 7777 - <<'EOF'    run the commands on stdin
"""
import socket
import sys


class Station:
    def __init__(self, port=7777, host="127.0.0.1"):
        self.sock = socket.create_connection((host, port), timeout=10)
        self.buf = b""
        self.banner = self._read_response()

    def _read_line(self):
        while b"\n" not in self.buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise EOFError("station closed the connection")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return line.decode("utf-8", "replace").rstrip("\r")

    def _read_response(self):
        """Status line, then body lines until a lone '.'."""
        status = self._read_line()
        if status.startswith("+DATA"):
            return status, []
        body = []
        while True:
            line = self._read_line()
            if line == ".":
                break
            body.append(line[1:] if line.startswith("..") else line)
        return status, body

    def send(self, cmd):
        self.sock.sendall((cmd + "\n").encode())
        return self._read_response()

    def put(self, path, text):
        """Upload a script: `put`, the lines, then a lone '.'."""
        self.send(f"put {path}")
        for line in text.splitlines():
            self.sock.sendall(((("." + line) if line.startswith(".") else line) + "\n").encode())
        self.sock.sendall(b".\n")
        return self._read_response()

    def close(self):
        try:
            self.send("quit")
        except Exception:
            pass
        self.sock.close()


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 7777
    src = sys.stdin if len(sys.argv) < 3 or sys.argv[2] == "-" else open(sys.argv[2])
    st = Station(port)
    print(st.banner[0])
    for raw in src:
        cmd = raw.rstrip("\n")
        if not cmd or cmd.startswith("#"):
            continue
        print(f"\n> {cmd}")
        status, body = st.send(cmd)
        print(status)
        for line in body:
            print(line)
    st.close()


if __name__ == "__main__":
    main()
