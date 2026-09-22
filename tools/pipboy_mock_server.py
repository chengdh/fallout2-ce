#!/usr/bin/env python3
"""Pip-Boy Link mock server.

Implements the exact wire protocol documented in PIPBOY.md so the client app can
be developed and tested before (or without) a running game:

    python3 tools/pipboy_mock_server.py [--port 27000] [--scenario demo|drift]

`demo`   replays a canned Fallout 2 style character.
`drift`  mutates a few values on a timer so delta frames are easy to observe.

Wire format: uint32 LE length + uint8 type + JSON payload.
"""

from __future__ import annotations

import argparse
import json
import random
import socket
import struct
import threading
import time
from typing import Dict

MAGIC = "PIPB"
PROTOCOL = 1

MSG_KEEPALIVE = 0x00
MSG_HELLO = 0x01
MSG_WELCOME = 0x02
MSG_DATA_UPDATE = 0x03
MSG_DATA_DELTA = 0x04
MSG_COMMAND = 0x05
MSG_COMMAND_REPLY = 0x06
MSG_BYE = 0x07

MAX_PAYLOAD = 1024 * 1024
SAMPLE_INTERVAL_MS = 250


def flatten(prefix: str, value, out: Dict[str, str]) -> None:
    """Flatten nested JSON-ish data into 'dotted.path' -> JSON-encoded value."""
    if isinstance(value, dict):
        for key, item in value.items():
            flatten(f"{prefix}.{key}" if prefix else key, item, out)
    else:
        out[prefix] = json.dumps(value, ensure_ascii=False)


def base_snapshot() -> Dict[str, str]:
    data = {
        "PlayerInfo": {
            "PlayerName": "获选者",
            "CurrHP": 63,
            "MaxHP": 78,
            "CurrAP": 9,
            "MaxAP": 9,
            "CurrWeight": 128,
            "MaxWeight": 250,
            "Caps": 1420,
            "XPLevel": 7,
            "XP": 4200,
            "XPNext": 6000,
            "XPProgressPct": 70.0,
            "Karma": 120,
            "Reputation": 0,
            "DateMonth": 6,
            "Day": 14,
            "Year": 2241,
            "TimeHour": 21,
            "IsSneaking": False,
        },
        "Special": {
            "Strength": 6,
            "Perception": 7,
            "Endurance": 5,
            "Charisma": 4,
            "Intelligence": 8,
            "Agility": 7,
            "Luck": 5,
        },
        "Derived": {
            "ArmorClass": 12,
            "ActionPoints": 9,
            "CarryWeight": 250,
            "MeleeDamage": 3,
            "Sequence": 12,
            "HealingRate": 2,
            "CriticalChance": 8,
            "DamageThreshold": 3,
            "DamageResistance": 10,
            "RadiationResistance": 22,
            "PoisonResistance": 30,
        },
        "Conditions": {
            "Poison": 0,
            "Radiation": 12,
            "IsDead": False,
            "IsCrippled": False,
            "IsEncumbered": False,
        },
        "Skills": {
            "Small Guns": 95,
            "Energy Weapons": 55,
            "Speech": 90,
            "Repair": 75,
            "Science": 80,
            "Sneak": 50,
        },
        "Inventory": {
            "caps": 1420,
            "items": [
                {"name": "10mm 手枪", "count": 1, "weight": 3, "pid": 8},
                {"name": "10mm 穿甲弹", "count": 96, "weight": 0, "pid": 9},
                {"name": "治疗针", "count": 4, "weight": 0, "pid": 16},
            ],
            "weapon": "10mm 手枪",
            "armor": "皮甲",
        },
        "Map": {
            "Name": "The Den",
            "City": "The Den",
            "Elevation": 0,
            "IsWorldmap": False,
        },
        "Server": {
            "UptimeSec": 0,
            "SampleIntervalMs": SAMPLE_INTERVAL_MS,
            "ConnectedClients": 1,
        },
    }
    out: Dict[str, str] = {}
    flatten("", data, out)
    return out


def send_frame(sock: socket.socket, msg_type: int, payload: str = "") -> bool:
    body = payload.encode("utf-8")
    if len(body) > MAX_PAYLOAD:
        return False
    try:
        sock.sendall(struct.pack("<IB", len(body), msg_type) + body)
    except OSError:
        return False
    return True


