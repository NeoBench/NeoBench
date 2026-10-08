#!/usr/bin/env python3
"""serialcap.py port outfile [seconds] -- read NeoBench's serial line.

FS-UAE is started with serial_port = tcp://127.0.0.1/PORT/wait, which
makes it listen, print "TCP: Waiting for serial connection..." and hold
the emulation until something answers.  This is that something: it
connects, copies every byte to the file as it arrives, and stops when
the peer closes or the time runs out.  It retries the connect for as
long as it has, because the emulator may not have reached listen() yet
when this starts.
"""
import socket
import sys
import time


def main():
    if len(sys.argv) < 3:
        print("usage: serialcap.py port outfile [seconds]", file=sys.stderr)
        return 2
    port = int(sys.argv[1])
    out = sys.argv[2]
    secs = int(sys.argv[3]) if len(sys.argv) > 3 else 300
    deadline = time.monotonic() + secs

    sock = None
    while sock is None:
        try:
            sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        except OSError:
            if time.monotonic() >= deadline:
                return 1
            time.sleep(0.5)

    sock.settimeout(5)
    with open(out, "wb") as f:
        while time.monotonic() < deadline:
            try:
                chunk = sock.recv(4096)
            except socket.timeout:
                continue
            if not chunk:
                break
            f.write(chunk)
            f.flush()
    sock.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
