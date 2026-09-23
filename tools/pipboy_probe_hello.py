#!/usr/bin/env python3
"""Send the EXACT handshake the Pip-Boy client sends, then dump server frames.

Usage:
    python3 pipboy_probe_hello.py --host 127.0.0.1 --port 27000

Run against `adb forward tcp:27000 tcp:27000` from the desktop, or point it at
any reachable host/port. Prints each frame the server sends so we can see
whether it answers with WELCOME + DATA_UPDATE, or rejects with bad_hello.
"""
import argparse
import json
import socket
import struct
import sys

# Mirror of PipBoyBridge.kt HELLO payload.
HELLO_PAYLOAD = json.dumps({
    "magic": "PIPB",
    "protocol": 1,
    "client": "pipboy-droid/0.1.0",
    "caps": ["delta"],
}).encode("utf-8")

MSG_NAMES = {
    0: "KEEPALIVE",
    1: "HELLO",
    2: "WELCOME",
    3: "DATA_UPDATE",
    4: "DATA_DELTA",
    5: "COMMAND",
    6: "COMMAND_REPLY",
    7: "BYE",
}


def frame(typ, payload: bytes) -> bytes:
    return struct.pack("<I", len(payload)) + bytes([typ]) + payload


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=27000)
    ap.add_argument("--timeout", type=float, default=6.0)
    args = ap.parse_args()

    print(f"[probe] connecting to {args.host}:{args.port} ...", flush=True)
    try:
        s = socket.create_connection((args.host, args.port), timeout=3.0)
    except OSError as e:
        print(f"[probe] CONNECTION REFUSED / failed: {e}", flush=True)
        return 1

    s.settimeout(args.timeout)
    s.sendall(frame(1, HELLO_PAYLOAD))
    print(f"[probe] sent HELLO ({len(HELLO_PAYLOAD)} bytes): {HELLO_PAYLOAD.decode()}", flush=True)

    buf = b""
    frames = 0
    try:
        while True:
            chunk = s.recv(4096)
            if not chunk:
                print("[probe] server closed the connection (EOF)", flush=True)
                break
            buf += chunk
            while len(buf) >= 5:
                length = struct.unpack("<I", buf[:4])[0]
                typ = buf[4]
                if len(buf) < 5 + length:
                    break
                payload = buf[5:5 + length]
                buf = buf[5 + length:]
                frames += 1
                name = MSG_NAMES.get(typ, f"0x{typ:02x}")
                try:
                    text = payload.decode("utf-8")
                except UnicodeDecodeError:
                    text = repr(payload)
                print(f"[probe] frame #{frames}: {name} (type={typ}, {length} bytes)")
                print(f"         {text}", flush=True)
                if typ in (3, 4):  # data; stop after first data frame
                    print("[probe] got data frame -> handshake OK", flush=True)
                    s.close()
                    return 0
    except socket.timeout:
        print(f"[probe] read timeout after {args.timeout}s ({frames} frames received)", flush=True)
    except OSError as e:
        print(f"[probe] socket error: {e}", flush=True)
    finally:
        try:
            s.close()
        except OSError:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