def read_frame(sock: socket.socket, buffer: bytearray):
    """Returns (type, payload_str) or None when incomplete/disconnected."""
    while True:
        if len(buffer) >= 5:
            (length, msg_type) = struct.unpack_from("<IB", buffer, 0)
            if length > MAX_PAYLOAD:
                return None
            if len(buffer) >= length + 5:
                payload = bytes(buffer[5 : 5 + length])
                del buffer[: length + 5]
                return msg_type, payload.decode("utf-8", errors="replace")
        try:
            chunk = sock.recv(4096)
        except OSError:
            return None
        if not chunk:
            return None
        buffer.extend(chunk)


def serve(conn: socket.socket, scenario: str) -> None:
    buffer = bytearray()
    started = time.time()

    conn.settimeout(5.0)
    frame = read_frame(conn, buffer)
    if frame is None or frame[0] != MSG_HELLO:
        send_frame(conn, MSG_WELCOME, json.dumps({"magic": MAGIC, "error": "bad_hello"}))
        conn.close()
        return
    try:
        hello = json.loads(frame[1])
    except json.JSONDecodeError:
        hello = {}
    if hello.get("magic") != MAGIC or hello.get("protocol") != PROTOCOL:
        send_frame(
            conn,
            MSG_WELCOME,
            json.dumps({"magic": MAGIC, "error": "protocol_mismatch", "server_protocol": PROTOCOL}),
        )
        conn.close()
        return

    conn.settimeout(None)
    send_frame(
        conn,
        MSG_WELCOME,
        json.dumps(
            {
                "magic": MAGIC,
                "protocol": PROTOCOL,
                "game": "fallout2-ce",
                "game_version": "mock",
                "caps": "delta,update",
                "sample_interval_ms": SAMPLE_INTERVAL_MS,
            }
        ),
    )

    snapshot = base_snapshot()
    send_frame(conn, MSG_DATA_UPDATE, json.dumps(snapshot, ensure_ascii=False))
    last_sent = dict(snapshot)
    last_send = time.time()
    last_recv = time.time()

    while True:
        now = time.time()

        # Drain client traffic (keepalive / command / bye).
        conn.settimeout(0.05)
        try:
            chunk = conn.recv(4096)
            if chunk:
                last_recv = now
                buffer.extend(chunk)
                while True:
                    frame = read_frame(conn, buffer)
                    if frame is None:
                        break
                    if frame[0] == MSG_BYE:
                        conn.close()
                        return
                    if frame[0] == MSG_COMMAND:
                        send_frame(conn, MSG_COMMAND_REPLY, json.dumps({"ok": False, "error": "not_supported"}))
        except socket.timeout:
            pass
        except OSError:
            conn.close()
            return

        # Drift a couple of values so the client sees delta frames.
        if scenario == "drift":
            hp = random.randint(40, 78)
            caps = int(snapshot["Inventory.caps"]) + random.randint(-5, 5)
            hour = (int(float(snapshot["PlayerInfo.TimeHour"])) + 1) % 24
            snapshot["PlayerInfo.CurrHP"] = json.dumps(hp)
            snapshot["Conditions.Radiation"] = json.dumps(random.randint(0, 30))
            snapshot["Inventory.caps"] = json.dumps(caps)
            snapshot["PlayerInfo.Caps"] = json.dumps(caps)
            snapshot["PlayerInfo.TimeHour"] = json.dumps(hour)
        snapshot["Server.UptimeSec"] = json.dumps(int(now - started))

        delta = {k: v for k, v in snapshot.items() if last_sent.get(k) != v}
        if delta:
            if not send_frame(conn, MSG_DATA_DELTA, json.dumps(delta, ensure_ascii=False)):
                conn.close()
                return
            last_sent = dict(snapshot)
            last_send = now

        if now - last_send > 2.0:
            if not send_frame(conn, MSG_KEEPALIVE):
                conn.close()
                return
            last_send = now

        if now - last_recv > 15.0:
            conn.close()
            return

        time.sleep(SAMPLE_INTERVAL_MS / 1000.0)


def main() -> None:
    parser = argparse.ArgumentParser(description="Pip-Boy Link mock server")
    parser.add_argument("--port", type=int, default=27000)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--scenario", choices=("demo", "drift"), default="drift")
    args = parser.parse_args()

    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind((args.bind, args.port))
    listener.listen(1)
    print(f"Pip-Boy Link mock server on {args.bind}:{args.port} (scenario={args.scenario})", flush=True)

    try:
        while True:
            conn, addr = listener.accept()
            print(f"client connected: {addr}", flush=True)
            thread = threading.Thread(target=serve, args=(conn, args.scenario), daemon=True)
            thread.start()
    except KeyboardInterrupt:
        print("\nshutting down")
    finally:
        listener.close()


if __name__ == "__main__":
    main()
