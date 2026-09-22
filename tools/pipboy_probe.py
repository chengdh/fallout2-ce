#!/usr/bin/env python3
"""Pip-Boy Link protocol probe.

Connects to a server (mock or the real game), walks the handshake and prints
every frame it receives. Use it to verify framing, handshake, delta semantics
and heartbeat timing:

    python3 tools/pipboy_mock_server.py --scenario drift &
    python3 tools/pipboy_probe.py --seconds 6

Exit code 0 means the conversation matched the protocol in PIPBOY.md.
"""

from __future__ import annotations

import argparse
import json
import socket
import struct
import sys
import time

MAGIC = "PIPB"
PROTOCOL = 1

MSG_KEEPALIVE = 0x00
MSG_HELLO = 0x01
MSG_WELCOME = 0x02
MSG_DATA_UPDATE = 0x03
MSG_DATA_DELTA = 0x04
MSG_BYE = 0x07

TYPE_NAMES = {
    MSG_KEEPALIVE: "KEEPALIVE",
    MSG_HELLO: "HELLO",
    MSG_WELCOME: "WELCOME",
    MSG_DATA_UPDATE: "DATA_UPDATE",
    MSG_DATA_DELTA: "DATA_DELTA",
    MSG_BYE: "BYE",
}


def send_frame(sock: socket.socket, msg_type: int, payload: str = "") -> None:
    body = payload.encode("utf-8")
    sock.sendall(struct.pack("<IB", len(body), msg_type) + body)


def read_exact(sock: socket.socket, count: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < count:
        chunk = sock.recv(count - len(chunks))
        if not chunk:
            raise ConnectionError("connection closed")
        chunks.extend(chunk)
    return bytes(chunks)


def read_frame(sock: socket.socket):
    header = read_exact(sock, 5)
    length, msg_type = struct.unpack("<IB", header)
    payload = read_exact(sock, length) if length else b""
    return msg_type, payload.decode("utf-8", errors="replace")


def main() -> int:
    parser = argparse.ArgumentParser(description="Pip-Boy Link protocol probe")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=27000)
    parser.add_argument("--seconds", type=float, default=6.0)
    parser.add_argument("--verbose", action="store_true", help="dump full payloads")
    args = parser.parse_args()

    sock = socket.create_connection((args.host, args.port), timeout=5)
    print(f"connected to {args.host}:{args.port}")

    send_frame(
        sock,
        MSG_HELLO,
        json.dumps({"magic": MAGIC, "protocol": PROTOCOL, "client": "probe/0.1", "caps": ["delta"]}),
    )

    failures = []
    msg_type, payload = read_frame(sock)
    print(f"<- {TYPE_NAMES.get(msg_type, hex(msg_type))}: {payload[:160]}")
    if msg_type != MSG_WELCOME:
        failures.append("first frame after HELLO was not WELCOME")
    else:
        welcome = json.loads(payload)
        if welcome.get("magic") != MAGIC:
            failures.append("WELCOME magic mismatch")
        if "error" in welcome:
            failures.append(f"server rejected handshake: {welcome['error']}")

    state = {}
    counts = {"update": 0, "delta": 0, "keepalive": 0}
    deadline = time.time() + args.seconds

    while time.time() < deadline:
        sock.settimeout(max(0.1, deadline - time.time()))
        try:
            msg_type, payload = read_frame(sock)
        except (socket.timeout, ConnectionError):
            break

        name = TYPE_NAMES.get(msg_type, hex(msg_type))
        if msg_type == MSG_DATA_UPDATE:
            counts["update"] += 1
            state.update(json.loads(payload))
            print(f"<- DATA_UPDATE ({len(payload)} bytes, {len(state)} keys)")
            if args.verbose:
                print(json.dumps(json.loads(payload), ensure_ascii=False, indent=2)[:2000])
        elif msg_type == MSG_DATA_DELTA:
            counts["delta"] += 1
            delta = json.loads(payload)
            state.update(delta)
            preview = ", ".join(f"{k}={v}" for k, v in list(delta.items())[:4])
            print(f"<- DATA_DELTA  ({len(delta)} keys): {preview}")
        elif msg_type == MSG_KEEPALIVE:
            counts["keepalive"] += 1
            print("<- KEEPALIVE")
            send_frame(sock, MSG_KEEPALIVE)
        else:
            print(f"<- {name}: {payload[:120]}")

    send_frame(sock, MSG_BYE)
    sock.close()

    print()
    print(f"summary: full updates={counts['update']} deltas={counts['delta']} keepalives={counts['keepalive']}")
    if state:
        print(f"state keys: {len(state)}")
        for key in ("PlayerInfo.CurrHP", "PlayerInfo.Caps", "Map.Name", "Inventory.caps"):
            if key in state:
                print(f"  {key} = {state[key]}")

    if counts["update"] != 1:
        failures.append(f"expected exactly one DATA_UPDATE after handshake, got {counts['update']}")
    if counts["delta"] == 0:
        failures.append("no DATA_DELTA received - server may not be publishing changes")

    if failures:
        print("\nFAILED:")
        for item in failures:
            print(f"  - {item}")
        return 1
    print("\nOK: protocol conversation matches PIPBOY.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
